#include "import/UnityPackage.h"
#include <windows.h>
#include <unordered_set>

static const std::unordered_set<std::string> kModelExt = {".fbx", ".obj", ".blend", ".dae", ".3ds", ".max", ".ma", ".mb"};
static const std::unordered_set<std::string> kTexExt = {".png", ".jpg", ".jpeg", ".tga", ".psd", ".tif", ".tiff", ".bmp", ".gif", ".exr", ".hdr", ".webp"};

PackageDb::PackageDb(const fs::path& dir) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_directory()) continue;
        fs::path pn = e.path() / L"pathname";
        std::string txt;
        if (!readFile(pn, txt)) continue;
        std::string p = trim(txt.substr(0, txt.find('\n')));
        PkgAsset a;
        a.guid = narrow(e.path().filename().wstring());
        a.path = p;
        fs::path up = u8path(p);
        a.ext = toLower(narrow(up.extension().wstring()));
        a.name = narrow(up.stem().wstring());
        if (fs::exists(e.path() / L"asset")) a.file = e.path() / L"asset";
        if (fs::exists(e.path() / L"asset.meta")) a.meta = e.path() / L"asset.meta";
        assets_[a.guid] = std::move(a);
    }
}

const PkgAsset* PackageDb::get(const std::string& guid) const {
    auto it = assets_.find(guid);
    return it == assets_.end() ? nullptr : &it->second;
}

std::vector<const PkgAsset*> PackageDb::byExt(std::initializer_list<const char*> exts) const {
    std::vector<const PkgAsset*> out;
    for (auto& [g, a] : assets_) {
        if (a.file.empty()) continue;
        for (auto e : exts)
            if (a.ext == e) {
                out.push_back(&a);
                break;
            }
    }
    std::sort(out.begin(), out.end(), [](auto* x, auto* y) { return x->path < y->path; });
    return out;
}

bool PackageDb::isModel(const std::string& guid) const {
    auto a = get(guid);
    return a && kModelExt.count(a->ext);
}
bool PackageDb::isTexture(const std::string& guid) const {
    auto a = get(guid);
    return a && !a->file.empty() && kTexExt.count(a->ext);
}

const std::vector<UnityDoc>& PackageDb::docs(const std::string& guid) {
    auto it = docCache_.find(guid);
    if (it != docCache_.end()) return it->second;
    std::vector<UnityDoc> docs;
    auto a = get(guid);
    if (a && !a->file.empty()) {
        std::string text;
        if (readFile(a->file, text) && startsWith(text, "%YAML")) docs = parseUnityYaml(text);
    }
    return docCache_[guid] = std::move(docs);
}

std::string PackageDb::metaText(const std::string& guid) const {
    auto a = get(guid);
    std::string t;
    if (a && !a->meta.empty()) readFile(a->meta, t);
    return t;
}

json PackageDb::metaDoc(const std::string& guid) const {
    std::string t = metaText(guid);
    if (t.empty()) return json::object();
    try {
        return parseYamlBlock(t);
    } catch (...) {
        return json::object();
    }
}

// ---------------------------------------------------------------------------
static bool isRefObject(const json& o) {
    if (!o.is_object() || !o.contains("fileID")) return false;
    for (auto it = o.begin(); it != o.end(); ++it)
        if (it.key() != "fileID" && it.key() != "guid" && it.key() != "type" && it.key() != "__local") return false;
    return true;
}

static void remapRefs(json& node, const std::string& instId) {
    if (node.is_array()) {
        for (auto& v : node) remapRefs(v, instId);
    } else if (node.is_object()) {
        if (isRefObject(node)) {
            if (node.contains("__local")) {
                node.erase("__local");
                return;
            }
            std::string g = node.contains("guid") && node["guid"].is_string() ? node["guid"].get<std::string>() : "";
            std::string f = node["fileID"].is_string() ? node["fileID"].get<std::string>() : "0";
            if (g.empty() && f != "0") node["fileID"] = xorId(instId, f);
            return;
        }
        for (auto it = node.begin(); it != node.end(); ++it) remapRefs(it.value(), instId);
    }
}

static void addChild(UObjMap& objects, const std::string& parentId, const std::string& childId) {
    auto it = objects.find(parentId);
    if (it == objects.end()) return;
    json& ch = it->second.d["m_Children"];
    if (!ch.is_array()) ch = json::array();
    for (auto& c : ch)
        if (yref(c).fileID == childId) return;
    ch.push_back({{"fileID", childId}});
}

UObjMap instantiatePrefab(PackageDb& db, const std::string& guid, int depth) {
    UObjMap objects;
    if (depth > 12) return objects;
    const auto& docs = db.docs(guid);
    std::vector<const UnityDoc*> instances, strippedDocs;
    for (auto& doc : docs) {
        if (doc.cls == 1001)
            instances.push_back(&doc);
        else if (doc.stripped)
            strippedDocs.push_back(&doc);
        else
            objects[doc.id] = UObj{doc.cls, doc.id, doc.type, false, doc.d, ""};
    }

    for (const UnityDoc* inst : instances) {
        const json& mod = yget(inst->d, "m_Modification");
        URef src = yref(yget(inst->d, "m_SourcePrefab"));
        URef parentRef = yref(yget(mod, "m_TransformParent"));
        const json& mods = yarr(mod, "m_Modifications");
        const PkgAsset* srcAsset = db.get(src.guid);

        if (srcAsset && srcAsset->ext == ".prefab") {
            UObjMap sub = instantiatePrefab(db, src.guid, depth + 1);
            for (auto& m : mods) {
                URef t = yref(yget(m, "target"));
                auto it = sub.find(t.fileID);
                if (it == sub.end()) continue;
                json val = yget(m, "value");
                URef oref = yref(yget(m, "objectReference"));
                if (!oref.isNull()) {
                    val = json{{"fileID", oref.fileID}, {"guid", oref.guid}};
                    if (oref.guid.empty()) val["__local"] = true;
                }
                setPropertyPath(it->second.d, ystr(m, "propertyPath"), val);
            }
            std::unordered_set<std::string> removed;
            for (auto& r : yarr(mod, "m_RemovedComponents")) removed.insert(yref(r).fileID);
            for (auto& r : yarr(mod, "m_RemovedGameObjects")) removed.insert(yref(r).fileID);
            std::string rootTf;
            for (auto& [oid, obj] : sub) {
                if (removed.count(oid)) continue;
                if (obj.type == "ModelInstance") {
                    std::string nInst = xorId(inst->id, obj.d["instanceId"].get<std::string>());
                    std::string nid = "mi:" + nInst;
                    UObj c = obj;
                    c.id = nid;
                    c.d["instanceId"] = nInst;
                    std::string par = c.d["parent"].get<std::string>();
                    if (!par.empty() && par != "0") c.d["parent"] = xorId(inst->id, par);
                    if (!objects.count(nid)) objects[nid] = std::move(c);
                    continue;
                }
                std::string nid = xorId(inst->id, oid);
                if (obj.stripped && obj.d.contains("modelInstance")) {
                    UObj c = obj;
                    c.id = nid;
                    c.d["modelInstance"] = "mi:" + xorId(inst->id, obj.d["modelInstance"].get<std::string>().substr(3));
                    if (!objects.count(nid)) objects[nid] = std::move(c);
                    continue;
                }
                UObj c = obj;
                c.id = nid;
                remapRefs(c.d, inst->id);
                bool isRoot = (obj.cls == 4 || obj.cls == 224) && yref(yget(obj.d, "m_Father")).isNull();
                if (!objects.count(nid)) objects[nid] = std::move(c);
                if (isRoot) rootTf = nid;
            }
            if (!rootTf.empty() && !parentRef.isNull()) {
                objects[rootTf].d["m_Father"] = json{{"fileID", parentRef.fileID}};
                if (objects.count(parentRef.fileID))
                    addChild(objects, parentRef.fileID, rootTf);
                else
                    objects[rootTf].pendingParent = parentRef.fileID;
            }
        } else {
            // Modell (FBX) példány: a hierarchiát futásidőben a modellfájlból olvassuk
            std::string name = srcAsset ? srcAsset->name : "Model";
            std::map<std::string, int> rootCand;
            json modList = json::array();
            for (auto& m : mods) {
                std::string pp = ystr(m, "propertyPath");
                std::string target = yref(yget(m, "target")).fileID;
                if (pp == "m_Name") name = ystr(m, "value");
                if (startsWith(pp, "m_RootOrder") || startsWith(pp, "m_LocalEulerAnglesHint") || startsWith(pp, "m_ConstrainProportionsScale")) rootCand[target]++;
                URef oref = yref(yget(m, "objectReference"));
                modList.push_back({{"target", target}, {"path", pp}, {"value", yget(m, "value").is_string() ? yget(m, "value") : json("")}, {"refFileID", oref.fileID}, {"refGuid", oref.guid}});
            }
            std::string rootTfId;
            int best = 0;
            for (auto& [k, v] : rootCand)
                if (v > best) {
                    best = v;
                    rootTfId = k;
                }
            std::string id = "mi:" + inst->id;
            UObj o;
            o.cls = -1;
            o.id = id;
            o.type = "ModelInstance";
            o.d = {{"model", src.guid}, {"instanceId", inst->id}, {"parent", parentRef.fileID}, {"name", name}, {"rootTransformSourceId", rootTfId}, {"modifications", modList}};
            objects[id] = std::move(o);
            for (const UnityDoc* sd : strippedDocs) {
                if (yref(yget(sd->d, "m_PrefabInstance")).fileID != inst->id) continue;
                UObj so;
                so.cls = sd->cls;
                so.id = sd->id;
                so.type = sd->type;
                so.stripped = true;
                so.d = {{"modelInstance", id}, {"sourceId", yref(yget(sd->d, "m_CorrespondingSourceObject")).fileID}};
                objects[sd->id] = std::move(so);
            }
        }
    }
    for (auto& [id, obj] : objects) {
        if (obj.pendingParent.empty()) continue;
        std::string p = obj.pendingParent;
        obj.pendingParent.clear();
        addChild(objects, p, id);
    }
    return objects;
}

// ---------------------------------------------------------------------------
AvatarTree buildAvatarTree(const UObjMap& objects, const UObj& rootGo) {
    AvatarTree tree;
    std::unordered_map<std::string, std::vector<const UObj*>> modelByParent;
    for (auto& [id, o] : objects)
        if (o.type == "ModelInstance") modelByParent[o.d["parent"].get<std::string>()].push_back(&o);

    auto find = [&](const std::string& id) -> const UObj* {
        auto it = objects.find(id);
        return it == objects.end() ? nullptr : &it->second;
    };
    auto transformOf = [&](const UObj& go) -> const UObj* {
        for (auto& c : yarr(go.d, "m_Component")) {
            const UObj* co = find(yref(yget(c, "component")).fileID);
            if (co && (co->cls == 4 || co->cls == 224)) return co;
        }
        return nullptr;
    };

    std::function<void(const UObj&, const std::string&, int)> walk = [&](const UObj& tf, const std::string& parentPath, int depth) {
        const UObj* go = find(yref(yget(tf.d, "m_GameObject")).fileID);
        if (!go || go->stripped) return;
        std::string name = ystr(go->d, "m_Name", "Object");
        std::string p = depth == 0 ? "" : (parentPath.empty() ? name : parentPath + "/" + name);
        TreeNode n;
        n.path = p;
        n.name = name;
        n.active = ystr(go->d, "m_IsActive", "1") != "0";
        n.hasTransform = true;
        n.position = posToRH(yvec3(yget(tf.d, "m_LocalPosition")));
        n.rotation = quatToRH(yquat(yget(tf.d, "m_LocalRotation")));
        n.scale = yvec3(yget(tf.d, "m_LocalScale"), {1, 1, 1});
        for (auto& c : yarr(go->d, "m_Component")) {
            std::string cid = yref(yget(c, "component")).fileID;
            if (const UObj* co = find(cid)) n.components.push_back(co);
            tree.idToPath[cid] = p;
        }
        tree.idToPath[go->id] = p;
        tree.idToPath[tf.id] = p;
        tree.nodes.push_back(std::move(n));
        for (auto& c : yarr(tf.d, "m_Children")) {
            const UObj* ct = find(yref(c).fileID);
            if (ct && !ct->stripped && (ct->cls == 4 || ct->cls == 224)) walk(*ct, p, depth + 1);
        }
        auto mit = modelByParent.find(tf.id);
        if (mit != modelByParent.end()) {
            for (const UObj* mi : mit->second) {
                std::string mname = mi->d["name"].get<std::string>();
                TreeNode mn;
                mn.path = p.empty() ? mname : p + "/" + mname;
                mn.name = mname;
                mn.modelInstance = mi;
                tree.idToPath[mi->id] = mn.path;
                tree.nodes.push_back(std::move(mn));
            }
        }
    };
    if (const UObj* rt = transformOf(rootGo)) walk(*rt, "", 0);
    return tree;
}

// ---------------------------------------------------------------------------
bool extractUnityPackage(const fs::path& file, const fs::path& outDir, bool gz) {
    std::error_code ec;
    fs::create_directories(outDir, ec);
    wchar_t sysDir[MAX_PATH];
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring tar = std::wstring(sysDir) + L"\\tar.exe";
    std::wstring cmd = L"\"" + tar + L"\" " + (gz ? L"-xzf" : L"-xf") + L" \"" + file.wstring() + L"\" -C \"" + outDir.wstring() + L"\"";
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring cmdBuf = cmd;
    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}
