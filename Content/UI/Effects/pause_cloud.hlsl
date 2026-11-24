// 暂停菜单与暗色设置页的体积暗黑云雾效果：左侧停靠、双深度定向受光云卷、边缘场景暖光散射、
// 电影暗角、稀疏飘浮暖金微尘与抗色带胶片微粒。全部在单个 GPU 像素着色器内完成，无贴图依赖。
//
// 参数（清单 effects.json 的顺序）：
//   cloud-ink：深炭暗底色与最高不透明度
//   cloud-ridge：云卷迎光面的灰炭高光色
//   cloud-rim：薄雾边缘吃入右侧场景光的暖色散射
//   cloud-density：云雾总浓度倍率（默认 0.98）
//   cloud-edge：主云边缘的水平归一化基准位置（默认 0.405；设置页可扩到 1.12 铺满全屏暗雾）
//   cloud-motes：飘浮微尘强度（默认 1.0；设置页可降至 0.3）

float cloudHash(float2 p) {
    float3 q = frac(p.xyx * .1031);
    q += dot(q, q.yzx + 33.33);
    return frac((q.x + q.y) * q.z);
}

float2 cloudHash2(float2 p) {
    float3 q = frac(float3(p.xyx) * float3(.1031, .1030, .0973));
    q += dot(q, q.yzx + 33.33);
    return frac((q.xx + q.yz) * q.zy);
}

float cloudNoise(float2 p) {
    float2 cell = floor(p);
    float2 f = p - cell;
    float2 u = f * f * f * (f * (f * 6. - 15.) + 10.);
    return lerp(lerp(cloudHash(cell), cloudHash(cell + float2(1., 0.)), u.x),
                lerp(cloudHash(cell + float2(0., 1.)), cloudHash(cell + float2(1., 1.)), u.x), u.y);
}

float cloudFbm3(float2 p) {
    float sum = 0.;
    float amp = .56;
    for (int i = 0; i < 3; i++) {
        sum += amp * cloudNoise(p);
        p = float2(1.62 * p.x - 1.18 * p.y, 1.18 * p.x + 1.62 * p.y) + float2(7.3, 3.1);
        amp *= .46;
    }
    return sum / .9404;
}

float cloudFbm4(float2 p) {
    float sum = 0.;
    float amp = .52;
    for (int i = 0; i < 4; i++) {
        sum += amp * cloudNoise(p);
        p = float2(1.64 * p.x - 1.16 * p.y, 1.16 * p.x + 1.64 * p.y) + float2(11.3, 4.7);
        amp *= .48;
    }
    return sum / .9627;
}

// 网格化程序飘浮微尘（两层视差，沿云雾过渡带缓慢上升并微弱闪烁）
float cloudMoteLayer(float2 aspectPos, float t, float scale, float speedY, float speedX, float seed) {
    float2 p = aspectPos * scale + float2(-t * speedX, t * speedY) + float2(seed * 13.7, seed * 7.1);
    float2 cell = floor(p);
    float2 local = frac(p);
    float2 rnd = cloudHash2(cell + seed * 19.3);
    float keep = step(.72, cloudHash(cell * 1.7 + seed * 5.9));
    float2 center = .18 + .64 * rnd;
    center.x += sin(t * (.65 + rnd.y * .9) + rnd.x * TwoPi) * .08;
    float d = length(local - center);
    float radius = lerp(.022, .058, rnd.x);
    float dotGlow = smoothstep(radius, 0., d);
    float flicker = .55 + .45 * sin(t * (1.3 + rnd.y * 1.7) + rnd.x * TwoPi);
    return keep * dotGlow * dotGlow * flicker;
}

float4 effect(EffectInput input) {
    float4 ink = input.parameters[0];
    float4 ridge = input.parameters[1];
    float4 rim = input.parameters[2];
    float density = max(input.parameters[3].x, .01);
    float edgeBase = max(input.parameters[4].x, .15);
    float moteStrength = max(input.parameters[5].x, 0.);

    float u = input.uv.x;
    float v = input.uv.y;
    float aspect = input.size.x / max(input.size.y, 1.);
    float t = input.time;

    // 纵横比归一化采样坐标：避免宽屏拉伸，保持饱满云团体积
    float2 drift = float2(t * .055, -t * .085);
    float2 q = float2(u * aspect, v);

    // 纵向柔和云团起伏（收紧横轴振幅，避免凹湾侵蚀左侧文字区）
    float macroLobe = sin(v * 5.2 - t * .12) * .014 + cos(v * 2.8 + .8) * .011;

    // 第一层 Domain Warping：翻涌烟雾流场
    float wx = cloudFbm3(q * float2(1.55, 2.35) + float2(drift.x * .55, drift.y * .78));
    float wy = cloudFbm3(q * float2(1.55, 2.35) + float2(4.7 - drift.x * .30, 2.1 + drift.y * .62));

    float2 cu = q * float2(2.45, 3.45) + float2(wx * 1.12 - drift.x * .35, wy * 1.45 + drift.y);
    float cloud = cloudFbm4(cu);

    // 定向受光导数：模拟右侧场景光打亮暗黑云卷迎光脊面
    float cloudLit = cloudFbm3(cu + float2(.14, -.10));
    float ridgeLight = saturate((cloud - cloudLit) * 3.8 + .42);

    // 高频细碎絮丝
    float wisps = cloudNoise(q * float2(4.9, 6.8) + float2(wy * 1.75 - drift.x * 1.0, drift.y * 1.45));

    // [远层宽幅薄雾]：紧贴主云外侧柔漫，横轴扰动收紧
    float backEdge = edgeBase + .07 + macroLobe * .9 + (wx - .5) * .105 + (cloud - .5) * .08;
    float backMask = saturate(1. - (u - (backEdge - .16)) / .28);
    float backSmooth = backMask * backMask * (3. - 2. * backMask);
    float backAlpha = backSmooth * .46 * (.68 + .45 * cloud) * density;

    // [近层主暗云]：横轴扰动锁定在安全区间，左侧文字区始终稳稳包裹于黑云内
    float frontPerturb = macroLobe + (cloud - .48) * .125 + (wisps - .5) * .048;
    float frontEdge = max(edgeBase - .05, edgeBase + frontPerturb);
    float frontMask = saturate(1. - (u - (frontEdge - .085)) / .165);
    float frontSmooth = frontMask * frontMask * (3. - 2. * frontMask);

    // 文字安全保护底：保证 u <= edgeBase - 0.125 始终饱满暗实
    float safeSmooth = smoothstep(edgeBase - .03, edgeBase - .125, u);
    frontSmooth = max(frontSmooth, safeSmooth * .94);
    float frontAlpha = min(ink.a, frontSmooth * (.90 + .12 * cloud) * density * ink.a);

    // 合成云雾总不透明度（Porter-Duff Over）
    float cloudAlpha = min(.99, frontAlpha + backAlpha * (1. - frontAlpha));

    // 立体灰炭受光层次 + 边缘场景暖光散射
    // edgeBase > 0.95 时为全屏暗雾模式（设置页）：保留通透轻盈的流动烟雾层次，不再按单侧边缘压暗
    float fullScreenMode = smoothstep(.80, 1.02, edgeBase);
    float centerGuard = saturate((u - max(edgeBase - .25, .04)) / .24);
    float billow = max(0., cloud * .72 + wisps * .28 - .22);
    float sculpt = billow * ridgeLight * (.26 + .74 * centerGuard);
    float rimScatter = pow(max(1. - frontSmooth, .0001), 1.35) * frontSmooth * 4.2 + backAlpha * (1. - frontSmooth) * .55;

    float3 frontColor = lerp(ink.rgb, ridge.rgb, saturate(sculpt * .92));
    frontColor += rim.rgb * (rimScatter * .15);

    if (fullScreenMode > .001) {
        // 全屏淡雅双层动态烟雾：低频缓流主烟团 + 中频斜向细雾缕，保持文字高对比同时提供电影空气感
        float2 flowA = q * float2(1.75, 2.15) + float2(wx * .95 - t * .042, wy * 1.15 - t * .068);
        float2 flowB = q * float2(3.10, 3.60) + float2(wy * 1.25 + t * .031, -wx * .85 - t * .092);
        float veilA = smoothstep(.34, .78, cloudFbm4(flowA));
        float veilB = smoothstep(.42, .84, cloudFbm3(flowB));
        float softSmoke = saturate(veilA * .52 + veilB * .32 + sculpt * .36);
        // 右侧与上方烟雾略轻灵、左侧目录与底部略沉静
        float spatialLift = lerp(.58, 1.05, smoothstep(.04, .88, u)) * lerp(1.08, .78, v);
        float3 ambientSmoke = lerp(ink.rgb, ridge.rgb, saturate(softSmoke * spatialLift * .72));
        ambientSmoke += rim.rgb * (veilB * veilA * spatialLift * .055);
        frontColor = lerp(frontColor, ambientSmoke, fullScreenMode);
        frontAlpha = lerp(frontAlpha, ink.a * lerp(.90, 1.02, veilA * .65 + veilB * .35), fullScreenMode);
        cloudAlpha = min(.99, frontAlpha + backAlpha * (1. - frontAlpha));
    }

    float3 backColor = lerp(ink.rgb, ridge.rgb, .32) + rim.rgb * .04;
    float wFront = frontAlpha / max(cloudAlpha, .0001);
    float wBack = (backAlpha * (1. - frontAlpha)) / max(cloudAlpha, .0001);
    float3 cloudColor = frontColor * wFront + backColor * wBack;

    // 电影级柔和四周暗角（右侧中心透亮，四角轻压）
    float2 vigPos = float2((u - .68) * 1.15, (v - .48) * 1.35);
    float vigDist = length(vigPos);
    float vigAlpha = smoothstep(.32, 1.25, vigDist) * .48;
    float3 vigPremul = ink.rgb * vigAlpha;

    // 云雾覆盖在暗角之上
    float3 premul = cloudColor * cloudAlpha + vigPremul * (1. - cloudAlpha);
    float outAlpha = cloudAlpha + vigAlpha * (1. - cloudAlpha);

    // 稀疏暖金飘浮微尘（暂停菜单集中在云雾过渡带；设置页全屏淡雅缓漂）
    if (moteStrength > .001) {
        float edgeBand = (1. - saturate(abs(u - (edgeBase - .01)) / .34)) * step(u, edgeBase + .30);
        float moteBand = lerp(edgeBand, .45 + .55 * smoothstep(.06, .75, u), fullScreenMode);
        float m1 = cloudMoteLayer(q, t, 8.5, .14, .035, 1.0);
        float m2 = cloudMoteLayer(q, t, 14.0, .22, .05, 3.7);
        float mote = (m1 * .72 + m2 * .48) * (.32 + .68 * moteBand) * moteStrength;
        float3 moteColor = lerp(rim.rgb, float3(1., .92, .72), .45);
        premul += moteColor * (mote * .55);
        outAlpha = saturate(outAlpha + mote * .22);
    }

    // 极细胶片微粒防暗部梯度色带
    float grain = (cloudHash(floor(input.pixel)) - .5) * .0038;
    premul = max(0., premul + grain * outAlpha);

    return float4(premul, saturate(outAlpha));
}
