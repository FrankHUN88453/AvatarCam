#pragma once
#include "core/Math.h"
#include "core/Util.h"

class Avatar;

// VRChat PhysBone / DynamicBone közelítő szimulációja (verlet, rögzített lépésköz)
class PhysicsSim {
public:
    void init(Avatar& av);
    void reset(Avatar& av);
    void step(float dt, Avatar& av, float strength);
    size_t chainCount() const { return chains_.size(); }

private:
    struct Bone {
        int node = -1;
        int parentBone = -1;  // a láncon belüli szülő (vagy -1)
        Vec3 tipLocal;        // a csúcs (gyermek) helye a csont lokális terében
        float length = 0;
        bool simulate = true;
        Vec3 cur, prev;       // csúcs világ pozíció
        Quat baseLocal;       // az animált (szimuláció előtti) lokális forgatás
        float t = 0;          // 0..1 pozíció a láncban (görbékhez)
    };
    struct Collider {
        int node = -1;
        int shape = 0;  // 0 gömb, 1 kapszula, 2 sík
        float radius = 0.05f, height = 0;
        Vec3 pos;
        Quat rot;
        bool inside = false;
    };
    struct Chain {
        std::vector<Bone> bones;  // hierarchia sorrendben
        float pull = 0.2f, spring = 0.2f, stiffness = 0.2f, gravity = 0, gravityFalloff = 0, radius = 0;
        float maxAngle = kPi;
        bool limit = false;
        std::vector<int> colliders;
        std::vector<std::pair<float, float>> pullCurve, springCurve, stiffCurve, gravCurve, radiusCurve;
        bool enabled = true;
    };
    std::vector<Chain> chains_;
    std::vector<Collider> colliders_;
    float accum_ = 0;
    bool initialized_ = false;
    static float evalCurve(const std::vector<std::pair<float, float>>& c, float t, float def);
    void simulate(Chain& c, Avatar& av, float h, float strength);
};
