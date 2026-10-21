#pragma once
#include "import/UnityYaml.h"
#include <unordered_map>
#include <map>
#include <memory>

struct PkgAsset {
    std::string guid;
    std::string path;  // Assets/... eredeti útvonal
    std::string ext;   // kisbetűs kiterjesztés ponttal
    std::string name;  // fájlnév kiterjesztés nélkül
    fs::path file;     // kicsomagolt tartalom (üres, ha mappa)
    fs::path meta;
};

class PackageDb {
public:
    explicit PackageDb(const fs::path& extractedDir);
    const PkgAsset* get(const std::string& guid) const;
    std::vector<const PkgAsset*> byExt(std::initializer_list<const char*> exts) const;
    bool isModel(const std::string& guid) const;
    bool isTexture(const std::string& guid) const;
    const std::vector<UnityDoc>& docs(const std::string& guid);
    std::string metaText(const std::string& guid) const;
    json metaDoc(const std::string& guid) const;
    const std::unordered_map<std::string, PkgAsset>& assets() const { return assets_; }

private:
    std::unordered_map<std::string, PkgAsset> assets_;
    std::unordered_map<std::string, std::vector<UnityDoc>> docCache_;
};

struct UObj {
    int cls = 0;
    std::string id;
    std::string type;
    bool stripped = false;
    json d;
    std::string pendingParent;
};
using UObjMap = std::unordered_map<std::string, UObj>;

// Prefab / jelenet példányosítása a beágyazott prefabokkal és felülírásokkal együtt
UObjMap instantiatePrefab(PackageDb& db, const std::string& guid, int depth = 0);

struct TreeNode {
    std::string path, name;
    bool active = true;
    bool hasTransform = false;
    Vec3 position{};
    Quat rotation{};
    Vec3 scale{1, 1, 1};
    std::vector<const UObj*> components;
    const UObj* modelInstance = nullptr;
};

struct AvatarTree {
    std::vector<TreeNode> nodes;
    std::unordered_map<std::string, std::string> idToPath;  // GameObject/Transform/komponens id -> útvonal
};

AvatarTree buildAvatarTree(const UObjMap& objects, const UObj& rootGo);

bool extractUnityPackage(const fs::path& file, const fs::path& outDir, bool gz);
