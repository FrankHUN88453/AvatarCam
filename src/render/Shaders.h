#pragma once
// HLSL shaderek (futásidőben fordítva, D3DCompile)

static const char* kMeshShader = R"HLSL(
#pragma pack_matrix(column_major)
struct Light { float4 posType; float4 dirRange; float4 color; float4 spot; };
cbuffer FrameCB : register(b0) {
    float4x4 viewProj;
    float4x4 view;
    float4x4 shadowVP;
    float4 cameraPos;      // w: idő
    float4 ambientSky;     // w: toon küszöb
    float4 ambientGround;  // w: toon lágyság
    float4 shadeTint;      // w: árnyalási mód (0 toon, 1 pbr, 2 unlit)
    float4 rimParams;      // rgb*intenzitás, w: kitevő
    float4 shadowParams;   // x: be, y: erősség, z: texel, w: bias
    float4 outlineParams;  // rgb, w: vastagság (px)
    float4 viewport;       // w, h, 1/w, 1/h
    float4 misc;           // x: fények száma, y: emisszió szorzó, z: fő fény index, w: matcap pótlás módja
    Light lights[8];
};
cbuffer ObjectCB : register(b1) { float4 objParams; };
cbuffer MaterialCB : register(b2) {
    float4 baseColor;
    float4 emissive;
    float4 hsv;        // x hue, y telítettség, z fényerő, w hue be
    float4 pbr;        // x fémesség, y érdesség, z normál erősség, w alfa küszöb
    float4 uvST;
    float4 alphaST;
    float4 shade;      // rgb, w: van saját árnyékszín
    float4 rim;        // anyag kontúrfény
    float4 flags;      // x alfa mód, y van textúra, z van normal, w van emissziós textúra
    float4 flags2;     // x van alfa maszk, y unlit, z körvonal szorzó, w kétoldalas
    float4 mcColor[4];
    float4 mcParams[4];   // x replace, y multiply, z add, w emisszió
    float4 mcParams2[4];  // x hue, y hue be, z van textúra, w van maszk
    float4 flags3;        // x emisszió * alapszín, y van emissziós maszk
};
StructuredBuffer<float4x4> bones : register(t0);
Texture2D tAlbedo : register(t0);
Texture2D tNormal : register(t1);
Texture2D tEmissive : register(t2);
Texture2D tAlpha : register(t3);
Texture2D tMatcap0 : register(t4);
Texture2D tMatcap1 : register(t5);
Texture2D tMatcap2 : register(t6);
Texture2D tMatcap3 : register(t7);
Texture2D tMask0 : register(t8);
Texture2D tMask1 : register(t9);
Texture2D tMask2 : register(t10);
Texture2D tMask3 : register(t11);
Texture2D<float> tShadow : register(t12);
Texture2D tEmMask : register(t13);
SamplerState sLinear : register(s0);
SamplerState sClamp : register(s1);
SamplerComparisonState sShadow : register(s2);

struct VSIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tan : TANGENT;
    uint4 j : BLENDINDICES;
    float4 w : BLENDWEIGHT;
};
struct VSOut {
    float4 pos : SV_Position;
    float3 wpos : TEXCOORD0;
    float3 nrm : TEXCOORD1;
    float4 tan : TEXCOORD2;
    float2 uv : TEXCOORD3;
    float4 spos : TEXCOORD4;
};

float4x4 skinMatrix(VSIn i) {
    return bones[i.j.x] * i.w.x + bones[i.j.y] * i.w.y + bones[i.j.z] * i.w.z + bones[i.j.w] * i.w.w;
}

VSOut VSMain(VSIn i) {
    VSOut o;
    float4x4 m = skinMatrix(i);
    float4 wp = mul(m, float4(i.pos, 1));
    o.wpos = wp.xyz;
    o.nrm = mul((float3x3)m, i.nrm);
    o.tan = float4(mul((float3x3)m, i.tan.xyz), i.tan.w);
    o.uv = i.uv;
    o.pos = mul(viewProj, wp);
    o.spos = mul(shadowVP, wp);
    return o;
}

VSOut VSOutline(VSIn i) {
    VSOut o = VSMain(i);
    float3 n = normalize(o.nrm);
    float4 cn = mul(viewProj, float4(n, 0));
    float2 dir = cn.xy;
    float l = length(dir);
    if (l > 1e-5) dir /= l;
    float width = outlineParams.w * flags2.z;
    o.pos.xy += dir * width * 2.0 * viewport.zw * o.pos.w;
    return o;
}

float4 VSShadow(VSIn i) : SV_Position {
    float4x4 m = skinMatrix(i);
    return mul(shadowVP, mul(m, float4(i.pos, 1)));
}
struct VSOutS { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOutS VSShadowMask(VSIn i) {
    VSOutS o;
    float4x4 m = skinMatrix(i);
    o.pos = mul(shadowVP, mul(m, float4(i.pos, 1)));
    o.uv = i.uv;
    return o;
}
void PSShadowMask(VSOutS i) {
    float a = baseColor.a;
    if (flags.y > 0.5) a *= tAlbedo.Sample(sLinear, i.uv * uvST.xy + uvST.zw).a;
    if (flags2.x > 0.5) a *= tAlpha.Sample(sLinear, i.uv * alphaST.xy + alphaST.zw).r;
    clip(a - max(pbr.w, 0.35));
}

float3 rgb2hsv(float3 c) {
    float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    float4 p = lerp(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
    float4 q = lerp(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}
float3 hsv2rgb(float3 c) {
    float3 p = abs(frac(c.xxx + float3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * lerp(1.0.xxx, saturate(p - 1.0), c.y);
}
float3 hueShift(float3 c, float h) {
    if (abs(frac(h)) < 1e-4) return c;
    float3 v = rgb2hsv(max(c, 0));
    v.x = frac(v.x + h);
    return hsv2rgb(v);
}
float3 adjustColor(float3 c) {
    // a telítettség növelése túllőhet (negatív / 1 feletti csatornák) – a Poiyomi is levágja,
    // különben a tónusleképezés a negatív értékekből fehér/rózsaszín foltokat és zöld szegélyeket csinál
    float hi = max(1, max(c.r, max(c.g, c.b)));
    if (hsv.w > 0.5) c = hueShift(c, hsv.x);
    if (abs(hsv.y) > 1e-4) {
        float l = dot(c, float3(0.2126, 0.7152, 0.0722));
        c = clamp(lerp(l.xxx, c, max(0, 1 + hsv.y)), 0, hi);
    }
    return c * max(0, 1 + hsv.z);
}

// Általános króm/ezüst matcap (nézőtérbeli normál xy): világos ég, sötét horizont sáv, közepes talaj, csúcsfény
float3 chromeMatcap(float2 n) {
    float y = n.y;
    float c = y > 0.04 ? lerp(0.34, 0.72, saturate(y)) : lerp(0.08, 0.24, saturate(-y));
    c *= 1 - 0.6 * exp(-abs(y - 0.04) * 16);
    float l = length(n);
    float h = l > 1e-4 ? pow(saturate(dot(n / l, normalize(float2(-0.45, 0.6))) * l), 12) : 0;
    return (c + h * 0.85).xxx;
}

float shadowFactor(float4 spos) {
    if (shadowParams.x < 0.5) return 1;
    float3 p = spos.xyz / spos.w;
    float2 uv = p.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv < 0) || any(uv > 1) || p.z > 1) return 1;
    float d = p.z - shadowParams.w;
    float s = 0;
    [unroll] for (int y = -1; y <= 1; y++)
        [unroll] for (int x = -1; x <= 1; x++)
            s += tShadow.SampleCmpLevelZero(sShadow, uv + float2(x, y) * shadowParams.z, d);
    s /= 9;
    return lerp(1, s, shadowParams.y);
}

float D_GGX(float NoH, float a) {
    float a2 = a * a;
    float f = (NoH * a2 - NoH) * NoH + 1;
    return a2 / (3.14159 * f * f + 1e-7);
}

void lightVector(int li, float3 wpos, out float3 L, out float atten) {
    Light lt = lights[li];
    int type = (int)lt.posType.w;
    if (type == 0) {
        L = normalize(lt.dirRange.xyz);
        atten = 1;
        return;
    }
    float3 d = lt.posType.xyz - wpos;
    float dist = length(d);
    L = d / max(dist, 1e-4);
    float r = saturate(1 - dist / max(lt.dirRange.w, 0.01));
    atten = r * r;
    if (type == 2) {
        float c = dot(-L, normalize(lt.dirRange.xyz));
        atten *= smoothstep(lt.spot.x, lt.spot.y, c);
    }
}

struct PSOut { float4 color : SV_Target0; float4 glow : SV_Target1; };
PSOut makeOut(float3 c, float a, float3 g) {
    PSOut o;
    o.color = float4(max(c, 0), a);
    o.glow = float4(max(g, 0), a);
    return o;
}
PSOut PSMain(VSOut i, bool front : SV_IsFrontFace) {
    float2 uv = i.uv * uvST.xy + uvST.zw;
    float4 albedo = baseColor;
    if (flags.y > 0.5) albedo *= tAlbedo.Sample(sLinear, uv);
    float alpha = albedo.a;
    if (flags2.x > 0.5) alpha *= tAlpha.Sample(sLinear, i.uv * alphaST.xy + alphaST.zw).r;
    int alphaMode = (int)flags.x;
    if (alphaMode == 1) {
        // alpha-to-coverage élsimítással
        alpha = (alpha - pbr.w) / max(fwidth(alpha), 1e-4) + 0.5;
    }
    if (alphaMode >= 2 && alpha < 0.002) discard;
    float3 col = adjustColor(albedo.rgb);

    float3 N = normalize(i.nrm);
    if (!front) N = -N;
    if (flags.z > 0.5) {
        float3 T = normalize(i.tan.xyz - N * dot(N, i.tan.xyz));
        float3 B = cross(N, T) * (i.tan.w < 0 ? -1 : 1);
        if (!front) B = -B;
        float3 tn = tNormal.Sample(sLinear, uv).xyz * 2 - 1;
        tn.xy *= pbr.z;
        N = normalize(T * tn.x + B * tn.y + N * max(tn.z, 0.05));
    }
    float3 V = normalize(cameraPos.xyz - i.wpos);
    float NoV = saturate(dot(N, V));

    // matcap rétegek
    float3 vn = normalize(mul((float3x3)view, N));
    float2 mcuv = vn.xy * float2(0.5, -0.5) + 0.5;
    float3 matEm = 0;
    [unroll] for (int k = 0; k < 4; k++) {
        if (mcColor[k].w < 0.5) continue;
        float3 mc = mcColor[k].rgb;
        float3 mcE = mc;
        if (mcParams2[k].z > 0.5) {
            float4 mct;
            if (k == 0) mct = tMatcap0.Sample(sClamp, mcuv);
            else if (k == 1) mct = tMatcap1.Sample(sClamp, mcuv);
            else if (k == 2) mct = tMatcap2.Sample(sClamp, mcuv);
            else mct = tMatcap3.Sample(sClamp, mcuv);
            mc *= mct.rgb;
            mcE = mc;
        } else {
            // hiányzó (Poiyomi beépített) matcap textúra: procedurális króm a színhez, lágy fény az emisszióhoz
            int fb = (int)misc.w;
            mc *= chromeMatcap(vn.xy) * (fb == 1 ? 0.45 : 1.0);
            float2 dd = mcuv - float2(0.45, 0.38);
            // ha a réteg színe semleges, de színeltolás van rajta, színes alapból indulunk,
            // különben a hue csúszkának nem lenne látható hatása
            float mx = max(mcE.r, max(mcE.g, mcE.b)), mn = min(mcE.r, min(mcE.g, mcE.b));
            if (mcParams2[k].y > 0.5 && mx - mn < 0.05 * mx) mcE = mx * float3(0.15, 0.7, 1.0) * 1.2;
            mcE *= 0.15 + 0.85 * pow(saturate(1 - length(dd) * 1.7), 1.5);
        }
        if (mcParams2[k].y > 0.5) {
            mc = hueShift(mc, mcParams2[k].x);
            mcE = hueShift(mcE, mcParams2[k].x);
        }
        float mask = 1;
        if (mcParams2[k].w > 0.5) {
            if (k == 0) mask = tMask0.Sample(sLinear, uv).r;
            else if (k == 1) mask = tMask1.Sample(sLinear, uv).r;
            else if (k == 2) mask = tMask2.Sample(sLinear, uv).r;
            else mask = tMask3.Sample(sLinear, uv).r;
        }
        if (mcParams2[k].z > 0.5 || (int)misc.w != 2) {
            col = lerp(col, mc, saturate(mcParams[k].x) * mask);
            col *= lerp(1.0.xxx, mc, saturate(mcParams[k].y) * mask);
            col += mc * mcParams[k].z * mask;
        }
        matEm += mcE * mcParams[k].w * mask;
    }

    int mode = (int)shadeTint.w;
    if (flags2.y > 0.5) mode = 2;
    float3 color;
    float3 hemi = lerp(ambientGround.rgb, ambientSky.rgb, N.y * 0.5 + 0.5);
    float3 sp = i.spos.xyz;
    float sf = shadowFactor(i.spos);
    if (mode == 2) {
        color = col;
    } else if (mode == 0) {
        float3 shadeC = shade.w > 0.5 ? shade.rgb : shadeTint.rgb;
        float3 lit = 0;
        int mainLight = (int)misc.z;
        for (int li = 0; li < (int)misc.x; li++) {
            float3 L;
            float att;
            lightVector(li, i.wpos, L, att);
            float ndl = dot(N, L);
            float r = smoothstep(ambientSky.w - ambientGround.w, ambientSky.w + ambientGround.w, ndl);
            if (lights[li].spot.z > 0.5) r *= sf;
            float3 lc = lights[li].color.rgb * att;
            if (li == mainLight) lit += lc * lerp(shadeC, 1.0.xxx, r);
            else lit += lc * r;
        }
        color = col * (hemi + lit);
    } else {
        float metal = pbr.x, rough = max(pbr.y, 0.04);
        float3 diff = col * (1 - metal);
        float3 F0 = lerp(0.04.xxx, col, metal);
        float3 lit = 0;
        for (int li = 0; li < (int)misc.x; li++) {
            float3 L;
            float att;
            lightVector(li, i.wpos, L, att);
            float ndl = saturate(dot(N, L));
            if (lights[li].spot.z > 0.5) ndl *= sf;
            float3 H = normalize(L + V);
            float NoH = saturate(dot(N, H));
            float3 F = F0 + (1 - F0) * pow(1 - saturate(dot(V, H)), 5);
            float a = rough * rough;
            float k = a * 0.5;
            float G = (NoV / (NoV * (1 - k) + k)) * (ndl / (ndl * (1 - k) + k));
            float3 spec = D_GGX(NoH, a) * G * F / max(4 * NoV * ndl, 1e-4);
            lit += lights[li].color.rgb * att * (diff * ndl + spec * ndl);
        }
        float3 R = reflect(-V, N);
        float3 env = lerp(ambientGround.rgb, ambientSky.rgb, R.y * 0.5 + 0.5);
        float3 Fe = F0 + (max((1 - rough).xxx, F0) - F0) * pow(1 - NoV, 5);
        color = diff * hemi + lit + env * Fe * (1 - rough * 0.7);
    }
    if (mode != 2) {
        float rimT = pow(1 - NoV, rimParams.w);
        color += rimParams.rgb * rimT * (0.6 + 0.4 * sf);
        color += rim.rgb * rimT;
    }
    float3 em = emissive.rgb;
    if (flags.w > 0.5) em *= tEmissive.Sample(sLinear, uv).rgb;
    if (flags3.x > 0.5) em *= col;
    if (flags3.y > 0.5) em *= tEmMask.Sample(sLinear, uv).r;
    float3 glow = (em + matEm) * misc.y;
    color += glow;

    if (alphaMode == 0) return makeOut(color, 1, glow);
    if (alphaMode == 1) return makeOut(color, saturate(alpha), glow);
    if (alphaMode == 3) return makeOut(color * alpha, 0, glow * alpha);
    return makeOut(color, saturate(alpha), glow);
}

PSOut PSOutline(VSOut i) {
    if ((int)flags.x == 1) {
        float a = baseColor.a;
        if (flags.y > 0.5) a *= tAlbedo.Sample(sLinear, i.uv * uvST.xy + uvST.zw).a;
        clip(a - pbr.w);
    }
    return makeOut(outlineParams.rgb, 1, 0);
}
)HLSL";

// Talaj (árnyékfogó) – egy négyzet az avatár talpa alatt
static const char* kGroundShader = R"HLSL(
#pragma pack_matrix(column_major)
cbuffer GroundCB : register(b0) {
    float4x4 viewProj;
    float4x4 shadowVP;
    float4 center;   // xyz középpont, w méret
    float4 params;   // x árnyék átlátszóság, y van árnyéktérkép, z folt erősség, w texel
};
Texture2D<float> tShadow : register(t0);
SamplerComparisonState sShadow : register(s0);
struct VSOut { float4 pos : SV_Position; float3 wpos : TEXCOORD0; float2 local : TEXCOORD1; };
VSOut VSGround(uint id : SV_VertexID) {
    float2 c = float2((id == 1 || id == 4 || id == 5) ? 1 : -1, (id == 2 || id == 3 || id == 5) ? 1 : -1);
    VSOut o;
    float3 p = float3(center.x + c.x * center.w, center.y, center.z + c.y * center.w);
    o.wpos = p;
    o.local = c;
    o.pos = mul(viewProj, float4(p, 1));
    return o;
}
struct PSOutG { float4 color : SV_Target0; float4 glow : SV_Target1; };
PSOutG PSGround(VSOut i) {
    float a = 0;
    if (params.y > 0.5) {
        float4 sp = mul(shadowVP, float4(i.wpos, 1));
        float3 p = sp.xyz / sp.w;
        float2 uv = p.xy * float2(0.5, -0.5) + 0.5;
        if (all(uv > 0) && all(uv < 1)) {
            float s = 0;
            [unroll] for (int y = -2; y <= 2; y++)
                [unroll] for (int x = -2; x <= 2; x++)
                    s += tShadow.SampleCmpLevelZero(sShadow, uv + float2(x, y) * params.w * 1.5, p.z - 0.002);
            a = 1 - s / 25;
        }
    }
    float r = length(i.local * float2(1, 1.6));
    float blob = saturate(1 - r * 3.2);
    blob = blob * blob * params.z;
    float edge = saturate(1 - length(i.local)) ;
    a = max(a, blob) * params.x * edge;
    PSOutG o;
    o.color = float4(0, 0, 0, a);
    o.glow = float4(0, 0, 0, a);
    return o;
}
)HLSL";

// Teljes képernyős utófeldolgozás: bloom, kompozitálás, másolás
static const char* kPostShader = R"HLSL(
cbuffer PostCB : register(b0) {
    float4 texel;      // forrás 1/w, 1/h, w: radius
    float4 bloomP;     // x küszöb, y lágy térd, z erősség, w be
    float4 bg1;        // rgb, w mód
    float4 bg2;        // rgb, w szög (rad)
    float4 bgP;        // x illesztés, y sötétítés, z kép képarány, w kimenet képarány
    float4 tone;       // x mód, y expozíció, z kontraszt, w telítettség
    float4 outP;       // x egyenes alfa, y tükrözés, z van háttérkép
};
Texture2D tSrc : register(t0);
Texture2D tBloom : register(t1);
Texture2D tBg : register(t2);
SamplerState sLinear : register(s0);
SamplerState sPoint : register(s1);
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VSFull(uint id : SV_VertexID) {
    VSOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.uv = p;
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
float4 PSBright(VSOut i) : SV_Target {
    float3 c = 0;
    // 4 mintás előszűrés
    [unroll] for (int k = 0; k < 4; k++) {
        float2 o = float2((k & 1) ? 0.5 : -0.5, (k & 2) ? 0.5 : -0.5) * texel.xy;
        c += tSrc.SampleLevel(sLinear, i.uv + o, 0).rgb;
    }
    c *= 0.25;
    float br = max(c.r, max(c.g, c.b));
    float knee = bloomP.x * bloomP.y + 1e-4;
    float soft = clamp(br - bloomP.x + knee, 0, 2 * knee);
    soft = soft * soft / (4 * knee);
    float contrib = max(soft, br - bloomP.x) / max(br, 1e-4);
    return float4(min(c * contrib, 30.0), 1);
}
float4 PSDown(VSOut i) : SV_Target {
    float2 t = texel.xy;
    float3 c = tSrc.SampleLevel(sLinear, i.uv, 0).rgb * 4;
    c += tSrc.SampleLevel(sLinear, i.uv + float2(-t.x, -t.y), 0).rgb;
    c += tSrc.SampleLevel(sLinear, i.uv + float2(t.x, -t.y), 0).rgb;
    c += tSrc.SampleLevel(sLinear, i.uv + float2(-t.x, t.y), 0).rgb;
    c += tSrc.SampleLevel(sLinear, i.uv + float2(t.x, t.y), 0).rgb;
    return float4(c / 8, 1);
}
float4 PSUp(VSOut i) : SV_Target {
    float2 t = texel.xy * texel.w;
    float3 c = 0;
    c += tSrc.SampleLevel(sLinear, i.uv + float2(-t.x * 2, 0), 0).rgb;
    c += tSrc.SampleLevel(sLinear, i.uv + float2(-t.x, t.y), 0).rgb * 2;
    c += tSrc.SampleLevel(sLinear, i.uv + float2(0, t.y * 2), 0).rgb;
    c += tSrc.SampleLevel(sLinear, i.uv + float2(t.x, t.y), 0).rgb * 2;
    c += tSrc.SampleLevel(sLinear, i.uv + float2(t.x * 2, 0), 0).rgb;
    c += tSrc.SampleLevel(sLinear, i.uv + float2(t.x, -t.y), 0).rgb * 2;
    c += tSrc.SampleLevel(sLinear, i.uv + float2(0, -t.y * 2), 0).rgb;
    c += tSrc.SampleLevel(sLinear, i.uv + float2(-t.x, -t.y), 0).rgb * 2;
    return float4(c / 12, 1);
}

float3 acesFilm(float3 x) { return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)); }
float3 agx(float3 c) {
    const float3x3 inM = float3x3(0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0784335999999992, 0.878468636469772, 0.0784336,
                                  0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const float3x3 outM = float3x3(1.19687900512017, -0.0528968517574562, -0.0529716355144438, -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                                   -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    c = mul(c, inM);
    c = clamp(log2(max(c, 1e-10)), -12.47393, 4.026069);
    c = (c + 12.47393) / 16.49999;
    float3 x2 = c * c, x4 = x2 * x2;
    c = 15.5 * x4 * x2 - 40.14 * x4 * c + 31.96 * x4 - 6.868 * x2 * c + 0.4298 * x2 + 0.1191 * c - 0.00232;
    c = mul(c, outM);
    return pow(max(c, 0), 2.2);
}
float3 neutral(float3 color) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;
    const float d = 1 - startCompression;
    float newPeak = 1 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    float g = 1 - 1 / (desaturation * (peak - newPeak) + 1);
    return lerp(color, newPeak.xxx, g);
}
float3 toSrgb(float3 c) {
    c = max(c, 0);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1 / 2.4) - 0.055;
}
float3 toLinear(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }

float4 PSComposite(VSOut i) : SV_Target {
    float2 uv = i.uv;
    if (outP.y > 0.5) uv.x = 1 - uv.x;
    float4 s = tSrc.SampleLevel(sPoint, uv, 0);
    float3 b = bloomP.w > 0.5 ? tBloom.SampleLevel(sLinear, uv, 0).rgb * bloomP.z : 0;
    float3 c = (s.rgb + b) * tone.y;
    int tm = (int)tone.x;
    if (tm == 1) c = acesFilm(c * 0.8);
    else if (tm == 2) c = agx(c);
    else if (tm == 3) c = neutral(c);
    else c = saturate(c);
    // kontraszt és telítettség (a tónusleképezés után)
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = lerp(l.xxx, c, tone.w);
    c = saturate((c - 0.18) * tone.z + 0.18);
    float alpha = saturate(s.a + max(b.r, max(b.g, b.b)) * 0.6);
    // háttér
    int mode = (int)bg1.w;
    float3 bg = 0;
    float bgA = mode == 0 ? 0 : 1;
    if (mode == 1) bg = bg1.rgb;
    else if (mode == 2) {
        float2 d = float2(cos(bg2.w), sin(bg2.w));
        float t = saturate(dot(i.uv - 0.5, d) + 0.5);
        bg = lerp(bg1.rgb, bg2.rgb, t);
    } else if (mode == 3) {
        if (outP.z > 0.5) {
            float2 buv = i.uv;
            float ia = bgP.z, oa = bgP.w;
            int fit = (int)bgP.x;
            if (fit == 0) {  // kitöltés
                if (oa > ia) buv.y = (buv.y - 0.5) * ia / oa + 0.5;
                else buv.x = (buv.x - 0.5) * oa / ia + 0.5;
            } else if (fit == 1) {  // teljes kép
                if (oa > ia) buv.x = (buv.x - 0.5) * oa / ia + 0.5;
                else buv.y = (buv.y - 0.5) * ia / oa + 0.5;
            }
            if (all(buv >= 0) && all(buv <= 1)) bg = toLinear(tBg.SampleLevel(sLinear, buv, 0).rgb) * (1 - bgP.y);
            else bg = bg1.rgb;
        } else bg = bg1.rgb;
    } else if (mode == 4) bg = bg1.rgb;
    float3 outC = c + bg * (1 - alpha);
    float outA = mode == 0 ? alpha : 1;
    if (mode == 0 && outP.x > 0.5 && outA > 1e-4) outC /= outA;
    return float4(toSrgb(outC), outA);
}

float4 PSCopy(VSOut i) : SV_Target { return tSrc.SampleLevel(sLinear, i.uv, 0); }
)HLSL";
