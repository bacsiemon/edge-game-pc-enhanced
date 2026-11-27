// Motion Blur HLSL Shaders (Shader Model 3.0) for Direct3D 9

// Vertex Shader Constants
row_major float4x4 g_currWVP  : register(c0);
row_major float4x4 g_prevWVP  : register(c4);
float4             g_currWV_Z : register(c8);

// Pixel Shader Constants - Velocity
float4 g_velParams : register(c0); // x = velScale, y = maxRadiusPx, z = 0, w = 0
float4 g_texelSize : register(c1); // x = 1/W, y = 1/H, z = W, w = H

// Pixel Shader Constants - TileMax / NeighborMax
float4 g_tileStep : register(c0); // xy = stepUV, zw = startOffsetUV (or neighbor texelSize.xy)
float4 g_tileInfo : register(c1); // x = tileSize (loop tap count)

// Pixel Shader Constants - Gather
float4 g_gatherParams : register(c0); // x = 1/W, y = 1/H, z = W, w = H
float4 g_gatherCfg    : register(c1); // x = softZExtent, y = minVelPx, z = frameSeed, w = 0
float4 g_nmUvScale    : register(c2); // xy = (physW/(tileW*tileSize), physH/(tileH*tileSize)), zw = 0

sampler2D s0 : register(s0);
sampler2D s1 : register(s1);
sampler2D s2 : register(s2);

struct VS_VEL_IN {
    float3 pos : POSITION0;
};

struct VS_VEL_OUT {
    float4 outPos   : POSITION0;
    float4 currClip : TEXCOORD0;
    float4 prevClip : TEXCOORD1;
    float  linDepth : TEXCOORD2;
};

VS_VEL_OUT VS_Velocity(VS_VEL_IN v) {
    VS_VEL_OUT o;
    float4 p = float4(v.pos, 1.0f);
    o.outPos   = mul(p, g_currWVP);
    o.currClip = o.outPos;
    o.prevClip = mul(p, g_prevWVP);
    o.linDepth = -dot(p, g_currWV_Z) * 0.001f;
    return o;
}

float SafeLen2(float2 v) {
    return sqrt(dot(v, v) + 1e-12f);
}

float4 PS_Velocity(
    float4 currClip : TEXCOORD0,
    float4 prevClip : TEXCOORD1,
    float  linDepth : TEXCOORD2
) : COLOR0 {
    float2 velUv = float2(0.0f, 0.0f);
    if (currClip.w > 1e-4f && prevClip.w > 1e-4f) {
        float2 currNdc = currClip.xy / currClip.w;
        float2 prevNdc = prevClip.xy / prevClip.w;
        velUv = float2((currNdc.x - prevNdc.x) * 0.5f, -(currNdc.y - prevNdc.y) * 0.5f);
        // Reject single-frame teleports
        if (dot(velUv, velUv) > 0.1225f) {
            velUv = float2(0.0f, 0.0f);
        } else {
            velUv *= g_velParams.x;
            float2 velPx = velUv * g_texelSize.zw;
            float lenPx = SafeLen2(velPx);
            if (lenPx > g_velParams.y && lenPx > 1e-5f) {
                velPx *= (g_velParams.y / lenPx);
                velUv = velPx * g_texelSize.xy;
            }
        }
    }
    return float4(velUv.x, velUv.y, max(linDepth, 0.0f), 1.0f);
}

float4 PS_TileMax1D(float2 uv : TEXCOORD0) : COLOR0 {
    float2 bestVel = float2(0.0f, 0.0f);
    float  bestMag = -1.0f;
    float2 baseUv  = uv + g_tileStep.zw;
    int    count   = (int)g_tileInfo.x;
    [loop]
    for (int i = 0; i < count; ++i) {
        float2 v = tex2Dlod(s0, float4(baseUv + g_tileStep.xy * (float)i, 0.0f, 0.0f)).xy;
        float m = dot(v, v);
        if (m > bestMag) {
            bestMag = m;
            bestVel = v;
        }
    }
    return float4(bestVel.x, bestVel.y, 0.0f, 1.0f);
}

float4 PS_NeighborMax(float2 uv : TEXCOORD0) : COLOR0 {
    float2 bestVel = float2(0.0f, 0.0f);
    float  bestMag = -1.0f;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy) {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx) {
            float2 v = tex2D(s0, uv + float2((float)dx, (float)dy) * g_tileStep.xy).xy;
            float m = dot(v, v);
            if (m > bestMag) {
                bestMag = m;
                bestVel = v;
            }
        }
    }
    return float4(bestVel.x, bestVel.y, 0.0f, 1.0f);
}

float SoftDepthCompare(float za, float zb) {
    return saturate(1.0f - (zb - za) / g_gatherCfg.x);
}

float ConeWeight(float distPx, float velLenPx) {
    return saturate(1.0f - distPx / max(velLenPx, 1e-4f));
}

float CylinderWeight(float distPx, float velLenPx) {
    float ramp = saturate((1.05f * velLenPx - distPx) / max(0.1f * velLenPx, 1e-4f));
    return ramp * saturate(velLenPx * 100.0f);
}

float4 PS_Gather(float2 uv : TEXCOORD0, float2 vpos : VPOS) : COLOR0 {
    float4 cColor = tex2D(s0, uv);
    float2 vn     = tex2D(s2, uv * g_nmUvScale.xy).xy;
    float  vnLenPx = SafeLen2(vn * g_gatherParams.zw);
    [branch]
    if (vnLenPx < g_gatherCfg.y) {
        return float4(cColor.rgb, 1.0f);
    }

    float4 cVD = tex2Dlod(s1, float4(uv, 0.0f, 0.0f));
    float  cDepth = (cVD.a > 0.5f) ? cVD.z : 1000.0f;
    float  cVelLenPx = (cVD.a > 0.5f) ? SafeLen2(cVD.xy * g_gatherParams.zw) : 0.0f;

    float jitter = frac(52.9829189f * frac(dot(vpos + g_gatherCfg.z, float2(0.06711056f, 0.00583715f)))) - 0.5f;

    float  w0 = 1.0f / max(cVelLenPx, 0.5f);
    float3 sumColor = cColor.rgb * w0;
    float  sumWeight = w0;

    [unroll]
    for (int k = 1; k <= 5; ++k) {
        float tPos = ((float)k - 0.5f + jitter) / 5.0f;
        float tNeg = ((float)k - 0.5f - jitter) / 5.0f;

        // Positive half
        {
            float2 sUv = uv + vn * tPos;
            float3 sCol = tex2Dlod(s0, float4(sUv, 0.0f, 0.0f)).rgb;
            float4 sVD  = tex2Dlod(s1, float4(sUv, 0.0f, 0.0f));
            float  sDepth = (sVD.a > 0.5f) ? sVD.z : 1000.0f;
            float  sVelLenPx = (sVD.a > 0.5f) ? SafeLen2(sVD.xy * g_gatherParams.zw) : 0.0f;
            float  distPx = abs(tPos) * vnLenPx;

            float f = SoftDepthCompare(cDepth, sDepth);
            float b = SoftDepthCompare(sDepth, cDepth);
            float w = f * ConeWeight(distPx, sVelLenPx)
                    + b * ConeWeight(distPx, cVelLenPx)
                    + CylinderWeight(distPx, sVelLenPx) * CylinderWeight(distPx, cVelLenPx) * 2.0f;
            sumColor += sCol * w;
            sumWeight += w;
        }

        // Negative half
        {
            float2 sUv = uv - vn * tNeg;
            float3 sCol = tex2Dlod(s0, float4(sUv, 0.0f, 0.0f)).rgb;
            float4 sVD  = tex2Dlod(s1, float4(sUv, 0.0f, 0.0f));
            float  sDepth = (sVD.a > 0.5f) ? sVD.z : 1000.0f;
            float  sVelLenPx = (sVD.a > 0.5f) ? SafeLen2(sVD.xy * g_gatherParams.zw) : 0.0f;
            float  distPx = abs(tNeg) * vnLenPx;

            float f = SoftDepthCompare(cDepth, sDepth);
            float b = SoftDepthCompare(sDepth, cDepth);
            float w = f * ConeWeight(distPx, sVelLenPx)
                    + b * ConeWeight(distPx, cVelLenPx)
                    + CylinderWeight(distPx, sVelLenPx) * CylinderWeight(distPx, cVelLenPx) * 2.0f;
            sumColor += sCol * w;
            sumWeight += w;
        }
    }

    return float4(sumColor / max(sumWeight, 1e-5f), 1.0f);
}
