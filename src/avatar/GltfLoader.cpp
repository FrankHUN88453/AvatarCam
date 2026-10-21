#include "avatar/Model.h"
#include "cgltf.h"
#include <unordered_map>
#include "core/I18n.h"

static void readFloats(const cgltf_accessor* acc, size_t comps, std::vector<float>& out) {
    out.resize(acc->count * comps);
    for (size_t i = 0; i < acc->count; i++) cgltf_accessor_read_float(acc, i, &out[i * comps], comps);
}

bool loadGltfModel(const fs::path& file, Model& out, std::string& err) {
    std::string bytes;
    if (!readFile(file, bytes)) {
        err = T("The model could not be read.");
        return false;
    }
    cgltf_options opts = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse(&opts, bytes.data(), bytes.size(), &data) != cgltf_result_success) {
        err = T("Invalid glTF/VRM file.");
        return false;
    }
    std::string base = pathToUtf8(file);
    if (cgltf_load_buffers(&opts, data, base.c_str()) != cgltf_result_success) {
        err = T("Could not load the glTF buffers.");
        cgltf_free(data);
        return false;
    }

    // VRM bővítmény (a springBone külön bővítmény, ezért összefésüljük)
    json springBone;
    for (size_t i = 0; i < data->data_extensions_count; i++) {
        const cgltf_extension& e = data->data_extensions[i];
        std::string name = e.name ? e.name : "";
        try {
            if (name == "VRMC_vrm" || name == "VRM") {
                out.vrm = json::parse(e.data);
                out.vrmVersion = name == "VRM" ? 0 : 1;
                out.vrm["__ext"] = name;
            } else if (name == "VRMC_springBone")
                springBone = json::parse(e.data);
        } catch (...) {
        }
    }
    if (!springBone.is_null() && out.vrm.is_object()) out.vrm["__springBone"] = springBone;
    bool isVrm0 = out.vrm.is_object() && out.vrm.value("__ext", "") == "VRM";

    // képek
    for (size_t i = 0; i < data->images_count; i++) {
        const cgltf_image& im = data->images[i];
        ModelImage img;
        img.name = im.name ? im.name : ("image" + std::to_string(i));
        img.mime = im.mime_type ? im.mime_type : "";
        if (im.buffer_view) {
            const uint8_t* p = (const uint8_t*)im.buffer_view->buffer->data + im.buffer_view->offset;
            img.bytes.assign(p, p + im.buffer_view->size);
        } else if (im.uri) {
            std::string uri = im.uri;
            if (startsWith(uri, "data:")) {
                size_t comma = uri.find(',');
                void* outData = nullptr;
                std::string b64 = uri.substr(comma + 1);
                size_t sz = (b64.size() * 3) / 4;
                while (!b64.empty() && b64.back() == '=') {
                    b64.pop_back();
                    sz--;
                }
                if (cgltf_load_buffer_base64(&opts, sz, uri.c_str() + comma + 1, &outData) == cgltf_result_success) {
                    img.bytes.assign((uint8_t*)outData, (uint8_t*)outData + sz);
                    free(outData);
                }
            } else {
                std::string decoded = uri;
                cgltf_decode_uri(decoded.data());
                decoded.resize(strlen(decoded.c_str()));
                std::string fileBytes;
                if (readFile(file.parent_path() / u8path(decoded), fileBytes)) img.bytes.assign(fileBytes.begin(), fileBytes.end());
            }
        }
        out.images.push_back(std::move(img));
    }
    auto texRef = [&](const cgltf_texture_view& tv) {
        ModelTexRef r;
        if (tv.texture && tv.texture->image) r.image = (int)(tv.texture->image - data->images);
        else if (tv.texture && tv.texture->basisu_image) r.image = (int)(tv.texture->basisu_image - data->images);
        return r;
    };

    // anyagok
    for (size_t i = 0; i < data->materials_count; i++) {
        const cgltf_material& m = data->materials[i];
        ModelMaterial mm;
        mm.name = m.name ? m.name : ("material" + std::to_string(i));
        if (m.has_pbr_metallic_roughness) {
            const auto& pbr = m.pbr_metallic_roughness;
            mm.baseColor = Vec4(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3]);
            mm.baseTex = texRef(pbr.base_color_texture);
            mm.metal = pbr.metallic_factor;
            mm.rough = pbr.roughness_factor;
        }
        mm.normalTex = texRef(m.normal_texture);
        mm.normalScale = m.normal_texture.scale;
        mm.emissive = Vec3(m.emissive_factor[0], m.emissive_factor[1], m.emissive_factor[2]);
        if (m.has_emissive_strength) mm.emissiveStrength = m.emissive_strength.emissive_strength;
        mm.emissiveTex = texRef(m.emissive_texture);
        mm.alphaMode = m.alpha_mode == cgltf_alpha_mode_mask ? "mask" : m.alpha_mode == cgltf_alpha_mode_blend ? "blend" : "opaque";
        mm.cutoff = m.alpha_cutoff;
        mm.doubleSided = m.double_sided;
        mm.unlit = m.unlit;
        for (size_t e = 0; e < m.extensions_count; e++) {
            if (!m.extensions[e].name || std::string(m.extensions[e].name) != "VRMC_materials_mtoon") continue;
            try {
                json mt = json::parse(m.extensions[e].data);
                mm.mtoon = true;
                if (mt.contains("shadeColorFactor")) {
                    auto& c = mt["shadeColorFactor"];
                    mm.shadeColor = Vec3(c[0], c[1], c[2]);
                }
                if (mt.contains("parametricRimColorFactor")) {
                    auto& c = mt["parametricRimColorFactor"];
                    mm.rimColor = Vec3(c[0], c[1], c[2]);
                }
                if (mt.contains("matcapTexture")) mm.matcapTex.image = data->textures[mt["matcapTexture"].value("index", 0)].image ? (int)(data->textures[mt["matcapTexture"].value("index", 0)].image - data->images) : -1;
                if (mt.contains("matcapFactor")) {
                    auto& c = mt["matcapFactor"];
                    mm.matcapFactor = Vec3(c[0], c[1], c[2]);
                }
                if (mt.value("outlineWidthMode", "none") != "none") mm.outlineWidth = mt.value("outlineWidthFactor", 0.0f);
                if (mt.contains("outlineColorFactor")) {
                    auto& c = mt["outlineColorFactor"];
                    mm.outlineColor = Vec3(c[0], c[1], c[2]);
                }
            } catch (...) {
            }
        }
        out.materials.push_back(mm);
    }
    // VRM 0.x: MToon tulajdonságok a VRM.materialProperties-ben
    if (isVrm0 && out.vrm.contains("materialProperties")) {
        for (auto& mp : out.vrm["materialProperties"]) {
            std::string nm = mp.value("name", "");
            for (auto& mm : out.materials) {
                if (mm.name != nm) continue;
                std::string shader = mp.value("shader", "");
                if (shader.find("MToon") == std::string::npos) continue;
                mm.mtoon = true;
                auto& vp = mp["vectorProperties"];
                if (vp.contains("_ShadeColor")) mm.shadeColor = Vec3(vp["_ShadeColor"][0], vp["_ShadeColor"][1], vp["_ShadeColor"][2]);
                if (vp.contains("_RimColor")) mm.rimColor = Vec3(vp["_RimColor"][0], vp["_RimColor"][1], vp["_RimColor"][2]);
                auto& fp = mp["floatProperties"];
                int blend = fp.value("_BlendMode", 0.0f);
                mm.alphaMode = blend == 1 ? "mask" : blend >= 2 ? "blend" : mm.alphaMode;
                if (fp.contains("_Cutoff")) mm.cutoff = fp["_Cutoff"];
                if (fp.value("_CullMode", 2.0f) == 0) mm.doubleSided = true;
                if (fp.value("_OutlineWidthMode", 0.0f) > 0) mm.outlineWidth = fp.value("_OutlineWidth", 0.0f) * 0.01f;
                auto& tp = mp["textureProperties"];
                if (tp.contains("_SphereAdd")) {
                    int ti = tp["_SphereAdd"];
                    if (ti >= 0 && (size_t)ti < data->textures_count && data->textures[ti].image) mm.matcapTex.image = (int)(data->textures[ti].image - data->images);
                }
            }
        }
    }

    // csomópontok
    out.nodes.clear();
    out.nodes.push_back(ModelNode{"", -1, {}, {}});
    std::vector<int> nodeIdx(data->nodes_count, -1);
    for (size_t i = 0; i < data->nodes_count; i++) {
        const cgltf_node& n = data->nodes[i];
        ModelNode mn;
        mn.name = n.name ? n.name : ("node" + std::to_string(i));
        if (n.has_matrix) {
            Mat4 m;
            for (int k = 0; k < 16; k++) m.m[k] = n.matrix[k];
            mn.local.t = m.getTranslation();
            mn.local.r = m.getRotation();
            mn.local.s = m.getScale();
        } else {
            if (n.has_translation) mn.local.t = Vec3(n.translation[0], n.translation[1], n.translation[2]);
            if (n.has_rotation) mn.local.r = Quat(n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]).normalized();
            if (n.has_scale) mn.local.s = Vec3(n.scale[0], n.scale[1], n.scale[2]);
        }
        nodeIdx[i] = (int)out.nodes.size();
        out.nodes.push_back(mn);
        // VRM 1.0 csomópont kényszer
        for (size_t e = 0; e < n.extensions_count; e++)
            if (n.extensions[e].name && std::string(n.extensions[e].name) == "VRMC_node_constraint") {
                try {
                    json c = json::parse(n.extensions[e].data);
                    if (c.contains("constraint")) out.constraints.push_back({nodeIdx[i], c["constraint"]});
                } catch (...) {
                }
            }
    }
    for (size_t i = 0; i < data->nodes_count; i++) {
        const cgltf_node& n = data->nodes[i];
        int me = nodeIdx[i];
        int parent = n.parent ? nodeIdx[n.parent - data->nodes] : 0;
        out.nodes[me].parent = parent;
        out.nodes[parent].children.push_back(me);
    }
    // VRM 0.x a -Z irányba néz: 180°-os forgatás, hogy +Z felé nézzen (mint a Unity avatárok)
    if (isVrm0) out.nodes[0].local.r = Quat::axisAngle({0, 1, 0}, kPi);

    // hálók
    for (size_t i = 0; i < data->nodes_count; i++) {
        const cgltf_node& n = data->nodes[i];
        if (!n.mesh) continue;
        const cgltf_mesh& mesh = *n.mesh;
        ModelMesh mm;
        mm.name = n.name ? n.name : (mesh.name ? mesh.name : "mesh");
        mm.node = nodeIdx[i];
        mm.sourceMesh = (int)(n.mesh - data->meshes);
        mm.skinned = n.skin != nullptr;
        size_t morphCount = 0;
        for (size_t p = 0; p < mesh.primitives_count; p++) morphCount = std::max(morphCount, mesh.primitives[p].targets_count);
        mm.morphs.resize(morphCount);
        for (size_t t = 0; t < morphCount; t++) mm.morphs[t].name = t < mesh.target_names_count && mesh.target_names[t] ? mesh.target_names[t] : ("morph" + std::to_string(t));
        std::vector<float> tmp;
        for (size_t p = 0; p < mesh.primitives_count; p++) {
            const cgltf_primitive& prim = mesh.primitives[p];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            uint32_t baseV = (uint32_t)mm.pos.size();
            size_t count = 0;
            bool hasNrm = false, hasTan = false;
            for (size_t a = 0; a < prim.attributes_count; a++) {
                const cgltf_attribute& at = prim.attributes[a];
                if (at.type == cgltf_attribute_type_position) {
                    count = at.data->count;
                    readFloats(at.data, 3, tmp);
                    for (size_t k = 0; k < count; k++) mm.pos.push_back({tmp[k * 3], tmp[k * 3 + 1], tmp[k * 3 + 2]});
                }
            }
            if (!count) continue;
            mm.nrm.resize(baseV + count, {0, 1, 0});
            mm.uv.resize(baseV + count, {0, 0});
            mm.tan.resize(baseV + count, {1, 0, 0, 1});
            if (mm.skinned) {
                mm.joints.resize(baseV + count, {0, 0, 0, 0});
                mm.weights.resize(baseV + count, {1, 0, 0, 0});
            }
            for (size_t a = 0; a < prim.attributes_count; a++) {
                const cgltf_attribute& at = prim.attributes[a];
                if (at.type == cgltf_attribute_type_normal) {
                    hasNrm = true;
                    readFloats(at.data, 3, tmp);
                    for (size_t k = 0; k < count; k++) mm.nrm[baseV + k] = normalize({tmp[k * 3], tmp[k * 3 + 1], tmp[k * 3 + 2]});
                } else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) {
                    readFloats(at.data, 2, tmp);
                    for (size_t k = 0; k < count; k++) mm.uv[baseV + k] = {tmp[k * 2], tmp[k * 2 + 1]};
                } else if (at.type == cgltf_attribute_type_tangent) {
                    hasTan = true;
                    readFloats(at.data, 4, tmp);
                    for (size_t k = 0; k < count; k++) mm.tan[baseV + k] = {tmp[k * 4], tmp[k * 4 + 1], tmp[k * 4 + 2], tmp[k * 4 + 3]};
                } else if (at.type == cgltf_attribute_type_joints && at.index == 0 && mm.skinned) {
                    for (size_t k = 0; k < count; k++) {
                        cgltf_uint j[4] = {0, 0, 0, 0};
                        cgltf_accessor_read_uint(at.data, k, j, 4);
                        mm.joints[baseV + k] = {(uint16_t)j[0], (uint16_t)j[1], (uint16_t)j[2], (uint16_t)j[3]};
                    }
                } else if (at.type == cgltf_attribute_type_weights && at.index == 0 && mm.skinned) {
                    readFloats(at.data, 4, tmp);
                    for (size_t k = 0; k < count; k++) {
                        Vec4 w{tmp[k * 4], tmp[k * 4 + 1], tmp[k * 4 + 2], tmp[k * 4 + 3]};
                        float s = w.x + w.y + w.z + w.w;
                        if (s > 1e-6f) w = Vec4(w.x / s, w.y / s, w.z / s, w.w / s);
                        mm.weights[baseV + k] = w;
                    }
                }
            }
            (void)hasNrm;
            (void)hasTan;
            ModelSubmesh sm;
            sm.start = (uint32_t)mm.indices.size();
            sm.slot = (int)mm.materials.size();
            if (prim.indices) {
                for (size_t k = 0; k < prim.indices->count; k++) mm.indices.push_back(baseV + (uint32_t)cgltf_accessor_read_index(prim.indices, k));
            } else
                for (size_t k = 0; k < count; k++) mm.indices.push_back(baseV + (uint32_t)k);
            sm.count = (uint32_t)mm.indices.size() - sm.start;
            mm.submeshes.push_back(sm);
            mm.materials.push_back(prim.material ? (int)(prim.material - data->materials) : -1);
            // morph célok
            for (size_t t = 0; t < prim.targets_count; t++) {
                ModelMorph& morph = mm.morphs[t];
                std::vector<float> dp, dn;
                for (size_t a = 0; a < prim.targets[t].attributes_count; a++) {
                    const cgltf_attribute& at = prim.targets[t].attributes[a];
                    if (at.type == cgltf_attribute_type_position) readFloats(at.data, 3, dp);
                    else if (at.type == cgltf_attribute_type_normal) readFloats(at.data, 3, dn);
                }
                for (size_t k = 0; k < count; k++) {
                    Vec3 p = dp.empty() ? Vec3() : Vec3(dp[k * 3], dp[k * 3 + 1], dp[k * 3 + 2]);
                    Vec3 nn = dn.empty() ? Vec3() : Vec3(dn[k * 3], dn[k * 3 + 1], dn[k * 3 + 2]);
                    if (length(p) < 1e-9f && length(nn) < 1e-9f) continue;
                    morph.idx.push_back(baseV + (uint32_t)k);
                    morph.dp.push_back(p);
                    morph.dn.push_back(nn);
                }
            }
        }
        if (mm.pos.empty()) continue;
        bool anyTan = false;
        for (size_t p = 0; p < mesh.primitives_count && !anyTan; p++)
            for (size_t a = 0; a < mesh.primitives[p].attributes_count; a++)
                if (mesh.primitives[p].attributes[a].type == cgltf_attribute_type_tangent) anyTan = true;
        if (!anyTan) computeTangents(mm);
        if (mm.skinned) {
            const cgltf_skin& skin = *n.skin;
            for (size_t j = 0; j < skin.joints_count; j++) {
                mm.jointNodes.push_back(nodeIdx[skin.joints[j] - data->nodes]);
                Mat4 ib;
                if (skin.inverse_bind_matrices) cgltf_accessor_read_float(skin.inverse_bind_matrices, j, ib.m, 16);
                mm.invBind.push_back(ib);
            }
        }
        mm.bmin = mm.bmax = mm.pos[0];
        for (auto& p : mm.pos) {
            mm.bmin = vmin(mm.bmin, p);
            mm.bmax = vmax(mm.bmax, p);
        }
        out.meshes.push_back(std::move(mm));
    }
    // glTF csomópontnevek a VRM-hez (index -> név)
    json names = json::array();
    for (size_t i = 0; i < data->nodes_count; i++) names.push_back(out.nodes[nodeIdx[i]].name);
    out.nodeExtras = names;
    cgltf_free(data);
    return true;
}
