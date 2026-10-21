#include "avatar/Physics.h"
#include "avatar/Avatar.h"
#include <unordered_set>

static Vec3 jv3p(const json& j, Vec3 def = {0, 0, 0}) {
    if (!j.is_array() || j.size() < 3) return def;
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

float PhysicsSim::evalCurve(const std::vector<std::pair<float, float>>& c, float t, float def) {
    if (c.empty()) return def;
    if (t <= c.front().first) return c.front().second;
    for (size_t i = 0; i + 1 < c.size(); i++)
        if (t <= c[i + 1].first) {
            float f = (t - c[i].first) / std::max(1e-5f, c[i + 1].first - c[i].first);
            return lerpf(c[i].second, c[i + 1].second, f);
        }
    return c.back().second;
}

void PhysicsSim::init(Avatar& av) {
    chains_.clear();
    colliders_.clear();
    const json& m = av.manifest;
    for (auto& c : m.value("colliders", json::array())) {
        Collider col;
        std::string p = c.value("path", "");
        auto it = av.nodeByPath.find(p);
        if (it == av.nodeByPath.end()) {
            colliders_.push_back(col);
            continue;
        }
        col.node = it->second;
        std::string shape = c.value("shape", "sphere");
        col.shape = shape == "capsule" ? 1 : shape == "plane" ? 2 : 0;
        col.radius = c.value("radius", 0.05f);
        col.height = c.value("height", 0.0f);
        col.pos = jv3p(c["position"]);
        auto& q = c["quaternion"];
        if (q.is_array()) col.rot = Quat(q[0], q[1], q[2], q[3]).normalized();
        col.inside = c.value("inside", false);
        colliders_.push_back(col);
    }
    auto curve = [](const json& j) {
        std::vector<std::pair<float, float>> out;
        if (j.is_array())
            for (auto& k : j) out.push_back({k[0].get<float>(), k[1].get<float>()});
        return out;
    };
    for (auto& pb : m.value("physBones", json::array())) {
        auto rit = av.nodeByPath.find(pb.value("root", ""));
        if (rit == av.nodeByPath.end()) continue;
        Chain ch;
        ch.enabled = pb.value("enabled", true);
        ch.pull = pb.value("pull", 0.2f);
        ch.spring = pb.value("spring", 0.2f);
        ch.stiffness = pb.value("stiffness", 0.2f);
        ch.gravity = pb.value("gravity", 0.0f);
        ch.gravityFalloff = pb.value("gravityFalloff", 0.0f);
        ch.radius = pb.value("radius", 0.0f);
        int lt = pb.value("limitType", 0);
        ch.limit = lt != 0;
        ch.maxAngle = std::max(pb.value("maxAngleX", 45.0f), pb.value("maxAngleZ", 45.0f)) * kDeg2Rad;
        for (auto& c : pb.value("colliders", json::array())) ch.colliders.push_back(c.get<int>());
        if (pb.contains("curves")) {
            ch.pullCurve = curve(pb["curves"].value("pull", json::array()));
            ch.springCurve = curve(pb["curves"].value("spring", json::array()));
            ch.stiffCurve = curve(pb["curves"].value("stiffness", json::array()));
            ch.gravCurve = curve(pb["curves"].value("gravity", json::array()));
            ch.radiusCurve = curve(pb["curves"].value("radius", json::array()));
        }
        std::unordered_set<int> ignore;
        for (auto& ig : pb.value("ignore", json::array())) {
            auto it = av.nodeByPath.find(ig.get<std::string>());
            if (it != av.nodeByPath.end()) ignore.insert(it->second);
        }
        Vec3 endpoint = jv3p(pb["endpoint"]);
        float endLength = pb.value("endLength", 0.0f);
        int multiChild = pb.value("multiChild", 0);
        // lánc bejárása
        int maxDepth = 0;
        std::function<void(int, int, int)> walk = [&](int node, int parentBone, int depth) {
            if (ignore.count(node)) return;
            maxDepth = std::max(maxDepth, depth);
            std::vector<int> kids;
            for (int c : av.nodes[node].children)
                if (!ignore.count(c)) kids.push_back(c);
            Bone b;
            b.node = node;
            b.parentBone = parentBone;
            b.t = (float)depth;
            if (kids.size() == 1 || (kids.size() > 1 && multiChild != 0)) {
                Vec3 tip(0, 0, 0);
                if (multiChild == 2 && kids.size() > 1) {
                    for (int k : kids) tip += av.nodes[k].rest.t;
                    tip = tip / (float)kids.size();
                } else
                    tip = av.nodes[kids[0]].rest.t;
                b.tipLocal = tip;
            } else if (kids.empty()) {
                if (length(endpoint) > 1e-5f) b.tipLocal = endpoint;
                else if (endLength > 0 && length(av.nodes[node].rest.t) > 1e-5f) b.tipLocal = normalize(av.nodes[node].rest.t) * (length(av.nodes[node].rest.t) * endLength);
                else b.simulate = false;
            } else
                b.simulate = false;  // több gyermek, "Ignore" mód
            int me = (int)ch.bones.size();
            ch.bones.push_back(b);
            for (int k : kids) walk(k, me, depth + 1);
        };
        walk(rit->second, -1, 0);
        for (auto& b : ch.bones) b.t = maxDepth > 0 ? b.t / maxDepth : 0;
        bool any = false;
        for (auto& b : ch.bones) any |= b.simulate;
        if (any) chains_.push_back(std::move(ch));
    }
    // VRM spring bone (0.x és 1.0)
    std::shared_ptr<Model> base = av.meshes.empty() ? nullptr : av.meshes[0].model;
    if (base && base->vrm.is_object()) {
        auto nodeOf = [&](int gltfNode) -> int {
            if (gltfNode < 0) return -1;
            // a glTF csomópontok sorrendben, a 0. avatár csomópont a szintetikus gyökér
            const std::string& n = base->nodes.size() > (size_t)gltfNode + 1 ? base->nodes[gltfNode + 1].name : "";
            return n.empty() ? -1 : av.findNodeByName(n);
        };
        auto addChainFrom = [&](int root, float stiff, float drag, float grav, float radius, const std::vector<int>& cols) {
            if (root < 0) return;
            Chain ch;
            ch.pull = clampf(stiff * 0.25f, 0.02f, 1.0f);
            ch.spring = clampf(1 - drag, 0, 1);
            ch.stiffness = 0;
            ch.gravity = grav;
            ch.radius = radius;
            ch.colliders = cols;
            std::function<void(int, int)> walk = [&](int node, int parentBone) {
                Bone b;
                b.node = node;
                b.parentBone = parentBone;
                auto& kids = av.nodes[node].children;
                if (!kids.empty()) b.tipLocal = av.nodes[kids[0]].rest.t;
                else if (length(av.nodes[node].rest.t) > 1e-5f) b.tipLocal = normalize(av.nodes[node].rest.t) * 0.07f;
                else b.simulate = false;
                int me = (int)ch.bones.size();
                ch.bones.push_back(b);
                if (!kids.empty()) walk(kids[0], me);
            };
            walk(root, -1);
            chains_.push_back(std::move(ch));
        };
        if (base->vrm.contains("secondaryAnimation")) {
            auto& sa = base->vrm["secondaryAnimation"];
            std::vector<std::vector<int>> groups;
            for (auto& g : sa.value("colliderGroups", json::array())) {
                std::vector<int> ids;
                int node = nodeOf(g.value("node", -1));
                for (auto& c : g.value("colliders", json::array())) {
                    Collider col;
                    col.node = node;
                    col.radius = c.value("radius", 0.05f);
                    auto o = c.value("offset", json::object());
                    col.pos = Vec3(o.value("x", 0.0f), o.value("y", 0.0f), o.value("z", 0.0f));
                    ids.push_back((int)colliders_.size());
                    colliders_.push_back(col);
                }
                groups.push_back(ids);
            }
            for (auto& bg : sa.value("boneGroups", json::array())) {
                std::vector<int> cols;
                for (auto& gi : bg.value("colliderGroups", json::array()))
                    if (gi.get<int>() < (int)groups.size()) cols.insert(cols.end(), groups[gi.get<int>()].begin(), groups[gi.get<int>()].end());
                for (auto& b : bg.value("bones", json::array()))
                    addChainFrom(nodeOf(b.get<int>()), bg.value("stiffiness", 1.0f), bg.value("dragForce", 0.4f), bg.value("gravityPower", 0.0f), bg.value("hitRadius", 0.02f), cols);
            }
        }
        if (base->vrm.contains("__springBone")) {
            auto& sb = base->vrm["__springBone"];
            std::vector<int> colMap;
            for (auto& c : sb.value("colliders", json::array())) {
                Collider col;
                col.node = nodeOf(c.value("node", -1));
                auto shape = c.value("shape", json::object());
                if (shape.contains("sphere")) {
                    col.radius = shape["sphere"].value("radius", 0.05f);
                    col.pos = jv3p(shape["sphere"].value("offset", json::array({0, 0, 0})));
                } else if (shape.contains("capsule")) {
                    col.shape = 1;
                    col.radius = shape["capsule"].value("radius", 0.05f);
                    Vec3 a = jv3p(shape["capsule"].value("offset", json::array({0, 0, 0})));
                    Vec3 b = jv3p(shape["capsule"].value("tail", json::array({0, 0, 0})));
                    col.pos = (a + b) * 0.5f;
                    col.height = length(b - a) + col.radius * 2;
                    col.rot = fromTo({0, 1, 0}, b - a);
                }
                colMap.push_back((int)colliders_.size());
                colliders_.push_back(col);
            }
            std::vector<std::vector<int>> groups;
            for (auto& g : sb.value("colliderGroups", json::array())) {
                std::vector<int> ids;
                for (auto& c : g.value("colliders", json::array()))
                    if (c.get<int>() < (int)colMap.size()) ids.push_back(colMap[c.get<int>()]);
                groups.push_back(ids);
            }
            for (auto& s : sb.value("springs", json::array())) {
                std::vector<int> cols;
                for (auto& gi : s.value("colliderGroups", json::array()))
                    if (gi.get<int>() < (int)groups.size()) cols.insert(cols.end(), groups[gi.get<int>()].begin(), groups[gi.get<int>()].end());
                auto joints = s.value("joints", json::array());
                if (joints.empty()) continue;
                auto& j0 = joints[0];
                addChainFrom(nodeOf(j0.value("node", -1)), j0.value("stiffness", 1.0f), j0.value("dragForce", 0.4f), j0.value("gravityPower", 0.0f), j0.value("hitRadius", 0.02f), cols);
            }
        }
    }
    initialized_ = false;
    logMsg("Physics: %zu chains, %zu colliders", chains_.size(), colliders_.size());
}

void PhysicsSim::reset(Avatar&) { initialized_ = false; }

void PhysicsSim::step(float dt, Avatar& av, float strength) {
    if (chains_.empty()) return;
    if (!initialized_) {
        for (auto& c : chains_)
            for (auto& b : c.bones) b.cur = b.prev = av.nodes[b.node].world.transformPoint(b.tipLocal);
        initialized_ = true;
        accum_ = 0;
    }
    for (auto& c : chains_)
        for (auto& b : c.bones) b.baseLocal = av.nodes[b.node].local.r;
    const float h = 1.0f / 90.0f;
    accum_ += std::min(dt, 0.1f);
    int steps = 0;
    while (accum_ >= h && steps < 9) {
        accum_ -= h;
        steps++;
        for (auto& c : chains_)
            if (c.enabled) simulate(c, av, h, strength);
    }
    // a köztes lépéshez is kell érvényes forgatás: az utolsó állapotot alkalmazzuk
    if (steps == 0)
        for (auto& c : chains_)
            if (c.enabled) simulate(c, av, 0, strength);
}

void PhysicsSim::simulate(Chain& c, Avatar& av, float h, float strength) {
    for (size_t bi = 0; bi < c.bones.size(); bi++) {
        Bone& b = c.bones[bi];
        AvNode& n = av.nodes[b.node];
        n.local.r = b.baseLocal;
        // a csont világ mátrixa a (már szimulált) szülőkből
        n.world = n.parent >= 0 ? av.nodes[n.parent].world * n.local.matrix() : n.local.matrix();
        if (!b.simulate) continue;
        Vec3 head = n.world.getTranslation();
        Vec3 restTip = n.world.transformPoint(b.tipLocal);
        float segLen = length(restTip - head);
        if (segLen < 1e-6f) continue;
        if (h > 0) {
            float pull = evalCurve(c.pullCurve, b.t, c.pull);
            float spring = evalCurve(c.springCurve, b.t, c.spring);
            float stiff = evalCurve(c.stiffCurve, b.t, c.stiffness);
            float grav = evalCurve(c.gravCurve, b.t, c.gravity);
            float pullF = clampf((pull * 0.35f + stiff * 0.25f) / std::max(0.2f, strength), 0.01f, 0.95f);
            float retain = clampf(0.55f + 0.42f * spring, 0, 0.98f);
            Vec3 vel = (b.cur - b.prev) * retain;
            Vec3 next = b.cur + vel + (restTip - b.cur) * pullF + Vec3(0, -1, 0) * (grav * segLen * 0.12f * strength);
            // hossz megtartása
            Vec3 d = next - head;
            float dl = length(d);
            next = dl > 1e-6f ? head + d * (segLen / dl) : restTip;
            // szöghatár
            if (c.limit) {
                Vec3 a = normalize(restTip - head), bdir = normalize(next - head);
                float ang = std::acos(clampf(dot(a, bdir), -1, 1));
                if (ang > c.maxAngle && ang > 1e-4f) {
                    Quat q = fromTo(a, bdir);
                    Quat lim = scaleRotation(q, c.maxAngle / ang);
                    next = head + lim.rotate(a) * segLen;
                }
            }
            // ütközés
            float r = evalCurve(c.radiusCurve, b.t, 1.0f) * c.radius * n.worldScale;
            for (int ci : c.colliders) {
                if (ci < 0 || ci >= (int)colliders_.size()) continue;
                const Collider& col = colliders_[ci];
                if (col.node < 0) continue;
                const Mat4& cw = av.nodes[col.node].world;
                float s = cw.getScale().x;
                float cr = col.radius * s;
                Vec3 cp = cw.transformPoint(col.pos);
                Vec3 closest = cp;
                if (col.shape == 1) {
                    Vec3 up = normalize(cw.transformDir(col.rot.rotate({0, 1, 0})));
                    float half = std::max(0.0f, col.height * s * 0.5f - cr);
                    Vec3 a = cp - up * half, e = cp + up * half;
                    Vec3 ab = e - a;
                    float t = clampf(dot(next - a, ab) / std::max(1e-8f, dot(ab, ab)), 0, 1);
                    closest = a + ab * t;
                } else if (col.shape == 2) {
                    Vec3 nrm = normalize(cw.transformDir(col.rot.rotate({0, 1, 0})));
                    float dist = dot(next - cp, nrm);
                    if (dist < r) next = next + nrm * (r - dist);
                    continue;
                }
                Vec3 diff = next - closest;
                float dd = length(diff);
                float minD = cr + r;
                if (!col.inside && dd < minD && dd > 1e-6f) next = closest + diff * (minD / dd);
                else if (col.inside && dd > cr - r && dd > 1e-6f) next = closest + diff * ((cr - r) / dd);
            }
            d = next - head;
            dl = length(d);
            if (dl > 1e-6f) next = head + d * (segLen / dl);
            b.prev = b.cur;
            b.cur = next;
        }
        // forgatás: a pihenő irányból az új irányba (világ térben), majd vissza lokálisba
        Vec3 from = restTip - head, to = b.cur - head;
        if (length(to) < 1e-6f) continue;
        Quat rotW = fromTo(from, to);
        Quat parentW = n.parent >= 0 ? av.nodes[n.parent].world.getRotation() : Quat();
        Quat curW = n.world.getRotation();
        Quat newW = rotW * curW;
        n.local.r = (parentW.conj() * newW).normalized();
        n.world = n.parent >= 0 ? av.nodes[n.parent].world * n.local.matrix() : n.local.matrix();
    }
    // a lánc alatti, nem szimulált csomópontok frissítése
    for (auto& b : c.bones)
        for (int ch : av.nodes[b.node].children) {
            bool inChain = false;
            for (auto& o : c.bones)
                if (o.node == ch) inChain = true;
            if (!inChain) av.computeWorld(ch);
        }
}
