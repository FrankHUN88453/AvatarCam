#pragma once
// CPU oldali modell (FBX / glTF / VRM betöltés eredménye), jobbkezes, Y-fel, méter egység.
#include "core/Math.h"
#include "core/Util.h"
#include <array>

struct ModelNode {
    std::string name;
    int parent = -1;
    std::vector<int> children;
    Transform local;
};

struct ModelSubmesh {
    uint32_t start = 0, count = 0;
    int slot = 0;
};

struct ModelMorph {
    std::string name;
    std::vector<uint32_t> idx;  // érintett (kimeneti) csúcsok
    std::vector<Vec3> dp, dn;   // pozíció és normál eltolás
};

struct ModelMesh {
    std::string name;
    int node = -1;
    int sourceMesh = -1;  // glTF mesh index (VRM 0.x kötésekhez)
    bool skinned = false;
    std::vector<Vec3> pos, nrm;
    std::vector<Vec2> uv;
    std::vector<Vec4> tan;
    std::vector<std::array<uint16_t, 4>> joints;
    std::vector<Vec4> weights;
    std::vector<uint32_t> indices;
    std::vector<ModelSubmesh> submeshes;
    std::vector<int> jointNodes;  // modell csomópont indexek
    std::vector<Mat4> invBind;
    std::vector<ModelMorph> morphs;
    std::vector<int> materials;  // slot -> modell anyag index (-1 ha nincs)
    Vec3 bmin{0, 0, 0}, bmax{0, 0, 0};
};

struct ModelTexRef {
    std::string file;  // fájlnév (FBX) vagy üres
    int image = -1;    // beágyazott kép index (glTF / FBX beágyazott)
    bool valid() const { return image >= 0 || !file.empty(); }
};

struct ModelMaterial {
    std::string name;
    Vec4 baseColor{1, 1, 1, 1};
    ModelTexRef baseTex, normalTex, emissiveTex;
    Vec3 emissive{0, 0, 0};
    float emissiveStrength = 1;
    float normalScale = 1;
    std::string alphaMode = "opaque";
    float cutoff = 0.5f;
    bool doubleSided = false;
    float metal = 0, rough = 0.85f;
    bool unlit = false;
    // MToon (VRM)
    bool mtoon = false;
    Vec3 shadeColor{0.7f, 0.7f, 0.8f};
    ModelTexRef matcapTex, rimTex;
    Vec3 rimColor{0, 0, 0};
    Vec3 matcapFactor{1, 1, 1};
    float outlineWidth = 0;
    Vec3 outlineColor{0, 0, 0};
};

struct ModelImage {
    std::string name, mime;
    std::vector<uint8_t> bytes;
};

struct Model {
    std::vector<ModelNode> nodes;  // 0 = gyökér
    std::vector<ModelMesh> meshes;
    std::vector<ModelMaterial> materials;
    std::vector<ModelImage> images;
    json vrm;             // VRM bővítmény (0.x vagy 1.0) nyers JSON
    int vrmVersion = 0;   // 0 = nem VRM
    json nodeExtras;      // pl. glTF csomópontnevek
    std::vector<std::pair<int, json>> constraints;  // VRMC_node_constraint (modell csomópont, leírás; a forrás glTF index)
    int findNode(const std::string& name) const {
        for (size_t i = 0; i < nodes.size(); i++)
            if (nodes[i].name == name) return (int)i;
        return -1;
    }
};

bool loadFbxModel(const fs::path& file, Model& out, std::string& err);
bool loadGltfModel(const fs::path& file, Model& out, std::string& err);
bool loadModel(const fs::path& file, Model& out, std::string& err);

// Érintővektorok számítása UV alapján, ha a fájl nem tartalmazza
void computeTangents(ModelMesh& m);
