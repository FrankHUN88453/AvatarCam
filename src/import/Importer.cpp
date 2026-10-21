#include "import/Importer.h"
#include "import/UnityPackage.h"
#include <windows.h>
#include <thread>
#include <atomic>
#include <unordered_set>
#include "stb_image.h"
#include "stb_image_write.h"
#include "stb_image_resize2.h"
#include "core/I18n.h"

namespace {

const char* kVisemeKeys[] = {"sil", "pp", "ff", "th", "dd", "kk", "ch", "ss", "nn", "rr", "aa", "e", "ih", "oh", "ou"};

bool isWordChar(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }
// egész szavas keresés kisbetűs szövegben
bool hasWord(const std::string& lower, const char* w) {
    size_t n = strlen(w), p = 0;
    while ((p = lower.find(w, p)) != std::string::npos) {
        bool l = p == 0 || !isWordChar(lower[p - 1]);
        bool r = p + n >= lower.size() || !isWordChar(lower[p + n]);
        if (l && r) return true;
        p++;
    }
    return false;
}
bool hasSub(const std::string& lower, const char* w) { return lower.find(w) != std::string::npos; }

// Felnőtt tartalmú kiegészítő-rendszerek (DPS/TPS/SPS stb.) – nem importáljuk
bool isAdultName(const std::string& name) {
    std::string l = toLower(name);
    return hasSub(l, "penetrator") || hasSub(l, "orifice") || hasSub(l, "__dps") || hasWord(l, "dps") || hasWord(l, "tps") || hasSub(l, "tps_") ||
           hasWord(l, "sps") || hasSub(l, "tvf-") || hasSub(l, "knaughty") || hasSub(l, "hotdogger");
}
// Menüpontok, amelyeket nem viszünk át (felnőtt rendszerek, helyváltoztatás)
bool isSkippedMenu(const std::string& text) {
    std::string l = toLower(text);
    return hasWord(l, "dps") || hasWord(l, "tps") || hasWord(l, "sps") || hasSub(l, "orifice") || hasSub(l, "penetrat") || hasWord(l, "peen") ||
           hasSub(l, "kink") || hasSub(l, "sheath") || hasSub(l, "throb") || hasSub(l, "gogo") || hasSub(l, "go/") || hasSub(l, "vrcemote");
}
std::string stripRichText(const std::string& s) {
    std::string out;
    bool tag = false;
    for (char c : s) {
        if (c == '<') tag = true;
        else if (c == '>') tag = false;
        else if (!tag) out += c;
    }
    return trim(out);
}

json v3(const Vec3& v) { return json::array({v.x, v.y, v.z}); }
json q4(const Quat& q) { return json::array({q.x, q.y, q.z, q.w}); }

const json& objAt(const UObj* o) { return o->d; }

bool isDescriptor(const UObj& o) { return o.cls == 114 && o.d.contains("ViewPosition") && o.d.contains("VisemeBlendShapes"); }
bool isPhysBone(const UObj& o) { return o.cls == 114 && o.d.contains("pull") && o.d.contains("spring") && o.d.contains("rootTransform"); }
bool isPhysCollider(const UObj& o) { return o.cls == 114 && o.d.contains("shapeType") && o.d.contains("bonesAsSpheres"); }
bool isDynamicBone(const UObj& o) { return o.cls == 114 && o.d.contains("m_Root") && o.d.contains("m_Damping") && o.d.contains("m_Elasticity"); }

// ---------------------------------------------------------------------------
// FBX gyors szöveges vizsgálata: anyag- és textúranevek
struct FbxScan {
    std::vector<std::string> materials, textures;
};
FbxScan scanFbx(const fs::path& file) {
    FbxScan r;
    std::string buf;
    if (!readFile(file, buf)) return r;
    std::unordered_set<std::string> mats, texs;
    auto printable = [](unsigned char c) { return c >= 0x20 && c < 0x7f; };
    const std::string marker("\x00\x01Material", 10);
    size_t p = 0;
    while ((p = buf.find(marker, p)) != std::string::npos) {
        size_t s = p;
        while (s > 0 && p - s < 96 && printable((unsigned char)buf[s - 1])) s--;
        if (s < p) mats.insert(buf.substr(s, p - s));
        p += marker.size();
    }
    // ASCII FBX: Material::Név
    if (startsWith(buf, "; FBX")) {
        size_t q = 0;
        while ((q = buf.find("Material::", q)) != std::string::npos) {
            size_t e = buf.find('"', q);
            if (e != std::string::npos) mats.insert(buf.substr(q + 10, e - q - 10));
            q += 10;
        }
    }
    static const char* exts[] = {".png", ".jpg", ".jpeg", ".tga", ".psd", ".tif", ".tiff", ".bmp", ".webp"};
    std::string lower = toLower(buf);
    for (auto e : exts) {
        size_t q = 0, n = strlen(e);
        while ((q = lower.find(e, q)) != std::string::npos) {
            size_t s = q;
            while (s > 0 && q - s < 200 && printable((unsigned char)buf[s - 1])) s--;
            std::string full = buf.substr(s, q + n - s);
            size_t slash = full.find_last_of("/\\");
            std::string base = slash == std::string::npos ? full : full.substr(slash + 1);
            if (!base.empty() && base.size() > n) texs.insert(base);
            q += n;
        }
    }
    r.materials.assign(mats.begin(), mats.end());
    r.textures.assign(texs.begin(), texs.end());
    return r;
}

// ---------------------------------------------------------------------------
// Textúra átalakítás: bármilyen (stb által olvasható) kép -> PNG, max méret
struct TexJob {
    std::string id;
    fs::path src;
    fs::path dst;
    bool normal = false;
    bool srgb = true;
    bool icon = false;
    int maxSize = 2048;
    // eredmény
    bool ok = false;
    bool alpha = false;
    int w = 0, h = 0;
    std::string error;
};

void pngWriteFunc(void* ctx, void* data, int size) { static_cast<std::string*>(ctx)->append((const char*)data, size); }

void convertTexture(TexJob& job) {
    std::string bytes;
    if (!readFile(job.src, bytes)) {
        job.error = T("unreadable");
        return;
    }
    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load_from_memory((const stbi_uc*)bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
    bytes.clear();
    bytes.shrink_to_fit();
    if (!px) {
        job.error = stbi_failure_reason() ? stbi_failure_reason() : T("unknown format");
        return;
    }
    bool alpha = false;
    if (comp == 4 || comp == 2) {
        for (size_t i = 0, n = (size_t)w * h; i < n; i++)
            if (px[i * 4 + 3] != 255) {
                alpha = true;
                break;
            }
    }
    int maxSize = job.icon ? 128 : job.maxSize;
    int nw = w, nh = h;
    if (std::max(w, h) > maxSize) {
        float s = (float)maxSize / std::max(w, h);
        nw = std::max(1, (int)std::lround(w * s));
        nh = std::max(1, (int)std::lround(h * s));
    }
    std::vector<unsigned char> out;
    const unsigned char* src = px;
    if (nw != w || nh != h) {
        out.resize((size_t)nw * nh * 4);
        if (job.srgb && !job.normal)
            stbir_resize_uint8_srgb(px, w, h, 0, out.data(), nw, nh, 0, STBIR_RGBA);
        else
            stbir_resize_uint8_linear(px, w, h, 0, out.data(), nw, nh, 0, STBIR_RGBA);
        src = out.data();
    }
    std::string png;
    stbi_write_png_compression_level = 3;
    int channels = alpha ? 4 : 3;
    std::vector<unsigned char> rgb;
    if (!alpha) {
        rgb.resize((size_t)nw * nh * 3);
        for (size_t i = 0, n = (size_t)nw * nh; i < n; i++) {
            rgb[i * 3] = src[i * 4];
            rgb[i * 3 + 1] = src[i * 4 + 1];
            rgb[i * 3 + 2] = src[i * 4 + 2];
        }
    }
    stbi_write_png_to_func(pngWriteFunc, &png, nw, nh, channels, alpha ? (const void*)src : (const void*)rgb.data(), nw * channels);
    stbi_image_free(px);
    if (png.empty() || !writeFile(job.dst, png)) {
        job.error = T("write error");
        return;
    }
    job.ok = true;
    job.alpha = alpha;
    job.w = nw;
    job.h = nh;
}

void runTextureJobs(std::vector<TexJob>& jobs, const ImportLog& log, float p0, float p1) {
    std::atomic<size_t> next{0}, done{0};
    unsigned n = std::max(1u, std::min(3u, std::thread::hardware_concurrency()));
    std::mutex logMx;
    auto worker = [&] {
        for (;;) {
            size_t i = next++;
            if (i >= jobs.size()) return;
            convertTexture(jobs[i]);
            size_t d = ++done;
            std::lock_guard<std::mutex> lk(logMx);
            log(format("  texture %zu/%zu: %s%s", d, jobs.size(), pathToUtf8(jobs[i].dst.filename()).c_str(), jobs[i].ok ? "" : (" – ERROR: " + jobs[i].error).c_str()),
                p0 + (p1 - p0) * float(d) / std::max<size_t>(1, jobs.size()));
        }
    };
    std::vector<std::thread> ts;
    for (unsigned k = 0; k < n; k++) ts.emplace_back(worker);
    for (auto& t : ts) t.join();
}

// ---------------------------------------------------------------------------
std::string uniqueAvatarId(const std::string& name) {
    std::string base = slugify(name);
    std::string id = base;
    int n = 2;
    while (fs::exists(avatarsDir() / u8path(id))) id = base + "-" + std::to_string(n++);
    return id;
}

// ===========================================================================
// Unity avatár építése
class UnityAvatarBuilder {
public:
    UnityAvatarBuilder(PackageDb& db, const ImportLog& log) : db_(db), log_(log) {}

    std::string build(const UObjMap& objects, const UObj& descriptor, const UObj& rootGo, const PkgAsset& srcAsset, const fs::path& sourceFile);

private:
    PackageDb& db_;
    const ImportLog& log_;
    fs::path out_;
    std::unordered_map<std::string, std::string> models_;  // guid -> relatív fájl
    std::unordered_map<std::string, std::string> texIds_;  // guid -> id
    std::vector<TexJob> texJobs_;
    std::unordered_map<std::string, std::string> matIds_;  // guid -> id ("" ha nincs)
    json materials_ = json::object();
    std::unordered_map<std::string, std::string> clipIds_;
    json clips_ = json::object();
    std::unordered_map<std::string, std::string> icons_;
    std::vector<TexJob> iconJobs_;
    std::vector<std::string> adultRoots_;

    bool excluded(const std::string& p) const {
        for (auto& e : adultRoots_)
            if (p == e || startsWith(p, e + "/")) return true;
        return false;
    }

    bool preserveHierarchy(const std::string& guid) {
        std::string meta = db_.metaText(guid);
        return meta.find("preserveHierarchy: 1") != std::string::npos;
    }

    std::string useModel(const std::string& guid) {
        auto it = models_.find(guid);
        if (it != models_.end()) return it->second;
        const PkgAsset* a = db_.get(guid);
        if (!a || a->file.empty()) return "";
        std::string file = "models/" + slugify(a->name) + a->ext;
        int k = 2;
        auto taken = [&](const std::string& f) {
            for (auto& [g, v] : models_)
                if (v == f) return true;
            return false;
        };
        while (taken(file)) file = "models/" + slugify(a->name) + "-" + std::to_string(k++) + a->ext;
        std::error_code ec;
        fs::create_directories(out_ / L"models", ec);
        fs::copy_file(a->file, out_ / u8path(file), fs::copy_options::overwrite_existing, ec);
        models_[guid] = file;
        return file;
    }

    std::string useTexture(const std::string& guid, const std::string& kind) {
        auto it = texIds_.find(guid);
        if (it != texIds_.end()) {
            if (kind == "normal")
                for (auto& j : texJobs_)
                    if (j.id == it->second) j.normal = true, j.srgb = false;
            return it->second;
        }
        const PkgAsset* a = db_.get(guid);
        if (!a || a->file.empty()) return "";
        std::string tid = slugify(a->name) + "-" + guid.substr(0, 6);
        std::string meta = db_.metaText(guid);
        bool normal = kind == "normal" || meta.find("textureType: 1\n") != std::string::npos || meta.find("textureType: 1\r") != std::string::npos;
        bool srgb = meta.find("sRGBTexture: 0") == std::string::npos && !normal && kind != "data";
        TexJob j;
        j.id = tid;
        j.src = a->file;
        j.dst = out_ / L"textures" / u8path(tid + ".png");
        j.normal = normal;
        j.srgb = srgb;
        texJobs_.push_back(j);
        texIds_[guid] = tid;
        return tid;
    }

    std::string useIcon(const std::string& guid) {
        auto it = icons_.find(guid);
        if (it != icons_.end()) return it->second;
        const PkgAsset* a = db_.get(guid);
        if (!a || a->file.empty()) return "";
        std::string rel = "icons/" + slugify(a->name) + "-" + guid.substr(0, 6) + ".png";
        TexJob j;
        j.id = rel;
        j.src = a->file;
        j.dst = out_ / u8path(rel);
        j.icon = true;
        iconJobs_.push_back(j);
        icons_[guid] = rel;
        return rel;
    }

    std::string useMaterial(const std::string& guid);
    json convertMaterial(const std::string& guid);
    std::string useClip(const std::string& guid);
    json convertClip(const std::string& guid);
    json convertController(const std::string& guid);
    json convertMenu(const std::string& guid, std::unordered_set<std::string>& seen);
    json convertParameters(const std::string& guid);
    std::string findMaterialByName(const std::string& name);
    json fbxTextureMap(const std::vector<std::string>& names);
};

std::string UnityAvatarBuilder::findMaterialByName(const std::string& name) {
    for (auto& [g, a] : db_.assets())
        if (a.ext == ".mat" && iequals(a.name, name)) return g;
    return "";
}

json UnityAvatarBuilder::fbxTextureMap(const std::vector<std::string>& names) {
    json out = json::object();
    if (names.empty()) return out;
    std::unordered_map<std::string, std::string> byBase;
    for (auto& [g, a] : db_.assets()) {
        if (a.file.empty()) continue;
        std::string base = toLower(narrow(u8path(a.path).filename().wstring()));
        byBase.emplace(base, g);
    }
    for (auto& n : names) {
        auto it = byBase.find(toLower(n));
        if (it == byBase.end() || !db_.isTexture(it->second)) continue;
        std::string tid = useTexture(it->second, "color");
        if (!tid.empty()) out[toLower(n)] = tid;
    }
    return out;
}

std::string UnityAvatarBuilder::useMaterial(const std::string& guid) {
    if (guid.empty()) return "";
    auto it = matIds_.find(guid);
    if (it != matIds_.end()) return it->second;
    matIds_[guid] = "";
    const PkgAsset* a = db_.get(guid);
    if (!a || a->ext != ".mat") return "";
    json m = convertMaterial(guid);
    if (m.is_null()) return "";
    std::string mid = slugify(m["name"].get<std::string>()) + "-" + guid.substr(0, 6);
    materials_[mid] = m;
    matIds_[guid] = mid;
    return mid;
}

static std::string shaderFamily(const std::string& name) {
    std::string n = toLower(name);
    if (hasSub(n, "poiyomi")) return "poiyomi";
    if (hasSub(n, "liltoon") || startsWith(n, "_lil") || hasSub(n, "lil/")) return "liltoon";
    if (hasSub(n, "mtoon")) return "mtoon";
    if (hasSub(n, "toon")) return "toon";
    if (hasSub(n, "unlit")) return "unlit";
    if (hasSub(n, "particle") || hasSub(n, "additive")) return "particle";
    if (hasSub(n, "standard") || hasSub(n, "diffuse") || hasSub(n, "lit")) return "standard";
    return "other";
}

json UnityAvatarBuilder::convertMaterial(const std::string& guid) {
    const auto& docs = db_.docs(guid);
    const UnityDoc* md = nullptr;
    for (auto& d : docs)
        if (d.cls == 21) md = &d;
    if (!md) return nullptr;
    const json& D = md->d;
    // shader név
    std::string shader = "Unknown";
    const json& tags = yget(D, "stringTagMap");
    if (yget(tags, "OriginalShader").is_string())
        shader = yget(tags, "OriginalShader").get<std::string>();
    else {
        URef s = yref(yget(D, "m_Shader"));
        const PkgAsset* sa = db_.get(s.guid);
        if (sa && !sa->file.empty()) {
            std::string head;
            readFile(sa->file, head);
            head = head.substr(0, 4000);
            size_t p = head.find("Shader \"");
            if (p != std::string::npos) {
                size_t e = head.find('"', p + 8);
                shader = head.substr(p + 8, e - p - 8);
            } else
                shader = sa->name;
        } else if (s.fileID == "46")
            shader = "Standard";
        else if (s.fileID == "10752")
            shader = "Unlit/Texture";
        else if (s.fileID == "10750")
            shader = "Unlit/Transparent";
        else if (s.fileID == "10751")
            shader = "Unlit/Transparent Cutout";
    }
    std::string family = shaderFamily(shader);

    // tulajdonságok
    struct TexEnv {
        std::string guid;
        float sx = 1, sy = 1, ox = 0, oy = 0;
    };
    std::unordered_map<std::string, TexEnv> tex;
    std::unordered_map<std::string, float> f;
    std::unordered_map<std::string, Vec4> c;
    const json& sp = yget(D, "m_SavedProperties");
    for (auto& e : yarr(sp, "m_TexEnvs")) {
        if (!e.is_object() || e.empty()) continue;
        auto it = e.begin();
        TexEnv t;
        t.guid = yref(yget(it.value(), "m_Texture")).guid;
        Vec3 sc = yvec3(yget(it.value(), "m_Scale"), {1, 1, 0});
        Vec3 of = yvec3(yget(it.value(), "m_Offset"));
        t.sx = sc.x, t.sy = sc.y, t.ox = of.x, t.oy = of.y;
        tex[it.key()] = t;
    }
    for (const char* key : {"m_Floats", "m_Ints"})
        for (auto& e : yarr(sp, key)) {
            if (!e.is_object() || e.empty()) continue;
            auto it = e.begin();
            f[it.key()] = ynum(it.value());
        }
    for (auto& e : yarr(sp, "m_Colors")) {
        if (!e.is_object() || e.empty()) continue;
        auto it = e.begin();
        c[it.key()] = ycolor(it.value());
    }
    std::string keywords = ystr(D, "m_ShaderKeywords") + " " + (yget(tags, "OriginalKeywords").is_string() ? yget(tags, "OriginalKeywords").get<std::string>() : "");
    for (auto& k : yarr(D, "m_ValidKeywords"))
        if (k.is_string()) keywords += " " + k.get<std::string>();
    auto hasKw = [&](const char* k) { return keywords.find(k) != std::string::npos; };
    int queue = yint(D, "m_CustomRenderQueue", -1);
    std::string renderType = yget(tags, "RenderType").is_string() ? yget(tags, "RenderType").get<std::string>() : "";

    auto F = [&](const char* k, float def) {
        auto it = f.find(k);
        return it == f.end() ? def : it->second;
    };
    auto hasF = [&](const char* k) { return f.count(k) > 0; };
    auto C = [&](const char* k) -> const Vec4* {
        auto it = c.find(k);
        return it == c.end() ? nullptr : &it->second;
    };
    auto hasTex = [&](const char* k) {
        auto it = tex.find(k);
        return it != tex.end() && !it->second.guid.empty() && db_.isTexture(it->second.guid);
    };
    auto T = [&](const char* k, const char* kind) -> std::string { return hasTex(k) ? useTexture(tex[k].guid, kind) : std::string(); };

    json m;
    json bindings = json::object();  // animálható Unity tulajdonság -> mező
    m["hueShiftOn"] = true;
    m["name"] = ystr(D, "m_Name", "Material");
    m["shader"] = shader;
    m["family"] = family;
    Vec3 color{1, 1, 1};
    float opacity = 1;
    std::string map, normalMap, emissiveMap, alphaMap, matcap;
    float normalScale = 1;
    Vec3 emissive{0, 0, 0};
    float emissiveIntensity = 1;
    std::string alphaMode = "opaque";
    float alphaCutoff = F("_Cutoff", 0.5f);
    std::string side = "front";
    float metalness = 0, roughness = 0.85f;
    bool unlit = false;
    float hueShift = 0, saturation = 0, brightness = 0;
    float matcapStrength = 0;
    std::string matcapMode = "add";
    json rimColor = nullptr, shadeColor = nullptr;
    float repeat[2] = {1, 1}, offset[2] = {0, 0};

    for (const char* k : {"_Color", "_BaseColor", "_MainColor", "_TintColor"})
        if (auto v = C(k)) {
            color = {v->x, v->y, v->z};
            opacity = v->w;
            break;
        }
    for (const char* k : {"_MainTex", "_BaseMap", "_BaseColorMap", "_MainTexture", "_Albedo", "_BaseTexture"})
        if (hasTex(k)) {
            map = T(k, "color");
            repeat[0] = tex[k].sx, repeat[1] = tex[k].sy, offset[0] = tex[k].ox, offset[1] = tex[k].oy;
            break;
        }
    for (const char* k : {"_BumpMap", "_NormalMap", "_NormalTex"})
        if (hasTex(k)) {
            normalMap = T(k, "normal");
            normalScale = F("_BumpScale", 1);
            break;
        }
    float cull = hasF("_Cull") ? F("_Cull", 2) : F("_CullMode", 2);
    side = cull == 0 ? "double" : cull == 1 ? "back" : "front";
    bool rqTransparent = queue >= 2900 || renderType.find("Transparent") != std::string::npos;
    bool rqCutout = (queue >= 2450 && queue < 2900) || renderType.find("TransparentCutout") != std::string::npos;
    std::string sl = toLower(shader);

    if (family == "poiyomi") {
        int mode = (int)F("_Mode", 0);
        switch (mode) {
            case 1: alphaMode = "mask"; break;
            case 2: case 3: case 6: case 7: case 9: alphaMode = "blend"; break;
            case 4: case 5: alphaMode = "add"; break;
            default: alphaMode = "opaque";
        }
        if (F("_AlphaForceOpaque", 0) == 1) alphaMode = "opaque";
        for (const char* k : {"_ClippingMask", "_AlphaMask"})
            if (hasTex(k)) {
                alphaMap = T(k, "data");
                break;
            }
        const char* slots[4][6] = {{"_EnableEmission", "_EmissionMap", "_EmissionColor", "_EmissionStrength", "_EmissionMask", "_EmissionBaseColorAsMap"},
                                   {"_EnableEmission1", "_EmissionMap1", "_EmissionColor1", "_EmissionStrength1", "_EmissionMask1", "_EmissionBaseColorAsMap1"},
                                   {"_EnableEmission2", "_EmissionMap2", "_EmissionColor2", "_EmissionStrength2", "_EmissionMask2", "_EmissionBaseColorAsMap2"},
                                   {"_EnableEmission3", "_EmissionMap3", "_EmissionColor3", "_EmissionStrength3", "_EmissionMask3", "_EmissionBaseColorAsMap3"}};
        float bestScore = 0;
        for (auto& s : slots) {
            if (F(s[0], 0) != 1) continue;
            float strength = F(s[3], 1);
            Vec4 col = C(s[2]) ? *C(s[2]) : Vec4(1, 1, 1, 1);
            float lum = (col.x + col.y + col.z) / 3;
            bool has = hasTex(s[1]);
            bool missing = tex.count(s[1]) && !tex[s[1]].guid.empty() && !has;
            float score = strength * lum * (has ? 1.5f : missing ? 0.05f : 1.0f);
            if (score > bestScore) {
                bestScore = score;
                bindings[s[3]] = "emissiveIntensity";
                for (int ci = 0; ci < 3; ci++) bindings[std::string(s[2]) + "." + "rgb"[ci]] = "emissive." + std::to_string(ci);
                emissive = {col.x, col.y, col.z};
                emissiveIntensity = std::min(strength, 20.0f) * (missing ? 0.05f : 1.0f);
                emissiveMap = has ? T(s[1], "color") : "";
                std::string mask = T(s[4], "data");
                m["emissiveMask"] = mask.empty() ? json(nullptr) : json(mask);
                m["emissiveUseBase"] = F(s[5], 0) == 1;
            }
        }
        if (bestScore <= 0.001f) emissive = {0, 0, 0};
        hueShift = F("_MainHueShift", 0);
        m["hueShiftOn"] = F("_MainHueShiftToggle", 0) == 1;
        bindings["_MainHueShift"] = "hueShift";
        bindings["_MainHueShiftToggle"] = "hueShiftOn";
        saturation = F("_Saturation", 0);
        bindings["_Saturation"] = "saturation";
        // Matcap rétegek (Poiyomi: _Matcap, _Matcap2, _Matcap3, _Matcap4)
        json matcaps = json::array();
        const char* prefixes[] = {"_Matcap", "_Matcap2", "_Matcap3", "_Matcap4"};
        for (int li = 0; li < 4; li++) {
            std::string p = prefixes[li];
            auto P = [&](const char* suffix) { return p + suffix; };
            bool enabled = F(P("Enable").c_str(), 0) == 1;
            float emission = F(P("EmissionStrength").c_str(), 0);
            float replace = F(P("Replace").c_str(), 0), multiply = F(P("Multiply").c_str(), 0), add = F(P("Add").c_str(), 0), mixed = F(P("Mixed").c_str(), 0);
            std::string texId = T(p.c_str(), "color");
            std::string maskId = T(P("Mask").c_str(), "data");
            Vec4 col = C(P("Color").c_str()) ? *C(P("Color").c_str()) : Vec4(1, 1, 1, 1);
            json layer = {{"enabled", enabled},
                          {"texture", texId.empty() ? json(nullptr) : json(texId)},
                          {"mask", maskId.empty() ? json(nullptr) : json(maskId)},
                          {"color", v3(col.xyz())},
                          {"intensity", F(P("Intensity").c_str(), 1)},
                          {"emission", emission},
                          {"replace", replace},
                          {"multiply", multiply},
                          {"add", add + mixed},
                          {"hueShift", F(P("HueShift").c_str(), 0)},
                          {"hueShiftOn", F(P("HueShiftEnabled").c_str(), 0) == 1},
                          {"border", F(P("Border").c_str(), 0.43f)}};
            std::string base = "matcaps." + std::to_string(li) + ".";
            for (auto [prop, field] : {std::pair{"Enable", "enabled"}, {"Intensity", "intensity"}, {"EmissionStrength", "emission"}, {"Replace", "replace"}, {"Multiply", "multiply"},
                                       {"Add", "add"}, {"HueShift", "hueShift"}, {"HueShiftEnabled", "hueShiftOn"}})
                bindings[P(prop)] = base + field;
            for (int ci = 0; ci < 3; ci++) bindings[P("Color.") + "rgb"[ci]] = base + "color." + std::to_string(ci);
            matcaps.push_back(layer);
        }
        m["matcaps"] = matcaps;
        bindings["_ClippingMask_ST.x"] = "alphaMapST.0";
        bindings["_ClippingMask_ST.y"] = "alphaMapST.1";
        bindings["_ClippingMask_ST.z"] = "alphaMapST.2";
        bindings["_ClippingMask_ST.w"] = "alphaMapST.3";
        if (tex.count("_ClippingMask")) {
            auto& cm = tex["_ClippingMask"];
            m["alphaMapST"] = json::array({cm.sx, cm.sy, cm.ox, cm.oy});
        }
        if (F("_EnableRimLighting", 0) == 1 && C("_RimLightColor")) rimColor = v3(C("_RimLightColor")->xyz());
        if (hasKw("MOCHIE_PBR") && hasF("_MochieMetallicMultiplier")) {
            metalness = std::min(1.0f, F("_MochieMetallicMultiplier", 0));
            if (hasF("_MochieRoughnessMultiplier")) roughness = clampf(F("_MochieRoughnessMultiplier", 1), 0.05f, 1.0f);
        }
        if (C("_ShadowColor")) shadeColor = v3(C("_ShadowColor")->xyz());
    } else if (family == "liltoon") {
        int tm = (int)F("_TransparentMode", 0);
        alphaMode = tm == 1 ? "mask" : (tm == 2 || tm == 3) ? "blend" : "opaque";
        if (hasSub(sl, "cutout")) alphaMode = "mask";
        else if (hasSub(sl, "trans")) alphaMode = "blend";
        if (hasTex("_AlphaMask") && F("_AlphaMaskMode", 0) > 0) alphaMap = T("_AlphaMask", "data");
        if (F("_UseEmission", 0) == 1) {
            Vec4 ec = C("_EmissionColor") ? *C("_EmissionColor") : Vec4(1, 1, 1, 1);
            emissive = ec.xyz();
            emissiveMap = T("_EmissionMap", "color");
        }
        if (auto hsvg = C("_MainTexHSVG")) {
            hueShift = hsvg->x;
            saturation = hsvg->y - 1;
            brightness = hsvg->z - 1;
        }
        if (F("_UseMatCap", 0) == 1 && hasTex("_MatCapTex")) {
            matcap = T("_MatCapTex", "color");
            matcapStrength = C("_MatCapColor") ? C("_MatCapColor")->w : 1;
            int bm = (int)F("_MatCapBlendMode", 0);
            matcapMode = bm == 0 ? "replace" : bm == 3 ? "multiply" : "add";
        }
        if (F("_UseRim", 0) == 1 && C("_RimColor")) rimColor = v3(C("_RimColor")->xyz());
        if (C("_ShadowColor")) shadeColor = v3(C("_ShadowColor")->xyz());
    } else if (family == "mtoon") {
        int bm = (int)F("_BlendMode", 0);
        alphaMode = bm == 1 ? "mask" : bm >= 2 ? "blend" : "opaque";
        if (C("_ShadeColor")) shadeColor = v3(C("_ShadeColor")->xyz());
        if (hasTex("_EmissionMap") || (C("_EmissionColor") && (C("_EmissionColor")->x + C("_EmissionColor")->y + C("_EmissionColor")->z) > 0.03f)) {
            emissive = C("_EmissionColor") ? C("_EmissionColor")->xyz() : Vec3(1, 1, 1);
            emissiveMap = T("_EmissionMap", "color");
        }
        if (hasTex("_SphereAdd")) {
            matcap = T("_SphereAdd", "color");
            matcapStrength = 1;
            matcapMode = "add";
        }
        if (C("_RimColor") && (C("_RimColor")->x + C("_RimColor")->y + C("_RimColor")->z) > 0.03f) rimColor = v3(C("_RimColor")->xyz());
    } else {
        if (hasF("_Mode") && family == "standard") {
            int mode = (int)F("_Mode", 0);
            alphaMode = mode == 1 ? "mask" : mode >= 2 ? "blend" : "opaque";
        } else
            alphaMode = rqCutout ? "mask" : rqTransparent ? "blend" : "opaque";
        if (hasSub(sl, "cutout")) alphaMode = "mask";
        else if (hasSub(sl, "transparent") || hasSub(sl, "fade")) alphaMode = "blend";
        if (family == "particle") alphaMode = (hasSub(sl, "additive") || hasSub(sl, "add")) ? "add" : "blend";
        metalness = F("_Metallic", 0);
        float gloss = hasF("_Glossiness") ? F("_Glossiness", 0.5f) : F("_Smoothness", 0.15f);
        roughness = std::max(0.04f, 1 - gloss);
        bool emOn = hasKw("_EMISSION") || F("_UseEmission", 0) == 1 || F("_EnableEmission", 0) == 1;
        if (auto ec = C("_EmissionColor"))
            if ((emOn || family != "standard") && (ec->x + ec->y + ec->z) > 0.03f) {
                emissive = ec->xyz();
                emissiveMap = T("_EmissionMap", "color");
            }
        if (family == "unlit" || family == "particle") unlit = true;
        if (family == "toon" && C("_ShadeColor")) shadeColor = v3(C("_ShadeColor")->xyz());
    }
    float emMax = std::max({emissive.x, emissive.y, emissive.z});
    if (emMax > 1) {
        emissive = emissive / emMax;
        emissiveIntensity *= emMax;
    }
    if (alphaMode == "opaque") opacity = 1;

    for (const char* k : {"_Color", "_BaseColor", "_MainColor"})
        if (C(k)) {
            for (int ci = 0; ci < 3; ci++) bindings[std::string(k) + "." + "rgb"[ci]] = "color." + std::to_string(ci);
            bindings[std::string(k) + ".a"] = "opacity";
            break;
        }
    if (family != "poiyomi" && C("_EmissionColor"))
        for (int ci = 0; ci < 3; ci++) bindings[std::string("_EmissionColor.") + "rgb"[ci]] = "emissive." + std::to_string(ci);
    m["bindings"] = bindings;
    m["color"] = v3(color);
    m["opacity"] = opacity;
    m["map"] = map.empty() ? json(nullptr) : json(map);
    m["mapRepeat"] = json::array({repeat[0], repeat[1]});
    m["mapOffset"] = json::array({offset[0], offset[1]});
    m["normalMap"] = normalMap.empty() ? json(nullptr) : json(normalMap);
    m["normalScale"] = normalScale;
    m["emissive"] = v3(emissive);
    m["emissiveMap"] = emissiveMap.empty() ? json(nullptr) : json(emissiveMap);
    m["emissiveIntensity"] = emissiveIntensity;
    m["alphaMap"] = alphaMap.empty() ? json(nullptr) : json(alphaMap);
    m["alphaMode"] = alphaMode;
    m["alphaCutoff"] = alphaCutoff;
    m["side"] = side;
    m["metalness"] = metalness;
    m["roughness"] = roughness;
    m["unlit"] = unlit;
    m["hueShift"] = hueShift;
    m["saturation"] = saturation;
    m["brightness"] = brightness;
    m["matcap"] = matcap.empty() ? json(nullptr) : json(matcap);
    m["matcapStrength"] = matcapStrength;
    m["matcapMode"] = matcapMode;
    m["rimColor"] = rimColor;
    m["shadeColor"] = shadeColor;
    m["renderQueue"] = queue;
    return m;
}

// --- animációs klipek ---
static Quat unityEulerToRH(const Vec3& e) {
    Quat qx = Quat::axisAngle({1, 0, 0}, e.x * kDeg2Rad);
    Quat qy = Quat::axisAngle({0, 1, 0}, e.y * kDeg2Rad);
    Quat qz = Quat::axisAngle({0, 0, 1}, e.z * kDeg2Rad);
    return quatToRH(qy * qx * qz);
}

std::string UnityAvatarBuilder::useClip(const std::string& guid) {
    auto it = clipIds_.find(guid);
    if (it != clipIds_.end()) return it->second;
    const PkgAsset* a = db_.get(guid);
    if (!a || a->ext != ".anim") {
        clipIds_[guid] = "";
        return "";
    }
    std::string cid = slugify(a->name) + "-" + guid.substr(0, 6);
    clipIds_[guid] = cid;
    json clip = convertClip(guid);
    if (!clip.is_null()) clips_[cid] = clip;
    return cid;
}

json UnityAvatarBuilder::convertClip(const std::string& guid) {
    const auto& docs = db_.docs(guid);
    const UnityDoc* cd = nullptr;
    for (auto& d : docs)
        if (d.cls == 74) cd = &d;
    if (!cd) return nullptr;
    const json& D = cd->d;
    json curves = json::array();
    float maxT = 0;
    auto floatKeys = [&](const json& curve) {
        json ks = json::array();
        for (auto& k : yarr(curve, "m_Curve")) {
            float t = ynum(k, "time");
            maxT = std::max(maxT, t);
            ks.push_back(json::array({t, ynum(k, "value")}));
        }
        return ks;
    };
    auto add = [&](const std::string& path, const char* type, const json& keys, json extra = json::object()) {
        if (excluded(path) || keys.empty()) return;
        json c = {{"path", path}, {"type", type}, {"keys", keys}};
        for (auto it = extra.begin(); it != extra.end(); ++it) c[it.key()] = it.value();
        curves.push_back(c);
    };
    for (auto& fc : yarr(D, "m_FloatCurves")) {
        int cls = yint(fc, "classID");
        std::string attr = ystr(fc, "attribute");
        std::string p = ystr(fc, "path");
        json keys = floatKeys(yget(fc, "curve"));
        if (cls == 1 && attr == "m_IsActive") add(p, "active", keys);
        else if ((cls == 137 || cls == 23) && attr == "m_Enabled") add(p, "enabled", keys);
        else if (cls == 137 && startsWith(attr, "blendShape.")) add(p, "blend", keys, {{"prop", attr.substr(11)}});
        else if ((cls == 137 || cls == 23) && startsWith(attr, "material.")) add(p, "mat", keys, {{"prop", attr.substr(9)}});
        else if (cls == 114 && attr == "m_Enabled") add(p, "behaviour", keys);
        else if (cls == 4 && startsWith(attr, "m_LocalScale.") && attr.size() == 14) add(p, "scaleAxis", keys, {{"prop", attr.substr(13)}});
    }
    auto vecCurves = [&](const char* field, const char* type, int kind) {
        for (auto& pc : yarr(D, field)) {
            json ks = json::array();
            for (auto& k : yarr(yget(pc, "curve"), "m_Curve")) {
                float t = ynum(k, "time");
                maxT = std::max(maxT, t);
                const json& v = yget(k, "value");
                if (kind == 0) ks.push_back(json::array({t, v3(posToRH(yvec3(v)))}));
                else if (kind == 1) ks.push_back(json::array({t, v3(yvec3(v, {1, 1, 1}))}));
                else if (kind == 2) ks.push_back(json::array({t, q4(quatToRH(yquat(v)))}));
                else ks.push_back(json::array({t, q4(unityEulerToRH(yvec3(v)))}));
            }
            add(ystr(pc, "path"), type, ks);
        }
    };
    vecCurves("m_PositionCurves", "pos", 0);
    vecCurves("m_ScaleCurves", "scale", 1);
    vecCurves("m_RotationCurves", "rot", 2);
    vecCurves("m_EulerCurves", "rot", 3);
    for (auto& pp : yarr(D, "m_PPtrCurves")) {
        std::string attr = ystr(pp, "attribute");
        if (!startsWith(attr, "m_Materials.Array.data[")) continue;
        int slot = std::atoi(attr.c_str() + 23);
        json ks = json::array();
        for (auto& k : yarr(pp, "curve")) {
            float t = ynum(k, "time");
            maxT = std::max(maxT, t);
            URef r = yref(yget(k, "value"));
            std::string mid = r.guid.empty() ? "" : useMaterial(r.guid);
            ks.push_back(json::array({t, mid.empty() ? json(nullptr) : json(mid)}));
        }
        add(ystr(pp, "path"), "matref", ks, {{"slot", slot}});
    }
    const json& st = yget(D, "m_AnimationClipSettings");
    float stop = ynum(st, "m_StopTime", maxT);
    return {{"name", ystr(D, "m_Name", "Clip")}, {"length", std::max(stop, maxT)}, {"loop", ystr(st, "m_LoopTime") == "1"}, {"curves", curves}};
}

// --- FX animator controller ---
json UnityAvatarBuilder::convertController(const std::string& guid) {
    const auto& docs = db_.docs(guid);
    std::unordered_map<std::string, const UnityDoc*> byId;
    const UnityDoc* ctrl = nullptr;
    for (auto& d : docs) {
        byId[d.id] = &d;
        if (d.cls == 91) ctrl = &d;
    }
    if (!ctrl) return nullptr;
    json params = json::array();
    for (auto& p : yarr(ctrl->d, "m_AnimatorParameters")) {
        int t = yint(p, "m_Type");
        std::string type = t == 1 ? "float" : t == 3 ? "int" : t == 4 ? "bool" : t == 9 ? "trigger" : "float";
        float def = type == "float" ? ynum(p, "m_DefaultFloat") : type == "int" ? (float)yint(p, "m_DefaultInt") : (ystr(p, "m_DefaultBool") == "1" ? 1.f : 0.f);
        params.push_back({{"name", ystr(p, "m_Name")}, {"type", type}, {"default", def}});
    }
    std::unordered_map<std::string, json> motionCache;
    std::function<json(const URef&)> convertMotion = [&](const URef& r) -> json {
        if (r.isNull()) return nullptr;
        if (!r.guid.empty()) {
            std::string cid = useClip(r.guid);
            return cid.empty() ? json(nullptr) : json{{"clip", cid}};
        }
        auto mc = motionCache.find(r.fileID);
        if (mc != motionCache.end()) return mc->second;
        auto it = byId.find(r.fileID);
        if (it == byId.end() || it->second->cls != 206) return nullptr;
        const json& bt = it->second->d;
        json res = {{"tree", yint(bt, "m_BlendType")}, {"param", ystr(bt, "m_BlendParameter")}, {"paramY", ystr(bt, "m_BlendParameterY")}, {"children", json::array()}};
        motionCache[r.fileID] = res;  // ciklus elleni védelem
        for (auto& ch : yarr(bt, "m_Childs")) {
            Vec3 pos = yvec3(yget(ch, "m_Position"));
            res["children"].push_back({{"motion", convertMotion(yref(yget(ch, "m_Motion")))},
                                       {"threshold", ynum(ch, "m_Threshold")},
                                       {"position", json::array({pos.x, pos.y})},
                                       {"directParam", ystr(ch, "m_DirectBlendParameter")},
                                       {"timeScale", ynum(ch, "m_TimeScale", 1)}});
        }
        motionCache[r.fileID] = res;
        return res;
    };
    auto conditions = [&](const json& conds) {
        json out = json::array();
        for (auto& c : conds) {
            int m = yint(c, "m_ConditionMode");
            const char* mode = m == 1 ? "if" : m == 2 ? "ifnot" : m == 3 ? "greater" : m == 4 ? "less" : m == 6 ? "equals" : m == 7 ? "notequal" : "if";
            out.push_back({{"mode", mode}, {"param", ystr(c, "m_ConditionEvent")}, {"threshold", ynum(c, "m_EventTreshold")}});
        }
        return out;
    };
    auto behaviours = [&](const json& ids) {
        json out = json::array();
        for (auto& r : ids) {
            auto it = byId.find(yref(r).fileID);
            if (it == byId.end() || it->second->cls != 114) continue;
            const json& b = it->second->d;
            if (!yget(b, "parameters").is_array() || !b.contains("localOnly")) continue;
            json ops = json::array();
            for (auto& p : yarr(b, "parameters")) {
                int t = yint(p, "type");
                ops.push_back({{"op", t == 1 ? "add" : t == 2 ? "random" : t == 3 ? "copy" : "set"},
                               {"name", ystr(p, "name")},
                               {"source", ystr(p, "source")},
                               {"value", ynum(p, "value")},
                               {"min", ynum(p, "valueMin")},
                               {"max", ynum(p, "valueMax")},
                               {"chance", ynum(p, "chance", 1)}});
            }
            out.push_back({{"type", "driver"}, {"ops", ops}});
        }
        return out;
    };
    auto transition = [&](const json& tid) -> json {
        auto it = byId.find(yref(tid).fileID);
        if (it == byId.end()) return nullptr;
        const json& t = it->second->d;
        URef dst = yref(yget(t, "m_DstState")), dstM = yref(yget(t, "m_DstStateMachine"));
        return {{"to", dst.isNull() ? json(nullptr) : json(dst.fileID)},
                {"toMachine", dstM.isNull() ? json(nullptr) : json(dstM.fileID)},
                {"exit", ystr(t, "m_IsExit") == "1"},
                {"conditions", conditions(yarr(t, "m_Conditions"))},
                {"hasExitTime", ystr(t, "m_HasExitTime") == "1"},
                {"exitTime", ynum(t, "m_ExitTime", 1)},
                {"canSelf", ystr(t, "m_CanTransitionToSelf") == "1"}};
    };
    json states = json::object(), machines = json::object();
    std::function<void(const std::string&)> visit = [&](const std::string& mid) {
        if (machines.contains(mid)) return;
        auto it = byId.find(mid);
        if (it == byId.end() || it->second->cls != 1107) return;
        const json& sm = it->second->d;
        json m = {{"states", json::array()}, {"machines", json::array()}, {"any", json::array()}, {"entry", json::array()}};
        URef def = yref(yget(sm, "m_DefaultState"));
        m["defaultState"] = def.isNull() ? json(nullptr) : json(def.fileID);
        for (auto& t : yarr(sm, "m_AnyStateTransitions"))
            if (auto tr = transition(t); !tr.is_null()) m["any"].push_back(tr);
        for (auto& t : yarr(sm, "m_EntryTransitions"))
            if (auto tr = transition(t); !tr.is_null()) m["entry"].push_back(tr);
        m["behaviours"] = behaviours(yarr(sm, "m_StateMachineBehaviours"));
        machines[mid] = m;
        for (auto& cs : yarr(sm, "m_ChildStates")) {
            std::string sid = yref(yget(cs, "m_State")).fileID;
            auto sit = byId.find(sid);
            if (sit == byId.end()) continue;
            const json& st = sit->second->d;
            machines[mid]["states"].push_back(sid);
            json trs = json::array();
            for (auto& t : yarr(st, "m_Transitions"))
                if (auto tr = transition(t); !tr.is_null()) trs.push_back(tr);
            states[sid] = {{"name", ystr(st, "m_Name")},
                           {"machine", mid},
                           {"motion", convertMotion(yref(yget(st, "m_Motion")))},
                           {"speed", ynum(st, "m_Speed", 1)},
                           {"timeParam", ystr(st, "m_TimeParameterActive") == "1" ? ystr(st, "m_TimeParameter") : ""},
                           {"transitions", trs},
                           {"behaviours", behaviours(yarr(st, "m_StateMachineBehaviours"))},
                           {"writeDefaults", ystr(st, "m_WriteDefaultValues") != "0"}};
        }
        for (auto& cm : yarr(sm, "m_ChildStateMachines")) {
            std::string cid = yref(yget(cm, "m_StateMachine")).fileID;
            machines[mid]["machines"].push_back(cid);
            visit(cid);
        }
    };
    json layers = json::array();
    int li = 0;
    for (auto& l : yarr(ctrl->d, "m_AnimatorLayers")) {
        std::string mid = yref(yget(l, "m_StateMachine")).fileID;
        visit(mid);
        layers.push_back({{"name", ystr(l, "m_Name", "Layer " + std::to_string(li))},
                          {"machine", mid},
                          {"weight", li == 0 ? 1.0f : ynum(l, "m_DefaultWeight", 0)},
                          {"additive", yint(l, "m_BlendingMode") == 1}});
        li++;
    }
    return {{"name", ystr(ctrl->d, "m_Name", "Controller")}, {"params", params}, {"layers", layers}, {"states", states}, {"machines", machines}};
}

// --- kifejezés menü és paraméterek ---
json UnityAvatarBuilder::convertMenu(const std::string& guid, std::unordered_set<std::string>& seen) {
    if (guid.empty() || seen.count(guid)) return nullptr;
    seen.insert(guid);
    const auto& docs = db_.docs(guid);
    const UnityDoc* md = nullptr;
    for (auto& d : docs)
        if (d.cls == 114 && yget(d.d, "controls").is_array()) md = &d;
    if (!md) return nullptr;
    json controls = json::array();
    for (auto& c : yarr(md->d, "controls")) {
        std::string name = stripRichText(ystr(c, "name"));
        std::string param = ystr(yget(c, "parameter"), "name");
        std::string allParams = param;
        json subParams = json::array();
        for (auto& p : yarr(c, "subParameters")) {
            std::string pn = ystr(p, "name");
            subParams.push_back(pn);
            allParams += " " + pn;
        }
        if (isSkippedMenu(name) || isSkippedMenu(allParams)) continue;
        int t = yint(c, "type");
        const char* type = t == 101 ? "button" : t == 102 ? "toggle" : t == 103 ? "submenu" : t == 201 ? "twoaxis" : t == 202 ? "fouraxis" : t == 203 ? "radial" : "button";
        URef ic = yref(yget(c, "icon"));
        json labels = json::array();
        for (auto& l : yarr(c, "labels")) {
            URef li = yref(yget(l, "icon"));
            labels.push_back({{"name", stripRichText(ystr(l, "name"))}, {"icon", db_.isTexture(li.guid) ? json(useIcon(li.guid)) : json(nullptr)}});
        }
        URef sub = yref(yget(c, "subMenu"));
        controls.push_back({{"name", name},
                            {"type", type},
                            {"icon", db_.isTexture(ic.guid) ? json(useIcon(ic.guid)) : json(nullptr)},
                            {"parameter", param},
                            {"value", ynum(c, "value", 1)},
                            {"subParameters", subParams},
                            {"labels", labels},
                            {"subMenu", sub.guid.empty() ? json(nullptr) : convertMenu(sub.guid, seen)}});
    }
    const PkgAsset* a = db_.get(guid);
    return {{"name", stripRichText(ystr(md->d, "m_Name", a ? a->name : "Menu"))}, {"controls", controls}};
}

json UnityAvatarBuilder::convertParameters(const std::string& guid) {
    json out = json::array();
    const auto& docs = db_.docs(guid);
    for (auto& d : docs) {
        if (d.cls != 114 || !yget(d.d, "parameters").is_array()) continue;
        for (auto& p : yarr(d.d, "parameters")) {
            std::string name = ystr(p, "name");
            if (name.empty()) continue;
            int vt = yint(p, "valueType");
            out.push_back({{"name", name}, {"type", vt == 0 ? "int" : vt == 1 ? "float" : "bool"}, {"default", ynum(p, "defaultValue")}, {"saved", ystr(p, "saved", "1") != "0"}});
        }
        break;
    }
    return out;
}

static int countControls(const json& menu) {
    int n = 0;
    if (!menu.is_object()) return 0;
    for (auto& c : menu["controls"]) {
        n++;
        if (c["subMenu"].is_object()) n += countControls(c["subMenu"]);
    }
    return n;
}

// --- avatár összeállítása ---
std::string UnityAvatarBuilder::build(const UObjMap& objects, const UObj& descriptor, const UObj& rootGo, const PkgAsset& srcAsset, const fs::path& sourceFile) {
    std::string name = ystr(rootGo.d, "m_Name", "Avatar");
    std::string id = uniqueAvatarId(name);
    fs::path finalDir = avatarsDir() / u8path(id);
    out_ = avatarsDir() / u8path(id + ".__tmp");
    std::error_code ec;
    fs::remove_all(out_, ec);
    fs::create_directories(out_ / L"textures", ec);
    fs::create_directories(out_ / L"icons", ec);
    log_("Avatar: " + name + "  (" + srcAsset.path + ")", 0.12f);

    AvatarTree tree = buildAvatarTree(objects, rootGo);
    for (auto& n : tree.nodes)
        if (!n.path.empty() && isAdultName(n.name)) adultRoots_.push_back(n.path);
    if (!adultRoots_.empty()) log_(format("  %zu adult add-on systems skipped", adultRoots_.size()), 0.12f);
    auto pathOf = [&](const json& r) -> json {
        URef rr = yref(r);
        if (rr.isNull()) return nullptr;
        auto it = tree.idToPath.find(rr.fileID);
        return it == tree.idToPath.end() ? json(nullptr) : json(it->second);
    };

    // alapmodell
    const TreeNode& rootNode = tree.nodes.front();
    std::string baseGuid;
    for (auto* c : rootNode.components)
        if (c->cls == 95) baseGuid = yref(yget(c->d, "m_Avatar")).guid;
    if (baseGuid.empty() || !db_.isModel(baseGuid)) {
        std::unordered_map<std::string, int> counts;
        for (auto& n : tree.nodes)
            for (auto* c : n.components)
                if (c->cls == 137) {
                    std::string g = yref(yget(c->d, "m_Mesh")).guid;
                    if (!g.empty() && db_.isModel(g)) counts[g]++;
                }
        int best = 0;
        for (auto& [g, k] : counts)
            if (k > best) best = k, baseGuid = g;
    }
    if (baseGuid.empty()) throw std::runtime_error(T("The avatar's base model (FBX) was not found."));
    std::string baseModel = useModel(baseGuid);
    log_("  alapmodell: " + db_.get(baseGuid)->path, 0.15f);

    // csomópontok
    json nodes = json::array();
    for (auto& n : tree.nodes) {
        if (excluded(n.path)) continue;
        json o = {{"path", n.path}, {"name", n.name}, {"active", n.active}};
        if (n.hasTransform) {
            o["position"] = v3(n.position);
            o["quaternion"] = q4(n.rotation);
            o["scale"] = v3(n.scale);
        }
        for (auto* c : n.components) {
            if (c->cls != 137 && c->cls != 23) continue;
            URef meshRef;
            if (c->cls == 137)
                meshRef = yref(yget(c->d, "m_Mesh"));
            else
                for (auto* mf : n.components)
                    if (mf->cls == 33) meshRef = yref(yget(mf->d, "m_Mesh"));
            json r = {{"kind", c->cls == 137 ? "skinned" : "mesh"}, {"enabled", ystr(c->d, "m_Enabled", "1") != "0"}};
            json mats = json::array();
            for (auto& m : yarr(c->d, "m_Materials")) {
                URef mr = yref(m);
                std::string mid = mr.isNull() ? "" : useMaterial(mr.guid);
                mats.push_back(mid.empty() ? json(nullptr) : json(mid));
            }
            r["materials"] = mats;
            json bs = json::array();
            for (auto& w : yarr(c->d, "m_BlendShapeWeights")) bs.push_back(ynum(w));
            r["blendShapes"] = bs;
            if (!meshRef.guid.empty() && meshRef.guid != baseGuid) {
                if (db_.isModel(meshRef.guid)) r["model"] = useModel(meshRef.guid);
                else if (meshRef.guid.find_first_not_of('0') != std::string::npos && meshRef.guid.substr(16, 1) == "e") r["builtin"] = std::atoi(meshRef.fileID.c_str());
            }
            if (c->cls == 137) {
                json bones = json::array();
                for (auto& b : yarr(c->d, "m_Bones")) bones.push_back(pathOf(b));
                if (r.contains("model")) r["bones"] = bones;
                r["rootBone"] = pathOf(yget(c->d, "m_RootBone"));
            }
            o["renderer"] = r;
            break;
        }
        if (n.modelInstance) {
            const json& mi = n.modelInstance->d;
            std::string mguid = mi["model"].get<std::string>();
            std::string file = useModel(mguid);
            if (file.empty()) continue;
            Vec3 p{0, 0, 0}, s{1, 1, 1};
            Quat q{};
            bool active = true;
            json matOverrides = json::array(), blendOverrides = json::array();
            std::string rootId = mi["rootTransformSourceId"].get<std::string>();
            for (auto& m : mi["modifications"]) {
                std::string pp = m["path"].get<std::string>();
                std::string target = m["target"].get<std::string>();
                const json& val = m["value"];
                if (target == rootId && startsWith(pp, "m_Local") && pp.size() > 3) {
                    char axis = pp.back();
                    int ai = axis == 'x' ? 0 : axis == 'y' ? 1 : axis == 'z' ? 2 : 3;
                    if (startsWith(pp, "m_LocalPosition.") && ai < 3) p[ai] = ynum(val);
                    else if (startsWith(pp, "m_LocalRotation.")) (&q.x)[ai] = ynum(val);
                    else if (startsWith(pp, "m_LocalScale.") && ai < 3) s[ai] = ynum(val);
                } else if (pp == "m_IsActive" && target == "919132149155446097")
                    active = ystr(m, "value") != "0";
                else if (startsWith(pp, "m_Materials.Array.data[")) {
                    std::string g = m["refGuid"].get<std::string>();
                    if (!g.empty()) {
                        std::string mid = useMaterial(g);
                        matOverrides.push_back({{"target", target}, {"slot", std::atoi(pp.c_str() + 23)}, {"material", mid.empty() ? json(nullptr) : json(mid)}});
                    }
                } else if (startsWith(pp, "m_BlendShapeWeights.Array.data[")) {
                    blendOverrides.push_back({{"target", target}, {"index", std::atoi(pp.c_str() + 31)}, {"value", ynum(val)}});
                }
            }
            FbxScan scan = scanFbx(db_.get(mguid)->file);
            json byName = json::object();
            for (auto& mn : scan.materials) {
                std::string g = findMaterialByName(mn);
                if (!g.empty()) {
                    std::string mid = useMaterial(g);
                    if (!mid.empty()) byName[mn] = mid;
                }
            }
            o["position"] = v3(posToRH(p));
            o["quaternion"] = q4(quatToRH(q.normalized()));
            o["scale"] = v3(s);
            o["active"] = active;
            o["model"] = {{"file", file},
                          {"preserveHierarchy", preserveHierarchy(mguid)},
                          {"materialOverrides", matOverrides},
                          {"blendOverrides", blendOverrides},
                          {"materialsByName", byName},
                          {"textures", fbxTextureMap(scan.textures)}};
        }
        nodes.push_back(o);
    }

    // az alapmodell beágyazott anyagainak név szerinti párosítása
    FbxScan baseScan = scanFbx(db_.get(baseGuid)->file);
    json materialsByName = json::object();
    for (auto& mn : baseScan.materials) {
        std::string g = findMaterialByName(mn);
        if (!g.empty()) {
            std::string mid = useMaterial(g);
            if (!mid.empty()) materialsByName[mn] = mid;
        }
    }
    json fbxTextures = fbxTextureMap(baseScan.textures);

    // VRChat descriptor
    const json& D = descriptor.d;
    json face;
    face["lipSync"] = yint(D, "lipSync");
    face["visemeMesh"] = pathOf(yget(D, "VisemeSkinnedMesh"));
    json vis = json::object();
    int vi = 0;
    for (auto& s : yarr(D, "VisemeBlendShapes")) {
        if (vi < 15 && s.is_string() && !s.get<std::string>().empty() && s.get<std::string>() != "-none-") vis[kVisemeKeys[vi]] = s;
        vi++;
    }
    face["visemes"] = vis;
    face["mouthOpen"] = ystr(D, "MouthOpenBlendShapeName");
    face["jawBone"] = pathOf(yget(D, "lipSyncJawBone"));
    face["jawClosed"] = q4(quatToRH(yquat(yget(D, "lipSyncJawClosed"))));
    face["jawOpen"] = q4(quatToRH(yquat(yget(D, "lipSyncJawOpen"))));
    const json& eye = yget(D, "customEyeLookSettings");
    auto eyeQ = [&](const char* k) -> json {
        const json& o = yget(eye, k);
        if (!o.is_object()) return nullptr;
        return {{"left", q4(quatToRH(yquat(yget(o, "left"))))}, {"right", q4(quatToRH(yquat(yget(o, "right"))))}};
    };
    face["eyes"] = {{"enabled", ystr(D, "enableEyeLook") == "1"},
                    {"left", pathOf(yget(eye, "leftEye"))},
                    {"right", pathOf(yget(eye, "rightEye"))},
                    {"straight", eyeQ("eyesLookingStraight")},
                    {"up", eyeQ("eyesLookingUp")},
                    {"down", eyeQ("eyesLookingDown")},
                    {"lookLeft", eyeQ("eyesLookingLeft")},
                    {"lookRight", eyeQ("eyesLookingRight")}};
    std::vector<int> lids = hexInt32Array(ystr(eye, "eyelidsBlendshapes"));
    auto lid = [&](size_t i) -> json { return i < lids.size() && lids[i] >= 0 ? json(lids[i]) : json(nullptr); };
    face["eyelids"] = {{"type", yint(eye, "eyelidType")}, {"mesh", pathOf(yget(eye, "eyelidsSkinnedMesh"))}, {"blink", lid(0)}, {"lookUp", lid(1)}, {"lookDown", lid(2)}};

    // fizika
    json colliders = json::array();
    std::unordered_map<std::string, int> colliderIdx;
    for (auto& n : tree.nodes) {
        if (excluded(n.path)) continue;
        for (auto* c : n.components) {
            if (!isPhysCollider(*c)) continue;
            colliderIdx[c->id] = (int)colliders.size();
            json p = pathOf(yget(c->d, "rootTransform"));
            int shape = yint(c->d, "shapeType");
            colliders.push_back({{"path", p.is_null() ? json(n.path) : p},
                                 {"shape", shape == 1 ? "capsule" : shape == 2 ? "plane" : "sphere"},
                                 {"radius", ynum(c->d, "radius", 0.05f)},
                                 {"height", ynum(c->d, "height")},
                                 {"position", v3(posToRH(yvec3(yget(c->d, "position"))))},
                                 {"quaternion", q4(quatToRH(yquat(yget(c->d, "rotation"))))},
                                 {"inside", ystr(c->d, "insideBounds") == "1"}});
        }
    }
    auto curveKeys = [&](const json& cv) {
        json ks = json::array();
        for (auto& k : yarr(cv, "m_Curve")) ks.push_back(json::array({ynum(k, "time"), ynum(k, "value")}));
        return ks;
    };
    json physBones = json::array();
    for (auto& n : tree.nodes) {
        if (excluded(n.path)) continue;
        for (auto* c : n.components) {
            const json& d = c->d;
            if (isPhysBone(*c)) {
                json root = pathOf(yget(d, "rootTransform"));
                json ignore = json::array();
                for (auto& r : yarr(d, "ignoreTransforms"))
                    if (auto p = pathOf(r); !p.is_null()) ignore.push_back(p);
                json cols = json::array();
                for (auto& r : yarr(d, "colliders")) {
                    auto it = colliderIdx.find(yref(r).fileID);
                    if (it != colliderIdx.end()) cols.push_back(it->second);
                }
                physBones.push_back({{"type", "physbone"},
                                     {"root", root.is_null() ? json(n.path) : root},
                                     {"ignore", ignore},
                                     {"enabled", ystr(d, "m_Enabled", "1") != "0"},
                                     {"integration", yint(d, "integrationType")},
                                     {"pull", ynum(d, "pull", 0.2f)},
                                     {"spring", ynum(d, "spring", 0.2f)},
                                     {"stiffness", ynum(d, "stiffness", 0.2f)},
                                     {"gravity", ynum(d, "gravity")},
                                     {"gravityFalloff", ynum(d, "gravityFalloff")},
                                     {"immobile", ynum(d, "immobile")},
                                     {"radius", ynum(d, "radius")},
                                     {"limitType", yint(d, "limitType")},
                                     {"maxAngleX", ynum(d, "maxAngleX", 45)},
                                     {"maxAngleZ", ynum(d, "maxAngleZ", 45)},
                                     {"endpoint", v3(posToRH(yvec3(yget(d, "endpointPosition"))))},
                                     {"multiChild", yint(d, "multiChildType")},
                                     {"colliders", cols},
                                     {"curves",
                                      {{"pull", curveKeys(yget(d, "pullCurve"))},
                                       {"spring", curveKeys(yget(d, "springCurve"))},
                                       {"stiffness", curveKeys(yget(d, "stiffnessCurve"))},
                                       {"gravity", curveKeys(yget(d, "gravityCurve"))},
                                       {"radius", curveKeys(yget(d, "radiusCurve"))}}}});
            } else if (isDynamicBone(*c)) {
                json root = pathOf(yget(d, "m_Root"));
                json ignore = json::array();
                for (auto& r : yarr(d, "m_Exclusions"))
                    if (auto p = pathOf(r); !p.is_null()) ignore.push_back(p);
                physBones.push_back({{"type", "dynamicbone"},
                                     {"root", root.is_null() ? json(n.path) : root},
                                     {"ignore", ignore},
                                     {"enabled", ystr(d, "m_Enabled", "1") != "0"},
                                     {"integration", 1},
                                     {"pull", std::min(1.0f, ynum(d, "m_Elasticity", 0.1f) * 2)},
                                     {"spring", std::max(0.0f, 1 - ynum(d, "m_Damping", 0.1f) * 2)},
                                     {"stiffness", ynum(d, "m_Stiffness", 0.1f)},
                                     {"gravity", -ynum(yget(d, "m_Gravity"), "y") * 2},
                                     {"gravityFalloff", 0},
                                     {"immobile", ynum(d, "m_Inert")},
                                     {"radius", ynum(d, "m_Radius")},
                                     {"limitType", 0},
                                     {"maxAngleX", 180},
                                     {"maxAngleZ", 180},
                                     {"endpoint", v3(posToRH(yvec3(yget(d, "m_EndOffset"))))},
                                     {"endLength", ynum(d, "m_EndLength")},
                                     {"multiChild", 0},
                                     {"colliders", json::array()},
                                     {"curves", json::object()}});
            }
        }
    }

    // kifejezés menü, paraméterek, FX animátor
    json expressions = nullptr;
    URef menuRef = yref(yget(D, "expressionsMenu"));
    if (ystr(D, "customExpressions") == "1" || !menuRef.guid.empty()) {
        std::unordered_set<std::string> seen;
        json menu = convertMenu(menuRef.guid, seen);
        json params = convertParameters(yref(yget(D, "expressionParameters")).guid);
        json fx = nullptr;
        for (auto& l : yarr(D, "baseAnimationLayers")) {
            URef ctrl = yref(yget(l, "animatorController"));
            if (yint(l, "type") == 5 && ystr(l, "isDefault") != "1" && !ctrl.guid.empty()) {
                fx = convertController(ctrl.guid);
                break;
            }
        }
        expressions = {{"menu", menu}, {"parameters", params}, {"fx", fx}};
        log_(format("  menu: %d controls, %zu parameters, %zu FX layers, %zu clips", countControls(menu), params.size(), fx.is_object() ? fx["layers"].size() : (size_t)0, clips_.size()), 0.2f);
    }

    // textúrák
    log_(format("  converting %zu textures…", texJobs_.size()), 0.2f);
    runTextureJobs(texJobs_, log_, 0.2f, 0.95f);
    runTextureJobs(iconJobs_, [](const std::string&, float) {}, 0.95f, 0.97f);
    json textures = json::object();
    for (auto& j : texJobs_)
        if (j.ok) textures[j.id] = {{"file", "textures/" + j.id + ".png"}, {"normal", j.normal}, {"srgb", j.srgb}, {"alpha", j.alpha}, {"width", j.w}, {"height", j.h}};

    json humanoid = json::object();
    json meta = db_.metaDoc(baseGuid);
    for (auto& h : yarr(yget(yget(meta, "ModelImporter"), "humanDescription"), "human")) {
        std::string bone = ystr(h, "boneName"), hum = ystr(h, "humanName");
        if (bone.empty() || hum.empty()) continue;
        hum.erase(std::remove(hum.begin(), hum.end(), ' '), hum.end());
        humanoid[hum] = bone;
    }

    json manifest = {{"version", 1},
                     {"id", id},
                     {"name", name},
                     {"source", {{"type", "unitypackage"}, {"file", pathToUtf8(sourceFile.filename())}, {"asset", srcAsset.path}}},
                     {"format", "unity"},
                     {"model", baseModel},
                     {"preserveHierarchy", preserveHierarchy(baseGuid)},
                     {"humanoid", humanoid},
                     {"viewPosition", v3(posToRH(yvec3(yget(D, "ViewPosition"))))},
                     {"nodes", nodes},
                     {"materials", materials_},
                     {"materialsByName", materialsByName},
                     {"fbxTextures", fbxTextures},
                     {"textures", textures},
                     {"face", face},
                     {"physBones", physBones},
                     {"colliders", colliders},
                     {"expressions", expressions},
                     {"clips", clips_}};
    writeJson(out_ / L"avatar.json", manifest);
    fs::remove_all(finalDir, ec);
    fs::rename(out_, finalDir, ec);
    if (ec) throw std::runtime_error(T("Could not create the avatar folder: ") + ec.message());
    log_(format("  done: %s  (%zu materials, %zu textures, %zu PhysBones)", id.c_str(), materials_.size(), textures.size(), physBones.size()), 1.0f);
    return id;
}

// ===========================================================================
std::string importModelFile(const fs::path& file, const std::string& nameHint, const std::string& forcedExt, const ImportLog& log, const json* humanoid) {
    std::string ext = forcedExt.empty() ? toLower(narrow(file.extension().wstring())) : forcedExt;
    std::string name = nameHint.empty() ? narrow(file.stem().wstring()) : nameHint;
    std::string id = uniqueAvatarId(name);
    fs::path finalDir = avatarsDir() / u8path(id);
    fs::path out = avatarsDir() / u8path(id + ".__tmp");
    std::error_code ec;
    fs::remove_all(out, ec);
    fs::create_directories(out / L"models", ec);
    fs::create_directories(out / L"textures", ec);
    std::string modelFile = "models/" + slugify(name) + ext;
    fs::copy_file(file, out / u8path(modelFile), fs::copy_options::overwrite_existing, ec);
    if (ec) throw std::runtime_error(T("Could not copy the model: ") + ec.message());
    json textures = json::object(), fbxTextures = json::object();
    if (ext == ".gltf") {
        json g;
        if (readJson(file, g)) {
            for (const char* key : {"buffers", "images"})
                for (auto& b : g.value(key, json::array())) {
                    std::string uri = b.value("uri", "");
                    if (uri.empty() || startsWith(uri, "data:")) continue;
                    fs::path src = file.parent_path() / u8path(uri);
                    fs::path dst = out / L"models" / u8path(uri);
                    fs::create_directories(dst.parent_path(), ec);
                    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
                }
        }
    }
    if (ext == ".fbx" || ext == ".obj") {
        FbxScan scan = ext == ".fbx" ? scanFbx(file) : FbxScan{};
        std::unordered_set<std::string> wanted;
        for (auto& t : scan.textures) wanted.insert(toLower(t));
        std::vector<TexJob> jobs;
        std::vector<std::string> bases;
        int k = 0;
        fs::path dir = file.parent_path();
        for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it.depth() > 3) {
                it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file()) continue;
            std::string fn = narrow(it->path().filename().wstring());
            std::string lfn = toLower(fn);
            std::string e = toLower(narrow(it->path().extension().wstring()));
            if (e != ".png" && e != ".jpg" && e != ".jpeg" && e != ".tga" && e != ".psd" && e != ".bmp") continue;
            if (!wanted.empty() && !wanted.count(lfn)) continue;
            TexJob j;
            j.id = slugify(narrow(it->path().stem().wstring())) + "-" + std::to_string(k++);
            j.src = it->path();
            j.dst = out / L"textures" / u8path(j.id + ".png");
            j.normal = lfn.find("normal") != std::string::npos || lfn.find("nrm") != std::string::npos;
            j.srgb = !j.normal;
            jobs.push_back(j);
            bases.push_back(lfn);
        }
        runTextureJobs(jobs, log, 0.1f, 0.95f);
        for (size_t i = 0; i < jobs.size(); i++) {
            if (!jobs[i].ok) continue;
            textures[jobs[i].id] = {{"file", "textures/" + jobs[i].id + ".png"}, {"normal", jobs[i].normal}, {"srgb", jobs[i].srgb}, {"alpha", jobs[i].alpha}};
            fbxTextures[bases[i]] = jobs[i].id;
        }
    }
    std::string format = ext == ".vrm" ? "vrm" : ext == ".fbx" ? "fbx" : ext == ".obj" ? "obj" : "gltf";
    json manifest = {{"version", 1},
                     {"id", id},
                     {"name", name},
                     {"source", {{"type", "model"}, {"file", pathToUtf8(file.filename())}}},
                     {"format", format},
                     {"model", modelFile},
                     {"textures", textures},
                     {"fbxTextures", fbxTextures}};
    if (humanoid) manifest["humanoid"] = *humanoid;
    writeJson(out / L"avatar.json", manifest);
    fs::remove_all(finalDir, ec);
    fs::rename(out, finalDir, ec);
    if (ec) throw std::runtime_error(T("Could not create the avatar folder: ") + ec.message());
    log("  done: " + id, 1.0f);
    return id;
}

fs::path makeTempDir(const wchar_t* prefix) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    fs::path p = fs::path(tmp) / (std::wstring(prefix) + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    std::error_code ec;
    fs::create_directories(p, ec);
    return p;
}

std::vector<std::string> importUnityPackage(const fs::path& file, const ImportLog& log) {
    fs::path tmp = makeTempDir(L"avatarcam-pkg-");
    struct Cleanup {
        fs::path p;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(p, ec);
        }
    } cleanup{tmp};
    log("Extracting package…", 0.02f);
    if (!extractUnityPackage(file, tmp, true)) throw std::runtime_error(T("Could not extract the package."));
    PackageDb db(tmp);
    log(format("%zu files in the package. Looking for the avatar…", db.assets().size()), 0.08f);

    struct Cand {
        UObjMap objects;
        std::string descId, rootId;
        const PkgAsset* asset;
        int score;
    };
    std::map<std::string, Cand> byName;
    for (auto* a : db.byExt({".unity", ".prefab"})) {
        std::string text;
        if (!readFile(a->file, text) || !startsWith(text, "%YAML")) continue;
        if (text.find("VisemeBlendShapes") == std::string::npos && text.find("m_SourcePrefab") == std::string::npos) continue;
        UObjMap objs;
        try {
            objs = instantiatePrefab(db, a->guid);
        } catch (...) {
            continue;
        }
        for (auto& [id, o] : objs) {
            if (!isDescriptor(o)) continue;
            std::string goId = yref(yget(o.d, "m_GameObject")).fileID;
            auto git = objs.find(goId);
            if (git == objs.end() || git->second.stripped) continue;
            std::string name = ystr(git->second.d, "m_Name", "Avatar");
            std::string lp = toLower(a->path);
            int score = (a->ext == ".unity" ? 2 : 0) + (ystr(git->second.d, "m_IsActive", "1") == "0" ? -3 : 0) +
                        ((lp.find("do not use") != std::string::npos || lp.find("backup") != std::string::npos) ? -1 : 0);
            auto prev = byName.find(name);
            if (prev == byName.end() || score > prev->second.score) byName[name] = Cand{objs, id, goId, a, score};
        }
    }
    std::vector<std::string> ids;
    for (auto& [name, c] : byName) {
        UnityAvatarBuilder b(db, log);
        ids.push_back(b.build(c.objects, c.objects.at(c.descId), c.objects.at(c.rootId), *c.asset, file));
    }
    if (ids.empty()) {
        // nincs VRChat descriptor: a legnagyobb humanoid FBX
        const PkgAsset* best = nullptr;
        uintmax_t bestSize = 0;
        for (auto* a : db.byExt({".fbx"})) {
            std::string meta = db.metaText(a->guid);
            if (meta.find("animationType: 3") == std::string::npos || meta.find("humanName: Hips") == std::string::npos) continue;
            std::error_code ec;
            uintmax_t sz = fs::file_size(a->file, ec);
            if (sz > bestSize) bestSize = sz, best = a;
        }
        if (!best) throw std::runtime_error(T("No VRChat avatar or humanoid FBX model was found in the package."));
        log("No VRChat Avatar Descriptor, importing humanoid FBX: " + best->path, 0.1f);
        json humanoid = json::object();
        json meta = db.metaDoc(best->guid);
        for (auto& h : yarr(yget(yget(meta, "ModelImporter"), "humanDescription"), "human")) {
            std::string bone = ystr(h, "boneName"), hum = ystr(h, "humanName");
            hum.erase(std::remove(hum.begin(), hum.end(), ' '), hum.end());
            if (!bone.empty() && !hum.empty()) humanoid[hum] = bone;
        }
        // a textúrákat a csomag mappaszerkezetéből nem tudjuk a modell mellé tenni, ezért a csomagban név szerint keressük
        ids.push_back(importModelFile(best->file, best->name, ".fbx", log, &humanoid));
    }
    return ids;
}

}  // namespace

std::vector<std::string> importAvatarFile(const fs::path& file, const ImportLog& log, std::string* error) {
    try {
        std::error_code ec;
        fs::create_directories(avatarsDir(), ec);
        std::string ext = toLower(narrow(file.extension().wstring()));
        if (ext == ".unitypackage") return importUnityPackage(file, log);
        if (ext == ".vrm" || ext == ".glb" || ext == ".gltf" || ext == ".fbx") return {importModelFile(file, "", "", log, nullptr)};
        if (ext == ".zip") {
            fs::path tmp = makeTempDir(L"avatarcam-zip-");
            struct Cleanup {
                fs::path p;
                ~Cleanup() {
                    std::error_code ec;
                    fs::remove_all(p, ec);
                }
            } cleanup{tmp};
            log("Extracting ZIP…", 0.02f);
            if (!extractUnityPackage(file, tmp, false)) throw std::runtime_error(T("Could not extract the ZIP file."));
            fs::path best;
            int bestRank = 99;
            uintmax_t bestSize = 0;
            for (auto& e : fs::recursive_directory_iterator(tmp, ec)) {
                if (!e.is_regular_file()) continue;
                std::string x = toLower(narrow(e.path().extension().wstring()));
                int rank = x == ".unitypackage" ? 0 : x == ".vrm" ? 1 : x == ".glb" ? 2 : x == ".fbx" ? 3 : x == ".gltf" ? 4 : 99;
                uintmax_t sz = e.file_size(ec);
                if (rank < bestRank || (rank == bestRank && sz > bestSize)) best = e.path(), bestRank = rank, bestSize = sz;
            }
            if (best.empty()) throw std::runtime_error(T("The ZIP contains no supported model (.unitypackage, .vrm, .glb, .fbx)."));
            return importAvatarFile(best, log, error);
        }
        throw std::runtime_error(T("Unsupported file type: ") + ext);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        log(std::string("ERROR: ") + e.what(), 1.0f);
        return {};
    }
}

std::vector<AvatarListEntry> listAvatars() {
    std::vector<AvatarListEntry> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(avatarsDir(), ec)) {
        if (!e.is_directory()) continue;
        std::string dn = narrow(e.path().filename().wstring());
        if (endsWith(dn, ".__tmp")) continue;
        json m;
        if (!readJson(e.path() / L"avatar.json", m)) continue;
        AvatarListEntry a;
        a.id = dn;
        a.name = m.value("name", dn);
        a.format = m.value("format", "");
        a.source = m.contains("source") ? m["source"].value("file", "") : "";
        out.push_back(a);
    }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.name < b.name; });
    return out;
}
