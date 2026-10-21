#pragma once
#include "core/Math.h"
#include "core/Util.h"
#include <map>

void to_json(json& j, const Vec3& v);
void from_json(const json& j, Vec3& v);

struct LightSettings {
    std::string name = "Fény";
    int type = 0;  // 0 irányított, 1 pont, 2 spot
    bool enabled = true;
    Vec3 color{1, 1, 1};
    float intensity = 1.5f;
    float yaw = 35, pitch = 35;  // irányított/spot fény iránya (fok)
    Vec3 position{0.8f, 1.8f, 1.2f};
    float range = 6;
    float spotAngle = 35, spotSoftness = 0.3f;
    bool shadow = false;
    bool followCamera = false;
};

struct BackgroundSettings {
    int mode = 0;  // 0 átlátszó, 1 egyszínű, 2 színátmenet, 3 kép, 4 chroma key
    Vec3 color{0.08f, 0.09f, 0.12f};
    Vec3 color2{0.22f, 0.12f, 0.3f};
    float gradientAngle = 90;
    std::string image;
    int imageFit = 0;  // 0 kitöltés, 1 teljes kép, 2 nyújtás
    float imageDim = 0;
    Vec3 chroma{0, 1, 0};
};

struct CameraSettings {
    Vec3 target{0, 1.35f, 0};
    float distance = 1.3f;
    float yaw = 0, pitch = 4;
    float fov = 30;
    float roll = 0;
    bool followHead = false;
    float followAmount = 0.6f;
    float smoothing = 5;
    int preset = 1;
    std::string forAvatar;  // melyik avatárhoz lett beállítva
};

struct EffectSettings {
    int shading = 0;  // 0 toon, 1 PBR, 2 unlit
    float toonThreshold = 0.0f, toonSoftness = 0.08f;
    Vec3 shadeTint{0.72f, 0.70f, 0.82f};
    bool outline = false;
    Vec3 outlineColor{0.04f, 0.04f, 0.06f};
    float outlineWidth = 1.5f;
    bool rim = true;
    Vec3 rimColor{0.8f, 0.85f, 1.0f};
    float rimIntensity = 0.25f, rimPower = 4;
    bool bloom = true;
    bool bloomEmissiveOnly = true;
    float bloomStrength = 0.6f, bloomThreshold = 1.4f, bloomRadius = 0.7f;
    int toneMap = 1;  // 0 nincs, 1 ACES, 2 AgX, 3 Neutral
    float exposure = 1.0f, contrast = 1.0f, saturation = 1.0f;
    bool shadows = true;
    float shadowStrength = 0.65f;
    bool groundShadow = true;
    float groundShadowOpacity = 0.35f;
    Vec3 ambientSky{0.65f, 0.7f, 0.8f};
    Vec3 ambientGround{0.35f, 0.32f, 0.32f};
    float ambientIntensity = 0.55f;
    float emissionScale = 1.0f;
    int matcapFallback = 1;  // hiányzó Poiyomi matcap: 0 króm, 1 sötét fém, 2 kihagyás
    int msaa = 4;
};

struct OutputSettings {
    int width = 1920, height = 1080;
    bool outputWindow = false;
    bool outputBorderless = true;
    bool spout = false;
    std::string spoutName = "AvatarCam";
    bool straightAlpha = true;
    int fpsLimit = 60;
    bool mirror = false;
    int uiIdleFps = 15;  // a felület frissítése, ha az ablak nincs előtérben (0 = mindig teljes)
};

struct PoseSettings {
    float armDown = 72, armForward = 8, elbowBend = 12, wristBend = 5;
    float fingerCurl = 0.25f;
    float breathing = 1.0f, sway = 0.5f, headIdle = 0.5f;
    bool autoBlink = true;
    float blinkMin = 3.0f, blinkMax = 6.0f;  // két pislogás között véletlenszerűen ennyi idő telik (mp)
    bool doubleBlink = true;                 // néha dupla pislogás
    bool lookAround = true;
    float lookAmount = 0.4f;
    float bodyYaw = 0;  // az egész avatár elforgatása (fok)
    Vec3 bodyOffset{0, 0, 0};
    float scale = 1;
};

struct TrackingSettings {
    bool faceEnabled = false;
    bool handEnabled = false;
    float handSmoothing = 0.5f;
    bool handMirror = true;  // tükörkép: a jobb kezed az avatár bal kezét mozgatja
    std::string cameraName;
    int cameraIndex = 0;
    int cameraWidth = 640, cameraHeight = 480;
    bool mirror = true;
    float smoothing = 0.5f;
    float headScale = 1.0f;
    float bodyFollow = 0.3f;
    bool eyeTracking = true;
    bool mouthTracking = true;
    bool blinkTracking = true;
    float blinkThreshold = 0.5f;
    bool micEnabled = false;
    std::string micName;
    float micGain = 1.0f, micThreshold = 0.015f;
    bool vmcEnabled = false;
    int vmcPort = 39539;
    bool showCamera = false;
};

struct MaterialOverride {
    Vec3 tint{1, 1, 1};
    float hue = 0, saturation = 0, brightness = 0;
    bool emissiveOverride = false;
    Vec3 emissive{1, 1, 1};
    float emissiveBoost = 1;
    float opacity = 1;
    bool isDefault() const {
        return tint.x == 1 && tint.y == 1 && tint.z == 1 && hue == 0 && saturation == 0 && brightness == 0 && !emissiveOverride && emissiveBoost == 1 && opacity == 1;
    }
};

struct AvatarOverrides {
    std::map<std::string, bool> partVisible;
    std::map<std::string, MaterialOverride> materials;
    std::map<std::string, float> blendShapes;  // "<mesh útvonal>|<shape>" -> 0..1
    std::map<std::string, float> params;       // VRChat menü paraméterek
    std::map<std::string, float> expressions;  // VRM kifejezések (név -> 0..1)
    bool physics = true;
    float physicsStrength = 1;
    float globalHue = 0;
};

struct Hotkey {
    int vk = 0;
    bool ctrl = false, alt = false, shift = false;
    std::string action;  // "param", "gesture", "camera", "expression", "preset"
    std::string target;
    float value = 1;
    bool toggle = true;
};

struct Settings {
    std::string avatarId;
    BackgroundSettings background;
    std::vector<LightSettings> lights;
    CameraSettings camera;
    EffectSettings effects;
    OutputSettings output;
    PoseSettings pose;
    TrackingSettings tracking;
    std::map<std::string, AvatarOverrides> avatars;
    std::vector<Hotkey> hotkeys;
    float uiScale = 1.0f;
    int language = 0;  // felület nyelve: 0 angol, 1 magyar
    bool showPreview = true;

    Settings();
    void resetLights();
    json sceneToJson() const;  // előbeállításhoz: háttér, fények, kamera, effektek
    void sceneFromJson(const json& j);
};

void to_json(json& j, const Settings& s);
void from_json(const json& j, Settings& s);
bool loadSettings(Settings& s);
bool saveSettings(const Settings& s);
