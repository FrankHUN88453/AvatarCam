#pragma once
// Unity YAML olvasó (.prefab, .unity, .mat, .anim, .controller, .asset, .meta)
// A Unity által használt YAML részhalmazt olvassa, minden skalárt stringként tárol (JSON fában).
#include "core/Util.h"
#include "core/Math.h"

struct UnityDoc {
    int cls = 0;            // Unity osztály azonosító (pl. 1 GameObject, 4 Transform, 137 SkinnedMeshRenderer)
    std::string id;         // fileID (64 bites egész stringként)
    bool stripped = false;
    std::string type;       // pl. "GameObject"
    json d;                 // a dokumentum tartalma
};

std::vector<UnityDoc> parseUnityYaml(const std::string& text);
json parseYamlBlock(const std::string& text);  // egyetlen YAML dokumentum (pl. .meta)

// segédek a stringként tárolt értékekhez
float ynum(const json& j, float def = 0.0f);
int yint(const json& j, int def = 0);
float ynum(const json& obj, const char* key, float def = 0.0f);
int yint(const json& obj, const char* key, int def = 0);
std::string ystr(const json& obj, const char* key, const std::string& def = "");
Vec3 yvec3(const json& j, Vec3 def = {0, 0, 0});
Quat yquat(const json& j);
Vec4 ycolor(const json& j, Vec4 def = {1, 1, 1, 1});
const json& yget(const json& obj, const char* key);
const json& yarr(const json& obj, const char* key);  // mindig tömb (üres, ha nincs)

struct URef {
    std::string fileID = "0";
    std::string guid;
    bool isNull() const { return fileID.empty() || fileID == "0"; }
};
URef yref(const json& j);

std::string xorId(const std::string& a, const std::string& b);
void setPropertyPath(json& obj, const std::string& path, const json& value);
std::vector<int> hexInt32Array(const std::string& s);

// Unity (bal-kezes) -> jobb-kezes konverzió (X tükrözés)
inline Vec3 posToRH(const Vec3& p) { return {-p.x, p.y, p.z}; }
inline Quat quatToRH(const Quat& q) { return {q.x, -q.y, -q.z, q.w}; }
