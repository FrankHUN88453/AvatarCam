#pragma once
// Kis matematikai könyvtár. Jobbkezes koordináta-rendszer (Y fel, a kamera -Z felé néz),
// oszlopvektoros konvenció (v' = M * v), a mátrixok oszlopfolytonosan tárolva (HLSL column_major).
#include <cmath>
#include <algorithm>
#include <cstring>

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;
constexpr float kRad2Deg = 180.0f / kPi;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float saturate(float v) { return clampf(v, 0.0f, 1.0f); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstepf(float a, float b, float x) {
    float t = saturate((x - a) / (b - a));
    return t * t * (3 - 2 * t);
}
// Képkocka-független exponenciális simítás (k: 1/s)
inline float damp(float current, float target, float k, float dt) { return lerpf(current, target, 1.0f - std::exp(-k * dt)); }

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
};
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) {
    float l = length(v);
    return l > 1e-12f ? v / l : Vec3(0, 0, 0);
}
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline Vec3 vmin(const Vec3& a, const Vec3& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 vmax(const Vec3& a, const Vec3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    Vec4() = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    Vec4(const Vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    Vec3 xyz() const { return {x, y, z}; }
    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
};

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    Quat() = default;
    constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    static Quat axisAngle(const Vec3& axis, float angle) {
        Vec3 a = normalize(axis);
        float s = std::sin(angle * 0.5f);
        return {a.x * s, a.y * s, a.z * s, std::cos(angle * 0.5f)};
    }
    // Euler szögek (radián), alkalmazási sorrend: előbb Z (roll), majd X (pitch), végül Y (yaw)
    static Quat eulerYXZ(float pitch, float yaw, float roll) {
        return axisAngle({0, 1, 0}, yaw) * axisAngle({1, 0, 0}, pitch) * axisAngle({0, 0, 1}, roll);
    }
    Quat operator*(const Quat& b) const {
        return {w * b.x + x * b.w + y * b.z - z * b.y, w * b.y - x * b.z + y * b.w + z * b.x, w * b.z + x * b.y - y * b.x + z * b.w,
                w * b.w - x * b.x - y * b.y - z * b.z};
    }
    Quat conj() const { return {-x, -y, -z, w}; }
    Vec3 rotate(const Vec3& v) const {
        Vec3 u(x, y, z);
        Vec3 t = cross(u, v) * 2.0f;
        return v + t * w + cross(u, t);
    }
    float len() const { return std::sqrt(x * x + y * y + z * z + w * w); }
    Quat normalized() const {
        float l = len();
        return l > 1e-12f ? Quat(x / l, y / l, z / l, w / l) : Quat();
    }
};
inline float dot(const Quat& a, const Quat& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline Quat nlerp(const Quat& a, Quat b, float t) {
    if (dot(a, b) < 0) b = Quat(-b.x, -b.y, -b.z, -b.w);
    return Quat(lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t), lerpf(a.w, b.w, t)).normalized();
}
inline Quat slerp(const Quat& a, Quat b, float t) {
    float c = dot(a, b);
    if (c < 0) {
        b = Quat(-b.x, -b.y, -b.z, -b.w);
        c = -c;
    }
    if (c > 0.9995f) return nlerp(a, b, t);
    float th = std::acos(c);
    float s = std::sin(th);
    float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    return Quat(a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb);
}
// Forgatás, ami az a irányt b irányba viszi
inline Quat fromTo(const Vec3& a0, const Vec3& b0) {
    Vec3 a = normalize(a0), b = normalize(b0);
    float c = dot(a, b);
    if (c < -0.999999f) {
        Vec3 ax = cross({1, 0, 0}, a);
        if (length(ax) < 1e-6f) ax = cross({0, 1, 0}, a);
        return Quat::axisAngle(ax, kPi);
    }
    Vec3 v = cross(a, b);
    return Quat(v.x, v.y, v.z, 1 + c).normalized();
}
// Kis forgatások skálázása (szögsebesség-szerű súlyozás)
inline Quat scaleRotation(const Quat& q0, float s) {
    Quat q = q0.w < 0 ? Quat(-q0.x, -q0.y, -q0.z, -q0.w) : q0;
    float sinHalf = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (sinHalf < 1e-7f) return Quat();
    float angle = 2.0f * std::atan2(sinHalf, q.w);
    return Quat::axisAngle({q.x / sinHalf, q.y / sinHalf, q.z / sinHalf}, angle * s);
}

struct Mat4 {
    float m[16];  // oszlopfolytonos: m[col*4 + row]
    Mat4() { setIdentity(); }
    void setIdentity() {
        std::memset(m, 0, sizeof(m));
        m[0] = m[5] = m[10] = m[15] = 1;
    }
    static Mat4 identity() { return Mat4(); }
    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }
    static Mat4 translation(const Vec3& t) {
        Mat4 r;
        r.m[12] = t.x;
        r.m[13] = t.y;
        r.m[14] = t.z;
        return r;
    }
    static Mat4 scaling(const Vec3& s) {
        Mat4 r;
        r.m[0] = s.x;
        r.m[5] = s.y;
        r.m[10] = s.z;
        return r;
    }
    static Mat4 rotation(const Quat& q) {
        Mat4 r;
        float x = q.x, y = q.y, z = q.z, w = q.w;
        float x2 = x + x, y2 = y + y, z2 = z + z;
        float xx = x * x2, xy = x * y2, xz = x * z2, yy = y * y2, yz = y * z2, zz = z * z2, wx = w * x2, wy = w * y2, wz = w * z2;
        r.m[0] = 1 - (yy + zz);
        r.m[1] = xy + wz;
        r.m[2] = xz - wy;
        r.m[4] = xy - wz;
        r.m[5] = 1 - (xx + zz);
        r.m[6] = yz + wx;
        r.m[8] = xz + wy;
        r.m[9] = yz - wx;
        r.m[10] = 1 - (xx + yy);
        return r;
    }
    static Mat4 trs(const Vec3& t, const Quat& q, const Vec3& s) {
        Mat4 r = rotation(q);
        r.m[0] *= s.x; r.m[1] *= s.x; r.m[2] *= s.x;
        r.m[4] *= s.y; r.m[5] *= s.y; r.m[6] *= s.y;
        r.m[8] *= s.z; r.m[9] *= s.z; r.m[10] *= s.z;
        r.m[12] = t.x;
        r.m[13] = t.y;
        r.m[14] = t.z;
        return r;
    }
    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int c = 0; c < 4; c++)
            for (int rr = 0; rr < 4; rr++) {
                float s = 0;
                for (int k = 0; k < 4; k++) s += at(rr, k) * b.at(k, c);
                r.at(rr, c) = s;
            }
        return r;
    }
    Vec3 transformPoint(const Vec3& p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13], m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
    }
    Vec3 transformDir(const Vec3& d) const {
        return {m[0] * d.x + m[4] * d.y + m[8] * d.z, m[1] * d.x + m[5] * d.y + m[9] * d.z, m[2] * d.x + m[6] * d.y + m[10] * d.z};
    }
    Vec4 operator*(const Vec4& v) const {
        return {m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w, m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
                m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w, m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w};
    }
    Vec3 getTranslation() const { return {m[12], m[13], m[14]}; }
    Vec3 getScale() const {
        return {length({m[0], m[1], m[2]}), length({m[4], m[5], m[6]}), length({m[8], m[9], m[10]})};
    }
    Quat getRotation() const {
        Vec3 s = getScale();
        float r00 = m[0] / s.x, r10 = m[1] / s.x, r20 = m[2] / s.x;
        float r01 = m[4] / s.y, r11 = m[5] / s.y, r21 = m[6] / s.y;
        float r02 = m[8] / s.z, r12 = m[9] / s.z, r22 = m[10] / s.z;
        float tr = r00 + r11 + r22;
        Quat q;
        if (tr > 0) {
            float S = std::sqrt(tr + 1.0f) * 2;
            q.w = 0.25f * S;
            q.x = (r21 - r12) / S;
            q.y = (r02 - r20) / S;
            q.z = (r10 - r01) / S;
        } else if (r00 > r11 && r00 > r22) {
            float S = std::sqrt(1.0f + r00 - r11 - r22) * 2;
            q.w = (r21 - r12) / S;
            q.x = 0.25f * S;
            q.y = (r01 + r10) / S;
            q.z = (r02 + r20) / S;
        } else if (r11 > r22) {
            float S = std::sqrt(1.0f + r11 - r00 - r22) * 2;
            q.w = (r02 - r20) / S;
            q.x = (r01 + r10) / S;
            q.y = 0.25f * S;
            q.z = (r12 + r21) / S;
        } else {
            float S = std::sqrt(1.0f + r22 - r00 - r11) * 2;
            q.w = (r10 - r01) / S;
            q.x = (r02 + r20) / S;
            q.y = (r12 + r21) / S;
            q.z = 0.25f * S;
        }
        return q.normalized();
    }
    Mat4 inverse() const {
        const float* a = m;
        float inv[16];
        inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
        inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
        inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
        inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
        inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
        inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
        inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
        inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
        inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
        inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
        inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
        inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
        inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
        inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
        inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
        inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
        float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
        Mat4 r;
        if (std::fabs(det) < 1e-30f) return r;
        det = 1.0f / det;
        for (int i = 0; i < 16; i++) r.m[i] = inv[i] * det;
        return r;
    }
    Mat4 transposed() const {
        Mat4 r;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) r.at(i, j) = at(j, i);
        return r;
    }
    // Jobbkezes perspektíva, D3D mélység [0,1]
    static Mat4 perspective(float fovY, float aspect, float zn, float zf) {
        Mat4 r;
        std::memset(r.m, 0, sizeof(r.m));
        float f = 1.0f / std::tan(fovY * 0.5f);
        r.at(0, 0) = f / aspect;
        r.at(1, 1) = f;
        r.at(2, 2) = zf / (zn - zf);
        r.at(2, 3) = zn * zf / (zn - zf);
        r.at(3, 2) = -1;
        return r;
    }
    static Mat4 ortho(float l, float rr, float b, float t, float zn, float zf) {
        Mat4 r;
        r.at(0, 0) = 2 / (rr - l);
        r.at(1, 1) = 2 / (t - b);
        r.at(2, 2) = 1 / (zn - zf);
        r.at(0, 3) = (l + rr) / (l - rr);
        r.at(1, 3) = (t + b) / (b - t);
        r.at(2, 3) = zn / (zn - zf);
        return r;
    }
    static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 f = normalize(target - eye);
        Vec3 s = normalize(cross(f, up));
        if (length(s) < 1e-6f) s = normalize(cross(f, Vec3(0, 0, 1)));
        Vec3 u = cross(s, f);
        Mat4 r;
        r.at(0, 0) = s.x; r.at(0, 1) = s.y; r.at(0, 2) = s.z;
        r.at(1, 0) = u.x; r.at(1, 1) = u.y; r.at(1, 2) = u.z;
        r.at(2, 0) = -f.x; r.at(2, 1) = -f.y; r.at(2, 2) = -f.z;
        r.at(0, 3) = -dot(s, eye);
        r.at(1, 3) = -dot(u, eye);
        r.at(2, 3) = dot(f, eye);
        return r;
    }
};

struct Transform {
    Vec3 t{0, 0, 0};
    Quat r{};
    Vec3 s{1, 1, 1};
    Mat4 matrix() const { return Mat4::trs(t, r, s); }
};

// sRGB <-> lineáris
inline float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
inline float linearToSrgb(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f; }
inline Vec3 srgbToLinear(const Vec3& c) { return {srgbToLinear(c.x), srgbToLinear(c.y), srgbToLinear(c.z)}; }
