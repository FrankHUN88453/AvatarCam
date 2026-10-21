#pragma once
#include "avatar/Model.h"
#include "gfx/Gfx.h"
#include <unordered_map>
#include <map>
#include <memory>
#include <atomic>

class Gfx;
struct AvatarOverrides;
struct PoseSettings;

// Anyag paraméterek (a shader ezekből dolgozik). Az animátor és a felhasználói felülírások ezeket módosítják.
struct MatcapLayer {
    float enabled = 0;
    float color[3] = {1, 1, 1};
    float intensity = 1;
    float emission = 0;
    float replace = 0, multiply = 0, add = 0;
    float hueShift = 0, hueShiftOn = 0;
    bool hasTex = false, hasMask = false;
};

struct MatParams {
    float color[3] = {1, 1, 1};
    float opacity = 1;
    float emissive[3] = {0, 0, 0};
    float emissiveIntensity = 1;
    float emissiveUseBase = 0;
    float hueShift = 0, hueShiftOn = 1, saturation = 0, brightness = 0;
    float alphaMapST[4] = {1, 1, 0, 0};
    float metalness = 0, roughness = 0.85f;
    float normalScale = 1;
    float alphaCutoff = 0.5f;
    MatcapLayer matcaps[4];
    float shade[3] = {-1, -1, -1};  // -1: globális árnyékszín
    float rim[3] = {0, 0, 0};
};

enum class AlphaMode { Opaque, Mask, Blend, Add };

struct AvMaterial {
    std::string key;    // "<renderer útvonal>#<slot>" – a felhasználói felülírások kulcsa
    std::string label;  // megjelenített név
    std::string sourceName;
    std::string family;
    AlphaMode alphaMode = AlphaMode::Opaque;
    int side = 0;  // 0 elöl, 1 hátul, 2 mindkettő
    bool unlit = false;
    bool flipV = false;  // Unity/FBX UV konvenció
    int renderQueue = -1;
    float uvST[4] = {1, 1, 0, 0};
    GpuTextureRef map, normalMap, emissiveMap, alphaMap, emissiveMask;
    GpuTextureRef matcapTex[4], matcapMask[4];
    MatParams base, cur;
    std::unordered_map<std::string, uint32_t> bindings;  // Unity tulajdonság -> mező eltolás a MatParams-ban
    float* field(uint32_t off) { return (float*)((char*)&cur + off); }
    // VRM MToon körvonal
    float outlineWidth = 0;
    Vec3 outlineColor{0, 0, 0};
};

struct AvNode {
    std::string name, path;
    int parent = -1;
    std::vector<int> children;
    Transform rest, local;
    Mat4 world;
    bool activeDefault = true, active = true, activeEff = true;
    bool created = false;
    float worldScale = 1;
};

struct AvMesh {
    std::string path;    // a renderer csomópontjának útvonala (ez a "testrész")
    std::string name;
    std::shared_ptr<Model> model;
    const ModelMesh* src = nullptr;
    int node = -1;
    std::vector<int> joints;  // AvNode indexek
    std::vector<Mat4> invBind;
    std::vector<int> mats;    // slot -> AvMaterial index
    bool enabledDefault = true, enabled = true;
    bool visible = true;      // végső láthatóság (csomópont + renderer + felülírás)
    std::vector<float> weightsDefault, weights, uploadedWeights;  // 0..1
    std::unordered_map<std::string, int> morphIndex;
    // GPU
    ComPtr<ID3D11Buffer> vbPosNrm, vbStatic, ib, bones;
    ComPtr<ID3D11ShaderResourceView> bonesSrv;
    bool dynamicPos = false;
    std::vector<float> morphScratch;  // pos+nrm
    Vec3 centerWorld{0, 0, 0};
};

// Humanoid csontok
enum HumanBone {
    HB_Hips, HB_Spine, HB_Chest, HB_UpperChest, HB_Neck, HB_Head, HB_LeftEye, HB_RightEye, HB_Jaw,
    HB_LeftShoulder, HB_LeftUpperArm, HB_LeftLowerArm, HB_LeftHand,
    HB_RightShoulder, HB_RightUpperArm, HB_RightLowerArm, HB_RightHand,
    HB_LeftUpperLeg, HB_LeftLowerLeg, HB_LeftFoot, HB_LeftToes,
    HB_RightUpperLeg, HB_RightLowerLeg, HB_RightFoot, HB_RightToes,
    // ujjak: bal majd jobb; hüvelyk, mutató, középső, gyűrűs, kisujj; 3 perc
    HB_LeftThumb1, HB_LeftThumb2, HB_LeftThumb3, HB_LeftIndex1, HB_LeftIndex2, HB_LeftIndex3, HB_LeftMiddle1, HB_LeftMiddle2, HB_LeftMiddle3,
    HB_LeftRing1, HB_LeftRing2, HB_LeftRing3, HB_LeftLittle1, HB_LeftLittle2, HB_LeftLittle3,
    HB_RightThumb1, HB_RightThumb2, HB_RightThumb3, HB_RightIndex1, HB_RightIndex2, HB_RightIndex3, HB_RightMiddle1, HB_RightMiddle2, HB_RightMiddle3,
    HB_RightRing1, HB_RightRing2, HB_RightRing3, HB_RightLittle1, HB_RightLittle2, HB_RightLittle3,
    HB_Count
};
const char* humanBoneName(int b);

// A pózoló bemenete (követés, tétlen animáció, kézjelek)
struct PoseInput {
    Quat head{};            // fej forgatás (avatár térben)
    Vec3 headOffset{0, 0, 0};
    float bodyFollow = 0.3f;
    float eyeYaw = 0, eyePitch = 0;  // radián
    bool eyesOverride = false;
    float blinkL = 0, blinkR = 0;
    std::unordered_map<std::string, float> arkit;  // ARKit 52 blendshape (0..1), ha van követés
    float visemes[15] = {};                        // sil..ou
    float mouthOpen = 0;
    int gestureLeft = 0, gestureRight = 0;
    float gestureWeightL = 1, gestureWeightR = 1;
    float time = 0;
    // kézkövetés (avatár bal = 0, jobb = 1), avatár térben (+Z előre, +X az avatár bal oldala)
    struct Hand {
        bool valid = false;
        Vec3 rel{0, 0, 0};             // csukló helye a fejhez (orrhoz) képest, méter (emberi méretben)
        Vec3 dir{0, 1, 0}, side{1, 0, 0};  // kézfej iránya (csukló -> középső ujjtő) és oldaliránya (kisujj -> mutatóujj)
        float finger[5][3] = {};       // ízületi hajlítás szögek (radián): hüvelyk, mutató, középső, gyűrűs, kis
    } hands[2];
};

class AnimatorSim;
class PhysicsSim;

class Avatar {
public:
    Avatar();
    ~Avatar();
    // Háttérszálon hívható (csak eszköz-szintű GPU erőforrásokat hoz létre)
    bool load(const fs::path& dir, Gfx& gfx, std::string& err, std::atomic<float>* progress = nullptr);

    std::string id, name, format;
    json manifest;
    std::vector<AvNode> nodes;
    std::vector<AvMesh> meshes;
    std::vector<AvMaterial> materials;
    int humanoid[HB_Count];
    std::unordered_map<std::string, int> nodeByPath;
    Vec3 viewPosition{0, 1.6f, 0.08f};
    float height = 1.7f;
    Vec3 boundsMin{-0.5f, 0, -0.3f}, boundsMax{0.5f, 1.8f, 0.3f};

    // Kifejezés menü / paraméterek
    struct Param {
        std::string name, type;
        float def = 0;
        bool menu = false;
    };
    std::vector<Param> params;
    std::unordered_map<std::string, float> paramValues;
    bool hasExpressions() const { return manifest.contains("expressions") && manifest["expressions"].is_object(); }

    // Arc
    struct BlendRef {
        int mesh = -1, index = -1;
    };
    BlendRef blink, blinkL, blinkR, lookUp, lookDown;
    BlendRef visemes[15];
    BlendRef mouthOpen;
    std::unordered_map<std::string, std::vector<BlendRef>> arkitShapes;  // kisbetűs ARKit név -> blendshape-ek
    struct VrmExpr {
        std::string name;
        std::vector<std::pair<BlendRef, float>> binds;
    };
    std::vector<VrmExpr> vrmExpressions;
    std::unordered_map<std::string, int> vrmExprIndex;
    int eyeL = -1, eyeR = -1;
    Quat eyeUp[2], eyeDown[2], eyeLeft[2], eyeRight[2];
    bool hasEyeLimits = false;

    // Frissítés
    void update(float dt, const PoseInput& in, const PoseSettings& pose, const AvatarOverrides* ov);
    void resetPhysics();
    void setParam(const std::string& name, float v);

    // Testrészek a felülethez
    struct Part {
        std::string path, label;
        std::vector<int> meshes;
        std::vector<int> materials;
    };
    std::vector<Part> parts;

    std::unique_ptr<AnimatorSim> animator;
    std::unique_ptr<PhysicsSim> physics;
    float physicsStrength = 1;
    bool physicsEnabled = true;

    // GPU feltöltés (fő szálon, rajzolás előtt)
    void uploadGpu(Gfx& gfx);

    int findNodeByName(const std::string& name) const;
    void computeWorld(int from = 0);
    Mat4 nodeWorld(int i) const { return nodes[i].world; }
    Vec3 headPosition() const;

private:
    struct LoadCtx;
    int ensureNode(const std::string& path);
    int addModelNodes(const std::shared_ptr<Model>& m, int attachTo, bool includeRoot, bool collapseSingleRoot = false);
    void setupMaterials(LoadCtx& ctx);
    void setupHumanoid();
    void setupFace();
    void setupParts();
    void applyPose(float dt, const PoseInput& in, const PoseSettings& pose);
    void applyFace(float dt, const PoseInput& in, const PoseSettings& pose);
    void setBoneDelta(int bone, const Quat& worldDelta);
    void applyConstraints();
    void applyHandIK(float dt, const PoseInput& in);
    void setWorldRotation(int node, const Quat& worldRot);
    float handWeight_[2] = {0, 0};
    float fingerWeight_[2] = {0, 0};
    struct Constraint {
        int type = 0;  // 0 roll, 1 aim, 2 rotation
        int dst = -1, src = -1;
        Vec3 axis{1, 0, 0};
        float weight = 1;
    };
    std::vector<Constraint> constraints_;

    std::unordered_map<const Model*, std::vector<int>> modelMap_;  // modell csomópont -> avatár csomópont
    std::vector<Vec3> restWorldPos_;
    std::vector<Quat> restWorldRot_;  // a gyökérhez képest, pihenő pózban
    std::vector<Quat> boneDelta_;     // humanoid csont delták (pihenő világ keret)
    float blinkTimer_ = 2, blinkPhase_ = -1, idleT_ = 0;
    bool doubleNext_ = false;
    Vec2 lookTarget_{0, 0}, lookCur_{0, 0};
    float lookTimer_ = 1;
    float smoothBlinkL_ = 0, smoothBlinkR_ = 0;
    float visemeSmooth_[15] = {};
};
