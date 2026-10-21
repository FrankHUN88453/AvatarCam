#include "avatar/Avatar.h"
#include "avatar/Animator.h"
#include "avatar/Physics.h"
#include "app/Settings.h"
#include <thread>
#include <mutex>
#include <future>
#include <unordered_set>
#include "core/I18n.h"

static const char* kHumanBoneNames[HB_Count] = {
    "Hips", "Spine", "Chest", "UpperChest", "Neck", "Head", "LeftEye", "RightEye", "Jaw",
    "LeftShoulder", "LeftUpperArm", "LeftLowerArm", "LeftHand",
    "RightShoulder", "RightUpperArm", "RightLowerArm", "RightHand",
    "LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "LeftToes",
    "RightUpperLeg", "RightLowerLeg", "RightFoot", "RightToes",
    "LeftThumbProximal", "LeftThumbIntermediate", "LeftThumbDistal", "LeftIndexProximal", "LeftIndexIntermediate", "LeftIndexDistal",
    "LeftMiddleProximal", "LeftMiddleIntermediate", "LeftMiddleDistal", "LeftRingProximal", "LeftRingIntermediate", "LeftRingDistal",
    "LeftLittleProximal", "LeftLittleIntermediate", "LeftLittleDistal",
    "RightThumbProximal", "RightThumbIntermediate", "RightThumbDistal", "RightIndexProximal", "RightIndexIntermediate", "RightIndexDistal",
    "RightMiddleProximal", "RightMiddleIntermediate", "RightMiddleDistal", "RightRingProximal", "RightRingIntermediate", "RightRingDistal",
    "RightLittleProximal", "RightLittleIntermediate", "RightLittleDistal"};

const char* humanBoneName(int b) { return b >= 0 && b < HB_Count ? kHumanBoneNames[b] : ""; }

static const char* kVisemeKeys[15] = {"sil", "pp", "ff", "th", "dd", "kk", "ch", "ss", "nn", "rr", "aa", "e", "ih", "oh", "ou"};

static const char* kArkit[] = {"eyeBlinkLeft", "eyeLookDownLeft", "eyeLookInLeft", "eyeLookOutLeft", "eyeLookUpLeft", "eyeSquintLeft", "eyeWideLeft",
                               "eyeBlinkRight", "eyeLookDownRight", "eyeLookInRight", "eyeLookOutRight", "eyeLookUpRight", "eyeSquintRight", "eyeWideRight",
                               "jawForward", "jawLeft", "jawRight", "jawOpen", "mouthClose", "mouthFunnel", "mouthPucker", "mouthLeft", "mouthRight",
                               "mouthSmileLeft", "mouthSmileRight", "mouthFrownLeft", "mouthFrownRight", "mouthDimpleLeft", "mouthDimpleRight",
                               "mouthStretchLeft", "mouthStretchRight", "mouthRollLower", "mouthRollUpper", "mouthShrugLower", "mouthShrugUpper",
                               "mouthPressLeft", "mouthPressRight", "mouthLowerDownLeft", "mouthLowerDownRight", "mouthUpperUpLeft", "mouthUpperUpRight",
                               "browDownLeft", "browDownRight", "browInnerUp", "browOuterUpLeft", "browOuterUpRight", "cheekPuff", "cheekSquintLeft",
                               "cheekSquintRight", "noseSneerLeft", "noseSneerRight", "tongueOut"};

static std::string normName(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') out += char(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += c;
    }
    return out;
}

static Vec3 jv3(const json& j, Vec3 def = {0, 0, 0}) {
    if (!j.is_array() || j.size() < 3) return def;
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}
static Quat jq(const json& j) {
    if (!j.is_array() || j.size() < 4) return {};
    return Quat(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()).normalized();
}

Avatar::Avatar() {
    for (int i = 0; i < HB_Count; i++) humanoid[i] = -1;
}
Avatar::~Avatar() = default;

int Avatar::findNodeByName(const std::string& n) const {
    for (size_t i = 0; i < nodes.size(); i++)
        if (nodes[i].name == n) return (int)i;
    return -1;
}

int Avatar::ensureNode(const std::string& path) {
    auto it = nodeByPath.find(path);
    if (it != nodeByPath.end()) return it->second;
    size_t slash = path.find_last_of('/');
    std::string parentPath = slash == std::string::npos ? "" : path.substr(0, slash);
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    int parent = ensureNode(parentPath);
    AvNode n;
    n.name = name;
    n.path = path;
    n.parent = parent;
    n.created = true;
    int idx = (int)nodes.size();
    nodes.push_back(n);
    nodes[parent].children.push_back(idx);
    nodeByPath[path] = idx;
    return idx;
}

// A modell csomópontjainak beillesztése. attachTo < 0: új gyökér; különben a modell gyökerének gyermekei ide kerülnek.
int Avatar::addModelNodes(const std::shared_ptr<Model>& m, int attachTo, bool includeRoot, bool collapseSingleRoot) {
    std::vector<int> map(m->nodes.size(), -1);
    // Unity: ha a modellnek egyetlen gyökér-csomópontja van (és a preserveHierarchy ki van kapcsolva),
    // az összeolvad a prefab gyökerével – a saját transzformja elvész.
    int collapsed = collapseSingleRoot && m->nodes[0].children.size() == 1 ? m->nodes[0].children[0] : -1;
    std::function<void(int, int)> add = [&](int mi, int parent) {
        const ModelNode& mn = m->nodes[mi];
        int idx;
        if (mi == 0 && !includeRoot) {
            idx = attachTo;
        } else if (mi == collapsed) {
            idx = parent;
        } else {
            AvNode n;
            n.name = mn.name;
            n.parent = parent;
            n.rest = n.local = mn.local;
            n.path = parent < 0 ? "" : (nodes[parent].path.empty() ? mn.name : nodes[parent].path + "/" + mn.name);
            idx = (int)nodes.size();
            nodes.push_back(n);
            if (parent >= 0) nodes[parent].children.push_back(idx);
            if (!nodeByPath.count(nodes[idx].path)) nodeByPath[nodes[idx].path] = idx;
        }
        map[mi] = idx;
        for (int c : mn.children) add(c, idx);
    };
    add(0, attachTo);
    // a modell csomópont -> avatár csomópont leképezést a háló csontjaihoz eltároljuk
    modelMap_[m.get()] = map;
    return map[0];
}

void Avatar::computeWorld(int from) {
    std::function<void(int)> rec = [&](int i) {
        AvNode& n = nodes[i];
        Mat4 l = n.local.matrix();
        n.world = n.parent >= 0 ? nodes[n.parent].world * l : l;
        for (int c : n.children) rec(c);
    };
    rec(from);
}

Vec3 Avatar::headPosition() const {
    if (humanoid[HB_Head] >= 0) return nodes[humanoid[HB_Head]].world.getTranslation();
    return viewPosition;
}

// ---------------------------------------------------------------------------
struct Avatar::LoadCtx {
    fs::path dir;
    Gfx* gfx;
    std::unordered_map<std::string, GpuTextureRef> texCache;  // textúra id vagy fájl -> textúra
    std::mutex mx;
    json texDefs;
    std::map<std::string, std::shared_ptr<Model>> models;
    std::atomic<float>* progress = nullptr;
};

bool Avatar::load(const fs::path& dir, Gfx& gfx, std::string& err, std::atomic<float>* progress) {
    if (!readJson(dir / L"avatar.json", manifest)) {
        err = T("Missing or invalid avatar.json");
        return false;
    }
    LoadCtx ctx;
    ctx.dir = dir;
    ctx.gfx = &gfx;
    ctx.progress = progress;
    ctx.texDefs = manifest.value("textures", json::object());
    id = manifest.value("id", narrow(dir.filename().wstring()));
    name = manifest.value("name", id);
    format = manifest.value("format", "fbx");

    auto loadModelFile = [&](const std::string& rel) -> std::shared_ptr<Model> {
        auto it = ctx.models.find(rel);
        if (it != ctx.models.end()) return it->second;
        auto m = std::make_shared<Model>();
        std::string e;
        if (!loadModel(dir / u8path(rel), *m, e)) {
            logMsg("Model load error (%s): %s", rel.c_str(), e.c_str());
            m = nullptr;
        }
        ctx.models[rel] = m;
        return m;
    };
    if (progress) *progress = 0.05f;
    auto base = loadModelFile(manifest.value("model", ""));
    if (!base) {
        err = T("The avatar's model could not be loaded.");
        return false;
    }
    if (progress) *progress = 0.35f;
    logMsg("  model loaded: %zu nodes, %zu meshes", base->nodes.size(), base->meshes.size());
    nodes.clear();
    nodeByPath.clear();
    addModelNodes(base, -1, true, format == "unity" && !manifest.value("preserveHierarchy", false));

    // --- mesh példányok létrehozása egy modellből ---
    struct PendingMesh {
        int mesh;
        std::shared_ptr<Model> model;
        const json* renderer;          // manifest renderer leírás (ha van)
        const json* modelInfo;         // csatolt modell (materialOverrides stb.)
    };
    std::vector<PendingMesh> pending;
    auto addMeshes = [&](const std::shared_ptr<Model>& m, const json* modelInfo) {
        auto& map = modelMap_[m.get()];
        for (size_t i = 0; i < m->meshes.size(); i++) {
            const ModelMesh& mm = m->meshes[i];
            AvMesh am;
            am.model = m;
            am.src = &mm;
            am.node = map[mm.node];
            am.path = nodes[am.node].path;
            am.name = mm.name;
            for (int j : mm.jointNodes) am.joints.push_back(map[j]);
            am.invBind = mm.invBind;
            am.weightsDefault.assign(mm.morphs.size(), 0.0f);
            for (size_t k = 0; k < mm.morphs.size(); k++) am.morphIndex[mm.morphs[k].name] = (int)k;
            meshes.push_back(std::move(am));
            pending.push_back({(int)meshes.size() - 1, m, nullptr, modelInfo});
        }
    };
    addMeshes(base, nullptr);

    // --- Unity manifest csomópontok ---
    std::unordered_map<std::string, const json*> rendererByPath;
    if (manifest.contains("nodes")) {
        for (auto& mn : manifest["nodes"]) {
            std::string p = mn.value("path", "");
            bool existed = nodeByPath.count(p) > 0;
            int idx = ensureNode(p);
            AvNode& n = nodes[idx];
            n.activeDefault = mn.value("active", true);
            if ((!existed || mn.contains("model")) && mn.contains("position")) {
                n.rest.t = jv3(mn["position"]);
                n.rest.r = jq(mn["quaternion"]);
                n.rest.s = jv3(mn["scale"], {1, 1, 1});
                n.local = n.rest;
            }
            if (mn.contains("renderer")) rendererByPath[p] = &mn["renderer"];
            if (mn.contains("model") && mn["model"].is_object()) {
                auto m = loadModelFile(mn["model"].value("file", ""));
                if (m) {
                    addModelNodes(m, idx, false, !mn["model"].value("preserveHierarchy", false));
                    addMeshes(m, &mn["model"]);
                }
            }
        }
        // renderer, aminek a hálója egy másik modellből jön (kicsomagolt prefab)
        for (auto& [p, r] : rendererByPath) {
            if (!r->contains("model")) continue;
            auto m = loadModelFile((*r)["model"].get<std::string>());
            if (!m) continue;
            int nodeIdx = nodeByPath[p];
            const std::string& nodeName = nodes[nodeIdx].name;
            const ModelMesh* pick = nullptr;
            for (auto& mm : m->meshes)
                if (m->nodes[mm.node].name == nodeName) pick = &mm;
            if (!pick && !m->meshes.empty()) pick = &m->meshes[0];
            if (!pick) continue;
            AvMesh am;
            am.model = m;
            am.src = pick;
            am.node = nodeIdx;
            am.path = p;
            am.name = nodeName;
            // csontok név szerint az avatár hierarchiájából
            for (int j : pick->jointNodes) {
                int found = findNodeByName(m->nodes[j].name);
                am.joints.push_back(found >= 0 ? found : nodeIdx);
            }
            am.invBind = pick->invBind;
            am.weightsDefault.assign(pick->morphs.size(), 0.0f);
            for (size_t k = 0; k < pick->morphs.size(); k++) am.morphIndex[pick->morphs[k].name] = (int)k;
            meshes.push_back(std::move(am));
            pending.push_back({(int)meshes.size() - 1, m, r, nullptr});
        }
    }
    // renderer leírások hozzárendelése a modellből jövő hálókhoz
    for (auto& pm : pending) {
        if (pm.renderer) continue;
        auto it = rendererByPath.find(meshes[pm.mesh].path);
        if (it != rendererByPath.end() && !it->second->contains("model")) pm.renderer = it->second;
    }
    for (auto& pm : pending) {
        AvMesh& am = meshes[pm.mesh];
        if (pm.renderer) {
            am.enabledDefault = pm.renderer->value("enabled", true);
            if (pm.renderer->contains("blendShapes")) {
                auto& bs = (*pm.renderer)["blendShapes"];
                for (size_t k = 0; k < bs.size() && k < am.weightsDefault.size(); k++) am.weightsDefault[k] = bs[k].get<float>() / 100.0f;
            }
        }
        if (pm.modelInfo && pm.modelInfo->contains("blendOverrides"))
            for (auto& bo : (*pm.modelInfo)["blendOverrides"]) {
                int k = bo.value("index", -1);
                if (k >= 0 && k < (int)am.weightsDefault.size()) am.weightsDefault[k] = bo.value("value", 0.0f) / 100.0f;
            }
        am.enabled = am.enabledDefault;
        am.weights = am.weightsDefault;
    }
    if (progress) *progress = 0.4f;
    logMsg("  hierarchy ready: %zu nodes, %zu meshes", nodes.size(), meshes.size());

    // --- anyagok ---
    // A setupMaterials a pending infót használja, ezért itt helyben végezzük el
    {
        auto texFile = [&](const std::string& texId) -> std::pair<std::string, bool> {
            auto it = ctx.texDefs.find(texId);
            if (it == ctx.texDefs.end()) return {"", true};
            return {it->value("file", ""), it->value("srgb", true)};
        };
        // textúra-kérések összegyűjtése, majd párhuzamos betöltés
        std::vector<std::tuple<std::string, std::wstring, bool, std::vector<uint8_t>>> requests;  // kulcs, fájl, srgb, memóriabájtok
        std::unordered_set<std::string> requested;
        auto requestTexId = [&](const std::string& texId, bool forceLinear) {
            if (texId.empty() || requested.count(texId)) return;
            auto [file, srgb] = texFile(texId);
            if (file.empty()) return;
            requested.insert(texId);
            requests.emplace_back(texId, (dir / u8path(file)).wstring(), srgb && !forceLinear, std::vector<uint8_t>{});
        };
        auto requestImage = [&](const std::shared_ptr<Model>& m, int img, bool srgb) {
            std::string key = "img:" + std::to_string((uintptr_t)m.get()) + ":" + std::to_string(img) + (srgb ? "s" : "l");
            if (requested.count(key) || img < 0 || img >= (int)m->images.size() || m->images[img].bytes.empty()) return key;
            requested.insert(key);
            requests.emplace_back(key, L"", srgb, m->images[img].bytes);
            return key;
        };

        struct SlotPlan {
            int mesh, slot;
            const json* def;  // manifest anyag
            const ModelMaterial* mm;
            std::shared_ptr<Model> model;
            const json* texMap;
            std::string defId;
        };
        std::vector<SlotPlan> plans;
        const json& mdefs = manifest.contains("materials") ? manifest["materials"] : json::object();
        const json& byName = manifest.contains("materialsByName") ? manifest["materialsByName"] : json::object();
        const json& fbxTex = manifest.contains("fbxTextures") ? manifest["fbxTextures"] : json::object();
        for (auto& pm : pending) {
            AvMesh& am = meshes[pm.mesh];
            size_t slots = std::max<size_t>(am.src->materials.size(), 1);
            for (auto& sm : am.src->submeshes) slots = std::max<size_t>(slots, sm.slot + 1);
            am.mats.assign(slots, -1);
            for (size_t s = 0; s < slots; s++) {
                SlotPlan sp{pm.mesh, (int)s, nullptr, nullptr, pm.model, &fbxTex, ""};
                if (pm.renderer && pm.renderer->contains("materials")) {
                    auto& ms = (*pm.renderer)["materials"];
                    if (s < ms.size() && ms[s].is_string() && mdefs.contains(ms[s].get<std::string>())) {
                        sp.defId = ms[s].get<std::string>();
                        sp.def = &mdefs[sp.defId];
                    }
                }
                if (!sp.def && pm.modelInfo) {
                    for (auto& mo : pm.modelInfo->value("materialOverrides", json::array()))
                        if (mo.value("slot", -1) == (int)s && mo["material"].is_string() && mdefs.contains(mo["material"].get<std::string>())) {
                            sp.defId = mo["material"].get<std::string>();
                            sp.def = &mdefs[sp.defId];
                        }
                }
                int mi = s < am.src->materials.size() ? am.src->materials[s] : -1;
                if (mi >= 0) sp.mm = &pm.model->materials[mi];
                if (!sp.def && sp.mm) {
                    const json& bn = pm.modelInfo ? pm.modelInfo->value("materialsByName", json::object()) : byName;
                    auto it = bn.find(sp.mm->name);
                    if (it != bn.end() && it->is_string() && mdefs.contains(it->get<std::string>())) {
                        sp.defId = it->get<std::string>();
                        sp.def = &mdefs[sp.defId];
                    }
                }
                if (pm.modelInfo && pm.modelInfo->contains("textures")) sp.texMap = &(*pm.modelInfo)["textures"];
                plans.push_back(sp);
            }
        }
        // anyag-csere animációk (matref) anyagai is kellenek
        for (auto& [cid, clip] : jobj(manifest, "clips").items())
            for (auto& cv : clip["curves"])
                if (cv["type"] == "matref")
                    for (auto& k : cv["keys"])
                        if (k[1].is_string() && mdefs.contains(k[1].get<std::string>())) {
                            auto& d = mdefs[k[1].get<std::string>()];
                            for (const char* f : {"map", "emissiveMap", "alphaMap", "matcap"})
                                if (d.contains(f) && d[f].is_string()) requestTexId(d[f], false);
                            if (d.contains("normalMap") && d["normalMap"].is_string()) requestTexId(d["normalMap"], true);
                        }
        auto fbxTexId = [&](const SlotPlan& sp, const ModelTexRef& r) -> std::string {
            if (!r.file.empty() && sp.texMap) {
                auto it = sp.texMap->find(toLower(r.file));
                if (it != sp.texMap->end()) return it->get<std::string>();
            }
            return "";
        };
        for (auto& sp : plans) {
            if (sp.def) {
                const json& d = *sp.def;
                for (const char* f : {"map", "emissiveMap", "alphaMap", "emissiveMask"})
                    if (d.contains(f) && d[f].is_string()) requestTexId(d[f], std::string(f) == "alphaMap" || std::string(f) == "emissiveMask");
                if (d.contains("normalMap") && d["normalMap"].is_string()) requestTexId(d["normalMap"], true);
                if (d.contains("matcap") && d["matcap"].is_string()) requestTexId(d["matcap"], false);
                for (auto& l : d.value("matcaps", json::array())) {
                    if (l["texture"].is_string()) requestTexId(l["texture"], false);
                    if (l["mask"].is_string()) requestTexId(l["mask"], true);
                }
            } else if (sp.mm) {
                for (auto* r : {&sp.mm->baseTex, &sp.mm->emissiveTex, &sp.mm->matcapTex}) {
                    std::string t = fbxTexId(sp, *r);
                    if (!t.empty()) requestTexId(t, false);
                    else if (r->image >= 0) requestImage(sp.model, r->image, true);
                }
                std::string t = fbxTexId(sp, sp.mm->normalTex);
                if (!t.empty()) requestTexId(t, true);
                else if (sp.mm->normalTex.image >= 0) requestImage(sp.model, sp.mm->normalTex.image, false);
            }
        }
        logMsg("  loading %zu textures…", requests.size());
        // párhuzamos dekódolás + GPU textúra létrehozás
        std::atomic<size_t> next{0}, done{0};
        auto worker = [&] {
            for (;;) {
                size_t i = next++;
                if (i >= requests.size()) return;
                auto& [key, file, srgb, bytes] = requests[i];
                GpuTextureRef t = bytes.empty() ? gfx.loadTextureFile(file, srgb) : gfx.loadTextureMemory(bytes.data(), bytes.size(), srgb);
                {
                    std::lock_guard<std::mutex> lk(ctx.mx);
                    ctx.texCache[key] = t;
                }
                size_t d = ++done;
                if (progress) *progress = 0.4f + 0.5f * float(d) / std::max<size_t>(1, requests.size());
            }
        };
        unsigned nt = std::max(1u, std::min(6u, std::thread::hardware_concurrency()));
        std::vector<std::thread> ts;
        for (unsigned k = 0; k < nt; k++) ts.emplace_back(worker);
        for (auto& t : ts) t.join();

        auto tex = [&](const json& d, const char* f) -> GpuTextureRef {
            if (!d.contains(f) || !d[f].is_string()) return nullptr;
            auto it = ctx.texCache.find(d[f].get<std::string>());
            return it == ctx.texCache.end() ? nullptr : it->second;
        };
        bool flipV = format == "unity" || format == "fbx" || format == "obj";
        auto buildFromDef = [&](AvMaterial& m, const json& d) {
            m.sourceName = d.value("name", "Material");
            m.family = d.value("family", "");
            m.flipV = flipV;
            std::string am = d.value("alphaMode", "opaque");
            m.alphaMode = am == "mask" ? AlphaMode::Mask : am == "blend" ? AlphaMode::Blend : am == "add" ? AlphaMode::Add : AlphaMode::Opaque;
            std::string side = d.value("side", "front");
            m.side = side == "back" ? 1 : side == "double" ? 2 : 0;
            m.unlit = d.value("unlit", false);
            m.renderQueue = d.value("renderQueue", -1);
            Vec3 c = jv3(d["color"], {1, 1, 1});
            MatParams& p = m.base;
            p.color[0] = c.x, p.color[1] = c.y, p.color[2] = c.z;
            p.opacity = d.value("opacity", 1.0f);
            Vec3 e = jv3(d["emissive"]);
            p.emissive[0] = e.x, p.emissive[1] = e.y, p.emissive[2] = e.z;
            p.emissiveIntensity = d.value("emissiveIntensity", 1.0f);
            p.emissiveUseBase = d.value("emissiveUseBase", false) ? 1.0f : 0.0f;
            p.hueShift = d.value("hueShift", 0.0f);
            p.hueShiftOn = d.value("hueShiftOn", true) ? 1.0f : 0.0f;
            p.saturation = d.value("saturation", 0.0f);
            p.brightness = d.value("brightness", 0.0f);
            p.metalness = d.value("metalness", 0.0f);
            p.roughness = d.value("roughness", 0.85f);
            p.normalScale = d.value("normalScale", 1.0f);
            p.alphaCutoff = d.value("alphaCutoff", 0.5f);
            if (d.contains("alphaMapST") && d["alphaMapST"].is_array())
                for (int k = 0; k < 4; k++) p.alphaMapST[k] = d["alphaMapST"][k];
            if (d.contains("shadeColor") && d["shadeColor"].is_array()) {
                Vec3 s = jv3(d["shadeColor"]);
                p.shade[0] = s.x, p.shade[1] = s.y, p.shade[2] = s.z;
            }
            if (d.contains("rimColor") && d["rimColor"].is_array()) {
                Vec3 r = jv3(d["rimColor"]);
                p.rim[0] = r.x, p.rim[1] = r.y, p.rim[2] = r.z;
            }
            if (d.contains("mapRepeat")) {
                m.uvST[0] = d["mapRepeat"][0], m.uvST[1] = d["mapRepeat"][1];
                m.uvST[2] = d["mapOffset"][0], m.uvST[3] = d["mapOffset"][1];
            }
            m.map = tex(d, "map");
            m.normalMap = tex(d, "normalMap");
            m.emissiveMap = tex(d, "emissiveMap");
            m.alphaMap = tex(d, "alphaMap");
            m.emissiveMask = tex(d, "emissiveMask");
            int li = 0;
            for (auto& l : d.value("matcaps", json::array())) {
                if (li >= 4) break;
                MatcapLayer& L = p.matcaps[li];
                L.enabled = l.value("enabled", false) ? 1.0f : 0.0f;
                Vec3 lc = jv3(l["color"], {1, 1, 1});
                L.color[0] = lc.x, L.color[1] = lc.y, L.color[2] = lc.z;
                L.intensity = l.value("intensity", 1.0f);
                L.emission = l.value("emission", 0.0f);
                L.replace = l.value("replace", 0.0f);
                L.multiply = l.value("multiply", 0.0f);
                L.add = l.value("add", 0.0f);
                L.hueShift = l.value("hueShift", 0.0f);
                L.hueShiftOn = l.value("hueShiftOn", false) ? 1.0f : 0.0f;
                m.matcapTex[li] = tex(l, "texture");
                m.matcapMask[li] = tex(l, "mask");
                L.hasTex = m.matcapTex[li] != nullptr;
                L.hasMask = m.matcapMask[li] != nullptr;
                li++;
            }
            // lilToon/MToon egyszerű matcap
            if (li == 0 && d.contains("matcap") && d["matcap"].is_string()) {
                MatcapLayer& L = p.matcaps[0];
                m.matcapTex[0] = tex(d, "matcap");
                L.enabled = m.matcapTex[0] ? 1.0f : 0.0f;
                L.hasTex = L.enabled > 0;
                float st = d.value("matcapStrength", 1.0f);
                std::string mode = d.value("matcapMode", "add");
                (mode == "replace" ? L.replace : mode == "multiply" ? L.multiply : L.add) = st;
            }
            if (flipV) {
                // Unity UV (bal-alsó origó) -> D3D: v' = v*sy + (1 - sy - oy)
                m.uvST[3] = 1.0f - m.uvST[1] - m.uvST[3];
            }
            // animálható tulajdonságok
            for (auto& [prop, field] : jobj(d, "bindings").items()) {
                std::string f = field.get<std::string>();
                float* ptr = nullptr;
                auto parts = split(f, '.');
                if (parts[0] == "hueShift") ptr = &m.cur.hueShift;
                else if (parts[0] == "hueShiftOn") ptr = &m.cur.hueShiftOn;
                else if (parts[0] == "saturation") ptr = &m.cur.saturation;
                else if (parts[0] == "emissiveIntensity") ptr = &m.cur.emissiveIntensity;
                else if (parts[0] == "opacity") ptr = &m.cur.opacity;
                else if (parts[0] == "color" && parts.size() > 1) ptr = &m.cur.color[std::atoi(parts[1].c_str()) % 3];
                else if (parts[0] == "emissive" && parts.size() > 1) ptr = &m.cur.emissive[std::atoi(parts[1].c_str()) % 3];
                else if (parts[0] == "alphaMapST" && parts.size() > 1) ptr = &m.cur.alphaMapST[std::atoi(parts[1].c_str()) % 4];
                else if (parts[0] == "matcaps" && parts.size() > 2) {
                    MatcapLayer& L = m.cur.matcaps[std::atoi(parts[1].c_str()) % 4];
                    const std::string& k = parts[2];
                    if (k == "enabled") ptr = &L.enabled;
                    else if (k == "intensity") ptr = &L.intensity;
                    else if (k == "emission") ptr = &L.emission;
                    else if (k == "replace") ptr = &L.replace;
                    else if (k == "multiply") ptr = &L.multiply;
                    else if (k == "add") ptr = &L.add;
                    else if (k == "hueShift") ptr = &L.hueShift;
                    else if (k == "hueShiftOn") ptr = &L.hueShiftOn;
                    else if (k == "color" && parts.size() > 3) ptr = &L.color[std::atoi(parts[3].c_str()) % 3];
                }
                if (ptr) m.bindings[prop] = (uint32_t)((char*)ptr - (char*)&m.cur);
            }
            m.cur = m.base;
        };
        auto imgTex = [&](const SlotPlan& sp, const ModelTexRef& r, bool srgb) -> GpuTextureRef {
            std::string t = fbxTexId(sp, r);
            if (!t.empty()) {
                auto it = ctx.texCache.find(t);
                if (it != ctx.texCache.end()) return it->second;
            }
            if (r.image >= 0) {
                std::string key = "img:" + std::to_string((uintptr_t)sp.model.get()) + ":" + std::to_string(r.image) + (srgb ? "s" : "l");
                auto it = ctx.texCache.find(key);
                if (it != ctx.texCache.end()) return it->second;
            }
            return nullptr;
        };
        auto buildFromModel = [&](AvMaterial& m, const SlotPlan& sp) {
            const ModelMaterial& mm = *sp.mm;
            m.sourceName = mm.name;
            m.family = mm.mtoon ? "mtoon" : "model";
            m.flipV = flipV;
            m.alphaMode = mm.alphaMode == "mask" ? AlphaMode::Mask : mm.alphaMode == "blend" ? AlphaMode::Blend : AlphaMode::Opaque;
            m.side = mm.doubleSided ? 2 : 0;
            m.unlit = mm.unlit;
            MatParams& p = m.base;
            p.color[0] = mm.baseColor.x, p.color[1] = mm.baseColor.y, p.color[2] = mm.baseColor.z;
            p.opacity = mm.baseColor.w;
            p.emissive[0] = mm.emissive.x, p.emissive[1] = mm.emissive.y, p.emissive[2] = mm.emissive.z;
            p.emissiveIntensity = mm.emissiveStrength;
            p.metalness = mm.metal;
            p.roughness = mm.rough;
            p.normalScale = mm.normalScale;
            p.alphaCutoff = mm.cutoff;
            if (mm.mtoon) {
                p.shade[0] = mm.shadeColor.x, p.shade[1] = mm.shadeColor.y, p.shade[2] = mm.shadeColor.z;
                p.rim[0] = mm.rimColor.x, p.rim[1] = mm.rimColor.y, p.rim[2] = mm.rimColor.z;
                m.outlineWidth = mm.outlineWidth;
                m.outlineColor = mm.outlineColor;
            }
            m.map = imgTex(sp, mm.baseTex, true);
            m.normalMap = imgTex(sp, mm.normalTex, false);
            m.emissiveMap = imgTex(sp, mm.emissiveTex, true);
            if (mm.matcapTex.valid()) {
                m.matcapTex[0] = imgTex(sp, mm.matcapTex, true);
                if (m.matcapTex[0]) {
                    p.matcaps[0].enabled = 1;
                    p.matcaps[0].hasTex = true;
                    p.matcaps[0].add = 1;
                    p.matcaps[0].color[0] = mm.matcapFactor.x, p.matcaps[0].color[1] = mm.matcapFactor.y, p.matcaps[0].color[2] = mm.matcapFactor.z;
                }
            }
            // FBX anyag textúra nélkül, fehér alapszínnel: kicsit szürkébbre vesszük, hogy ne égjen ki
            m.cur = m.base;
        };

        materials.clear();
        for (auto& sp : plans) {
            AvMesh& am = meshes[sp.mesh];
            AvMaterial m;
            m.key = am.path + "#" + std::to_string(sp.slot);
            if (sp.def) buildFromDef(m, *sp.def);
            else if (sp.mm) buildFromModel(m, sp);
            else {
                m.sourceName = "Alap";
                m.cur = m.base;
            }
            m.label = nodes[am.node].name + (am.mats.size() > 1 ? " – " + m.sourceName : "");
            am.mats[sp.slot] = (int)materials.size();
            materials.push_back(std::move(m));
        }
        // anyag-csere példányok előkészítése az animátornak: "<útvonal>#<slot>#<anyag id>" -> AvMaterial index
        for (auto& [cid, clip] : jobj(manifest, "clips").items())
            for (auto& cv : clip["curves"]) {
                if (cv["type"] != "matref") continue;
                std::string path = cv.value("path", "");
                int slot = cv.value("slot", 0);
                for (auto& k : cv["keys"]) {
                    if (!k[1].is_string()) continue;
                    std::string mid = k[1].get<std::string>();
                    if (!mdefs.contains(mid)) continue;
                    std::string key = path + "#" + std::to_string(slot) + "#" + mid;
                    bool exists = false;
                    for (auto& mm : materials)
                        if (mm.key == key) exists = true;
                    if (exists) continue;
                    AvMaterial m;
                    buildFromDef(m, mdefs[mid]);
                    m.key = key;
                    m.label = m.sourceName;
                    materials.push_back(std::move(m));
                }
            }
    }
    if (progress) *progress = 0.92f;
    logMsg("  materials ready: %zu", materials.size());

    // pihenő világ mátrixok, határoló doboz
    for (auto& n : nodes) n.local = n.rest;
    computeWorld(0);
    restWorldRot_.resize(nodes.size());
    restWorldPos_.resize(nodes.size());
    for (size_t i = 0; i < nodes.size(); i++) {
        restWorldRot_[i] = nodes[i].world.getRotation();
        restWorldPos_[i] = nodes[i].world.getTranslation();
    }
    for (auto& n : nodes) n.worldScale = n.world.getScale().x;

    setupHumanoid();
    setupFace();
    setupParts();
    // VRM csomópont kényszerek
    constraints_.clear();
    for (auto& [m, map] : modelMap_) {
        for (auto& [mn, c] : m->constraints) {
            Constraint k;
            k.dst = map[mn];
            auto axisOf = [](const std::string& a) {
                if (a == "NegativeX") return Vec3(-1, 0, 0);
                if (a == "PositiveY" || a == "Y") return Vec3(0, 1, 0);
                if (a == "NegativeY") return Vec3(0, -1, 0);
                if (a == "PositiveZ" || a == "Z") return Vec3(0, 0, 1);
                if (a == "NegativeZ") return Vec3(0, 0, -1);
                return Vec3(1, 0, 0);
            };
            const json* body = nullptr;
            if (c.contains("roll")) k.type = 0, body = &c["roll"], k.axis = axisOf((*body).value("rollAxis", "X"));
            else if (c.contains("aim")) k.type = 1, body = &c["aim"], k.axis = axisOf((*body).value("aimAxis", "PositiveX"));
            else if (c.contains("rotation")) k.type = 2, body = &c["rotation"];
            if (!body) continue;
            int srcG = body->value("source", -1);
            k.src = srcG >= 0 && srcG + 1 < (int)map.size() ? map[srcG + 1] : -1;
            k.weight = body->value("weight", 1.0f);
            if (k.dst >= 0 && k.src >= 0) constraints_.push_back(k);
        }
    }
    if (!constraints_.empty()) logMsg("Node constraints: %zu", constraints_.size());
    for (auto& am : meshes) {
        Mat4 w = nodes[am.node].world;
        Vec3 a = w.transformPoint(am.src->bmin), b = w.transformPoint(am.src->bmax);
        std::string mats;
        for (int mi : am.mats)
            if (mi >= 0) mats += ::format("[%s a%d em%.1f*%.2f] ", materials[mi].sourceName.c_str(), (int)materials[mi].alphaMode, materials[mi].base.emissiveIntensity,
                                        std::max({materials[mi].base.emissive[0], materials[mi].base.emissive[1], materials[mi].base.emissive[2]}));
        logMsg("  mesh %-40s %s%s skin=%d size=%.3f node.scale=%.3f %s", am.path.c_str(), nodes[am.node].activeDefault ? "" : "(inactive) ", am.enabledDefault ? "" : "(off) ",
               (int)am.src->skinned, length(b - a), nodes[am.node].world.getScale().x, mats.c_str());
    }

    // magasság és nézőpont
    Vec3 bmin(1e9f, 1e9f, 1e9f), bmax(-1e9f, -1e9f, -1e9f);
    for (auto& am : meshes) {
        if (!am.src || am.src->pos.empty()) continue;
        if (!nodes[am.node].activeDefault || !am.enabledDefault) continue;
        // durva becslés: a csontok világ pozícióiból vagy a csomópontból
        if (am.src->skinned) {
            for (size_t j = 0; j < am.joints.size(); j++) {
                Vec3 p = nodes[am.joints[j]].world.getTranslation();
                bmin = vmin(bmin, p);
                bmax = vmax(bmax, p);
            }
        } else {
            Mat4 w = nodes[am.node].world;
            bmin = vmin(bmin, w.transformPoint(am.src->bmin));
            bmax = vmax(bmax, w.transformPoint(am.src->bmax));
        }
    }
    if (bmax.y > bmin.y) {
        boundsMin = bmin;
        boundsMax = bmax;
    }
    if (humanoid[HB_Head] >= 0) {
        Vec3 h = nodes[humanoid[HB_Head]].world.getTranslation();
        height = std::max(h.y + 0.12f, 0.3f);
        viewPosition = h + Vec3(0, 0.06f, 0.08f);
    } else
        height = boundsMax.y;
    if (manifest.contains("viewPosition")) viewPosition = jv3(manifest["viewPosition"], viewPosition);

    // paraméterek
    params.clear();
    paramValues.clear();
    if (hasExpressions()) {
        auto& ex = manifest["expressions"];
        for (auto& p : ex.value("parameters", json::array())) {
            Param pr{p.value("name", ""), p.value("type", "float"), p.value("default", 0.0f), true};
            params.push_back(pr);
            paramValues[pr.name] = pr.def;
        }
        if (ex.contains("fx") && ex["fx"].is_object())
            for (auto& p : ex["fx"].value("params", json::array())) {
                std::string n = p.value("name", "");
                if (!paramValues.count(n)) {
                    params.push_back({n, p.value("type", "float"), p.value("default", 0.0f), false});
                    paramValues[n] = p.value("default", 0.0f);
                }
            }
    }
    // VRChat beépített paraméterek
    for (auto& [n, v] : std::initializer_list<std::pair<const char*, float>>{{"IsLocal", 1}, {"Grounded", 1}, {"Upright", 1}, {"VRMode", 0}, {"TrackingType", 1}, {"AFK", 0}, {"Seated", 0},
                                                                           {"InStation", 0}, {"GestureLeft", 0}, {"GestureRight", 0}, {"GestureLeftWeight", 1}, {"GestureRightWeight", 1},
                                                                           {"Viseme", 0}, {"Voice", 0}, {"MuteSelf", 0}, {"VelocityX", 0}, {"VelocityY", 0}, {"VelocityZ", 0},
                                                                           {"AngularY", 0}, {"IsOnFriendsList", 1}, {"ScaleFactor", 1}, {"ScaleModified", 0}}) {
        if (!paramValues.count(n)) paramValues[n] = v;
    }
    animator = std::make_unique<AnimatorSim>();
    if (hasExpressions() && manifest["expressions"].contains("fx") && manifest["expressions"]["fx"].is_object())
        animator->init(manifest["expressions"]["fx"], manifest.value("clips", json::object()), *this);
    physics = std::make_unique<PhysicsSim>();
    physics->init(*this);

    // GPU pufferek (eszköz szinten, szálbiztos)
    for (auto& am : meshes) {
        const ModelMesh& mm = *am.src;
        size_t nv = mm.pos.size();
        am.dynamicPos = !mm.morphs.empty();
        std::vector<float> pn(nv * 6);
        for (size_t v = 0; v < nv; v++) {
            pn[v * 6 + 0] = mm.pos[v].x, pn[v * 6 + 1] = mm.pos[v].y, pn[v * 6 + 2] = mm.pos[v].z;
            pn[v * 6 + 3] = mm.nrm[v].x, pn[v * 6 + 4] = mm.nrm[v].y, pn[v * 6 + 5] = mm.nrm[v].z;
        }
        if (am.dynamicPos) {
            am.vbPosNrm = gfx.createBuffer((UINT)(pn.size() * 4), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DYNAMIC, nullptr, D3D11_CPU_ACCESS_WRITE);
            am.morphScratch = pn;
            am.uploadedWeights.assign(am.weights.size(), -1.0f);
        } else
            am.vbPosNrm = gfx.createBuffer((UINT)(pn.size() * 4), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_IMMUTABLE, pn.data());
        struct StaticV {
            float uv[2];
            float tan[4];
            uint16_t j[4];
            float w[4];
        };
        std::vector<StaticV> sv(nv);
        for (size_t v = 0; v < nv; v++) {
            sv[v].uv[0] = mm.uv.size() > v ? mm.uv[v].x : 0;
            sv[v].uv[1] = mm.uv.size() > v ? mm.uv[v].y : 0;
            Vec4 t = mm.tan.size() > v ? mm.tan[v] : Vec4(1, 0, 0, 1);
            sv[v].tan[0] = t.x, sv[v].tan[1] = t.y, sv[v].tan[2] = t.z, sv[v].tan[3] = t.w;
            if (mm.skinned && mm.joints.size() > v) {
                for (int k = 0; k < 4; k++) sv[v].j[k] = mm.joints[v][k], sv[v].w[k] = mm.weights[v][k];
            } else {
                sv[v].j[0] = sv[v].j[1] = sv[v].j[2] = sv[v].j[3] = 0;
                sv[v].w[0] = 1, sv[v].w[1] = sv[v].w[2] = sv[v].w[3] = 0;
            }
        }
        am.vbStatic = gfx.createBuffer((UINT)(sv.size() * sizeof(StaticV)), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_IMMUTABLE, sv.data());
        am.ib = gfx.createBuffer((UINT)(mm.indices.size() * 4), D3D11_BIND_INDEX_BUFFER, D3D11_USAGE_IMMUTABLE, mm.indices.data());
        size_t nb = std::max<size_t>(1, am.joints.size());
        am.bones = gfx.createBuffer((UINT)(nb * 64), D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DYNAMIC, nullptr, D3D11_CPU_ACCESS_WRITE, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 64);
        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = DXGI_FORMAT_UNKNOWN;
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sd.Buffer.NumElements = (UINT)nb;
        gfx.dev()->CreateShaderResourceView(am.bones.Get(), &sd, &am.bonesSrv);
    }
    if (progress) *progress = 1.0f;
    logMsg("Avatar loaded: %s (%zu nodes, %zu meshes, %zu materials)", name.c_str(), nodes.size(), meshes.size(), materials.size());
    return true;
}

// ---------------------------------------------------------------------------
// Humanoid csontok felismerése
void Avatar::setupHumanoid() {
    for (int i = 0; i < HB_Count; i++) humanoid[i] = -1;
    auto findPreferArmature = [&](const std::string& n) {
        // több azonos nevű csomópont esetén a leghosszabb (legmélyebb) útvonalú csontot választjuk
        int best = -1;
        for (size_t i = 0; i < nodes.size(); i++)
            if (nodes[i].name == n && (best < 0 || nodes[i].path.size() < nodes[best].path.size())) best = (int)i;
        return best;
    };
    if (manifest.contains("humanoid") && manifest["humanoid"].is_object()) {
        for (auto& [hn, bn] : manifest["humanoid"].items()) {
            for (int b = 0; b < HB_Count; b++)
                if (hn == kHumanBoneNames[b]) humanoid[b] = findPreferArmature(bn.get<std::string>());
        }
    }
    // VRM
    std::shared_ptr<Model> base = meshes.empty() ? nullptr : meshes[0].model;
    if (base && base->vrmVersion >= 0 && base->vrm.is_object() && base->vrm.contains("humanoid")) {
        auto& map = modelMap_[base.get()];
        auto vrmName = [](std::string s) {
            // VRM név -> Unity humán név (pl. leftUpperArm -> LeftUpperArm, leftThumbMetacarpal -> LeftThumbProximal)
            if (s.empty()) return s;
            s[0] = (char)toupper(s[0]);
            auto rep = [&](const char* a, const char* b) {
                size_t p = s.find(a);
                if (p != std::string::npos) s.replace(p, strlen(a), b);
            };
            if (s.find("ThumbMetacarpal") != std::string::npos) rep("ThumbMetacarpal", "ThumbProximal");
            else if (s.find("ThumbProximal") != std::string::npos && s.find("Intermediate") == std::string::npos) rep("ThumbProximal", "ThumbIntermediate@");
            rep("Intermediate@", "Intermediate");
            return s;
        };
        auto assign = [&](const std::string& vname, int gltfNode) {
            std::string un = vrmName(vname);
            int idx = gltfNode >= 0 && gltfNode + 1 < (int)map.size() ? map[gltfNode + 1] : -1;
            for (int b = 0; b < HB_Count; b++)
                if (un == kHumanBoneNames[b]) humanoid[b] = idx;
        };
        auto& hum = base->vrm["humanoid"];
        if (base->vrmVersion == 1 && hum.contains("humanBones") && hum["humanBones"].is_object()) {
            for (auto& [k, v] : hum["humanBones"].items()) assign(k, v.value("node", -1));
        } else if (hum.contains("humanBones") && hum["humanBones"].is_array()) {
            // VRM 0.x: thumbProximal/Intermediate/Distal közvetlenül egyezik
            for (auto& b : hum["humanBones"]) {
                std::string bn = b.value("bone", "");
                if (bn.empty()) continue;
                std::string un = bn;
                un[0] = (char)toupper(un[0]);
                int idx = b.value("node", -1);
                int av = idx + 1 < (int)map.size() && idx >= 0 ? map[idx + 1] : -1;
                for (int k = 0; k < HB_Count; k++)
                    if (un == kHumanBoneNames[k]) humanoid[k] = av;
            }
        }
    }
    // név alapú felismerés (Mixamo, VRoid, Blender rigify, VRChat stb.)
    if (humanoid[HB_Hips] < 0) {
        struct Pat {
            int bone;
            std::vector<const char*> names;
        };
        std::vector<Pat> pats = {
            {HB_Hips, {"hips", "pelvis", "jbipchips", "hip"}},
            {HB_Spine, {"spine", "jbipcspine", "spine1"}},
            {HB_Chest, {"chest", "jbipcchest", "spine2", "upperbody"}},
            {HB_UpperChest, {"upperchest", "jbipcupperchest", "spine3"}},
            {HB_Neck, {"neck", "jbipcneck"}},
            {HB_Head, {"head", "jbipchead"}},
            {HB_LeftEye, {"lefteye", "eyel", "jadjlfaceeye", "eyeleft"}},
            {HB_RightEye, {"righteye", "eyer", "jadjrfaceeye", "eyeright"}},
            {HB_LeftShoulder, {"leftshoulder", "shoulderl", "jbiplshoulder", "lshoulder", "claviclel", "leftclavicle"}},
            {HB_LeftUpperArm, {"leftarm", "leftupperarm", "upperarml", "jbiplupperarm", "luparm", "arml", "larm"}},
            {HB_LeftLowerArm, {"leftforearm", "leftlowerarm", "lowerarml", "forearml", "jbipllowerarm", "lforearm", "elbowl"}},
            {HB_LeftHand, {"lefthand", "handl", "jbiplhand", "lhand", "wristl"}},
            {HB_RightShoulder, {"rightshoulder", "shoulderr", "jbiprshoulder", "rshoulder", "clavicler", "rightclavicle"}},
            {HB_RightUpperArm, {"rightarm", "rightupperarm", "upperarmr", "jbiprupperarm", "ruparm", "armr", "rarm"}},
            {HB_RightLowerArm, {"rightforearm", "rightlowerarm", "lowerarmr", "forearmr", "jbiprlowerarm", "rforearm", "elbowr"}},
            {HB_RightHand, {"righthand", "handr", "jbiprhand", "rhand", "wristr"}},
            {HB_LeftUpperLeg, {"leftupleg", "leftupperleg", "upperlegl", "thighl", "jbiplupperleg", "lthigh", "legl"}},
            {HB_LeftLowerLeg, {"leftleg", "leftlowerleg", "lowerlegl", "shinl", "calfl", "kneel", "jbipllowerleg", "lcalf"}},
            {HB_LeftFoot, {"leftfoot", "footl", "jbiplfoot", "lfoot", "anklel"}},
            {HB_RightUpperLeg, {"rightupleg", "rightupperleg", "upperlegr", "thighr", "jbiprupperleg", "rthigh", "legr"}},
            {HB_RightLowerLeg, {"rightleg", "rightlowerleg", "lowerlegr", "shinr", "calfr", "kneer", "jbiprlowerleg", "rcalf"}},
            {HB_RightFoot, {"rightfoot", "footr", "jbiprfoot", "rfoot", "ankler"}},
        };
        for (auto& p : pats) {
            for (const char* cand : p.names) {
                for (size_t i = 0; i < nodes.size() && humanoid[p.bone] < 0; i++) {
                    std::string n = normName(nodes[i].name);
                    // gyakori előtagok levágása
                    for (const char* pre : {"mixamorig", "bip01", "bip001", "def", "armature", "character"})
                        if (startsWith(n, pre) && n.size() > strlen(pre) + 2) n = n.substr(strlen(pre));
                    if (n == cand) humanoid[p.bone] = (int)i;
                }
                if (humanoid[p.bone] >= 0) break;
            }
        }
        // ujjak: a kéz alatti láncok
        for (int side = 0; side < 2; side++) {
            int hand = humanoid[side == 0 ? HB_LeftHand : HB_RightHand];
            if (hand < 0) continue;
            const char* fingers[5][3] = {{"thumb", "", ""}, {"index", "", ""}, {"middle", "", ""}, {"ring", "", ""}, {"little", "pinky", ""}};
            for (int f = 0; f < 5; f++) {
                for (int c : nodes[hand].children) {
                    std::string n = normName(nodes[c].name);
                    bool match = n.find(fingers[f][0]) != std::string::npos || (fingers[f][1][0] && n.find(fingers[f][1]) != std::string::npos);
                    if (!match) continue;
                    int cur = c;
                    int base = (side == 0 ? HB_LeftThumb1 : HB_RightThumb1) + f * 3;
                    for (int k = 0; k < 3 && cur >= 0; k++) {
                        humanoid[base + k] = cur;
                        cur = nodes[cur].children.empty() ? -1 : nodes[cur].children[0];
                    }
                    break;
                }
            }
        }
    }
    int found = 0;
    for (int i = 0; i < HB_Count; i++) found += humanoid[i] >= 0;
    logMsg("Humanoid bones: %d/%d", found, (int)HB_Count);
}

// ---------------------------------------------------------------------------
// Arc: pislogás, viseme, ARKit, VRM kifejezések, szemek
void Avatar::setupFace() {
    auto meshByPath = [&](const std::string& p) {
        for (size_t i = 0; i < meshes.size(); i++)
            if (meshes[i].path == p) return (int)i;
        return -1;
    };
    auto shapeByName = [&](int mesh, const std::string& n) -> BlendRef {
        if (mesh < 0) return {};
        auto it = meshes[mesh].morphIndex.find(n);
        if (it == meshes[mesh].morphIndex.end()) return {};
        return {mesh, it->second};
    };
    auto findShapeAny = [&](std::initializer_list<const char*> names) -> BlendRef {
        for (const char* nm : names) {
            std::string want = normName(nm);
            for (size_t i = 0; i < meshes.size(); i++)
                for (auto& [n, k] : meshes[i].morphIndex)
                    if (normName(n) == want) return {(int)i, k};
        }
        return {};
    };
    blink = blinkL = blinkR = lookUp = lookDown = mouthOpen = {};
    for (auto& v : visemes) v = {};
    if (manifest.contains("face") && manifest["face"].is_object()) {
        auto& f = manifest["face"];
        auto& lids = f["eyelids"];
        if (lids.is_object() && lids.value("type", 0) == 2 && lids["mesh"].is_string()) {
            int m = meshByPath(lids["mesh"]);
            auto idx = [&](const char* k) -> BlendRef {
                if (m < 0 || !lids[k].is_number()) return {};
                int i = lids[k].get<int>();
                return i >= 0 && i < (int)meshes[m].weightsDefault.size() ? BlendRef{m, i} : BlendRef{};
            };
            blink = idx("blink");
            lookUp = idx("lookUp");
            lookDown = idx("lookDown");
        }
        int vm = f["visemeMesh"].is_string() ? meshByPath(f["visemeMesh"]) : -1;
        for (int k = 0; k < 15; k++)
            if (f["visemes"].contains(kVisemeKeys[k])) visemes[k] = shapeByName(vm, f["visemes"][kVisemeKeys[k]].get<std::string>());
        if (f["mouthOpen"].is_string() && !f["mouthOpen"].get<std::string>().empty()) mouthOpen = shapeByName(vm, f["mouthOpen"]);
        auto& e = f["eyes"];
        if (e.is_object()) {
            if (e["left"].is_string() && nodeByPath.count(e["left"])) eyeL = nodeByPath[e["left"]];
            if (e["right"].is_string() && nodeByPath.count(e["right"])) eyeR = nodeByPath[e["right"]];
            if (e["up"].is_object()) {
                eyeUp[0] = jq(e["up"]["left"]), eyeUp[1] = jq(e["up"]["right"]);
                eyeDown[0] = jq(e["down"]["left"]), eyeDown[1] = jq(e["down"]["right"]);
                eyeLeft[0] = jq(e["lookLeft"]["left"]), eyeLeft[1] = jq(e["lookLeft"]["right"]);
                eyeRight[0] = jq(e["lookRight"]["left"]), eyeRight[1] = jq(e["lookRight"]["right"]);
                hasEyeLimits = std::fabs(eyeUp[0].w) < 0.99999f || std::fabs(eyeLeft[0].w) < 0.99999f;
            }
        }
    }
    if (eyeL < 0) eyeL = humanoid[HB_LeftEye];
    if (eyeR < 0) eyeR = humanoid[HB_RightEye];

    // VRM kifejezések
    vrmExpressions.clear();
    vrmExprIndex.clear();
    std::shared_ptr<Model> base = meshes.empty() ? nullptr : meshes[0].model;
    if (base && base->vrm.is_object()) {
        auto meshesByGltfNode = [&](int gltfNode) {
            std::vector<int> out;
            auto& map = modelMap_[base.get()];
            int av = gltfNode + 1 < (int)map.size() && gltfNode >= 0 ? map[gltfNode + 1] : -1;
            for (size_t i = 0; i < meshes.size(); i++)
                if (meshes[i].node == av) out.push_back((int)i);
            return out;
        };
        auto meshesByGltfMesh = [&](int gm) {
            std::vector<int> out;
            for (size_t i = 0; i < meshes.size(); i++)
                if (meshes[i].src->sourceMesh == gm && meshes[i].model == base) out.push_back((int)i);
            return out;
        };
        auto addExpr = [&](const std::string& name, const json& binds, bool v1) {
            VrmExpr ex;
            ex.name = name;
            for (auto& b : binds) {
                std::vector<int> ms = v1 ? meshesByGltfNode(b.value("node", -1)) : meshesByGltfMesh(b.value("mesh", -1));
                int idx = b.value("index", -1);
                float w = v1 ? b.value("weight", 1.0f) : b.value("weight", 100.0f) / 100.0f;
                for (int m : ms)
                    if (idx >= 0 && idx < (int)meshes[m].weightsDefault.size()) ex.binds.push_back({{m, idx}, w});
            }
            vrmExprIndex[toLower(name)] = (int)vrmExpressions.size();
            vrmExpressions.push_back(ex);
        };
        if (base->vrmVersion == 1 && base->vrm.contains("expressions")) {
            for (const char* group : {"preset", "custom"})
                for (auto& [k, v] : jobj(base->vrm["expressions"], group).items()) addExpr(k, v.value("morphTargetBinds", json::array()), true);
        } else if (base->vrm.contains("blendShapeMaster")) {
            for (auto& g : base->vrm["blendShapeMaster"].value("blendShapeGroups", json::array())) {
                std::string preset = g.value("presetName", "unknown");
                std::string nm = preset != "unknown" && !preset.empty() ? preset : g.value("name", "expr");
                // VRM 0 előbeállítás nevek egységesítése
                if (nm == "a") nm = "aa";
                else if (nm == "i") nm = "ih";
                else if (nm == "u") nm = "ou";
                else if (nm == "e") nm = "ee";
                else if (nm == "o") nm = "oh";
                else if (nm == "blink_l") nm = "blinkLeft";
                else if (nm == "blink_r") nm = "blinkRight";
                else if (nm == "joy") nm = "happy";
                else if (nm == "sorrow") nm = "sad";
                else if (nm == "fun") nm = "relaxed";
                addExpr(nm, g.value("binds", json::array()), false);
            }
        }
    }

    // ARKit (perfect sync) blendshape-ek
    arkitShapes.clear();
    for (const char* a : kArkit) {
        std::string want = normName(a);
        std::string wantShort = want;  // eyeBlink_L stílus
        size_t pl = wantShort.rfind("left"), pr = wantShort.rfind("right");
        if (pl != std::string::npos && pl + 4 == wantShort.size()) wantShort = wantShort.substr(0, pl) + "l";
        else if (pr != std::string::npos && pr + 5 == wantShort.size()) wantShort = wantShort.substr(0, pr) + "r";
        for (size_t i = 0; i < meshes.size(); i++)
            for (auto& [n, k] : meshes[i].morphIndex) {
                std::string nn = normName(n);
                // gyakori előtagok (pl. "blendShape1.")
                for (const char* pre : {"blendshape1", "blendshape2", "blendshape", "facialblends", "arkit"})
                    if (startsWith(nn, pre) && nn.size() > strlen(pre) + 3) nn = nn.substr(strlen(pre));
                if (nn == want || (wantShort != want && nn == wantShort)) arkitShapes[toLower(a)].push_back({(int)i, k});
            }
    }

    // tartalék nevek
    if (blink.mesh < 0) blink = findShapeAny({"vrc.blink", "blink", "eyes_closed", "eyesclosed", "blink_both", "まばたき"});
    if (blinkL.mesh < 0) blinkL = findShapeAny({"vrc.blink_left", "blink_l", "blinkleft", "wink_l", "eyeblinkleft", "ウィンク"});
    if (blinkR.mesh < 0) blinkR = findShapeAny({"vrc.blink_right", "blink_r", "blinkright", "wink_r", "eyeblinkright", "ウィンク右"});
    const char* vrcNames[15] = {"vrc.v_sil", "vrc.v_pp", "vrc.v_ff", "vrc.v_th", "vrc.v_dd", "vrc.v_kk", "vrc.v_ch", "vrc.v_ss", "vrc.v_nn", "vrc.v_rr", "vrc.v_aa", "vrc.v_e", "vrc.v_ih", "vrc.v_oh", "vrc.v_ou"};
    for (int k = 0; k < 15; k++)
        if (visemes[k].mesh < 0) visemes[k] = findShapeAny({vrcNames[k]});
    if (visemes[10].mesh < 0) visemes[10] = findShapeAny({"a", "aa", "mouth_a", "あ", "vowel_a"});
    if (visemes[12].mesh < 0) visemes[12] = findShapeAny({"i", "ih", "mouth_i", "い", "vowel_i"});
    if (visemes[14].mesh < 0) visemes[14] = findShapeAny({"u", "ou", "mouth_u", "う", "vowel_u"});
    if (visemes[11].mesh < 0) visemes[11] = findShapeAny({"e", "ee", "mouth_e", "え", "vowel_e"});
    if (visemes[13].mesh < 0) visemes[13] = findShapeAny({"o", "oh", "mouth_o", "お", "vowel_o"});
    if (mouthOpen.mesh < 0) mouthOpen = findShapeAny({"jawopen", "mouth_open", "mouthopen"});
}

void Avatar::setupParts() {
    parts.clear();
    std::map<std::string, int> byPath;
    for (size_t i = 0; i < meshes.size(); i++) {
        auto it = byPath.find(meshes[i].path);
        if (it == byPath.end()) {
            Part p;
            p.path = meshes[i].path;
            // a csomópont (Unity GameObject) neve beszédesebb, mint az FBX háló neve
            const std::string& nodeName = nodes[meshes[i].node].name;
            p.label = !nodeName.empty() ? nodeName : (meshes[i].name.empty() ? meshes[i].path : meshes[i].name);
            byPath[p.path] = (int)parts.size();
            parts.push_back(p);
            it = byPath.find(meshes[i].path);
        }
        Part& p = parts[it->second];
        p.meshes.push_back((int)i);
        for (int m : meshes[i].mats)
            if (m >= 0 && std::find(p.materials.begin(), p.materials.end(), m) == p.materials.end()) p.materials.push_back(m);
    }
}

void Avatar::setParam(const std::string& n, float v) { paramValues[n] = v; }

void Avatar::resetPhysics() {
    if (physics) physics->reset(*this);
}

// ---------------------------------------------------------------------------
// Póz
void Avatar::setBoneDelta(int b, const Quat& d) {
    int n = humanoid[b];
    if (n < 0) return;
    boneDelta_[n] = d * boneDelta_[n];
}

void Avatar::applyPose(float dt, const PoseInput& in, const PoseSettings& ps) {
    boneDelta_.assign(nodes.size(), Quat());
    auto rp = [&](int node) { return restWorldPos_[node]; };

    float t = in.time;
    // --- karok ---
    for (int side = 0; side < 2; side++) {
        int up = humanoid[side == 0 ? HB_LeftUpperArm : HB_RightUpperArm];
        int lo = humanoid[side == 0 ? HB_LeftLowerArm : HB_RightLowerArm];
        int hand = humanoid[side == 0 ? HB_LeftHand : HB_RightHand];
        if (up < 0 || lo < 0) continue;
        Vec3 dir = normalize(rp(lo) - rp(up));
        float curDown = std::asin(clampf(-dir.y, -1, 1));
        float want = ps.armDown * kDeg2Rad;
        Vec3 axis = normalize(cross(dir, Vec3(0, -1, 0)));
        if (length(axis) < 0.5f) axis = Vec3(0, 0, side == 0 ? -1.0f : 1.0f);
        Quat lower = Quat::axisAngle(axis, want - curDown);
        // kis légzés a karokban
        float breath = std::sin(t * 2 * kPi / 4.2f) * 0.015f * ps.breathing;
        Quat fwd = Quat::axisAngle({1, 0, 0}, -ps.armForward * kDeg2Rad + breath);
        setBoneDelta(side == 0 ? HB_LeftUpperArm : HB_RightUpperArm, fwd * lower);
        // könyök
        if (hand >= 0) {
            Vec3 fdir = normalize(rp(hand) - rp(lo));
            Vec3 eaxis = normalize(cross(fdir, Vec3(0, 0, 1)));
            if (length(eaxis) > 0.5f) setBoneDelta(side == 0 ? HB_LeftLowerArm : HB_RightLowerArm, Quat::axisAngle(eaxis, ps.elbowBend * kDeg2Rad));
            // csukló
            setBoneDelta(side == 0 ? HB_LeftHand : HB_RightHand, Quat::axisAngle(eaxis, ps.wristBend * kDeg2Rad));
        }
        // ujjak
        if (hand >= 0) {
            Vec3 hdir = dir;
            Vec3 palm = side == 0 ? normalize(cross(hdir, Vec3(0, 0, 1))) : normalize(cross(Vec3(0, 0, 1), hdir));
            int gesture = side == 0 ? in.gestureLeft : in.gestureRight;
            float relax = ps.fingerCurl;
            float curl[5] = {relax * 0.6f, relax, relax * 1.1f, relax * 1.2f, relax * 1.3f};
            switch (gesture) {
                case 1: curl[0] = 0.6f, curl[1] = curl[2] = curl[3] = curl[4] = 1.0f; break;               // ököl
                case 2: curl[0] = curl[1] = curl[2] = curl[3] = curl[4] = 0.0f; break;                      // nyitott tenyér
                case 3: curl[0] = 0.6f, curl[1] = 0, curl[2] = curl[3] = curl[4] = 1.0f; break;             // mutatás
                case 4: curl[0] = 0.6f, curl[1] = curl[2] = 0, curl[3] = curl[4] = 1.0f; break;             // béke
                case 5: curl[0] = 0.5f, curl[1] = 0, curl[2] = curl[3] = 1.0f, curl[4] = 0; break;          // rock
                case 6: curl[0] = 0, curl[1] = 0, curl[2] = curl[3] = curl[4] = 1.0f; break;                // pisztoly
                case 7: curl[0] = 0, curl[1] = curl[2] = curl[3] = curl[4] = 1.0f; break;                   // hüvelykujj fel
                default: break;
            }
            float jointAngle[3] = {65, 85, 55};
            // követett kéz: az ujjak a mért szögek szerint (lágy átmenettel)
            fingerWeight_[side] = damp(fingerWeight_[side], in.hands[side].valid ? 1.0f : 0.0f, 10, dt);
            float fw = fingerWeight_[side];
            for (int f = 0; f < 5; f++) {
                int baseBone = (side == 0 ? HB_LeftThumb1 : HB_RightThumb1) + f * 3;
                for (int k = 0; k < 3; k++) {
                    int b = baseBone + k;
                    int n = humanoid[b];
                    if (n < 0) continue;
                    int child = nodes[n].children.empty() ? -1 : nodes[n].children[0];
                    Vec3 fd = child >= 0 ? normalize(rp(child) - rp(n)) : hdir;
                    if (length(fd) < 0.5f) fd = hdir;
                    Vec3 ax = normalize(cross(fd, palm));
                    if (length(ax) < 0.5f) continue;
                    float ang = curl[f] * jointAngle[k] * kDeg2Rad * (f == 0 ? 0.6f : 1.0f);
                    if (fw > 0.001f) ang = lerpf(ang, in.hands[side].finger[f][k] * (f == 0 ? 0.7f : 1.0f), fw);
                    setBoneDelta(b, Quat::axisAngle(ax, ang));
                }
            }
        }
    }

    // --- törzs: légzés, ringatózás ---
    float br = std::sin(t * 2 * kPi / 4.2f);
    float sway1 = std::sin(t * 0.6f) * 0.6f + std::sin(t * 0.23f + 1.3f) * 0.4f;
    float sway2 = std::sin(t * 0.45f + 0.7f) * 0.5f + std::sin(t * 0.17f) * 0.5f;
    Quat hipsSway = Quat::axisAngle({0, 0, 1}, sway1 * 0.012f * ps.sway) * Quat::axisAngle({0, 1, 0}, sway2 * 0.02f * ps.sway);
    setBoneDelta(HB_Hips, hipsSway);
    setBoneDelta(HB_Spine, Quat::axisAngle({0, 0, 1}, -sway1 * 0.008f * ps.sway));
    int chestBone = humanoid[HB_UpperChest] >= 0 ? HB_UpperChest : HB_Chest;
    setBoneDelta(chestBone, Quat::axisAngle({1, 0, 0}, -br * 0.012f * ps.breathing));
    for (int side = 0; side < 2; side++)
        setBoneDelta(side == 0 ? HB_LeftShoulder : HB_RightShoulder, Quat::axisAngle({0, 0, 1}, (side == 0 ? 1.0f : -1.0f) * br * 0.01f * ps.breathing));

    // --- fej ---
    Quat head = in.head;
    // tétlen fejmozgás
    float hx = (std::sin(t * 0.31f) * 0.6f + std::sin(t * 0.77f + 2.1f) * 0.4f) * 0.025f * ps.headIdle;
    float hy = (std::sin(t * 0.27f + 0.5f) * 0.6f + std::sin(t * 0.61f) * 0.4f) * 0.04f * ps.headIdle;
    head = head * Quat::eulerYXZ(hx, hy, 0);
    float body = clampf(in.bodyFollow, 0, 1) * 0.4f;
    setBoneDelta(HB_Spine, scaleRotation(head, body * 0.4f));
    setBoneDelta(chestBone, scaleRotation(head, body * 0.6f));
    float rest = 1 - body;
    if (humanoid[HB_Neck] >= 0) {
        setBoneDelta(HB_Neck, scaleRotation(head, rest * 0.4f));
        setBoneDelta(HB_Head, scaleRotation(head, rest * 0.6f));
    } else
        setBoneDelta(HB_Head, scaleRotation(head, rest));

    // --- a delták alkalmazása a lokális forgatásokra ---
    for (size_t i = 0; i < nodes.size(); i++) {
        const Quat& d = boneDelta_[i];
        if (std::fabs(d.w) > 0.9999999f) continue;
        const Quat& W = restWorldRot_[i];
        nodes[i].local.r = (nodes[i].local.r * (W.conj() * d * W)).normalized();
    }

    // --- szemek ---
    float yaw = in.eyeYaw, pitch = in.eyePitch;
    if (!in.eyesOverride && ps.lookAround) {
        lookTimer_ -= dt;
        if (lookTimer_ <= 0) {
            float r = (float)rand() / RAND_MAX, r2 = (float)rand() / RAND_MAX;
            lookTarget_ = Vec2((r * 2 - 1) * 0.35f * ps.lookAmount, (r2 * 2 - 1) * 0.2f * ps.lookAmount);
            if (rand() % 3 == 0) lookTarget_ = Vec2(0, 0);
            lookTimer_ = 1.0f + 3.0f * (float)rand() / RAND_MAX;
        }
        lookCur_.x = damp(lookCur_.x, lookTarget_.x, 18, dt);
        lookCur_.y = damp(lookCur_.y, lookTarget_.y, 18, dt);
        yaw = lookCur_.x;
        pitch = lookCur_.y;
    }
    for (int e = 0; e < 2; e++) {
        int n = e == 0 ? eyeL : eyeR;
        if (n < 0) continue;
        if (hasEyeLimits) {
            // a VRChat descriptor határai (kb. ±1 = a megadott forgatás)
            float fx = clampf(yaw / 0.35f, -1, 1), fy = clampf(pitch / 0.25f, -1, 1);
            Quat qy = fx >= 0 ? slerp(Quat(), eyeLeft[e], fx) : slerp(Quat(), eyeRight[e], -fx);
            Quat qp = fy >= 0 ? slerp(Quat(), eyeUp[e], fy) : slerp(Quat(), eyeDown[e], -fy);
            nodes[n].local.r = (nodes[n].local.r * qy * qp).normalized();
        } else {
            Quat d = Quat::axisAngle({0, 1, 0}, clampf(yaw, -0.5f, 0.5f)) * Quat::axisAngle({1, 0, 0}, -clampf(pitch, -0.4f, 0.4f));
            const Quat& W = restWorldRot_[n];
            nodes[n].local.r = (nodes[n].local.r * (W.conj() * d * W)).normalized();
        }
    }
    // szemhéj blendshape a nézés irányához
    if (lookUp.mesh >= 0 && pitch > 0) meshes[lookUp.mesh].weights[lookUp.index] = std::max(meshes[lookUp.mesh].weights[lookUp.index], clampf(pitch / 0.25f, 0, 1) * 0.6f);
    if (lookDown.mesh >= 0 && pitch < 0) meshes[lookDown.mesh].weights[lookDown.index] = std::max(meshes[lookDown.mesh].weights[lookDown.index], clampf(-pitch / 0.25f, 0, 1) * 0.6f);
}

void Avatar::applyFace(float dt, const PoseInput& in, const PoseSettings& ps) {
    auto setW = [&](const BlendRef& r, float v) {
        if (r.mesh < 0) return;
        float& w = meshes[r.mesh].weights[r.index];
        w = std::max(w, clampf(v, 0, 1));
    };
    // pislogás
    float bl = in.blinkL, brr = in.blinkR;
    if (ps.autoBlink) {
        blinkTimer_ -= dt;
        if (blinkTimer_ <= 0 && blinkPhase_ < 0) {
            blinkPhase_ = 0;
            // a következő pislogás a beállított tartományon belül, véletlenszerűen
            float r = (float)rand() / RAND_MAX;
            float lo = std::max(0.3f, std::min(ps.blinkMin, ps.blinkMax)), hi = std::max(ps.blinkMin, ps.blinkMax);
            blinkTimer_ = lo + (hi - lo) * r;
            if (ps.doubleBlink && !doubleNext_ && rand() % 6 == 0) {
                blinkTimer_ = 0.25f;  // néha dupla pislogás (a második után ismét a tartomány szerint)
                doubleNext_ = true;
            } else
                doubleNext_ = false;
        }
        if (blinkPhase_ >= 0) {
            blinkPhase_ += dt;
            float d = 0.16f;
            float v = blinkPhase_ < d * 0.45f ? blinkPhase_ / (d * 0.45f) : 1.0f - (blinkPhase_ - d * 0.45f) / (d * 0.55f);
            v = clampf(v, 0, 1);
            bl = std::max(bl, v);
            brr = std::max(brr, v);
            if (blinkPhase_ > d) blinkPhase_ = -1;
        }
    }
    smoothBlinkL_ = bl;
    smoothBlinkR_ = brr;
    bool arkitEyes = !in.arkit.empty() && arkitShapes.count("eyeblinkleft");
    if (arkitEyes) {
        for (auto& r : arkitShapes["eyeblinkleft"]) setW(r, bl);
        for (auto& r : arkitShapes["eyeblinkright"]) setW(r, brr);
    } else if (blinkL.mesh >= 0 && blinkR.mesh >= 0 && blink.mesh < 0) {
        setW(blinkL, bl);
        setW(blinkR, brr);
    } else if (blink.mesh >= 0) {
        setW(blink, std::max(bl, brr) * 0.5f + std::min(bl, brr) * 0.5f);
    } else if (vrmExprIndex.count("blink")) {
        auto& ex = vrmExpressions[vrmExprIndex["blink"]];
        for (auto& [r, w] : ex.binds) setW(r, w * (bl + brr) * 0.5f);
    }
    // ARKit (perfect sync) – a teljes arc
    if (!in.arkit.empty() && !arkitShapes.empty()) {
        for (auto& [name, val] : in.arkit) {
            if (name == "eyeblinkleft" || name == "eyeblinkright") continue;
            auto it = arkitShapes.find(name);
            if (it == arkitShapes.end()) continue;
            for (auto& r : it->second) setW(r, val);
        }
    }
    // viseme-ek
    bool arkitMouth = !in.arkit.empty() && arkitShapes.count("jawopen");
    if (!arkitMouth) {
        bool anyViseme = false;
        for (int k = 0; k < 15; k++) anyViseme |= visemes[k].mesh >= 0;
        for (int k = 0; k < 15; k++) visemeSmooth_[k] = damp(visemeSmooth_[k], in.visemes[k], 25, dt);
        if (anyViseme) {
            for (int k = 1; k < 15; k++) setW(visemes[k], visemeSmooth_[k]);
        } else if (!vrmExprIndex.empty()) {
            const char* vk[5] = {"aa", "ih", "ou", "ee", "oh"};
            int map[5] = {10, 12, 14, 11, 13};
            for (int k = 0; k < 5; k++) {
                auto it = vrmExprIndex.find(vk[k]);
                if (it == vrmExprIndex.end()) continue;
                for (auto& [r, w] : vrmExpressions[it->second].binds) setW(r, w * visemeSmooth_[map[k]]);
            }
        }
        if (mouthOpen.mesh >= 0 && !anyViseme) setW(mouthOpen, in.mouthOpen);
    }
}

// ---------------------------------------------------------------------------
void Avatar::update(float dt, const PoseInput& in, const PoseSettings& ps, const AvatarOverrides* ov) {
    idleT_ += dt;
    for (auto& n : nodes) {
        n.local = n.rest;
        n.active = n.activeDefault;
    }
    for (auto& m : meshes) {
        m.enabled = m.enabledDefault;
        std::copy(m.weightsDefault.begin(), m.weightsDefault.end(), m.weights.begin());
    }
    for (auto& m : materials) m.cur = m.base;
    // eredeti anyag-hozzárendelés visszaállítása (az animátor cserélhet)
    if (animator) animator->resetMaterialSlots(*this);

    // felhasználói paraméterek -> animátor
    if (ov)
        for (auto& [k, v] : ov->params) paramValues[k] = v;
    paramValues["GestureLeft"] = (float)in.gestureLeft;
    paramValues["GestureRight"] = (float)in.gestureRight;
    paramValues["GestureLeftWeight"] = in.gestureWeightL;
    paramValues["GestureRightWeight"] = in.gestureWeightR;
    int vis = 0;
    float best = 0.15f;
    for (int k = 1; k < 15; k++)
        if (in.visemes[k] > best) best = in.visemes[k], vis = k;
    paramValues["Viseme"] = (float)vis;
    float voice = 0;
    for (int k = 1; k < 15; k++) voice = std::max(voice, in.visemes[k]);
    paramValues["Voice"] = voice;
    if (animator) animator->evaluate(dt, *this);

    // felhasználói felülírások
    if (ov) {
        for (auto& [key, val] : ov->blendShapes) {
            size_t bar = key.find('|');
            if (bar == std::string::npos) continue;
            std::string path = key.substr(0, bar), shape = key.substr(bar + 1);
            for (auto& m : meshes)
                if (m.path == path) {
                    auto it = m.morphIndex.find(shape);
                    if (it != m.morphIndex.end()) m.weights[it->second] = val;
                }
        }
        for (size_t mi = 0; mi < materials.size(); mi++) {
            AvMaterial& m = materials[mi];
            std::string key = m.key;
            size_t h2 = key.find('#', key.find('#') + 1);
            if (h2 != std::string::npos) key = key.substr(0, h2);  // anyag-csere példány: ugyanaz a kulcs
            auto it = ov->materials.find(key);
            float gh = ov->globalHue;
            if (it == ov->materials.end() && gh == 0) continue;
            MatParams& p = m.cur;
            if (gh != 0) {
                p.hueShift += gh;
                p.hueShiftOn = 1;
                for (auto& L : p.matcaps) L.hueShift += gh, L.hueShiftOn = 1;
            }
            if (it == ov->materials.end()) continue;
            const MaterialOverride& o = it->second;
            p.color[0] *= o.tint.x, p.color[1] *= o.tint.y, p.color[2] *= o.tint.z;
            if (o.hue != 0) {
                p.hueShift += o.hue;
                p.hueShiftOn = 1;
                for (auto& L : p.matcaps) L.hueShift += o.hue, L.hueShiftOn = 1;
            }
            p.saturation += o.saturation;
            p.brightness += o.brightness;
            if (o.emissiveOverride) {
                p.emissive[0] = o.emissive.x, p.emissive[1] = o.emissive.y, p.emissive[2] = o.emissive.z;
                if (p.emissiveIntensity < 0.05f) p.emissiveIntensity = 1;
            }
            p.emissiveIntensity *= o.emissiveBoost;
            for (auto& L : p.matcaps) L.emission *= o.emissiveBoost;
            p.opacity *= o.opacity;
        }
    }

    applyFace(dt, in, ps);
    // VRM kifejezések (pl. happy, angry) a felületről / gyorsbillentyűről
    if (ov)
        for (auto& [name, w] : ov->expressions) {
            auto it = vrmExprIndex.find(toLower(name));
            if (it == vrmExprIndex.end() || w <= 0) continue;
            for (auto& [r, bw] : vrmExpressions[it->second].binds) {
                float& cw = meshes[r.mesh].weights[r.index];
                cw = std::max(cw, bw * w);
            }
        }
    applyPose(dt, in, ps);

    // elhelyezés: az avatár gyökér csomópontja
    nodes[0].local.t = nodes[0].rest.t + ps.bodyOffset + in.headOffset * 0.0f;
    nodes[0].local.r = Quat::axisAngle({0, 1, 0}, ps.bodyYaw * kDeg2Rad) * nodes[0].rest.r;
    nodes[0].local.s = nodes[0].rest.s * ps.scale;
    computeWorld(0);
    applyHandIK(dt, in);
    applyConstraints();

    if (physics && physicsEnabled && (!ov || ov->physics)) physics->step(dt, *this, ov ? ov->physicsStrength : 1.0f);

    // láthatóság
    std::function<void(int, bool)> vis2 = [&](int i, bool parentActive) {
        nodes[i].activeEff = parentActive && nodes[i].active;
        for (int c : nodes[i].children) vis2(c, nodes[i].activeEff);
    };
    vis2(0, true);
    for (auto& m : meshes) {
        m.visible = nodes[m.node].activeEff && m.enabled;
        if (ov) {
            auto it = ov->partVisible.find(m.path);
            if (it != ov->partVisible.end()) m.visible = it->second;
        }
    }
}

void Avatar::uploadGpu(Gfx& gfx) {
    std::vector<Mat4> mats;
    for (auto& am : meshes) {
        if (!am.visible) continue;
        // csont mátrixok
        mats.resize(std::max<size_t>(1, am.joints.size()));
        if (am.src->skinned) {
            for (size_t j = 0; j < am.joints.size(); j++) mats[j] = nodes[am.joints[j]].world * am.invBind[j];
        } else
            mats[0] = nodes[am.node].world;
        gfx.update(am.bones.Get(), mats.data(), mats.size() * sizeof(Mat4));
        Vec3 c = (am.src->bmin + am.src->bmax) * 0.5f;
        am.centerWorld = am.src->skinned ? (am.joints.empty() ? c : nodes[am.joints[0]].world.getTranslation()) : nodes[am.node].world.transformPoint(c);
        // blendshape-ek (CPU)
        if (am.dynamicPos) {
            bool changed = false;
            for (size_t k = 0; k < am.weights.size(); k++)
                if (std::fabs(am.weights[k] - am.uploadedWeights[k]) > 1e-4f) changed = true;
            if (changed) {
                const ModelMesh& mm = *am.src;
                size_t nv = mm.pos.size();
                float* d = am.morphScratch.data();
                for (size_t v = 0; v < nv; v++) {
                    d[v * 6 + 0] = mm.pos[v].x, d[v * 6 + 1] = mm.pos[v].y, d[v * 6 + 2] = mm.pos[v].z;
                    d[v * 6 + 3] = mm.nrm[v].x, d[v * 6 + 4] = mm.nrm[v].y, d[v * 6 + 5] = mm.nrm[v].z;
                }
                for (size_t k = 0; k < am.weights.size(); k++) {
                    float w = am.weights[k];
                    if (w <= 1e-4f) continue;
                    const ModelMorph& mo = mm.morphs[k];
                    for (size_t i = 0; i < mo.idx.size(); i++) {
                        uint32_t v = mo.idx[i];
                        d[v * 6 + 0] += mo.dp[i].x * w, d[v * 6 + 1] += mo.dp[i].y * w, d[v * 6 + 2] += mo.dp[i].z * w;
                        d[v * 6 + 3] += mo.dn[i].x * w, d[v * 6 + 4] += mo.dn[i].y * w, d[v * 6 + 5] += mo.dn[i].z * w;
                    }
                }
                gfx.update(am.vbPosNrm.Get(), d, am.morphScratch.size() * 4);
                am.uploadedWeights = am.weights;
            }
        }
    }
}

// VRM 1.0 csomópont kényszerek (VRMC_node_constraint specifikáció szerint)
void Avatar::applyConstraints() {
    for (auto& k : constraints_) {
        AvNode& dst = nodes[k.dst];
        const AvNode& src = nodes[k.src];
        Quat dstRest = dst.rest.r;
        Quat target = dst.local.r;
        if (k.type == 0) {
            Quat deltaSrc = src.rest.r.conj() * src.local.r;
            Quat inParent = src.rest.r * deltaSrc * src.rest.r.conj();
            Quat inDst = dstRest.conj() * inParent * dstRest;
            Vec3 to = inDst.rotate(k.axis);
            Quat ft = fromTo(k.axis, to);
            target = slerp(dstRest, dstRest * ft.conj() * inDst, k.weight);
        } else if (k.type == 1) {
            Quat parentW = dst.parent >= 0 ? nodes[dst.parent].world.getRotation() : Quat();
            Vec3 from = (parentW * dstRest).rotate(k.axis);
            Vec3 to = normalize(src.world.getTranslation() - dst.world.getTranslation());
            if (length(to) < 1e-6f) continue;
            Quat ft = fromTo(from, to);
            target = slerp(dstRest, parentW.conj() * ft * parentW * dstRest, k.weight);
        } else {
            Quat deltaSrc = src.rest.r.conj() * src.local.r;
            target = slerp(dstRest, dstRest * deltaSrc, k.weight);
        }
        dst.local.r = target.normalized();
        computeWorld(k.dst);
    }
}

// Egy csomópont világ-forgatásának beállítása (a szülő világ-forgatásából számolt lokálissal)
void Avatar::setWorldRotation(int n, const Quat& w) {
    Quat pw = nodes[n].parent >= 0 ? nodes[nodes[n].parent].world.getRotation() : Quat();
    nodes[n].local.r = (pw.conj() * w).normalized();
    computeWorld(n);
}

// Kézkövetés: kétcsontos IK a karra, a kézfej tájolása a tenyér síkjából
void Avatar::applyHandIK(float dt, const PoseInput& in) {
    Quat placeR = nodes[0].world.getRotation() * nodes[0].rest.r.conj();
    float placeS = nodes[0].world.getScale().x / std::max(1e-6f, nodes[0].rest.s.x);
    for (int side = 0; side < 2; side++) {
        const PoseInput::Hand& h = in.hands[side];
        handWeight_[side] = damp(handWeight_[side], h.valid ? 1.0f : 0.0f, h.valid ? 12.0f : 4.0f, dt);
        float w = handWeight_[side];
        int U = humanoid[side == 0 ? HB_LeftUpperArm : HB_RightUpperArm];
        int L = humanoid[side == 0 ? HB_LeftLowerArm : HB_RightLowerArm];
        int H = humanoid[side == 0 ? HB_LeftHand : HB_RightHand];
        int head = humanoid[HB_Head];
        if (w < 0.002f || U < 0 || L < 0 || H < 0 || head < 0) continue;
        if (!h.valid && w < 0.002f) continue;
        Vec3 S = nodes[U].world.getTranslation(), E0 = nodes[L].world.getTranslation(), W0 = nodes[H].world.getTranslation();
        float a = length(E0 - S), b = length(W0 - E0);
        if (a < 1e-4f || b < 1e-4f) continue;
        // a karhossz arányában skálázzuk az emberi mozgást
        float armScale = (length(restWorldPos_[L] - restWorldPos_[U]) + length(restWorldPos_[H] - restWorldPos_[L])) / 0.56f;
        Vec3 headPos = nodes[head].world.getTranslation();
        Vec3 nose = headPos + placeR.rotate(in.head.rotate(Vec3(0, 0.04f, 0.10f) * armScale)) * placeS;
        Vec3 target = nose + placeR.rotate(h.rel * armScale) * placeS;
        target = lerp(W0, target, w);
        // kétcsontos IK
        Vec3 st = target - S;
        float d = clampf(length(st), std::fabs(a - b) + 1e-3f, (a + b) * 0.999f);
        Vec3 dir = normalize(st);
        float outward = side == 0 ? 1.0f : -1.0f;
        Vec3 pole = normalize(placeR.rotate(Vec3(0.55f * outward, -1.0f, -0.45f)));
        Vec3 perp = pole - dir * dot(pole, dir);
        if (length(perp) < 1e-4f) perp = placeR.rotate(Vec3(0, -1, 0));
        perp = normalize(perp);
        float cosA = clampf((a * a + d * d - b * b) / (2 * a * d), -1, 1);
        float sinA = std::sqrt(std::max(0.0f, 1 - cosA * cosA));
        Vec3 E = S + dir * (a * cosA) + perp * (a * sinA);
        Vec3 T = S + dir * d;
        setWorldRotation(U, fromTo(E0 - S, E - S) * nodes[U].world.getRotation());
        Vec3 E1 = nodes[L].world.getTranslation(), W1 = nodes[H].world.getTranslation();
        setWorldRotation(L, fromTo(W1 - E1, T - E1) * nodes[L].world.getRotation());
        // kézfej tájolás
        int mid = humanoid[side == 0 ? HB_LeftMiddle1 : HB_RightMiddle1];
        int idx = humanoid[side == 0 ? HB_LeftIndex1 : HB_RightIndex1];
        int lit = humanoid[side == 0 ? HB_LeftLittle1 : HB_RightLittle1];
        if (h.valid && mid >= 0 && idx >= 0 && lit >= 0 && length(h.dir) > 0.5f && length(h.side) > 0.5f) {
            auto basis = [](Vec3 x, Vec3 sd) {
                x = normalize(x);
                Vec3 z = normalize(cross(x, sd));
                Vec3 y = cross(z, x);
                Mat4 m;
                m.at(0, 0) = x.x, m.at(1, 0) = x.y, m.at(2, 0) = x.z;
                m.at(0, 1) = y.x, m.at(1, 1) = y.y, m.at(2, 1) = y.z;
                m.at(0, 2) = z.x, m.at(1, 2) = z.y, m.at(2, 2) = z.z;
                return m.getRotation();
            };
            Quat Bt = basis(h.dir, h.side);
            Quat Br = basis(restWorldPos_[mid] - restWorldPos_[H], restWorldPos_[idx] - restWorldPos_[lit]);
            Quat targetW = placeR * (Bt * Br.conj()) * restWorldRot_[H];
            Quat cur = nodes[H].world.getRotation();
            setWorldRotation(H, slerp(cur, targetW, w));
        }
    }
}
