#include "avatar/Animator.h"
#include "avatar/Avatar.h"

static int condMode(const std::string& m) {
    if (m == "if") return 1;
    if (m == "ifnot") return 2;
    if (m == "greater") return 3;
    if (m == "less") return 4;
    if (m == "equals") return 6;
    if (m == "notequal") return 7;
    return 1;
}

int AnimatorSim::targetFor(const std::string& key, TK kind, Avatar& av, const json& cv) {
    auto it = targetIndex_.find(key);
    if (it != targetIndex_.end()) return it->second;
    std::string path = cv.value("path", "");
    Target t;
    t.kind = kind;
    auto nit = av.nodeByPath.find(path);
    t.node = nit == av.nodeByPath.end() ? -1 : nit->second;
    std::vector<int> meshesAtPath;
    for (size_t i = 0; i < av.meshes.size(); i++)
        if (av.meshes[i].path == path) meshesAtPath.push_back((int)i);
    bool ok = true;
    switch (kind) {
        case TK::Active:
        case TK::Pos:
        case TK::Rot:
        case TK::Scale:
        case TK::ScaleAxis:
            ok = t.node >= 0;
            if (kind == TK::ScaleAxis) {
                std::string a = cv.value("prop", "x");
                t.axis = a == "y" ? 1 : a == "z" ? 2 : 0;
            }
            break;
        case TK::Enabled:
            ok = !meshesAtPath.empty();
            if (ok) t.mesh = meshesAtPath[0];
            break;
        case TK::Blend: {
            ok = false;
            std::string shape = cv.value("prop", "");
            for (int m : meshesAtPath) {
                auto mi = av.meshes[m].morphIndex.find(shape);
                if (mi != av.meshes[m].morphIndex.end()) {
                    t.mesh = m;
                    t.morph = mi->second;
                    ok = true;
                    break;
                }
            }
            break;
        }
        case TK::Mat: {
            std::string prop = cv.value("prop", "");
            for (int m : meshesAtPath)
                for (int mat : av.meshes[m].mats) {
                    if (mat < 0) continue;
                    auto b = av.materials[mat].bindings.find(prop);
                    if (b != av.materials[mat].bindings.end()) t.ptrs.push_back({mat, b->second});
                }
            // anyag-csere példányok is kapják meg
            for (size_t mi = 0; mi < av.materials.size(); mi++) {
                auto& mm = av.materials[mi];
                if (startsWith(mm.key, path + "#") && std::count(mm.key.begin(), mm.key.end(), '#') == 2) {
                    auto b = mm.bindings.find(prop);
                    if (b != mm.bindings.end()) t.ptrs.push_back({(int)mi, b->second});
                }
            }
            ok = !t.ptrs.empty();
            break;
        }
        case TK::MatRef:
            ok = !meshesAtPath.empty();
            if (ok) {
                t.mesh = meshesAtPath[0];
                t.slot = cv.value("slot", 0);
            }
            break;
    }
    int idx = -1;
    if (ok) {
        idx = (int)targets_.size();
        targets_.push_back(t);
    }
    targetIndex_[key] = idx;
    return idx;
}

std::shared_ptr<AnimatorSim::Motion> AnimatorSim::parseMotion(const json& m, const std::unordered_map<std::string, int>& clipIds) {
    if (!m.is_object()) return nullptr;
    auto out = std::make_shared<Motion>();
    if (m.contains("clip")) {
        auto it = clipIds.find(m["clip"].get<std::string>());
        out->clip = it == clipIds.end() ? -1 : it->second;
        return out;
    }
    out->tree = m.value("tree", 0);
    out->param = m.value("param", "");
    out->paramY = m.value("paramY", "");
    for (auto& c : m.value("children", json::array())) {
        Motion::Child ch;
        ch.m = parseMotion(c["motion"], clipIds);
        ch.threshold = c.value("threshold", 0.0f);
        if (c.contains("position")) ch.pos = Vec2(c["position"][0], c["position"][1]);
        ch.directParam = c.value("directParam", "");
        out->children.push_back(ch);
    }
    return out;
}

void AnimatorSim::init(const json& fx, const json& clips, Avatar& av) {
    // klipek fordítása
    std::unordered_map<std::string, int> clipIds;
    for (auto& [cid, c] : clips.items()) {
        Clip clip;
        clip.length = c.value("length", 0.0f);
        clip.loop = c.value("loop", false);
        for (auto& cv : c["curves"]) {
            std::string type = cv.value("type", "");
            std::string path = cv.value("path", "");
            TK kind;
            int stride = 1;
            if (type == "active") kind = TK::Active;
            else if (type == "enabled") kind = TK::Enabled;
            else if (type == "blend") kind = TK::Blend;
            else if (type == "mat") kind = TK::Mat;
            else if (type == "matref") kind = TK::MatRef;
            else if (type == "pos") kind = TK::Pos, stride = 3;
            else if (type == "rot") kind = TK::Rot, stride = 4;
            else if (type == "scale") kind = TK::Scale, stride = 3;
            else if (type == "scaleAxis") kind = TK::ScaleAxis;
            else continue;
            std::string key = type + "|" + path + "|" + cv.value("prop", "") + "|" + std::to_string(cv.value("slot", 0));
            int t = targetFor(key, kind, av, cv);
            if (t < 0) continue;
            Curve curve;
            curve.target = t;
            curve.stride = stride;
            for (auto& k : cv["keys"]) {
                curve.times.push_back(k[0].get<float>());
                if (kind == TK::MatRef) {
                    int mat = -1;
                    if (k[1].is_string()) {
                        std::string want = path + "#" + std::to_string(cv.value("slot", 0)) + "#" + k[1].get<std::string>();
                        for (size_t i = 0; i < av.materials.size(); i++)
                            if (av.materials[i].key == want) mat = (int)i;
                    }
                    curve.matIdx.push_back(mat);
                    curve.values.push_back(0);
                } else if (stride == 1)
                    curve.values.push_back(k[1].get<float>());
                else
                    for (int s = 0; s < stride; s++) curve.values.push_back(k[1][s].get<float>());
            }
            if (!curve.times.empty()) clip.curves.push_back(std::move(curve));
        }
        clipIds[cid] = (int)clips_.size();
        clips_.push_back(std::move(clip));
    }
    auto parseTransitions = [&](const json& arr) {
        std::vector<Transition> out;
        for (auto& t : arr) {
            Transition tr;
            tr.to = t["to"].is_string() ? t["to"].get<std::string>() : "";
            tr.toMachine = t["toMachine"].is_string() ? t["toMachine"].get<std::string>() : "";
            tr.exit = t.value("exit", false);
            tr.hasExitTime = t.value("hasExitTime", false);
            tr.exitTime = t.value("exitTime", 1.0f);
            tr.canSelf = t.value("canSelf", false);
            for (auto& c : t.value("conditions", json::array())) tr.conds.push_back({condMode(c.value("mode", "if")), c.value("param", ""), c.value("threshold", 0.0f)});
            out.push_back(tr);
        }
        return out;
    };
    for (auto& [mid, m] : jobj(fx, "machines").items()) {
        Machine mm;
        for (auto& s : m["states"]) mm.states.push_back(s.get<std::string>());
        for (auto& s : m["machines"]) mm.machines.push_back(s.get<std::string>());
        mm.defaultState = m["defaultState"].is_string() ? m["defaultState"].get<std::string>() : "";
        mm.any = parseTransitions(m.value("any", json::array()));
        mm.entry = parseTransitions(m.value("entry", json::array()));
        machines_[mid] = mm;
    }
    for (auto& [sid, s] : jobj(fx, "states").items()) {
        State st;
        st.name = s.value("name", "");
        st.machine = s.value("machine", "");
        st.motion = parseMotion(s["motion"], clipIds);
        st.speed = s.value("speed", 1.0f);
        st.timeParam = s.value("timeParam", "");
        st.transitions = parseTransitions(s.value("transitions", json::array()));
        for (auto& b : s.value("behaviours", json::array())) {
            std::vector<DriverOp> ops;
            for (auto& o : b.value("ops", json::array())) {
                std::string op = o.value("op", "set");
                ops.push_back({op == "add" ? 1 : op == "random" ? 2 : op == "copy" ? 3 : 0, o.value("name", ""), o.value("source", ""), o.value("value", 0.0f),
                               o.value("min", 0.0f), o.value("max", 1.0f), o.value("chance", 1.0f)});
            }
            st.drivers.push_back(ops);
        }
        states_[sid] = st;
    }
    for (auto& l : fx.value("layers", json::array())) {
        Layer layer;
        layer.name = l.value("name", "");
        layer.machine = l.value("machine", "");
        layer.weight = l.value("weight", 1.0f);
        layers_.push_back(layer);
    }
    for (auto& l : layers_) enterState(l, enterMachine(l.machine, av), av);
    acc_.assign(targets_.size() * 4, 0);
    accW_.assign(targets_.size(), 0);
    isTouched_.assign(targets_.size(), 0);
    origMats_.resize(av.meshes.size());
    for (size_t i = 0; i < av.meshes.size(); i++) origMats_[i] = av.meshes[i].mats;
    logMsg("Animator: %zu layers, %zu states, %zu clips, %zu animated properties", layers_.size(), states_.size(), clips_.size(), targets_.size());
}

void AnimatorSim::resetMaterialSlots(Avatar& av) {
    for (size_t i = 0; i < av.meshes.size() && i < origMats_.size(); i++) av.meshes[i].mats = origMats_[i];
}

bool AnimatorSim::condsOk(const std::vector<Cond>& cs, Avatar& av) const {
    for (auto& c : cs) {
        auto it = av.paramValues.find(c.param);
        if (it == av.paramValues.end()) return false;
        float v = it->second;
        switch (c.mode) {
            case 1: if (!(v > 0.5f)) return false; break;
            case 2: if (v > 0.5f) return false; break;
            case 3: if (!(v > c.threshold)) return false; break;
            case 4: if (!(v < c.threshold)) return false; break;
            case 6: if (std::fabs(v - c.threshold) > 0.001f) return false; break;
            case 7: if (std::fabs(v - c.threshold) <= 0.001f) return false; break;
        }
    }
    return true;
}

std::string AnimatorSim::enterMachine(const std::string& mid, Avatar& av, int depth) {
    auto it = machines_.find(mid);
    if (it == machines_.end() || depth > 8) return "";
    for (auto& t : it->second.entry) {
        if (!condsOk(t.conds, av)) continue;
        if (!t.to.empty()) return t.to;
        if (!t.toMachine.empty()) return enterMachine(t.toMachine, av, depth + 1);
    }
    return it->second.defaultState;
}

void AnimatorSim::enterState(Layer& l, const std::string& sid, Avatar& av) {
    l.current = sid;
    l.time = 0;
    auto it = states_.find(sid);
    if (it == states_.end()) return;
    for (auto& ops : it->second.drivers)
        for (auto& o : ops) {
            float& v = av.paramValues[o.name];
            switch (o.op) {
                case 0: v = o.value; break;
                case 1: v += o.value; break;
                case 2: {
                    float r = (float)rand() / RAND_MAX;
                    v = (o.max > o.min) ? o.min + r * (o.max - o.min) : (r < o.chance ? 1.0f : 0.0f);
                    break;
                }
                case 3: {
                    auto s = av.paramValues.find(o.source);
                    if (s != av.paramValues.end()) v = s->second;
                    break;
                }
            }
        }
}

float AnimatorSim::clipLength(const Motion& m) const {
    if (m.clip >= 0) return clips_[m.clip].length;
    float l = 0;
    for (auto& c : m.children)
        if (c.m) l = std::max(l, clipLength(*c.m));
    return l;
}

void AnimatorSim::sampleClip(int ci, float t, float w) {
    if (ci < 0 || w <= 1e-5f) return;
    const Clip& clip = clips_[ci];
    for (const Curve& c : clip.curves) {
        // kulcskocka keresés
        size_t n = c.times.size();
        size_t k = 0;
        while (k + 1 < n && c.times[k + 1] <= t) k++;
        float f = 0;
        if (k + 1 < n && c.times[k + 1] > c.times[k]) f = clampf((t - c.times[k]) / (c.times[k + 1] - c.times[k]), 0, 1);
        int ti = c.target;
        if (!isTouched_[ti]) {
            isTouched_[ti] = 1;
            touched_.push_back(ti);
            accW_[ti] = 0;
            for (int s = 0; s < 4; s++) acc_[ti * 4 + s] = 0;
        }
        const Target& tg = targets_[ti];
        if (tg.kind == TK::MatRef) {
            // diszkrét: a nagyobb súlyú nyer
            if (w >= accW_[ti]) acc_[ti * 4] = (float)c.matIdx[f < 1 ? k : std::min(k + 1, n - 1)];
            accW_[ti] = std::max(accW_[ti], w);
            continue;
        }
        if (tg.kind == TK::Rot) {
            const float* a = &c.values[k * 4];
            const float* b = &c.values[std::min(k + 1, n - 1) * 4];
            Quat q = nlerp(Quat(a[0], a[1], a[2], a[3]), Quat(b[0], b[1], b[2], b[3]), f);
            Quat prev(acc_[ti * 4], acc_[ti * 4 + 1], acc_[ti * 4 + 2], acc_[ti * 4 + 3]);
            if (accW_[ti] > 0 && dot(prev, q) < 0) q = Quat(-q.x, -q.y, -q.z, -q.w);
            acc_[ti * 4] += q.x * w, acc_[ti * 4 + 1] += q.y * w, acc_[ti * 4 + 2] += q.z * w, acc_[ti * 4 + 3] += q.w * w;
            accW_[ti] += w;
            continue;
        }
        for (int s = 0; s < c.stride; s++) {
            float a = c.values[k * c.stride + s];
            float b = c.values[std::min(k + 1, n - 1) * c.stride + s];
            acc_[ti * 4 + s] += lerpf(a, b, f) * w;
        }
        accW_[ti] += w;
    }
}

void AnimatorSim::sampleMotion(const Motion& m, float time, float weight, const State& st, Avatar& av, std::vector<std::pair<int, float>>& out) {
    if (weight <= 1e-5f) return;
    if (m.tree < 0) {
        out.push_back({m.clip, weight});
        return;
    }
    auto pv = [&](const std::string& n) {
        auto it = av.paramValues.find(n);
        return it == av.paramValues.end() ? 0.0f : it->second;
    };
    std::vector<float> ws(m.children.size(), 0);
    if (m.tree == 0) {
        // 1D: a két szomszédos küszöb közötti lineáris súlyozás
        float p = pv(m.param);
        std::vector<int> order(m.children.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
        std::sort(order.begin(), order.end(), [&](int a, int b) { return m.children[a].threshold < m.children[b].threshold; });
        if (!order.empty()) {
            if (p <= m.children[order.front()].threshold) ws[order.front()] = 1;
            else if (p >= m.children[order.back()].threshold) ws[order.back()] = 1;
            else
                for (size_t i = 0; i + 1 < order.size(); i++) {
                    float a = m.children[order[i]].threshold, b = m.children[order[i + 1]].threshold;
                    if (p >= a && p <= b) {
                        float f = b > a ? (p - a) / (b - a) : 0;
                        ws[order[i]] = 1 - f;
                        ws[order[i + 1]] = f;
                        break;
                    }
                }
        }
    } else if (m.tree == 4) {
        for (size_t i = 0; i < m.children.size(); i++) ws[i] = pv(m.children[i].directParam);
    } else {
        // 2D: inverz távolság súlyozás
        Vec2 p(pv(m.param), pv(m.paramY));
        float total = 0;
        for (size_t i = 0; i < m.children.size(); i++) {
            Vec2 d = m.children[i].pos - p;
            float dist = std::sqrt(d.x * d.x + d.y * d.y);
            if (dist < 1e-4f) {
                std::fill(ws.begin(), ws.end(), 0.0f);
                ws[i] = 1;
                total = 1;
                break;
            }
            ws[i] = 1.0f / (dist * dist);
            total += ws[i];
        }
        if (total > 0)
            for (auto& w : ws) w /= total;
    }
    for (size_t i = 0; i < m.children.size(); i++)
        if (m.children[i].m) sampleMotion(*m.children[i].m, time, weight * ws[i], st, av, out);
}

void AnimatorSim::evaluate(float dt, Avatar& av) {
    if (layers_.empty()) return;
    std::vector<std::pair<int, float>> clipW;
    // rétegenként: átmenetek, majd mintavétel; a későbbi réteg felülírja a korábbit
    std::vector<float> finalV(targets_.size() * 4, 0);
    std::vector<char> finalSet(targets_.size(), 0);
    for (auto& l : layers_) {
        if (l.weight <= 0.001f && &l != &layers_[0]) continue;
        // átmenetek (max. néhány lépés képkockánként)
        for (int iter = 0; iter < 6; iter++) {
            auto sit = states_.find(l.current);
            if (sit == states_.end()) {
                std::string s = enterMachine(l.machine, av);
                if (s.empty()) break;
                enterState(l, s, av);
                continue;
            }
            const State& st = sit->second;
            const Transition* take = nullptr;
            // Any State átmenetek: a gyökér és az aktuális állapotgép
            for (const std::string& mid : {l.machine, st.machine}) {
                auto mit = machines_.find(mid);
                if (mit == machines_.end()) continue;
                for (auto& t : mit->second.any) {
                    if (t.conds.empty()) continue;
                    if (!condsOk(t.conds, av)) continue;
                    if (t.to == l.current && !t.canSelf) continue;
                    take = &t;
                    break;
                }
                if (take) break;
            }
            if (!take) {
                float len = st.motion ? clipLength(*st.motion) : 0;
                float norm = len > 0 ? l.time * st.speed / len : 1e9f;
                for (auto& t : st.transitions) {
                    if (t.conds.empty() && !t.hasExitTime) continue;
                    if (t.hasExitTime && norm < t.exitTime) continue;
                    if (!condsOk(t.conds, av)) continue;
                    take = &t;
                    break;
                }
            }
            if (!take) break;
            std::string next;
            if (take->exit) next = enterMachine(l.machine, av);
            else if (!take->to.empty()) next = take->to;
            else if (!take->toMachine.empty()) next = enterMachine(take->toMachine, av);
            if (next.empty() || (next == l.current && !take->canSelf && take->exit == false && iter > 0)) break;
            enterState(l, next, av);
        }
        l.time += dt;
        auto sit = states_.find(l.current);
        if (sit == states_.end() || !sit->second.motion) continue;
        const State& st = sit->second;
        clipW.clear();
        sampleMotion(*st.motion, l.time, 1.0f, st, av, clipW);
        // klipek mintavétele ebbe a rétegbe
        for (int t : touched_) isTouched_[t] = 0;
        touched_.clear();
        for (auto& [ci, w] : clipW) {
            if (ci < 0) continue;
            const Clip& clip = clips_[ci];
            float t;
            if (!st.timeParam.empty()) {
                auto pv = av.paramValues.find(st.timeParam);
                t = clampf(pv == av.paramValues.end() ? 0 : pv->second, 0, 1) * clip.length;
            } else {
                t = l.time * st.speed;
                if (clip.length > 0) t = clip.loop ? std::fmod(t, clip.length) : std::min(t, clip.length);
                else t = 0;
            }
            sampleClip(ci, t, w);
        }
        float lw = &l == &layers_[0] ? 1.0f : clampf(l.weight, 0, 1);
        for (int ti : touched_) {
            float w = accW_[ti];
            if (w <= 1e-6f) continue;
            const Target& tg = targets_[ti];
            if (tg.kind == TK::MatRef) {
                finalV[ti * 4] = acc_[ti * 4];
                finalSet[ti] = 1;
                continue;
            }
            float inv = 1.0f / w;
            float blendW = lw * std::min(1.0f, w);
            for (int s = 0; s < 4; s++) {
                float v = acc_[ti * 4 + s] * inv;
                finalV[ti * 4 + s] = finalSet[ti] ? lerpf(finalV[ti * 4 + s], v, blendW) : v;
            }
            finalSet[ti] = 1;
        }
    }
    // eredmények alkalmazása
    for (size_t ti = 0; ti < targets_.size(); ti++) {
        if (!finalSet[ti]) continue;
        const Target& t = targets_[ti];
        const float* v = &finalV[ti * 4];
        switch (t.kind) {
            case TK::Active: av.nodes[t.node].active = v[0] > 0.5f; break;
            case TK::Enabled: av.meshes[t.mesh].enabled = v[0] > 0.5f; break;
            case TK::Blend: av.meshes[t.mesh].weights[t.morph] = clampf(v[0] / 100.0f, -1, 2); break;
            case TK::Mat:
                for (auto& [mi, off] : t.ptrs) *av.materials[mi].field(off) = v[0];
                break;
            case TK::MatRef: {
                int mi = (int)v[0];
                if (mi >= 0 && t.slot < (int)av.meshes[t.mesh].mats.size()) av.meshes[t.mesh].mats[t.slot] = mi;
                break;
            }
            case TK::Pos: av.nodes[t.node].local.t = Vec3(v[0], v[1], v[2]); break;
            case TK::Rot: av.nodes[t.node].local.r = Quat(v[0], v[1], v[2], v[3]).normalized(); break;
            case TK::Scale: av.nodes[t.node].local.s = Vec3(v[0], v[1], v[2]); break;
            case TK::ScaleAxis: av.nodes[t.node].local.s[t.axis] = v[0]; break;
        }
    }
}

std::vector<std::pair<std::string, std::string>> AnimatorSim::activeStates() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (auto& l : layers_) {
        auto it = states_.find(l.current);
        out.push_back({l.name, it == states_.end() ? "?" : it->second.name});
    }
    return out;
}
