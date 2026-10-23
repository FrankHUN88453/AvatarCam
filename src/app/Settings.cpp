#include "app/Settings.h"
#include "core/I18n.h"

void to_json(json& j, const Vec3& v) { j = json::array({v.x, v.y, v.z}); }
void from_json(const json& j, Vec3& v) {
    if (j.is_array() && j.size() >= 3) v = Vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
}

#define F(name) {#name, s.name}
#define G(name)                                                \
    if (j.contains(#name)) try {                               \
            j.at(#name).get_to(s.name);                        \
        } catch (...) {                                        \
        }

static void to_json(json& j, const LightSettings& s) {
    j = {F(name), F(type), F(enabled), F(color), F(intensity), F(yaw), F(pitch), F(position), F(range), F(spotAngle), F(spotSoftness), F(shadow), F(followCamera)};
}
static void from_json(const json& j, LightSettings& s) {
    G(name) G(type) G(enabled) G(color) G(intensity) G(yaw) G(pitch) G(position) G(range) G(spotAngle) G(spotSoftness) G(shadow) G(followCamera)
    // a régebbi (csak magyar) változat alapértelmezett fénynevei -> angol kulcs, amit a felület az aktuális nyelven mutat
    static const std::pair<const char*, const char*> oldNames[] = {
        {"Fő fény", "Key light"}, {"Derítő", "Fill light"}, {"Hátsó (kontúr)", "Back (rim)"}, {"Lila", "Purple"}, {"Cián", "Cyan"},
        {"Rózsaszín kontúr", "Pink rim"}, {"Nap", "Sun"}, {"Égbolt", "Sky"}, {"Kontúr", "Rim"}, {"Fő", "Key"}, {"Elöl", "Front"},
        {"Új fény", "New light"}, {"Pontfény", "Point light"}, {"Spotfény", "Spot light"}};
    for (auto& [hu, en] : oldNames)
        if (s.name == hu) s.name = en;
}
static void to_json(json& j, const BackgroundSettings& s) { j = {F(mode), F(color), F(color2), F(gradientAngle), F(image), F(imageFit), F(imageDim), F(chroma)}; }
static void from_json(const json& j, BackgroundSettings& s) { G(mode) G(color) G(color2) G(gradientAngle) G(image) G(imageFit) G(imageDim) G(chroma) }
static void to_json(json& j, const CameraSettings& s) {
    j = {F(target), F(distance), F(yaw), F(pitch), F(fov), F(roll), F(followHead), F(followAmount), F(smoothing), F(preset), F(forAvatar)};
}
static void from_json(const json& j, CameraSettings& s) { G(target) G(distance) G(yaw) G(pitch) G(fov) G(roll) G(followHead) G(followAmount) G(smoothing) G(preset) G(forAvatar) }
static void to_json(json& j, const EffectSettings& s) {
    j = {F(shading), F(toonThreshold), F(toonSoftness), F(shadeTint), F(outline), F(outlineColor), F(outlineWidth), F(rim), F(rimColor), F(rimIntensity), F(rimPower),
         F(bloom), F(bloomEmissiveOnly), F(bloomStrength), F(bloomThreshold), F(bloomRadius), F(toneMap), F(exposure), F(contrast), F(saturation), F(shadows), F(shadowStrength),
         F(groundShadow), F(groundShadowOpacity), F(ambientSky), F(ambientGround), F(ambientIntensity), F(emissionScale), F(matcapFallback), F(msaa)};
}
static void from_json(const json& j, EffectSettings& s) {
    G(shading) G(toonThreshold) G(toonSoftness) G(shadeTint) G(outline) G(outlineColor) G(outlineWidth) G(rim) G(rimColor) G(rimIntensity) G(rimPower) G(bloom) G(bloomEmissiveOnly)
    G(bloomStrength) G(bloomThreshold) G(bloomRadius) G(toneMap) G(exposure) G(contrast) G(saturation) G(shadows) G(shadowStrength) G(groundShadow)
    G(groundShadowOpacity) G(ambientSky) G(ambientGround) G(ambientIntensity) G(emissionScale) G(matcapFallback) G(msaa)
}
static void to_json(json& j, const OutputSettings& s) { j = {F(width), F(height), F(outputWindow), F(outputBorderless), F(spout), F(spoutName), F(straightAlpha), F(fpsLimit), F(mirror), F(uiIdleFps)}; }
static void from_json(const json& j, OutputSettings& s) { G(width) G(height) G(outputWindow) G(outputBorderless) G(spout) G(spoutName) G(straightAlpha) G(fpsLimit) G(mirror) G(uiIdleFps) }
static void to_json(json& j, const PoseSettings& s) {
    j = {F(armDown), F(armForward), F(elbowBend), F(wristBend), F(fingerCurl), F(breathing), F(sway), F(headIdle), F(autoBlink), F(blinkMin), F(blinkMax), F(doubleBlink), F(lookAround),
         F(lookAmount), F(bodyYaw), F(bodyOffset), F(scale)};
}
static void from_json(const json& j, PoseSettings& s) {
    G(armDown) G(armForward) G(elbowBend) G(wristBend) G(fingerCurl) G(breathing) G(sway) G(headIdle) G(autoBlink) G(blinkMin) G(blinkMax) G(doubleBlink) G(lookAround) G(lookAmount)
    G(bodyYaw) G(bodyOffset) G(scale)
    // régi beállítás (egyetlen átlagos időköz) átváltása tartománnyá: 4 mp -> 3..6 mp
    if (j.contains("blinkInterval") && !j.contains("blinkMin")) try {
            float iv = j.at("blinkInterval").get<float>();
            s.blinkMin = iv * 0.75f;
            s.blinkMax = iv * 1.5f;
        } catch (...) {
        }
    s.blinkMin = std::clamp(s.blinkMin, 0.3f, 60.0f);
    s.blinkMax = std::clamp(s.blinkMax, s.blinkMin, 60.0f);
}
static void to_json(json& j, const TrackingSettings& s) {
    j = {F(faceEnabled), F(handEnabled), F(handSmoothing), F(handMirror), F(cameraName), F(cameraIndex), F(cameraWidth), F(cameraHeight), F(mirror), F(smoothing), F(headScale), F(bodyFollow), F(eyeTracking),
         F(mouthTracking), F(blinkTracking), F(blinkThreshold), F(micEnabled), F(micName), F(micGain), F(micThreshold), F(micAutoGain), F(micMouth), F(vmcEnabled), F(vmcPort), F(showCamera)};
}
static void from_json(const json& j, TrackingSettings& s) {
    G(faceEnabled) G(handEnabled) G(handSmoothing) G(handMirror) G(cameraName) G(cameraIndex) G(cameraWidth) G(cameraHeight) G(mirror) G(smoothing) G(headScale) G(bodyFollow) G(eyeTracking) G(mouthTracking)
    G(blinkTracking) G(blinkThreshold) G(micEnabled) G(micName) G(micGain) G(micThreshold) G(micAutoGain) G(micMouth) G(vmcEnabled) G(vmcPort) G(showCamera)
}
static void to_json(json& j, const MaterialOverride& s) { j = {F(tint), F(hue), F(saturation), F(brightness), F(emissiveOverride), F(emissive), F(emissiveBoost), F(opacity)}; }
static void from_json(const json& j, MaterialOverride& s) { G(tint) G(hue) G(saturation) G(brightness) G(emissiveOverride) G(emissive) G(emissiveBoost) G(opacity) }
static void to_json(json& j, const AvatarOverrides& s) { j = {F(partVisible), F(materials), F(blendShapes), F(params), F(expressions), F(physics), F(physicsStrength), F(globalHue)}; }
static void from_json(const json& j, AvatarOverrides& s) { G(partVisible) G(materials) G(blendShapes) G(params) G(expressions) G(physics) G(physicsStrength) G(globalHue) }
static void to_json(json& j, const Hotkey& s) { j = {F(vk), F(ctrl), F(alt), F(shift), F(action), F(target), F(value), F(toggle)}; }
static void from_json(const json& j, Hotkey& s) { G(vk) G(ctrl) G(alt) G(shift) G(action) G(target) G(value) G(toggle) }

Settings::Settings() { resetLights(); }

void Settings::resetLights() {
    lights.clear();
    LightSettings key;
    key.name = "Key light";
    key.yaw = 30;
    key.pitch = 35;
    key.intensity = 1.6f;
    key.color = {1.0f, 0.97f, 0.92f};
    key.shadow = true;
    lights.push_back(key);
    LightSettings fill;
    fill.name = "Fill light";
    fill.yaw = -45;
    fill.pitch = 15;
    fill.intensity = 0.45f;
    fill.color = {0.7f, 0.8f, 1.0f};
    lights.push_back(fill);
    LightSettings rim;
    rim.name = "Back (rim)";
    rim.yaw = 160;
    rim.pitch = 25;
    rim.intensity = 1.2f;
    rim.color = {0.55f, 0.75f, 1.0f};
    lights.push_back(rim);
}

json Settings::sceneToJson() const {
    json j;
    j["background"] = background;
    j["lights"] = lights;
    j["camera"] = camera;
    j["effects"] = effects;
    j["pose"] = pose;
    return j;
}

void Settings::sceneFromJson(const json& j) {
    Settings& s = *this;
    G(background) G(lights) G(camera) G(effects) G(pose)
}

void to_json(json& j, const Settings& s) {
    j = {F(avatarId), F(background), F(lights), F(camera), F(effects), F(output), F(pose), F(tracking), F(avatars), F(hotkeys), F(uiScale), F(language), F(checkUpdates), F(skippedVersion), F(showPreview)};
}
void from_json(const json& j, Settings& s) {
    G(avatarId) G(background) G(lights) G(camera) G(effects) G(output) G(pose) G(tracking) G(avatars) G(hotkeys) G(uiScale) G(language) G(checkUpdates) G(skippedVersion) G(showPreview)
}

fs::path gSettingsPath;
static fs::path settingsPath() { return gSettingsPath.empty() ? dataDir() / L"settings.json" : gSettingsPath; }

bool loadSettings(Settings& s) {
    json j;
    if (!readJson(settingsPath(), j)) return false;
    try {
        from_json(j, s);
        return true;
    } catch (...) {
        return false;
    }
}

bool saveSettings(const Settings& s) {
    json j;
    to_json(j, s);
    return writeJson(settingsPath(), j, 2);
}
