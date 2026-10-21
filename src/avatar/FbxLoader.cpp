#include "avatar/Model.h"
#include "ufbx.h"
#include <unordered_map>
#include "core/I18n.h"

static Vec3 V(const ufbx_vec3& v) { return {(float)v.x, (float)v.y, (float)v.z}; }
static Quat Q(const ufbx_quat& q) { return Quat((float)q.x, (float)q.y, (float)q.z, (float)q.w); }
static Mat4 M(const ufbx_matrix& m) {
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int k = 0; k < 3; k++) r.at(k, c) = (float)m.cols[c].v[k];
    r.at(3, 0) = r.at(3, 1) = r.at(3, 2) = 0;
    r.at(3, 3) = 1;
    return r;
}

void computeTangents(ModelMesh& m) {
    size_t n = m.pos.size();
    if (m.uv.size() != n) return;
    std::vector<Vec3> t1(n, {0, 0, 0}), t2(n, {0, 0, 0});
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        uint32_t a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
        Vec3 e1 = m.pos[b] - m.pos[a], e2 = m.pos[c] - m.pos[a];
        Vec2 d1 = m.uv[b] - m.uv[a], d2 = m.uv[c] - m.uv[a];
        float r = d1.x * d2.y - d2.x * d1.y;
        if (std::fabs(r) < 1e-12f) continue;
        r = 1.0f / r;
        Vec3 sdir = (e1 * d2.y - e2 * d1.y) * r;
        Vec3 tdir = (e2 * d1.x - e1 * d2.x) * r;
        for (uint32_t v : {a, b, c}) {
            t1[v] += sdir;
            t2[v] += tdir;
        }
    }
    m.tan.resize(n);
    for (size_t i = 0; i < n; i++) {
        Vec3 nn = m.nrm[i];
        Vec3 t = normalize(t1[i] - nn * dot(nn, t1[i]));
        if (length(t) < 0.5f) {
            Vec3 up = std::fabs(nn.y) < 0.99f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
            t = normalize(cross(up, nn));
        }
        float w = dot(cross(nn, t), t2[i]) < 0 ? -1.0f : 1.0f;
        m.tan[i] = Vec4(t, w);
    }
}

bool loadFbxModel(const fs::path& file, Model& out, std::string& err) {
    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    opts.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
    opts.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_COMPENSATE;
    opts.pivot_handling = UFBX_PIVOT_HANDLING_ADJUST_TO_PIVOT;
    opts.generate_missing_normals = true;
    opts.clean_skin_weights = true;
    opts.ignore_animation = true;
    opts.allow_nodes_out_of_root = true;
    opts.connect_broken_elements = true;
    std::string path = pathToUtf8(file);
    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file_len(path.c_str(), path.size(), &opts, &error);
    if (!scene) {
        err = std::string("FBX hiba: ") + std::string(error.description.data, error.description.length);
        return false;
    }

    // csomópontok (a ufbx gyökér = 0)
    std::unordered_map<const ufbx_node*, int> nodeIndex;
    out.nodes.clear();
    std::function<void(const ufbx_node*, int)> addNode = [&](const ufbx_node* n, int parent) {
        int idx = (int)out.nodes.size();
        nodeIndex[n] = idx;
        ModelNode mn;
        mn.name = n->is_root ? "" : std::string(n->name.data, n->name.length);
        mn.parent = parent;
        mn.local.t = V(n->local_transform.translation);
        mn.local.r = Q(n->local_transform.rotation).normalized();
        mn.local.s = V(n->local_transform.scale);
        out.nodes.push_back(mn);
        if (parent >= 0) out.nodes[parent].children.push_back(idx);
        for (size_t i = 0; i < n->children.count; i++) addNode(n->children.data[i], idx);
    };
    addNode(scene->root_node, -1);

    // anyagok
    std::unordered_map<const ufbx_material*, int> matIndex;
    std::unordered_map<const ufbx_texture*, int> embedded;
    auto texRef = [&](const ufbx_texture* t) -> ModelTexRef {
        ModelTexRef r;
        if (!t) return r;
        if (t->type == UFBX_TEXTURE_FILE || t->type == UFBX_TEXTURE_LAYERED) {
            if (t->content.size > 0) {
                auto it = embedded.find(t);
                if (it == embedded.end()) {
                    ModelImage img;
                    img.name = std::string(t->filename.data, t->filename.length);
                    img.bytes.assign((const uint8_t*)t->content.data, (const uint8_t*)t->content.data + t->content.size);
                    embedded[t] = (int)out.images.size();
                    out.images.push_back(std::move(img));
                }
                r.image = embedded[t];
            }
            std::string fn(t->filename.data, t->filename.length);
            if (fn.empty()) fn = std::string(t->relative_filename.data, t->relative_filename.length);
            size_t slash = fn.find_last_of("/\\");
            r.file = slash == std::string::npos ? fn : fn.substr(slash + 1);
        }
        return r;
    };
    for (size_t i = 0; i < scene->materials.count; i++) {
        const ufbx_material* m = scene->materials.data[i];
        ModelMaterial mm;
        mm.name = std::string(m->name.data, m->name.length);
        const ufbx_material_map& dc = m->fbx.diffuse_color;
        if (dc.has_value) mm.baseColor = Vec4(V(dc.value_vec3), 1);
        if (m->fbx.diffuse_factor.has_value && m->fbx.diffuse_factor.value_real > 0) {
            float f = (float)m->fbx.diffuse_factor.value_real;
            mm.baseColor = Vec4(mm.baseColor.xyz() * f, 1);
        }
        mm.baseTex = texRef(dc.texture ? dc.texture : m->pbr.base_color.texture);
        if (mm.baseTex.valid()) mm.baseColor = Vec4(1, 1, 1, 1);
        mm.normalTex = texRef(m->fbx.normal_map.texture ? m->fbx.normal_map.texture : m->fbx.bump.texture);
        if (m->fbx.emission_color.has_value) mm.emissive = V(m->fbx.emission_color.value_vec3);
        if (m->fbx.emission_factor.has_value) mm.emissiveStrength = (float)m->fbx.emission_factor.value_real;
        mm.emissiveTex = texRef(m->fbx.emission_color.texture);
        if (m->fbx.transparency_factor.has_value && m->fbx.transparency_factor.value_real > 0.001 && m->fbx.transparency_factor.value_real < 0.999) {
            mm.alphaMode = "blend";
            mm.baseColor.w = 1.0f - (float)m->fbx.transparency_factor.value_real;
        }
        if (m->fbx.transparency_color.texture) mm.alphaMode = "mask";
        matIndex[m] = (int)out.materials.size();
        out.materials.push_back(mm);
    }

    // hálók
    std::vector<uint32_t> triIdx;
    for (size_t ni = 0; ni < scene->nodes.count; ni++) {
        const ufbx_node* node = scene->nodes.data[ni];
        const ufbx_mesh* mesh = node->mesh;
        if (!mesh || mesh->num_faces == 0) continue;
        ModelMesh mm;
        mm.name = std::string(node->name.data, node->name.length);
        mm.node = nodeIndex[node];
        triIdx.resize(mesh->max_face_triangles * 3);

        const ufbx_skin_deformer* skin = mesh->skin_deformers.count ? mesh->skin_deformers.data[0] : nullptr;
        mm.skinned = skin && skin->clusters.count > 0;

        // sarokpontok (triangulált) -> csúcs adatok
        struct Corner {
            Vec3 p, n;
            Vec2 uv;
            Vec4 t;
            uint32_t src;
        };
        std::vector<Corner> corners;
        corners.reserve(mesh->num_triangles * 3);
        std::vector<std::vector<uint32_t>> partCorners(std::max<size_t>(1, mesh->materials.count));
        bool hasTan = mesh->vertex_tangent.exists && mesh->vertex_bitangent.exists;
        for (size_t pi = 0; pi < mesh->material_parts.count; pi++) {
            const ufbx_mesh_part& part = mesh->material_parts.data[pi];
            size_t slot = part.index < partCorners.size() ? part.index : 0;
            for (size_t fi = 0; fi < part.face_indices.count; fi++) {
                ufbx_face face = mesh->faces.data[part.face_indices.data[fi]];
                uint32_t nt = ufbx_triangulate_face(triIdx.data(), triIdx.size(), mesh, face);
                for (uint32_t k = 0; k < nt * 3; k++) {
                    uint32_t ix = triIdx[k];
                    Corner c;
                    c.p = V(ufbx_get_vertex_vec3(&mesh->vertex_position, ix));
                    c.n = mesh->vertex_normal.exists ? normalize(V(ufbx_get_vertex_vec3(&mesh->vertex_normal, ix))) : Vec3(0, 1, 0);
                    if (mesh->vertex_uv.exists) {
                        ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, ix);
                        c.uv = {(float)uv.x, 1.0f - (float)uv.y};
                    }
                    if (hasTan) {
                        Vec3 t = normalize(V(ufbx_get_vertex_vec3(&mesh->vertex_tangent, ix)));
                        Vec3 b = V(ufbx_get_vertex_vec3(&mesh->vertex_bitangent, ix));
                        c.t = Vec4(t, dot(cross(c.n, t), b) < 0 ? -1.0f : 1.0f);
                    }
                    c.src = mesh->vertex_indices.data[ix];
                    partCorners[slot].push_back((uint32_t)corners.size());
                    corners.push_back(c);
                }
            }
        }
        if (corners.empty()) continue;

        // deduplikálás
        struct Key {
            uint32_t src;
            int32_t n[3];
            int32_t uv[2];
            bool operator==(const Key& o) const { return src == o.src && n[0] == o.n[0] && n[1] == o.n[1] && n[2] == o.n[2] && uv[0] == o.uv[0] && uv[1] == o.uv[1]; }
        };
        struct KeyHash {
            size_t operator()(const Key& k) const {
                size_t h = k.src * 2654435761u;
                for (int i = 0; i < 3; i++) h ^= (size_t)(k.n[i] * 73856093) + (h << 6) + (h >> 2);
                for (int i = 0; i < 2; i++) h ^= (size_t)(k.uv[i] * 19349663) + (h << 6) + (h >> 2);
                return h;
            }
        };
        std::unordered_map<Key, uint32_t, KeyHash> dedup;
        dedup.reserve(corners.size());
        std::vector<uint32_t> cornerToVert(corners.size());
        std::vector<uint32_t> vertSrc;
        for (size_t ci = 0; ci < corners.size(); ci++) {
            const Corner& c = corners[ci];
            Key k{c.src, {(int32_t)std::lround(c.n.x * 4096), (int32_t)std::lround(c.n.y * 4096), (int32_t)std::lround(c.n.z * 4096)}, {(int32_t)std::lround(c.uv.x * 65536), (int32_t)std::lround(c.uv.y * 65536)}};
            auto it = dedup.find(k);
            if (it != dedup.end()) {
                cornerToVert[ci] = it->second;
                continue;
            }
            uint32_t v = (uint32_t)mm.pos.size();
            dedup.emplace(k, v);
            cornerToVert[ci] = v;
            mm.pos.push_back(c.p);
            mm.nrm.push_back(c.n);
            mm.uv.push_back(c.uv);
            if (hasTan) mm.tan.push_back(c.t);
            vertSrc.push_back(c.src);
        }
        for (size_t s = 0; s < partCorners.size(); s++) {
            if (partCorners[s].empty()) continue;
            ModelSubmesh sm;
            sm.start = (uint32_t)mm.indices.size();
            sm.slot = (int)s;
            // a modell (és a Unity) óramutatóval ellentétes körüljárást használ
            for (uint32_t ci : partCorners[s]) mm.indices.push_back(cornerToVert[ci]);
            sm.count = (uint32_t)mm.indices.size() - sm.start;
            mm.submeshes.push_back(sm);
        }
        if (!hasTan) computeTangents(mm);

        // anyag slotok (a csomópont anyaglistája szerint, ahogy a Unity is)
        size_t slotCount = std::max(node->materials.count, mesh->materials.count);
        for (size_t s = 0; s < std::max<size_t>(1, slotCount); s++) {
            const ufbx_material* m = s < node->materials.count ? node->materials.data[s] : (s < mesh->materials.count ? mesh->materials.data[s] : nullptr);
            auto it = matIndex.find(m);
            mm.materials.push_back(it == matIndex.end() ? -1 : it->second);
        }

        size_t nv = mm.pos.size();
        if (mm.skinned) {
            for (size_t c = 0; c < skin->clusters.count; c++) {
                const ufbx_skin_cluster* cl = skin->clusters.data[c];
                mm.jointNodes.push_back(cl->bone_node ? nodeIndex[cl->bone_node] : mm.node);
                mm.invBind.push_back(M(cl->geometry_to_bone));
            }
            // tartalék csont a súly nélküli csúcsokhoz: a háló saját csomópontja
            int fallback = (int)mm.jointNodes.size();
            mm.jointNodes.push_back(mm.node);
            mm.invBind.push_back(M(node->geometry_to_node));
            mm.joints.resize(nv);
            mm.weights.resize(nv);
            for (size_t v = 0; v < nv; v++) {
                const ufbx_skin_vertex& sv = skin->vertices.data[vertSrc[v]];
                std::array<uint16_t, 4> j{0, 0, 0, 0};
                Vec4 w{0, 0, 0, 0};
                float total = 0;
                for (uint32_t k = 0; k < sv.num_weights && k < 4; k++) {
                    const ufbx_skin_weight& sw = skin->weights.data[sv.weight_begin + k];
                    j[k] = (uint16_t)sw.cluster_index;
                    w[k] = (float)sw.weight;
                    total += w[k];
                }
                if (total < 1e-6f) {
                    j = {(uint16_t)fallback, 0, 0, 0};
                    w = {1, 0, 0, 0};
                } else
                    for (int k = 0; k < 4; k++) w[k] /= total;
                mm.joints[v] = j;
                mm.weights[v] = w;
            }
        } else {
            // nem bőrözött háló: a geometria a csomópont terébe kerül
            Mat4 g = M(node->geometry_to_node);
            Mat4 gn = g.inverse().transposed();
            for (size_t v = 0; v < nv; v++) {
                mm.pos[v] = g.transformPoint(mm.pos[v]);
                mm.nrm[v] = normalize(gn.transformDir(mm.nrm[v]));
                if (v < mm.tan.size()) mm.tan[v] = Vec4(normalize(g.transformDir(mm.tan[v].xyz())), mm.tan[v].w);
            }
        }

        // blendshape-ek (a csatornák sorrendje egyezik a Unity indexeivel)
        if (mesh->blend_deformers.count) {
            std::unordered_map<uint32_t, std::vector<uint32_t>> srcToVerts;
            for (uint32_t v = 0; v < nv; v++) srcToVerts[vertSrc[v]].push_back(v);
            for (size_t bd = 0; bd < mesh->blend_deformers.count; bd++) {
                const ufbx_blend_deformer* def = mesh->blend_deformers.data[bd];
                for (size_t ch = 0; ch < def->channels.count; ch++) {
                    const ufbx_blend_channel* chan = def->channels.data[ch];
                    ModelMorph morph;
                    morph.name = std::string(chan->name.data, chan->name.length);
                    const ufbx_blend_shape* shape = chan->keyframes.count ? chan->keyframes.data[chan->keyframes.count - 1].shape : chan->target_shape;
                    if (shape) {
                        bool hasN = shape->normal_offsets.count == shape->num_offsets;
                        Mat4 g = mm.skinned ? Mat4() : M(node->geometry_to_node);
                        for (size_t o = 0; o < shape->num_offsets; o++) {
                            uint32_t sv = shape->offset_vertices.data[o];
                            auto it = srcToVerts.find(sv);
                            if (it == srcToVerts.end()) continue;
                            Vec3 dp = V(shape->position_offsets.data[o]);
                            Vec3 dn = hasN ? V(shape->normal_offsets.data[o]) : Vec3(0, 0, 0);
                            if (!mm.skinned) {
                                dp = g.transformDir(dp);
                                dn = g.transformDir(dn);
                            }
                            if (length(dp) < 1e-9f && length(dn) < 1e-9f) continue;
                            for (uint32_t v : it->second) {
                                morph.idx.push_back(v);
                                morph.dp.push_back(dp);
                                morph.dn.push_back(dn);
                            }
                        }
                    }
                    mm.morphs.push_back(std::move(morph));
                }
            }
        }
        mm.bmin = mm.bmax = mm.pos[0];
        for (auto& p : mm.pos) {
            mm.bmin = vmin(mm.bmin, p);
            mm.bmax = vmax(mm.bmax, p);
        }
        out.meshes.push_back(std::move(mm));
    }
    ufbx_free_scene(scene);
    return true;
}

bool loadModel(const fs::path& file, Model& out, std::string& err) {
    std::string ext = toLower(narrow(file.extension().wstring()));
    if (ext == ".fbx" || ext == ".obj") return loadFbxModel(file, out, err);
    if (ext == ".glb" || ext == ".gltf" || ext == ".vrm") return loadGltfModel(file, out, err);
    err = T("Unsupported model format: ") + ext;
    return false;
}
