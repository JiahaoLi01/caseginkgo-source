// 游戏里的 toast 的底：深色的框，贴着屏幕右边；左边是干笔收尾那样不规则的边；框里一层淡淡的、慢慢往左流的烟。
// 边和烟都在这里算，不用贴图，页面上只有一个元素。位置都用 CSS px（input.position），不随分辨率变样。
//
// 参数（清单 effects.json 的顺序）：
//   toast-ink：框的颜色与不透明度
//   toast-smoke：烟的颜色，不透明度是烟最浓的地方盖住框的比例
//   toast-edge：不规则的边最深伸进框多少，CSS px
//   toast-seed：换一个数就换一条边（每条 toast 不同）

float toastHash(float2 p) {
    float3 q = frac(p.xyx * .1031);
    q += dot(q, q.yzx + 33.33);
    return frac((q.x + q.y) * q.z);
}

float toastNoise(float2 p) {
    float2 cell = floor(p);
    float2 f = p - cell;
    float2 u = f * f * f * (f * (f * 6. - 15.) + 10.);
    return lerp(lerp(toastHash(cell), toastHash(cell + float2(1., 0.)), u.x),
                lerp(toastHash(cell + float2(0., 1.)), toastHash(cell + float2(1., 1.)), u.x), u.y);
}

// 四层噪声叠起来，每层转一个角度、频率加倍；结果在 0 到 1 之间，多数落在 0.3 到 0.7
float toastFbm(float2 p) {
    float sum = 0.;
    float amplitude = .5;
    for (int octave = 0; octave < 4; octave++) {
        sum += amplitude * toastNoise(p);
        p = float2(1.6 * p.x - 1.2 * p.y, 1.2 * p.x + 1.6 * p.y) + float2(11.3, 4.7);
        amplitude *= .5;
    }
    return sum / .9375;
}

float4 effect(EffectInput input) {
    float4 ink = input.parameters[0];
    float4 smokeColor = input.parameters[1];
    float reach = max(input.parameters[2].x, 1.);
    float seed = input.parameters[3].x;
    float2 p = input.position;
    float t = input.time;

    // 边：横坐标减去一片二维的噪声，等于零的地方就是边，有凸出的、凹进去的，也有碎开的小块（像墨边，不是齿）
    float push = toastFbm(p / 70. + seed * 1.3);
    float rough = saturate((toastFbm((p + float2(18., -12.) * push) / float2(34., 24.) + seed * 3.7) - .28) / .44);
    float fine = toastNoise(p / 3.8 + seed * 5.1);
    float inside = p.x - reach * (1.04 - 1.08 * rough) + (fine - .5) * 3.;
    float width = max(fwidth(inside), .001) + 2.2;
    float coverage = saturate(inside / width + .5);

    // 靠边的一段墨淡一些，往里慢慢变实
    coverage *= lerp(.62, 1., smoothstep(0., 64., inside));

    // 烟：坐标先被另外两团慢慢变的噪声推歪，再整体往左流（x 加上 t，花纹往左走）；约 11 px/s
    float2 q = p / 118.;
    float2 warp = float2(toastFbm(q * 1.1 + float2(t * .05, seed)), toastFbm(q * 1.1 + float2(seed + 7.3, -t * .04)));
    float cloud = toastFbm(q * float2(.9, 1.7) + 1.7 * warp + float2(t * .095, t * .022));
    float smoke = smoothstep(.40, .74, cloud);

    float3 color = lerp(ink.rgb, smokeColor.rgb, smoke * smokeColor.a);
    float alpha = ink.a * coverage;
    return float4(color * alpha, alpha);
}
