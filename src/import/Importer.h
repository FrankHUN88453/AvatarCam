#pragma once
#include "core/Util.h"

using ImportLog = std::function<void(const std::string& msg, float progress)>;

// Avatár importálása: .unitypackage (VRChat), .vrm, .glb, .gltf, .fbx, .zip
// Visszaadja az új avatár azonosítókat (az avatars/ mappán belüli könyvtárnevek).
std::vector<std::string> importAvatarFile(const fs::path& file, const ImportLog& log, std::string* error);

struct AvatarListEntry {
    std::string id, name, format, source;
};
std::vector<AvatarListEntry> listAvatars();
