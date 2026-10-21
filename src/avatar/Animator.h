#pragma once
#include "core/Util.h"
#include "core/Math.h"
#include <unordered_map>

class Avatar;

// A VRChat FX animator controller egyszerűsített szimulációja: állapotgépek, átmenetek,
// blend tree-k, paraméter-meghajtók és a klipek által animált tulajdonságok.
class AnimatorSim {
public:
    void init(const json& fx, const json& clips, Avatar& av);
    void evaluate(float dt, Avatar& av);
    void resetMaterialSlots(Avatar& av);
    bool empty() const { return layers_.empty(); }
    std::vector<std::pair<std::string, std::string>> activeStates() const;  // réteg -> állapot (hibakereséshez)

private:
    enum class TK { Active, Enabled, Blend, Mat, MatRef, Pos, Rot, Scale, ScaleAxis };
    struct Target {
        TK kind;
        int node = -1, mesh = -1, morph = -1, slot = -1, axis = 0;
        std::vector<std::pair<int, uint32_t>> ptrs;  // anyag index + mező eltolás
    };
    struct Curve {
        int target;
        std::vector<float> times;
        std::vector<float> values;  // stride: 1, 3 vagy 4
        int stride = 1;
        std::vector<int> matIdx;    // MatRef: kulcskockánként az AvMaterial index
    };
    struct Clip {
        float length = 0;
        bool loop = false;
        std::vector<Curve> curves;
    };
    struct Cond {
        int mode;  // 1 if, 2 ifnot, 3 greater, 4 less, 6 equals, 7 notequal
        std::string param;
        float threshold;
    };
    struct Transition {
        std::string to, toMachine;
        bool exit = false, hasExitTime = false, canSelf = false;
        float exitTime = 1;
        std::vector<Cond> conds;
    };
    struct Motion {
        int clip = -1;
        int tree = -1;  // blend tree típus, -1 ha klip
        std::string param, paramY;
        struct Child {
            std::shared_ptr<Motion> m;
            float threshold = 0;
            Vec2 pos;
            std::string directParam;
        };
        std::vector<Child> children;
    };
    struct DriverOp {
        int op;  // 0 set, 1 add, 2 random, 3 copy
        std::string name, source;
        float value, min, max, chance;
    };
    struct State {
        std::string name, machine;
        std::shared_ptr<Motion> motion;
        float speed = 1;
        std::string timeParam;
        std::vector<Transition> transitions;
        std::vector<std::vector<DriverOp>> drivers;
    };
    struct Machine {
        std::vector<std::string> states, machines;
        std::string defaultState;
        std::vector<Transition> any, entry;
    };
    struct Layer {
        std::string name, machine;
        float weight = 1;
        std::string current;
        float time = 0;
    };

    std::unordered_map<std::string, State> states_;
    std::unordered_map<std::string, Machine> machines_;
    std::vector<Layer> layers_;
    std::vector<Clip> clips_;
    std::vector<Target> targets_;
    std::unordered_map<std::string, int> targetIndex_;
    std::vector<std::vector<int>> origMats_;
    // kiértékelés
    std::vector<float> acc_;
    std::vector<float> accW_;
    std::vector<int> touched_;
    std::vector<char> isTouched_;

    int targetFor(const std::string& key, TK kind, Avatar& av, const json& cv);
    std::shared_ptr<Motion> parseMotion(const json& m, const std::unordered_map<std::string, int>& clipIds);
    bool condsOk(const std::vector<Cond>& c, Avatar& av) const;
    std::string enterMachine(const std::string& mid, Avatar& av, int depth = 0);
    void enterState(Layer& l, const std::string& sid, Avatar& av);
    void sampleMotion(const Motion& m, float time, float weight, const State& st, Avatar& av, std::vector<std::pair<int, float>>& clipWeights);
    void sampleClip(int clip, float t, float w);
    float clipLength(const Motion& m) const;
};
