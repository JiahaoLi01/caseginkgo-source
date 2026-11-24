// 描述区的预览：按当前的值算、画（Docs/Design/CameraPipeline.md 的公式，与 C++ 同一套），值一变就重画，带时间的会动。
//
// CameraMath：说明里的数与预览共用（构图的投影、临界阻尼与欠阻尼弹簧、滑铲的速度、噪声、修改器的权重）。
// CameraPreviews.create(kind, host, ctx)：在 host（描述区的预览卡里那块画布）里画一种预览，返回 {update(ctx), frame(time)}；
// ctx 是页面给的读值的接口（见 page.js 的 previewContext）：当前与默认的值、组的信息、移动的数、焦点（正在讲的那一项）。
// SVG 的坐标就是 px（weft 没有 viewBox），子元素不带 opacity（半透明用 rgba）；字用绝对定位的 HTML（pp-figure-label）。
(() => {
  const {C, clamp, fmt} = Paper;
  const DEG = Math.PI / 180;

  // 原地更新与画图的小工具在主题里（paper.js 的 Paper.sketch），设置界面的预览也用
  const {paint, svgEl, svgRoot, div, label, area, sample, axes, niceTicks, canvasWidth, GRID} = Paper.sketch;

  // ===== 数学 =====

  const CameraMath = {
    /** 与 ComputeOrbitArmLength 相同：外环的水平半径换算为视线距离，以三次缓动连接中环。 */
    orbitDistance(length, pitch, orbit) {
      if (!orbit.bEnabled) return length;
      const angle = ((pitch % 360) + 540) % 360 - 180;
      const upper = angle < 0;
      const end = clamp(upper ? orbit.UpperPitch : orbit.LowerPitch, 5, 80);
      const ratio = clamp(upper ? orbit.UpperRadiusScale : orbit.LowerRadiusScale, 0.05, 1);
      const t = clamp(Math.abs(angle) / end, 0, 1), w = t * t * (3 - 2 * t);
      return length * (1 + (ratio / Math.cos(end * DEG) - 1) * w);
    },

    /** 用完整配置路径读值，目录、小节和单项预览共用，不依赖当前焦点的分组。 */
    orbitSettings(c) {
      return {
        bEnabled: c.abs('Orbit.bEnabled') ?? true,
        UpperRadiusScale: c.abs('Orbit.UpperRadiusScale') ?? 0.35,
        UpperPitch: c.abs('Orbit.UpperPitch') ?? 60,
        LowerRadiusScale: c.abs('Orbit.LowerRadiusScale') ?? 0.5,
        LowerPitch: c.abs('Orbit.LowerPitch') ?? 45,
      };
    },

    /** 4:3 的水平视场 θ（度）→ 竖直视场、任意宽高比的水平视场（度） */
    fov(theta, aspect = 16 / 9) {
      const tanV = Math.tan(theta * DEG / 2) * 0.75;
      return {vertical: 2 * Math.atan(tanV) / DEG, horizontal: 2 * Math.atan(tanV * aspect) / DEG, tanV, tanH: tanV * aspect};
    },

    /**
     * 构图：相机放在哪、胶囊投到画面的哪里。p 是参数（ArmLength、ShoulderOffsetRight…），pitch 是视角的俯仰（度，抬头为正）
     * 坐标同 UE：X 向前、Y 向右、Z 向上，胶囊中心在原点，视角的偏航为 0
     */
    framing(p, move, pitch = 0, aspect = 16 / 9) {
      const pr = pitch * DEG;
      const f = [Math.cos(pr), 0, Math.sin(pr)];
      const u = [-Math.sin(pr), 0, Math.cos(pr)];
      const r = [0, 1, 0];
      const pivot = [p.PivotOffsetForward, p.PivotOffsetRight, p.PivotOffsetUp];
      const cam = [0, 1, 2].map(i => pivot[i] - p.ArmLength * f[i] + p.ShoulderOffsetRight * r[i] + p.ShoulderOffsetUp * u[i]);
      const fov = CameraMath.fov(p.FieldOfView, aspect);
      const project = point => {
        const d = [0, 1, 2].map(i => point[i] - cam[i]);
        const depth = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
        const right = d[0] * r[0] + d[1] * r[1] + d[2] * r[2];
        const up = d[0] * u[0] + d[1] * u[1] + d[2] * u[2];
        if (depth <= 1) return null;
        return {x: 0.5 + 0.5 * right / (depth * fov.tanH), y: 0.5 - 0.5 * up / (depth * fov.tanV), depth};
      };
      const half = move.capsuleHalfHeight || 88;
      const center = project([0, 0, 0]);
      const top = project([0, 0, half]);
      const feet = project([0, 0, -half]);
      return {
        pivot, cam, fov, project, half,
        centerX: center ? center.x : NaN,
        topY: top ? top.y : NaN,
        height: top && feet ? feet.y - top.y : NaN,
        distance: Math.hypot(cam[0], cam[1], cam[2] - p.PivotOffsetUp),
        offsetAngle: Math.atan2(p.ShoulderOffsetRight, Math.max(p.ArmLength, 1e-3)) / DEG,
      };
    },

    /** 临界阻尼的阶跃响应 1 − (1 + ωt)e^(−ωt)；到一半、到 90% 的时间 */
    /** 盖在弹簧之后的覆盖，权重的曲线（Ginkgo::Camera::BlendCurveValue）：进度 p，强度 k */
    blendCurve(p, curve, k) {
      const x = clamp(p, 0, 1), s = Math.max(k, 1);
      if (curve === 'Smooth') return x ** s / (x ** s + (1 - x) ** s);
      if (curve === 'EaseOut') return 1 - (1 - x) ** s;
      if (curve === 'EaseIn') return x ** s;
      return x;
    },

    critical(omega) {
      const step = t => omega > 0 ? 1 - (1 + omega * t) * Math.exp(-omega * t) : 1;
      return {step, t50: omega > 0 ? 1.678347 / omega : 0, t90: omega > 0 ? 3.889720 / omega : 0, t99: omega > 0 ? 6.638352 / omega : 0};
    },

    /** 欠阻尼的冲量响应：速度一下子加 J（°/s），x(t) = J/ω_d · e^(−ζωt) · sin(ω_d t)；峰值的时刻与大小、频率、收住（包络到 2%）的时间 */
    damped(J, omega, zeta) {
      const z = clamp(zeta, 0, 1);
      if (omega <= 0) return {x: () => 0, wd: 0, freq: 0, tPeak: 0, peak: 0, settle: 0};
      if (z > 0.99999) {
        const x = t => J * t * Math.exp(-omega * t);
        return {x, wd: 0, freq: 0, tPeak: 1 / omega, peak: x(1 / omega), settle: 5.8 / omega};
      }
      const wd = omega * Math.sqrt(1 - z * z);
      const x = t => t <= 0 ? 0 : J / wd * Math.exp(-z * omega * t) * Math.sin(wd * t);
      const tPeak = Math.atan2(wd, z * omega) / wd;
      return {x, wd, freq: wd / (2 * Math.PI), tPeak, peak: x(tPeak), settle: z > 0 ? Math.log(50) / (z * omega) : Infinity};
    },

    /** 滑铲：满速冲刺一铲。速度 v(t) = (v₀ + d/k)e^(−kt) − d/k；低于跑速、低于结束速度的时刻 */
    slide(move) {
      const sprint = move.sprintSpeed || 762, boost = move.slideBoost || 438.15, cap = move.slideBoostCap || 1168.4;
      const d = move.slideDecel || 127, k = move.slideDecay || 0.35667;
      const v0 = sprint >= cap ? sprint : Math.min(sprint + boost, cap);
      const speed = t => k > 0 ? (v0 + d / k) * Math.exp(-k * t) - d / k : v0 - d * t;
      const timeAt = v => k > 0 ? Math.log((v0 + d / k) / (v + d / k)) / k : (v0 - v) / d;
      const run = move.runSpeed || 476.25, end = move.slideEndSpeed || 317.5;
      return {v0, boostGained: v0 - sprint, speed, tRun: timeAt(run), tEnd: timeAt(end), run, end, d, k};
    },

    /** 噪声：三个正弦，频率比 1、φ、φ²，权重 0.6、0.3、0.1；相位按种子、轴、分量取 R2 序列（与 C++ 的 NoisePhase 一样） */
    noise(amplitude, frequency, seed, axis) {
      const ratios = [1, 1.6180339887498949, 2.6180339887498949];
      const weights = [0.6, 0.3, 0.1];
      const a1 = 0.7548776662466927, a2 = 0.5698402909980532;
      const phases = [0, 1, 2].map(c => {
        const index = 1 + seed * 9 + axis * 3 + c;
        const v = 0.5 + a1 * index + a2 * index * index;
        return 2 * Math.PI * (v - Math.floor(v));
      });
      return t => amplitude <= 0 ? 0 : amplitude * weights.reduce((sum, w, c) => sum + w * Math.sin(2 * Math.PI * frequency * ratios[c] * t + phases[c]), 0);
    },
  };

  // ===== 画图的小工具 =====

  const COLORS = [C.goldDeep, C.verdigrisDeep, C.wisteriaDeep, C.ochre];
  const FAINT = 'rgba(255, 251, 244, 0.12)';
  /**
   * 记下算过的结果：按输入拼的键 → 结果，留最近的 64 个。逐步积分的模拟（滑铲的修改器、跟随、碰撞，几千步）在 QuickJS 里
   * 一次要几毫秒到十几毫秒；在同一个目录里换讲的项时输入多半没变，直接拿
   */
  const MEMO_SIZE = 64;
  const memoCache = new Map();
  const memo = (key, compute) => {
    if (memoCache.has(key)) {
      const value = memoCache.get(key);
      memoCache.delete(key);
      memoCache.set(key, value);
      return value;
    }
    const value = compute();
    memoCache.set(key, value);
    if (memoCache.size > MEMO_SIZE) memoCache.delete(memoCache.keys().next().value);
    return value;
  };

  /** 动画的播放头：一条竖线加一个点；set(t, y) */
  const playhead = (svg, a, color = C.vermilion) => {
    const lineNode = svgEl(svg, 'line', {x1: 0, y1: a.box.t, x2: 0, y2: a.box.t + a.box.h, stroke: 'rgba(224, 101, 74, 0.55)', 'stroke-width': 1});
    const dot = svgEl(svg, 'circle', {cx: 0, cy: 0, r: 3.5, fill: color});
    return {
      set(t, y) {
        const x = a.X(t);
        lineNode.setAttribute('x1', x);
        lineNode.setAttribute('x2', x);
        dot.setAttribute('cx', x);
        dot.setAttribute('cy', a.Y(y));
      },
    };
  };

  /**
   * 一个小画面（16:9）：天、地平线、远处的柱子、角色、准星；按叠加的角度挪、转。放在一个 overflow: hidden 的盒子里（SVG 自己不裁）。
   * 返回 {set(pitch, yaw, roll)}；角度放大 magnify 倍画（冲刺落脚只有零点几度，原样画看不出来）
   */
  const miniFrame = (host, x, y, w, h, fovDeg, magnify = 1) => {
    const box = div(host, '', {position: 'absolute', left: `${x}px`, top: `${y}px`, width: `${w}px`, height: `${h}px`, overflow: 'hidden',
      border: `1px solid ${C.edge}`, boxSizing: 'border-box', backgroundColor: '#14161a'});
    const svg = svgRoot(box, w, h);
    const group = svgEl(svg, 'g', {});
    // 画面的内容画得比画面大一圈，晃的时候边上不露空
    svgEl(group, 'rect', {x: -w * 0.3, y: h * 0.5, width: w * 1.6, height: h * 0.9, fill: '#21242b'});
    svgEl(group, 'line', {x1: -w * 0.3, y1: h * 0.5, x2: w * 1.3, y2: h * 0.5, stroke: C.ink2, 'stroke-width': 1});
    // 远处的柱子：一条路径（一根一个元素的话，搭、排、画都按元素算）
    let posts = '';
    for (let i = -3; i <= 9; ++i) posts += `M${(w * (i / 6)).toFixed(1)} ${(h * 0.5).toFixed(1)}V${(h * 0.3).toFixed(1)}`;
    svgEl(group, 'path', {d: posts, stroke: 'rgba(255, 251, 244, 0.28)', 'stroke-width': 1.5, fill: 'none'});
    // 角色（左边三分之一）
    svgEl(group, 'circle', {cx: w * 0.33, cy: h * 0.36, r: h * 0.06, fill: C.ink2});
    svgEl(group, 'path', {d: `M${w * 0.29} ${h * 0.95}L${w * 0.30} ${h * 0.45}L${w * 0.36} ${h * 0.45}L${w * 0.37} ${h * 0.95}Z`, fill: C.ink2});
    // 准星在画面中心，不跟着画面晃（瞄准沿叠加之前的视角）
    svgEl(svg, 'path', {d: `M${w / 2 - 5} ${h / 2}h10M${w / 2} ${h / 2 - 5}v10`, stroke: C.vermilion, 'stroke-width': 1.2, fill: 'none'});
    const fov = CameraMath.fov(fovDeg || 90);
    return {
      set(pitch, yaw, roll) {
        // 俯仰为正是抬头：画面里的东西往下走；偏航为正向右看：东西往左走；侧倾为正向右倾：画面逆时针转
        const dy = pitch * magnify / fov.vertical * h;
        const dx = -yaw * magnify / fov.horizontal * w;
        group.setAttribute('transform', `translate(${dx.toFixed(2)} ${dy.toFixed(2)}) rotate(${(-roll * magnify).toFixed(3)} ${w / 2} ${h / 2})`);
      },
    };
  };

  // ===== 预览 =====

  const kinds = {};

  // ----- 三环：空间环与侧视轨迹；只演示轨道几何，肩位、跟随滞后与碰撞另行求值。

  kinds.orbit = (host, ctx) => {
    const width = canvasWidth(host);
    let state = null;
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 260), o = CameraMath.orbitSettings(c), length = c.params().ArmLength;
      const point = (pitch, enabled = o.bEnabled) => {
        const distance = CameraMath.orbitDistance(length, pitch, {...o, bEnabled: enabled});
        return {r: distance * Math.cos(pitch * DEG), z: -distance * Math.sin(pitch * DEG)};
      };
      const rings = [-o.UpperPitch, 0, o.LowerPitch].map(p => point(p));
      const samples = Array.from({length: 161}, (_, i) => point(i - 80));
      const extent = Math.max(length, ...samples.map(p => Math.max(p.r, Math.abs(p.z))), 1);
      const scale = Math.min((width * 0.48 - 32) / (2 * extent), 89 / extent);
      const cx = width * 0.25, cy = 136, sx = width * 0.88;
      label(host, 4, 2, o.bEnabled ? '三环空间位置' : '已关闭 · 原球面轨道');
      label(host, width * 0.54, 2, '侧视 · 连续俯仰');
      svgEl(svg, 'line', {x1: cx, y1: 30, x2: cx, y2: 236, stroke: GRID, 'stroke-width': 1});
      rings.forEach((p, i) => {
        svgEl(svg, 'ellipse', {cx, cy: cy - p.z * scale, rx: p.r * scale, ry: p.r * scale * 0.23,
          fill: 'none', stroke: COLORS[i], 'stroke-width': 1.8});
      });
      const path = (x, values, side) => values.map((p, i) => `${i ? 'L' : 'M'}${(x - p.r * scale).toFixed(2)} ${(cy - p.z * scale + (side ? 0 : p.r * scale * 0.1)).toFixed(2)}`).join(' ');
      const sphere = Array.from({length: 161}, (_, i) => point(i - 80, false));
      svgEl(svg, 'path', {d: path(sx, sphere, true), fill: 'none', stroke: C.ink2, 'stroke-width': 1, 'stroke-dasharray': '3 4'});
      svgEl(svg, 'path', {d: path(sx, samples, true), fill: 'none', stroke: C.goldDeep, 'stroke-width': 2});
      svgEl(svg, 'path', {d: path(cx, samples, false), fill: 'none', stroke: C.goldDeep, 'stroke-width': 1.5});
      rings.forEach((p, i) => svgEl(svg, 'circle', {cx: sx - p.r * scale, cy: cy - p.z * scale, r: 3.5, fill: COLORS[i]}));
      svgEl(svg, 'circle', {cx: sx, cy, r: 3, fill: C.ink2});
      label(host, sx - 22, cy + 8, '跟随点');
      label(host, 4, 238, `中环 ${fmt(length, 0)} cm · 上环 ${fmt(rings[0].r, 0)} cm · 下环 ${fmt(rings[2].r, 0)} cm`, {width: width - 8});
      state = {
        point, scale, cx, sx, cy,
        dot: svgEl(svg, 'circle', {r: 4, fill: C.vermilion}),
        sideDot: svgEl(svg, 'circle', {r: 4, fill: C.vermilion}),
      };
    });
    draw(ctx);
    return {
      update: draw,
      frame(time) {
        if (!state) return;
        const s = state, p = s.point(80 * Math.sin(time * 0.7));
        s.dot.setAttribute('cx', s.cx - p.r * s.scale);
        s.dot.setAttribute('cy', s.cy - p.z * s.scale + p.r * s.scale * 0.1);
        s.sideDot.setAttribute('cx', s.sx - p.r * s.scale);
        s.sideDot.setAttribute('cy', s.cy - p.z * s.scale);
      },
    };
  };

  // ----- 构图：16:9 的画面里胶囊投在哪里，旁边是俯视（或侧视）的相机、跟随点与视锥

  kinds.framing = (host, ctx) => {
    const width = canvasWidth(host);
    let svg = null;
    const draw = c => paint(host, () => {
      svg = svgRoot(host, width, 240);
      const p = c.params();
      const fr = CameraMath.framing(p, c.move);
      // 画面：16:9，宽 296
      const sw = 296, sh = sw * 9 / 16, sx = 0, sy = 22;
      label(host, sx, 2, `画面（16:9，平视）：中线 ${fmt(fr.centerX * 100, 0)}% 宽、胶囊顶 ${fmt(fr.topY * 100, 0)}% 高`, {width: sw + 40});
      svgEl(svg, 'rect', {x: sx, y: sy, width: sw, height: sh, fill: '#14161a', stroke: C.edge, 'stroke-width': 1});
      for (const k of [1, 2]) {
        svgEl(svg, 'line', {x1: sx + sw * k / 3, y1: sy, x2: sx + sw * k / 3, y2: sy + sh, stroke: GRID, 'stroke-width': 1});
        svgEl(svg, 'line', {x1: sx, y1: sy + sh * k / 3, x2: sx + sw, y2: sy + sh * k / 3, stroke: GRID, 'stroke-width': 1});
      }
      const g = svgEl(svg, 'g', {transform: `rotate(${-(p.Roll || 0)} ${sx + sw / 2} ${sy + sh / 2})`});
      // 地面上每 2 m 一根柱子（离角色 4–12 m），看视场与臂长的变化；几十根并成一条路径
      let posts = '';
      for (let depth = 400; depth <= 1200; depth += 400) {
        for (let side = -2000; side <= 2000; side += 200) {
          const a = fr.project([depth, side, -fr.half]);
          const b = fr.project([depth, side, -fr.half + 180]);
          if (a && b && a.x > -0.2 && a.x < 1.2) {
            posts += `M${(sx + a.x * sw).toFixed(1)} ${(sy + a.y * sh).toFixed(1)}L${(sx + b.x * sw).toFixed(1)} ${(sy + b.y * sh).toFixed(1)}`;
          }
        }
      }
      if (posts) svgEl(g, 'path', {d: posts, stroke: 'rgba(255, 251, 244, 0.22)', 'stroke-width': 1, fill: 'none'});
      // 胶囊（背对相机，按投影画成圆角的竖条）与头
      const half = fr.half, radius = c.move.capsuleRadius || 34;
      const outline = [];
      for (let i = 0; i <= 12; ++i) {
        const angle = Math.PI * i / 12;
        outline.push(fr.project([0, -radius * Math.cos(angle), half - radius + radius * Math.sin(angle)]));
      }
      for (let i = 0; i <= 12; ++i) {
        const angle = Math.PI + Math.PI * i / 12;
        outline.push(fr.project([0, -radius * Math.cos(angle), -half + radius + radius * Math.sin(angle)]));
      }
      if (outline.every(q => q)) {
        svgEl(g, 'path', {d: outline.map((q, i) => `${i ? 'L' : 'M'}${(sx + q.x * sw).toFixed(1)} ${(sy + q.y * sh).toFixed(1)}`).join('') + 'Z',
          fill: 'rgba(229, 154, 74, 0.28)', stroke: C.ochre, 'stroke-width': 1.2});
      }
      const center = fr.project([0, 0, 0]);
      if (center) svgEl(g, 'line', {x1: sx + center.x * sw, y1: sy, x2: sx + center.x * sw, y2: sy + sh, stroke: 'rgba(229, 154, 74, 0.65)', 'stroke-width': 1, 'stroke-dasharray': '3 3'});
      // 准星：画面中心
      svgEl(svg, 'path', {d: `M${sx + sw / 2 - 6} ${sy + sh / 2}h12M${sx + sw / 2} ${sy + sh / 2 - 6}v12`, stroke: C.vermilion, 'stroke-width': 1.4, fill: 'none'});
      label(host, sx, sy + sh + 6, `16:9 水平视场 ${fmt(fr.fov.horizontal, 1)}°、竖直 ${fmt(fr.fov.vertical, 1)}°；胶囊占画面高 ${fmt(fr.height * 100, 0)}%`, {width: sw + 40});

      // 俯视：X 向上（视角的前方）、Y 向右；跟随点、相机、臂、视锥
      const side = c.focusIn(['ShoulderOffsetUp', 'PivotOffsetUp']);
      const vx = sx + sw + 40, vw = width - vx - 4, vy = 22, vh = 200;
      label(host, vx, 2, side ? '侧视（前方朝右）' : '俯视（前方朝上）', {width: vw});
      svgEl(svg, 'rect', {x: vx, y: vy, width: vw, height: vh, fill: C.surface, stroke: C.edge, 'stroke-width': 1});
      const scale = Math.min(vw, vh) / 520;
      const ox = vx + vw / 2, oy = vy + vh * (side ? 0.62 : 0.32);
      // 俯视：世界 (X, Y) → 画面 (ox + Y·s, oy − X·s)；侧视：(X, Z) → (ox + X·s, oy − Z·s)
      const P = side ? (x, y, z) => [ox + 60 * scale + x * scale, oy - z * scale] : (x, y, z) => [ox + y * scale, oy + 40 * scale - x * scale];
      const [cx0, cy0] = P(0, 0, 0);
      if (side) {
        svgEl(svg, 'rect', {x: cx0 - radius * scale, y: cy0 - half * scale, width: radius * 2 * scale, height: half * 2 * scale, rx: radius * scale,
          fill: 'rgba(229, 154, 74, 0.28)', stroke: C.ochre, 'stroke-width': 1});
        svgEl(svg, 'line', {x1: vx, y1: cy0 + half * scale, x2: vx + vw, y2: cy0 + half * scale, stroke: C.ink2, 'stroke-width': 1});
      } else {
        svgEl(svg, 'circle', {cx: cx0, cy: cy0, r: radius * scale, fill: 'rgba(229, 154, 74, 0.28)', stroke: C.ochre, 'stroke-width': 1});
      }
      const [px, py] = P(...fr.pivot);
      const [kx, ky] = P(...fr.cam);
      // 视锥：俯视用 16:9 的水平视场，侧视用竖直视场
      const halfAngle = (side ? fr.fov.vertical : fr.fov.horizontal) / 2 * DEG;
      const reach = 480 * scale;
      const fwd = side ? [1, 0] : [0, -1];
      const rot = (v, a) => [v[0] * Math.cos(a) - v[1] * Math.sin(a), v[0] * Math.sin(a) + v[1] * Math.cos(a)];
      const e1 = rot(fwd, halfAngle), e2 = rot(fwd, -halfAngle);
      svgEl(svg, 'path', {d: `M${kx} ${ky}L${kx + e1[0] * reach} ${ky + e1[1] * reach}M${kx} ${ky}L${kx + e2[0] * reach} ${ky + e2[1] * reach}`,
        stroke: c.focusIn(['FieldOfView']) ? C.goldDeep : 'rgba(232, 207, 156, 0.52)', 'stroke-width': c.focusIn(['FieldOfView']) ? 2 : 1, fill: 'none'});
      // 臂：跟随点 → 往后臂长 → 往右（或往上）肩部偏移
      const armEnd = P(fr.pivot[0] - p.ArmLength, fr.pivot[1], fr.pivot[2]);
      svgEl(svg, 'line', {x1: px, y1: py, x2: armEnd[0], y2: armEnd[1], stroke: c.focusIn(['ArmLength']) ? C.goldDeep : C.ink2, 'stroke-width': c.focusIn(['ArmLength']) ? 2.5 : 1.2});
      svgEl(svg, 'line', {x1: armEnd[0], y1: armEnd[1], x2: kx, y2: ky,
        stroke: c.focusIn(['ShoulderOffsetRight', 'ShoulderOffsetUp']) ? C.goldDeep : C.ink2, 'stroke-width': c.focusIn(['ShoulderOffsetRight', 'ShoulderOffsetUp']) ? 2.5 : 1.2,
        'stroke-dasharray': '4 3'});
      svgEl(svg, 'line', {x1: cx0, y1: cy0, x2: px, y2: py, stroke: c.focusIn(['PivotOffsetForward', 'PivotOffsetRight', 'PivotOffsetUp']) ? C.goldDeep : C.ink3,
        'stroke-width': c.focusIn(['PivotOffsetForward', 'PivotOffsetRight', 'PivotOffsetUp']) ? 2.5 : 1});
      if (side) {
        // 俯仰时相机绕跟随点转：以跟随点为心的圆弧
        const radiusArc = Math.hypot(kx - px, ky - py);
        svgEl(svg, 'path', {d: `M${px - radiusArc * Math.cos(40 * DEG)} ${py - radiusArc * Math.sin(40 * DEG)}A${radiusArc} ${radiusArc} 0 0 0 ${px - radiusArc * Math.cos(-30 * DEG)} ${py - radiusArc * Math.sin(-30 * DEG)}`,
          fill: 'none', stroke: 'rgba(255, 251, 244, 0.26)', 'stroke-width': 1, 'stroke-dasharray': '2 3'});
      }
      svgEl(svg, 'circle', {cx: px, cy: py, r: 3.5, fill: C.verdigrisDeep});
      svgEl(svg, 'rect', {x: kx - 5, y: ky - 4, width: 10, height: 8, fill: C.bronze});
      label(host, px + 6, py - 16, '跟随点', {color: C.verdigrisDeep});
      label(host, kx + 7, ky - 2, '相机', {color: C.bronze});
      label(host, vx + 6, vy + vh - 18, `相机离跟随点 ${fmt(fr.distance, 0)} cm，让开 ${fmt(fr.offsetAngle, 1)}°`, {width: vw - 8});
    });
    draw(ctx);
    return {update: draw};
  };

  // ----- 临界阻尼弹簧的阶跃响应：当前的 ω（实线）与默认值（虚线），到一半、到 90% 的时刻，会动的点

  kinds.spring = (host, ctx) => {
    const width = canvasWidth(host);
    let state = null;
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const omega = c.value(), def = c.def();
      const cur = CameraMath.critical(omega), old = CameraMath.critical(def);
      const T = clamp(Math.max(cur.t99, old.t99, 0.1) * 1.1, 0.1, 4);
      const a = area({l: 44, t: 24, w: width - 60, h: 172}, [0, T], [0, 1.05]);
      axes(svg, host, a, {yTicks: [0, 0.5, 0.9, 1], yFormat: v => `${fmt(v * 100, 0)}%`, xTicks: niceTicks(0, T, 5), xFormat: v => `${fmt(v, 2)} s`,
        yTitle: '单位阶跃响应 x(t)'});
      if (def !== omega && def > 0) svgEl(svg, 'path', {d: a.line(sample(old.step, 0, T)), fill: 'none', stroke: C.ink3, 'stroke-width': 1.2, 'stroke-dasharray': '4 3'});
      svgEl(svg, 'path', {d: a.line(sample(cur.step, 0, T)), fill: 'none', stroke: C.goldDeep, 'stroke-width': 2});
      for (const [t, v, text] of [[cur.t50, 0.5, `${fmt(cur.t50, 3)} s`], [cur.t90, 0.9, `${fmt(cur.t90, 3)} s`]]) {
        if (!(t > 0 && t < T)) continue;
        svgEl(svg, 'line', {x1: a.X(t), y1: a.Y(0), x2: a.X(t), y2: a.Y(v), stroke: 'rgba(232, 207, 156, 0.50)', 'stroke-width': 1, 'stroke-dasharray': '2 2'});
        label(host, a.X(t) + 4, a.Y(v) + 4, text, {color: C.goldDeep, bold: true});
      }
      if (def !== omega) label(host, a.X(T) - 4, a.Y(0.45), `虚线：默认 ω ${fmt(def, 2)}`, {align: 'right', width: 180});
      state = {a, cur, T, head: playhead(svg, a)};
    });
    draw(ctx);
    return {
      update: draw,
      frame(time) {
        if (!state) return;
        const period = state.T + 0.8;
        const t = Math.min(time % period, state.T);
        state.head.set(t, state.cur.step(t));
      },
    };
  };

  // ----- 开镜的视场：按下、放下时权重怎么走；虚线是线性的对照，会动的点

  kinds.blendCurve = (host, ctx) => {
    const width = canvasWidth(host);
    let state = null;
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const tIn = c.get('BlendInTime'), tOut = c.get('BlendOutTime'), k = c.get('BlendCurveStrength');
      const inCurve = c.raw('BlendInCurve'), outCurve = c.raw('BlendOutCurve');
      const hold = 0.2;
      const T = Math.max(tIn + hold + tOut + 0.1, 0.5);
      const shape = (t, curveIn, curveOut) => {
        if (t <= tIn) return tIn > 0 ? CameraMath.blendCurve(t / tIn, curveIn, k) : 1;
        if (t <= tIn + hold) return 1;
        return tOut > 0 ? 1 - CameraMath.blendCurve((t - tIn - hold) / tOut, curveOut, k) : 0;
      };
      const weight = t => shape(t, inCurve, outCurve);
      const a = area({l: 44, t: 24, w: width - 60, h: 172}, [0, T], [0, 1.05]);
      axes(svg, host, a, {yTicks: [0, 0.5, 1], yFormat: v => `${fmt(v * 100, 0)}%`, xTicks: niceTicks(0, T, 5), xFormat: v => `${fmt(v, 2)} s`, yTitle: '开镜视场覆盖权重 w(t)'});
      svgEl(svg, 'path', {d: a.line(sample(t => shape(t, 'Linear', 'Linear'), 0, T)), fill: 'none', stroke: C.ink3, 'stroke-width': 1.2, 'stroke-dasharray': '4 3'});
      svgEl(svg, 'path', {d: a.line(sample(weight, 0, T)), fill: 'none', stroke: C.goldDeep, 'stroke-width': 2});
      label(host, a.X(0) + 6, a.Y(1.05) - 2, '按下开镜', {bold: true, color: C.goldDeep});
      label(host, a.X(tIn + hold) + 6, a.Y(1.05) - 2, '退出开镜', {bold: true, color: C.goldDeep});
      label(host, a.X(T) - 4, a.Y(0.5), '虚线：线性对照', {align: 'right', width: 120});
      state = {a, weight, T, head: playhead(svg, a)};
    });
    draw(ctx);
    return {
      update: draw,
      frame(time) {
        if (!state) return;
        const period = state.T + 0.6;
        const t = Math.min(time % period, state.T);
        state.head.set(t, state.weight(t));
      },
    };
  };

  // ----- 跟随：冲刺起步又急停时，跟随点落后胶囊中心多少（cm）；稳态滞后、最大滞后的线；下面一条俯视的小动画

  const simulateFollow = (omega, feedforward, maxLag, filterOmega, profile, T, dt = 0.002) => {
    let x = 0, v = 0, fv = 0, fa = 0;
    const out = [];
    for (let t = 0; t <= T + 1e-9; t += dt) {
      const {pos, vel} = profile(t);
      // 前馈的速度先过临界阻尼低通（ω ≤ 0 不过）
      if (filterOmega > 0) {
        const acc = filterOmega * filterOmega * (vel - fv) - 2 * filterOmega * fa;
        fa += acc * dt;
        fv += fa * dt;
      } else {
        fv = vel;
      }
      const target = pos + (omega > 0 ? feedforward * 2 * fv / omega : 0);
      if (omega > 0) {
        const acc = omega * omega * (target - x) - 2 * omega * v;
        v += acc * dt;
        x += v * dt;
      } else {
        x = pos;
        v = vel;
      }
      if (maxLag > 0 && Math.abs(pos - x) > maxLag) x = pos - Math.sign(pos - x) * maxLag;
      out.push({t, pos, x, lag: pos - x, fv});
    }
    return out;
  };

  kinds.followH = (host, ctx) => {
    const width = canvasWidth(host);
    let state = null;
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const omega = c.abs('Follow.Omega'), k = c.abs('Follow.Feedforward'), maxLag = c.abs('Follow.MaxLag'), filter = c.abs('Follow.FeedforwardFilterOmega');
      const v = c.move.sprintSpeed || 762;
      // 0.3 s 时一下子到冲刺速度（起步、lurch），1.6 s 时撞墙急停
      const tStart = 0.3, tStop = 1.6, T = 2.6;
      const profile = t => {
        if (t < tStart) return {pos: 0, vel: 0};
        if (t < tStop) return {pos: (t - tStart) * v, vel: v};
        return {pos: (tStop - tStart) * v, vel: 0};
      };
      const run = memo(`followH|${omega}|${k}|${maxLag}|${filter}|${v}`, () => simulateFollow(omega, k, maxLag, filter, profile, T));
      const lags = run.map(s => s.lag);
      const steady = omega > 0 ? (1 - k) * 2 * v / omega : 0;
      // 最大滞后只在曲线够得着它（一半以内）时画进来，不然会把曲线压扁
      const reach = Math.max(10, ...lags.map(Math.abs));
      const showMax = maxLag > 0 && maxLag < reach * 2;
      const lo = Math.min(0, ...lags) - 5, hi = Math.max(10, ...lags, showMax ? maxLag : 0) + 10;
      const a = area({l: 48, t: 24, w: width - 64, h: 132}, [0, T], [lo, hi]);
      axes(svg, host, a, {yTicks: niceTicks(lo, hi, 4), yFormat: y => `${fmt(y, 0)}`, xTicks: [0, 0.5, 1, 1.5, 2, 2.5], xFormat: x => `${fmt(x, 1)} s`,
        yTitle: `水平跟随滞后（cm）：0.3 s 起步至 ${fmt(v, 0)} cm/s，1.6 s 急停`});
      svgEl(svg, 'line', {x1: a.X(0), y1: a.Y(0), x2: a.X(T), y2: a.Y(0), stroke: C.ink3, 'stroke-width': 1});
      if (steady > 0.5) {
        svgEl(svg, 'line', {x1: a.X(tStart), y1: a.Y(steady), x2: a.X(tStop), y2: a.Y(steady), stroke: 'rgba(95, 224, 138, 0.60)', 'stroke-width': 1, 'stroke-dasharray': '4 3'});
        label(host, a.X(tStop) - 4, a.Y(steady) - 16, `稳态 ${fmt(steady, 1)} cm`, {align: 'right', color: C.verdigrisDeep, width: 120});
      }
      if (!showMax && maxLag > 0) label(host, a.box.l + a.box.w - 4, a.box.t + 2, `最大滞后上限 ${fmt(maxLag, 0)} cm（高于当前峰值，未触发钳制）`, {align: 'right', color: C.vermilion, width: 280});
      if (showMax) {
        svgEl(svg, 'line', {x1: a.X(0), y1: a.Y(maxLag), x2: a.X(T), y2: a.Y(maxLag), stroke: 'rgba(224, 101, 74, 0.65)', 'stroke-width': 1, 'stroke-dasharray': '6 3'});
        label(host, a.X(T) - 4, a.Y(maxLag) - 16, `最大滞后 ${fmt(maxLag, 0)} cm`, {align: 'right', color: C.vermilion, width: 140});
      }
      for (const [t0, t1] of [[tStart, tStop]]) {
        svgEl(svg, 'rect', {x: a.X(t0), y: a.box.t, width: a.X(t1) - a.X(t0), height: a.box.h, fill: 'rgba(229, 180, 67, 0.12)'});
      }
      svgEl(svg, 'path', {d: a.line(run.filter((s, i) => i % 4 === 0).map(s => [s.t, s.lag])), fill: 'none', stroke: C.goldDeep, 'stroke-width': 2});
      const peak = run.reduce((m, s) => Math.abs(s.lag) > Math.abs(m.lag) ? s : m, run[0]);
      label(host, a.X(peak.t) + 4, a.Y(peak.lag) - 2, `峰值 ${fmt(peak.lag, 1)} cm`, {color: C.goldDeep, bold: true});
      // 俯视的小动画：胶囊中心（赭）与跟随点（铜绿）沿一条线走
      const ty = 214;
      svgEl(svg, 'line', {x1: a.X(0), y1: ty, x2: a.X(T), y2: ty, stroke: GRID, 'stroke-width': 1});
      const span = (tStop - tStart) * v;
      const toX = pos => a.box.l + 20 + pos / span * (a.box.w - 40);
      const capsule = svgEl(svg, 'circle', {cx: toX(0), cy: ty, r: 6, fill: 'rgba(229, 154, 74, 0.35)', stroke: C.ochre, 'stroke-width': 1});
      const follower = svgEl(svg, 'circle', {cx: toX(0), cy: ty, r: 3.5, fill: C.verdigrisDeep});
      label(host, a.box.l, ty + 8, '赭：真实胶囊中心　绿：平滑跟随点', {width: 320});
      state = {run, T, a, capsule, follower, toX, head: playhead(svg, a)};
    });
    draw(ctx);
    return {
      update: draw,
      frame(time) {
        if (!state) return;
        const t = time % (state.T + 0.6);
        const index = clamp(Math.round(Math.min(t, state.T) / 0.002), 0, state.run.length - 1);
        const s = state.run[index];
        state.head.set(s.t, s.lag);
        state.capsule.setAttribute('cx', state.toX(s.pos));
        state.follower.setAttribute('cx', state.toX(s.x));
      },
    };
  };

  // ----- 跟随（竖直）：原地起跳时胶囊中心与相机跟随的高度

  kinds.followV = (host, ctx) => {
    const width = canvasWidth(host);
    let state = null;
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const omega = c.abs('Follow.VerticalOmega'), k = c.abs('Follow.VerticalFeedforward'), maxLag = c.abs('Follow.VerticalMaxLag');
      const vz = c.move.jumpZVelocity || 420, g = c.move.gravity || 980;
      const tJump = 0.2, air = 2 * vz / g, T = tJump + air + 0.8;
      const profile = t => {
        if (t < tJump || t > tJump + air) return {pos: 0, vel: 0};
        const s = t - tJump;
        return {pos: vz * s - 0.5 * g * s * s, vel: vz - g * s};
      };
      const run = memo(`followV|${omega}|${k}|${maxLag}|${vz}|${g}`, () => simulateFollow(omega, k, maxLag, 0, profile, T));
      const top = vz * vz / (2 * g);
      const lo = Math.min(-10, ...run.map(s => s.x)) - 5, hi = Math.max(top, ...run.map(s => s.x)) + 10;
      const a = area({l: 48, t: 24, w: width - 64, h: 160}, [0, T], [lo, hi]);
      axes(svg, host, a, {yTicks: niceTicks(lo, hi, 4), yFormat: y => fmt(y, 0), xTicks: niceTicks(0, T, 5), xFormat: x => `${fmt(x, 1)} s`,
        yTitle: `高度（cm）：原地起跳 ${fmt(vz, 0)} cm/s、重力 ${fmt(g, 0)} cm/s²`});
      svgEl(svg, 'path', {d: a.line(run.filter((s, i) => i % 4 === 0).map(s => [s.t, s.pos])), fill: 'none', stroke: C.ochre, 'stroke-width': 1.5});
      svgEl(svg, 'path', {d: a.line(run.filter((s, i) => i % 4 === 0).map(s => [s.t, s.x])), fill: 'none', stroke: C.verdigrisDeep, 'stroke-width': 2});
      const dip = run.reduce((m, s) => s.x < m.x ? s : m, run[0]);
      if (dip.x < -0.5) label(host, a.X(dip.t) + 4, a.Y(dip.x) + 2, `落地过冲 ${fmt(-dip.x, 1)} cm`, {color: C.vermilion, bold: true, width: 160});
      const lagMax = run.reduce((m, s) => Math.max(m, s.pos - s.x), 0);
      label(host, a.box.l, 212, `赭：胶囊中心高度　绿：跟随支点高度（最大滞后 ${fmt(lagMax, 1)} cm）`, {width: 420});
      state = {run, T, a, head: playhead(svg, a, C.verdigrisDeep)};
    });
    draw(ctx);
    return {
      update: draw,
      frame(time) {
        if (!state) return;
        const t = time % (state.T + 0.5);
        const s = state.run[clamp(Math.round(Math.min(t, state.T) / 0.002), 0, state.run.length - 1)];
        state.head.set(s.t, s.x);
      },
    };
  };

  // ----- 碰撞：一堵墙挡进来又离开，实际的臂（拉近瞬时、推远走弹簧）；左边一张侧视的小图，探测球的半径

  kinds.collision = (host, ctx) => {
    const width = canvasWidth(host);
    let state = null;
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const radius = c.abs('ProbeRadius'), omega = c.abs('PushOutOmega');
      const arm = c.abs('BaseParams.ArmLength') || 150;
      // 0.4 s 墙挡进来，要求压缩到只剩 60 cm（减去探测球的半径）；1.2 s 墙离开
      const tIn = 0.4, tOut = 1.2, T = 2.4, dt = 0.002;
      const required = t => (t >= tIn && t < tOut ? Math.max(arm - 60 + radius, 0) : 0);
      const run = memo(`collision|${radius}|${omega}|${arm}`, () => {
        let comp = 0, vel = 0;
        const out = [];
        for (let t = 0; t <= T + 1e-9; t += dt) {
          const need = required(t);
          if (need > comp) {
            comp = need;
            vel = 0;
          } else if (omega > 0) {
            const acc = omega * omega * (need - comp) - 2 * omega * vel;
            vel += acc * dt;
            comp += vel * dt;
            comp = Math.max(comp, need);
          } else {
            comp = need;
          }
          out.push({t, arm: arm - comp, need: arm - need});
        }
        return out;
      });
      const a = area({l: 200, t: 24, w: width - 214, h: 160}, [0, T], [0, arm * 1.1]);
      axes(svg, host, a, {yTicks: niceTicks(0, arm * 1.1, 3), yFormat: y => fmt(y, 0), xTicks: niceTicks(0, T, 4), xFormat: x => `${fmt(x, 1)} s`,
        yTitle: '实际有效臂长（cm）'});
      svgEl(svg, 'rect', {x: a.X(tIn), y: a.box.t, width: a.X(tOut) - a.X(tIn), height: a.box.h, fill: 'rgba(224, 101, 74, 0.12)'});
      label(host, a.X((tIn + tOut) / 2), a.box.t + 2, '墙体遮挡', {align: 'center', color: C.vermilion, width: 80});
      svgEl(svg, 'path', {d: a.line(run.filter((s, i) => i % 4 === 0).map(s => [s.t, s.need])), fill: 'none', stroke: C.ink3, 'stroke-width': 1, 'stroke-dasharray': '3 3'});
      svgEl(svg, 'path', {d: a.line(run.filter((s, i) => i % 4 === 0).map(s => [s.t, s.arm])), fill: 'none', stroke: C.goldDeep, 'stroke-width': 2});
      const back = CameraMath.critical(omega);
      label(host, a.X(tOut) + 6, a.Y(arm * 0.55), omega > 0 ? `推远恢复：50% ${fmt(back.t50, 2)} s、90% ${fmt(back.t90, 2)} s` : '推远无平滑（瞬时恢复）', {color: C.goldDeep, width: 220});
      // 侧视：跟随点在右，相机往左后；墙从上面落下
      const px = 160, py = 110, s = 120 / Math.max(arm, 1);
      svgEl(svg, 'rect', {x: 4, y: 24, width: 176, height: 172, fill: C.surface, stroke: C.edge, 'stroke-width': 1});
      svgEl(svg, 'circle', {cx: px, cy: py, r: 3.5, fill: C.verdigrisDeep});
      const wall = svgEl(svg, 'rect', {x: px - 60 * s - 8, y: 30, width: 6, height: 160, fill: 'rgba(255, 251, 244, 0.38)'});
      const armLine = svgEl(svg, 'line', {x1: px, y1: py, x2: px - arm * s, y2: py, stroke: C.ink2, 'stroke-width': 1.2});
      const probe = svgEl(svg, 'circle', {cx: px - arm * s, cy: py, r: Math.max(radius * s, 1.5), fill: 'rgba(229, 180, 67, 0.26)', stroke: C.goldDeep, 'stroke-width': 1});
      label(host, 10, 30, `探测球 r = ${fmt(radius, 0)} cm`, {color: C.goldDeep, width: 170});
      state = {run, T, a, wall, armLine, probe, px, py, s, tIn, tOut, head: playhead(svg, a)};
    });
    draw(ctx);
    return {
      update: draw,
      frame(time) {
        if (!state) return;
        const t = time % (state.T + 0.4);
        const sample = state.run[clamp(Math.round(Math.min(t, state.T) / 0.002), 0, state.run.length - 1)];
        state.head.set(sample.t, sample.arm);
        const x = state.px - sample.arm * state.s;
        state.armLine.setAttribute('x2', x);
        state.probe.setAttribute('cx', x);
        state.wall.setAttribute('height', sample.t >= state.tIn && sample.t < state.tOut ? 160 : 0);
      },
    };
  };

  // ----- 滑铲的时间线：满速冲刺一铲的速度（底下淡的），各修改器的权重与过了参数弹簧以后的参数（正在讲的那个加粗）

  /**
   * 按配置算一个修改器在这段速度曲线上的权重与参数（与 C++ 的参数层同一套：条件、锁住、线性淡入淡出，再过参数弹簧）。
   * 场景带 key 时记下结果：滑铲目录里换讲的项时，几个修改器的输入多半没变
   */
  const MODIFIER_DT = 0.004;  // 步长：权重线性淡入淡出、弹簧 ω ≤ 30 时 4 ms 一步画出来与 2 ms 分不出，算的量减半

  const conditionsKey = m => m.conditions.map(cond => [cond.scalar, cond.atLeast, cond.threshold, cond.thresholdScalar]);

  const simulateModifier = (m, scenario, springOmega, T, dt = MODIFIER_DT) => {
    if (!scenario.key) return integrateModifier(m, scenario, springOmega, T, dt);
    const key = JSON.stringify(['modifier', m.value, m.enabled, m.latch, m.blendIn, m.blendOut, conditionsKey(m), scenario.key, springOmega, T, dt]);
    return memo(key, () => integrateModifier(m, scenario, springOmega, T, dt));
  };

  /** 激活的开关序列：0 标签查询不成立，1 查询成立、条件不成立，2 都成立。只看条件与场景（滑铲的三个修改器条件一样，共用一份） */
  const gateOf = (m, scenario, T, dt) => {
    const compute = () => {
      const out = [];
      for (let t = 0; t <= T + 1e-9; t += dt) {
        if (!scenario.query(t)) {
          out.push(0);
          continue;
        }
        const scalars = scenario.scalars(t);
        out.push(m.conditions.every(cond => cond.holds(scalars)) ? 2 : 1);
      }
      return out;
    };
    return scenario.key ? memo(JSON.stringify(['gate', conditionsKey(m), scenario.key, T, dt]), compute) : compute();
  };

  const integrateModifier = (m, scenario, springOmega, T, dt) => {
    const gate = gateOf(m, scenario, T, dt);
    let weight = 0, latched = false, value = 0, velocity = 0;
    const out = [];
    for (let i = 0; i < gate.length; ++i) {
      const g = gate[i];
      if (g === 0) latched = false;
      if (g === 1 && m.latch) latched = true;
      const target = m.enabled && g === 2 && !latched;
      if (target) weight = m.blendIn > 0 ? Math.min(weight + dt / m.blendIn, 1) : 1;
      else weight = m.blendOut > 0 ? Math.max(weight - dt / m.blendOut, 0) : 0;
      const composed = weight * m.value;
      if (springOmega > 0) {
        const acc = springOmega * springOmega * (composed - value) - 2 * springOmega * velocity;
        velocity += acc * dt;
        value += velocity * dt;
      } else {
        value = composed;
      }
      out.push({t: i * dt, weight, composed, value});
    }
    return out;
  };

  const slideScenario = (move, t0 = 0.3) => {
    const slide = CameraMath.slide(move);
    const speedAt = t => t < t0 ? (move.sprintSpeed || 762) : Math.max(slide.speed(t - t0), 0);
    const inSlide = t => t >= t0 && t < t0 + slide.tEnd;
    return {
      slide, t0, speedAt, inSlide,
      key: JSON.stringify(['slide', move.sprintSpeed, move.slideBoost, move.slideBoostCap, move.slideDecel, move.slideDecay, move.runSpeed, move.slideEndSpeed, t0]),
      query: inSlide,
      scalars: t => ({
        'Movement.Value.HorizontalSpeed': speedAt(t),
        'Movement.Value.SlideBoost': inSlide(t) ? slide.boostGained : 0,
        'Movement.Value.RunSpeed': move.runSpeed || 476.25,
      }),
    };
  };

  kinds.slideTimeline = (host, ctx) => {
    const width = canvasWidth(host);
    let state = null;
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const scenario = slideScenario(c.move);
      const T = scenario.t0 + scenario.slide.tEnd + 0.8;
      const mods = c.modifiers().filter(m => m.slide);
      const series = mods.map(m => ({m, run: simulateModifier(m, scenario, c.springOmega(m.param), T)}));
      const focus = series.find(s => s.m.path === c.groupPath);
      const title = `满速冲刺滑铲：初速 ${fmt(scenario.slide.v0, 0)} cm/s，${fmt(scenario.slide.tRun, 2)} s 跌破跑速，${fmt(scenario.slide.tEnd, 2)} s 结束`;
      let a, toY, ghost = null;
      if (focus) {
        // 讲某一个修改器：纵轴是它的参数的真值，默认值（各项都取默认）画成灰的虚线，改了多少一眼看得出
        const d = focus.m.defaults;
        const changed = d.value !== focus.m.value || d.blendIn !== focus.m.blendIn || d.blendOut !== focus.m.blendOut || d.latch !== focus.m.latch
          || d.enabled !== focus.m.enabled || c.springOmegaDef(focus.m.param) !== c.springOmega(focus.m.param);
        if (changed) ghost = simulateModifier({...focus.m, ...d}, scenario, c.springOmegaDef(focus.m.param), T);
        const extent = [0, focus.m.value, d.value];
        const lo = Math.min(...extent), hi = Math.max(...extent);
        const pad = Math.max((hi - lo) * 0.12, 0.5);
        a = area({l: 52, t: 24, w: width - 68, h: 150}, [0, T], [lo - pad, hi + pad]);
        axes(svg, host, a, {xTicks: niceTicks(0, T, 5), xFormat: x => `${fmt(x, 1)} s`, yTicks: niceTicks(lo, hi, 3), yFormat: y => `${fmt(y, 1)}${focus.m.unit}`, yTitle: title});
        toY = (m, value) => value;
      } else {
        // 讲整个滑铲：各修改器的参数按自己的满值归一（视场 +10°、臂长 −24 cm、侧倾 −4° 都画成 0–1），字写真值
        a = area({l: 44, t: 24, w: width - 60, h: 150}, [0, T], [-0.05, 1.12]);
        axes(svg, host, a, {xTicks: niceTicks(0, T, 5), xFormat: x => `${fmt(x, 1)} s`, yTicks: [0, 1], yFormat: y => y ? '满' : '0', yTitle: title});
        toY = (m, value) => value / (Math.abs(m.value) > 1e-6 ? m.value : 1);
      }
      // 速度：淡的面积（按图的高度归一），虚线是跑速
      const vMax = Math.max(scenario.slide.v0, 1);
      const yAt = f => a.y0 + f * (a.y1 - a.y0);
      svgEl(svg, 'path', {d: a.line([[0, yAt(0)], ...sample(t => yAt(scenario.speedAt(t) / vMax), 0, T, 100), [T, yAt(0)]]) + 'Z', fill: 'rgba(255, 251, 244, 0.07)', stroke: 'none'});
      const runY = yAt((c.move.runSpeed || 476.25) / vMax);
      svgEl(svg, 'line', {x1: a.X(0), y1: a.Y(runY), x2: a.X(T), y2: a.Y(runY), stroke: 'rgba(255, 251, 244, 0.30)', 'stroke-width': 1, 'stroke-dasharray': '2 3'});
      label(host, a.X(T) - 2, a.Y(runY) - 15, `常态跑速 ${fmt(c.move.runSpeed || 476.25, 0)} cm/s（灰色面积为实时速度）`, {align: 'right', width: 260});
      if (ghost) {
        svgEl(svg, 'path', {d: a.line(ghost.filter((p, i) => i % 6 === 0).map(p => [p.t, p.value])), fill: 'none', stroke: C.ink3, 'stroke-width': 1.2, 'stroke-dasharray': '4 3'});
      }
      series.forEach((s, index) => {
        if (focus && s !== focus) return;
        const color = COLORS[index % COLORS.length];
        const strong = s === focus;
        if (strong) svgEl(svg, 'path', {d: a.line(s.run.filter((p, i) => i % 6 === 0).map(p => [p.t, p.composed])), fill: 'none', stroke: color, 'stroke-width': 1, 'stroke-dasharray': '3 3'});
        svgEl(svg, 'path', {d: a.line(s.run.filter((p, i) => i % 6 === 0).map(p => [p.t, toY(s.m, p.value)])), fill: 'none', stroke: color, 'stroke-width': strong ? 2.4 : 1.4});
      });
      series.forEach((s, index) => {
        label(host, a.box.l + 4 + index * 150, 196, `${s.m.title} ${s.m.value >= 0 ? '+' : ''}${fmt(s.m.value, 1)}${s.m.unit}`,
          {color: focus && s !== focus ? C.ink3 : COLORS[index % COLORS.length], bold: s === focus, width: 150});
      });
      label(host, a.box.l + 4, 214, focus
        ? `实线：参数弹簧输出；彩色虚线：权重 × 目标值${ghost ? '；灰虚线：默认参数对照' : ''}`
        : '实线：经参数弹簧平滑后的归一化响应', {width: width - 60});
      const head = focus || series[0];
      state = {T, a, head: playhead(svg, a, C.ink1), run: head ? head.run : [], map: p => (focus ? p.value : head ? toY(head.m, p.value) : 0)};
    });
    draw(ctx);
    return {
      update: draw,
      frame(time) {
        if (!state || !state.run.length) return;
        const t = time % (state.T + 0.6);
        const p = state.run[clamp(Math.round(Math.min(t, state.T) / MODIFIER_DT), 0, state.run.length - 1)];
        state.head.set(p.t, state.map(p));
      },
    };
  };

  // ----- 推拉变焦：平时与滑铲中（视场与臂长加上修改器的值）的两张画面，角色一样大、背景变宽

  kinds.dolly = (host, ctx) => {
    const width = canvasWidth(host);
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const base = c.params();
      const mods = c.modifiers().filter(m => m.slide);
      const delta = param => mods.filter(m => m.param === param && m.enabled).reduce((sum, m) => sum + m.value, 0);
      const slid = {...base, FieldOfView: base.FieldOfView + delta('FieldOfView'), ArmLength: base.ArmLength + delta('ArmLength')};
      const fw = Math.min((width - 24) / 2, 300), fh = fw * 9 / 16;
      const frames = [[base, '常态构图', 0], [slid, '滑铲加速构图', fw + 24]];
      const heights = [];
      for (const [params, title, x0] of frames) {
        const fr = CameraMath.framing(params, c.move);
        heights.push(fr.height);
        label(host, x0, 2, `${title}：视场 ${fmt(params.FieldOfView, 1)}°、臂长 ${fmt(params.ArmLength, 0)} cm`, {width: fw + 20});
        const y0 = 22;
        svgEl(svg, 'rect', {x: x0, y: y0, width: fw, height: fh, fill: '#14161a', stroke: C.edge, 'stroke-width': 1});
        // 柱子（两张画面加起来上百根）并成一条路径
        let posts = '';
        for (let depth = 300; depth <= 1500; depth += 300) {
          for (let side = -2400; side <= 2400; side += 150) {
            const a = fr.project([depth, side, -fr.half]);
            const b = fr.project([depth, side, -fr.half + 200]);
            if (a && b && a.x > 0 && a.x < 1) {
              posts += `M${(x0 + a.x * fw).toFixed(1)} ${(y0 + a.y * fh).toFixed(1)}L${(x0 + b.x * fw).toFixed(1)} ${(y0 + clamp(b.y, 0, 1) * fh).toFixed(1)}`;
            }
          }
        }
        if (posts) svgEl(svg, 'path', {d: posts, stroke: 'rgba(255, 251, 244, 0.24)', 'stroke-width': 1, fill: 'none'});
        const top = fr.project([0, 0, fr.half]), feet = fr.project([0, 0, -fr.half]);
        if (top && feet) {
          const r = 30 / Math.max(top.depth, 1) / fr.fov.tanH * fw * 0.5;
          svgEl(svg, 'rect', {x: x0 + top.x * fw - r, y: y0 + top.y * fh, width: 2 * r, height: (feet.y - top.y) * fh, rx: r, fill: 'rgba(229, 154, 74, 0.30)', stroke: C.ochre, 'stroke-width': 1});
        }
        label(host, x0, y0 + fh + 6, `胶囊占画面高 ${fmt(fr.height * 100, 1)}%，画面宽 ${fmt(fr.fov.horizontal, 1)}°`, {width: fw + 20});
      }
      const ratio = heights[1] / heights[0];
      label(host, 0, 22 + fh + 28, `滑铲时角色高度比为常态的 ${fmt(ratio * 100, 1)}%（满足 L·tan(θ/2) 恒定时主体等大）`, {width: width, color: Math.abs(ratio - 1) > 0.03 ? C.vermilion : C.verdigrisDeep, bold: true});
    });
    draw(ctx);
    return {update: draw};
  };

  // ----- 冲量弹簧：各轴的响应曲线（俯仰、偏航、侧倾、视场），右边一张会晃的小画面

  const impulseAxes = c => {
    const impulse = {Pitch: c.get('Impulse.Pitch'), Yaw: c.get('Impulse.Yaw'), Roll: c.get('Impulse.Roll'), FieldOfView: c.get('FieldOfViewImpulse')};
    return impulse;
  };

  kinds.impulse = (host, ctx) => {
    const width = canvasWidth(host);
    let state = null;
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const J = impulseAxes(c);
      const omega = c.get('Omega'), zeta = c.get('Zeta');
      const responses = Object.entries(J).filter(([, j]) => Math.abs(j) > 1e-6).map(([axis, j]) => ({axis, j, r: CameraMath.damped(j, omega, zeta)}));
      const any = responses[0] ? responses[0].r : CameraMath.damped(1, omega, zeta);
      const T = clamp(Number.isFinite(any.settle) ? any.settle * 1.15 : 1, 0.15, 1.6);
      const peak = Math.max(0.05, ...responses.map(s => Math.abs(s.r.peak)));
      const fw = 168, fh = 94.5;
      const a = area({l: 44, t: 24, w: width - 60 - fw - 16, h: 150}, [0, T], [-peak * 1.15, peak * 1.15]);
      axes(svg, host, a, {yTicks: niceTicks(-peak, peak, 4), yFormat: y => `${fmt(y, 2)}°`, xTicks: niceTicks(0, T, 4), xFormat: x => `${fmt(x * 1000, 0)} ms`,
        yTitle: '单次冲量后的角度偏转（°）'});
      svgEl(svg, 'line', {x1: a.X(0), y1: a.Y(0), x2: a.X(T), y2: a.Y(0), stroke: C.ink3, 'stroke-width': 1});
      const names = {Pitch: '俯仰', Yaw: '偏航', Roll: '侧倾', FieldOfView: '视场'};
      responses.forEach((s, index) => {
        const color = COLORS[index % COLORS.length];
        const focus = c.focusIn(['Impulse.' + s.axis, s.axis === 'FieldOfView' ? 'FieldOfViewImpulse' : '']);
        svgEl(svg, 'path', {d: a.line(sample(s.r.x, 0, T, 200)), fill: 'none', stroke: color, 'stroke-width': focus ? 2.4 : 1.6});
        svgEl(svg, 'circle', {cx: a.X(s.r.tPeak), cy: a.Y(s.r.peak), r: 3, fill: color});
        label(host, a.box.l + 4 + index * 118, 196, `${names[s.axis]} 峰值 ${fmt(s.r.peak, 2)}°`, {color, bold: focus, width: 118});
      });
      const freq = any.freq;
      const warn = freq > 15;
      label(host, a.box.l + 4, 214, `频率 ${fmt(freq, 1)} Hz · 首峰 ${fmt(any.tPeak * 1000, 0)} ms · 2% 整定 ${fmt(any.settle * 1000, 0)} ms${warn ? '（高于 15 Hz，30 fps 下存在混叠）' : ''}`,
        {width: width - 40, color: warn ? C.vermilion : C.ink2});
      const frame = miniFrame(host, width - fw - 6, 44, fw, fh, c.abs('BaseParams.FieldOfView') || 90);
      label(host, width - fw - 6, 44 + fh + 6, '画面示意（准星锚定真实指向）', {width: fw + 10});
      state = {responses, T, a, frame, head: playhead(svg, a, C.ink1), any};
    });
    draw(ctx);
    return {
      update: draw,
      frame(time) {
        if (!state) return;
        const t = time % (state.T + 0.5);
        const value = axis => {
          const s = state.responses.find(r => r.axis === axis);
          return s ? s.r.x(t) : 0;
        };
        state.frame.set(value('Pitch'), value('Yaw'), value('Roll'));
        const first = state.responses[0];
        state.head.set(Math.min(t, state.T), first ? first.r.x(Math.min(t, state.T)) : 0);
      },
    };
  };

  // ----- 冲刺落脚：一串落脚（左 +1、右 −1，每步 0.21 s；开头一对 20 ms 的是滑铲起身的两脚），按最小强度、最小间隔筛过，叠起来的响应

  kinds.footsteps = (host, ctx) => {
    const width = canvasWidth(host);
    let state = null;
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const J = impulseAxes(c);
      const omega = c.get('Omega'), zeta = c.get('Zeta'), mirror = !!c.get('bMirrorOnNegative');
      const minMagnitude = c.get('MinMagnitude'), minInterval = c.get('MinInterval');
      const events = [{t: 0.08, m: 1}, {t: 0.10, m: -1}];
      for (let i = 1; i <= 6; ++i) events.push({t: 0.10 + i * 0.21, m: i % 2 ? 1 : -1});
      let last = -Infinity;
      for (const e of events) {
        e.taken = Math.abs(e.m) >= minMagnitude && e.t - last >= minInterval;
        if (e.taken) last = e.t;
      }
      const T = 1.55;
      const axesNames = [['Pitch', '俯仰'], ['Yaw', '偏航'], ['Roll', '侧倾']].filter(([axis]) => Math.abs(J[axis]) > 1e-6);
      const curves = axesNames.map(([axis, name]) => {
        const r = CameraMath.damped(1, omega, zeta);
        const sideways = axis !== 'Pitch';
        const fn = t => events.reduce((sum, e) => !e.taken || t < e.t ? sum : sum + J[axis] * (sideways && mirror && e.m < 0 ? -1 : 1) * r.x(t - e.t), 0);
        return {axis, name, fn};
      });
      // 叠起来的曲线采一次，峰值与路径共用（一点要把八下落脚的响应加起来）
      const sampled = memo(JSON.stringify(['footsteps', J, omega, zeta, mirror, minMagnitude, minInterval]), () => curves.map(cv => sample(cv.fn, 0, T, 300)));
      const peak = Math.max(0.05, ...sampled.flatMap(points => points.map(p => Math.abs(p[1]))));
      const fw = 168, fh = 94.5;
      const a = area({l: 44, t: 24, w: width - 60 - fw - 16, h: 150}, [0, T], [-peak * 1.2, peak * 1.2]);
      axes(svg, host, a, {yTicks: niceTicks(-peak, peak, 4), yFormat: y => `${fmt(y, 2)}°`, xTicks: [0, 0.5, 1, 1.5], xFormat: x => `${fmt(x, 1)} s`,
        yTitle: '脚步叠加偏转角（左 +1 / 右 −1，°）'});
      svgEl(svg, 'line', {x1: a.X(0), y1: a.Y(0), x2: a.X(T), y2: a.Y(0), stroke: C.ink3, 'stroke-width': 1});
      for (const e of events) {
        svgEl(svg, 'line', {x1: a.X(e.t), y1: a.box.t + a.box.h, x2: a.X(e.t), y2: a.box.t + a.box.h - 10, stroke: e.taken ? C.ink1 : 'rgba(224, 101, 74, 0.85)', 'stroke-width': e.taken ? 2 : 1.2});
      }
      curves.forEach((cv, index) => {
        svgEl(svg, 'path', {d: a.line(sampled[index]), fill: 'none', stroke: COLORS[index % COLORS.length], 'stroke-width': 1.8});
        label(host, a.box.l + 4 + index * 90, 196, cv.name, {color: COLORS[index % COLORS.length], bold: true, width: 90});
      });
      const dropped = events.filter(e => !e.taken).length;
      label(host, a.box.l + 4, 214, `底部刻度为脚步事件：白线触发、红线过滤（已过滤 ${dropped} 次：小于间隔 ${fmt(minInterval, 2)} s 或低于强度门槛）`, {width: width - 60, color: dropped ? C.vermilion : C.ink2, wrap: true});
      const frame = miniFrame(host, width - fw - 6, 44, fw, fh, c.abs('BaseParams.FieldOfView') || 90, 4);
      label(host, width - fw - 6, 44 + fh + 6, '画面示意（偏转放大 4×；准星固定）', {width: fw + 20});
      state = {curves, T, a, frame, head: playhead(svg, a, C.ink1)};
    });
    draw(ctx);
    return {
      update: draw,
      frame(time) {
        if (!state) return;
        const t = Math.min(time % (state.T + 0.3), state.T);
        const value = axis => {
          const cv = state.curves.find(x => x.axis === axis);
          return cv ? cv.fn(t) : 0;
        };
        state.frame.set(value('Pitch'), value('Yaw'), value('Roll'));
        const first = state.curves[0];
        state.head.set(t, first ? first.fn(t) : 0);
      },
    };
  };

  // ----- 噪声 × 包络：各轴 12 s 的波形，右边一个方框里准星漂的轨迹（偏航横、俯仰竖，最近 4 s 的尾巴）

  kinds.noise = (host, ctx) => {
    const width = canvasWidth(host);
    let state = null;
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const seed = c.get('Seed') || 0;
      const envelope = clamp(c.get('MaxEnvelope') === undefined ? 1 : Math.min(1, c.get('MaxEnvelope')), 0, 1);
      const axesInfo = [['Pitch', '俯仰', 0], ['Yaw', '偏航', 1], ['Roll', '侧倾', 2]].map(([axis, name, index]) => ({
        axis, name, amp: c.get(`${axis}.Amplitude`) || 0, freq: c.get(`${axis}.Frequency`) || 0,
        fn: CameraMath.noise((c.get(`${axis}.Amplitude`) || 0) * envelope, c.get(`${axis}.Frequency`) || 0, seed, index),
      }));
      const active = axesInfo.filter(x => x.amp > 0);
      const T = 12;
      const peak = Math.max(0.05, ...active.map(x => x.amp));
      const box = 150;
      const a = area({l: 44, t: 24, w: width - 60 - box - 16, h: 150}, [0, T], [-peak * 1.1, peak * 1.1]);
      axes(svg, host, a, {yTicks: niceTicks(-peak, peak, 4), yFormat: y => `${fmt(y, 2)}°`, xTicks: [0, 3, 6, 9, 12], xFormat: x => `${x} s`,
        yTitle: '满包络（e = 1）下的准周期漂移角（°）'});
      svgEl(svg, 'line', {x1: a.X(0), y1: a.Y(0), x2: a.X(T), y2: a.Y(0), stroke: C.ink3, 'stroke-width': 1});
      active.forEach((x, index) => {
        const focus = c.focusIn([`${x.axis}.Amplitude`, `${x.axis}.Frequency`]);
        svgEl(svg, 'path', {d: a.line(sample(x.fn, 0, T, 400)), fill: 'none', stroke: COLORS[index % COLORS.length], 'stroke-width': focus ? 2.4 : 1.5});
        label(host, a.box.l + 4 + index * 150, 196, `${x.name} ${fmt(x.amp, 2)}°、${fmt(x.freq, 2)} Hz`, {color: COLORS[index % COLORS.length], bold: focus, width: 150});
      });
      label(host, a.box.l + 4, 214, '三谐波叠加：倍频比 1 : 1.618 : 2.618，权重 0.6 / 0.3 / 0.1，合成极值严格不超过设定幅度', {width: width - 40});
      // 方框：画面中心附近 ±峰值，准星漂的轨迹
      const bx = width - box - 6, by = 34;
      svgEl(svg, 'rect', {x: bx, y: by, width: box, height: box, fill: C.surface, stroke: C.edge, 'stroke-width': 1});
      svgEl(svg, 'path', {d: `M${bx + box / 2} ${by}v${box}M${bx} ${by + box / 2}h${box}`, stroke: GRID, 'stroke-width': 1, fill: 'none'});
      label(host, bx, by - 16, `画面中心 ±${fmt(peak * 1.1, 2)}°`, {width: box});
      const trail = svgEl(svg, 'path', {d: '', fill: 'none', stroke: 'rgba(232, 207, 156, 0.58)', 'stroke-width': 1.2});
      const dot = svgEl(svg, 'circle', {cx: bx + box / 2, cy: by + box / 2, r: 3.5, fill: C.vermilion});
      const pitch = axesInfo[0].fn, yaw = axesInfo[1].fn;
      const toBox = (t) => [bx + box / 2 + yaw(t) / (peak * 1.1) * box / 2, by + box / 2 - pitch(t) / (peak * 1.1) * box / 2];
      state = {a, T, trail, dot, toBox, head: playhead(svg, a, C.ink1), first: active[0]};
    });
    draw(ctx);
    return {
      update: draw,
      frame(time) {
        if (!state) return;
        const t = time;
        const points = [];
        for (let k = 0; k <= 80; ++k) points.push(state.toBox(t - 4 + 4 * k / 80));
        state.trail.setAttribute('d', points.map((p, i) => `${i ? 'L' : 'M'}${p[0].toFixed(1)} ${p[1].toFixed(1)}`).join(''));
        const [x, y] = state.toBox(t);
        state.dot.setAttribute('cx', x);
        state.dot.setAttribute('cy', y);
        const local = t % state.T;
        state.head.set(local, state.first ? state.first.fn(local) : 0);
      },
    };
  };

  // ----- 包络：连续量映射到 0–1（夹到 0–上限），再过包络的弹簧

  kinds.envelope = (host, ctx) => {
    const width = canvasWidth(host);
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const zero = c.get('ScalarForZero'), one = c.get('ScalarForOne'), max = c.get('MaxEnvelope');
      const span = one - zero;
      const map = s => clamp(Math.abs(span) > 1e-6 ? (s - zero) / span : 1, 0, max);
      const lo = Math.min(0, zero, one) - 0.1, hi = Math.max(1, zero, one) + 0.1;
      const a = area({l: 44, t: 24, w: (width - 80) / 2, h: 160}, [lo, hi], [0, Math.max(1, max) * 1.1]);
      axes(svg, host, a, {yTicks: [0, 1, max].filter((v, i, arr) => arr.indexOf(v) === i), yFormat: y => fmt(y, 2), xTicks: niceTicks(lo, hi, 3), xFormat: x => fmt(x, 2),
        yTitle: '连续量（开镜进度）→ 包络', xTitle: '开镜进度'});
      svgEl(svg, 'path', {d: a.line(sample(map, lo, hi, 120)), fill: 'none', stroke: C.goldDeep, 'stroke-width': 2});
      // 右边：开镜（进度 0 → 1，0.25 s 两头缓过）时包络的样子，包络的弹簧 ω
      const omega = c.get('EnvelopeOmega');
      const b = area({l: 44 + (width - 80) / 2 + 30, t: 24, w: (width - 80) / 2 - 10, h: 160}, [0, 1.5], [0, Math.max(1, max) * 1.1]);
      axes(svg, host, b, {xTicks: [0, 0.5, 1, 1.5], xFormat: x => `${fmt(x, 1)} s`, yTicks: [], yTitle: '开镜时的包络'});
      let value = 0, velocity = 0;
      const points = [];
      for (let t = 0; t <= 1.5; t += 0.005) {
        const s = clamp((t - 0.2) / 0.25, 0, 1);
        const ads = s * s * (3 - 2 * s);
        const target = map(ads);
        if (omega > 0) {
          const acc = omega * omega * (target - value) - 2 * omega * velocity;
          velocity += acc * 0.005;
          value += velocity * 0.005;
        } else {
          value = target;
        }
        points.push([t, value]);
      }
      svgEl(svg, 'path', {d: b.line(points), fill: 'none', stroke: C.verdigrisDeep, 'stroke-width': 2});
      label(host, b.box.l, 210, omega > 0 ? `包络弹簧 ω = ${fmt(omega, 1)}：90% 响应时间 ${fmt(CameraMath.critical(omega).t90, 2)} s` : '包络弹簧已旁路：直接跟随两端缓动的开镜进度', {width: 280});
    });
    draw(ctx);
    return {update: draw};
  };

  // ----- 叠加的转轴：绕相机自身与绕跟随点，同一个晃动（放大 5 倍）下相机与视线怎么走

  kinds.pivot = (host, ctx) => {
    const width = canvasWidth(host);
    let state = null;
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const current = c.value();
      const half = (width - 20) / 2;
      const panels = [['Camera', '绕相机自身', 0], ['Pivot', '绕跟随点', half + 20]].map(([mode, title, x0]) => {
        const on = current === mode;
        svgEl(svg, 'rect', {x: x0, y: 22, width: half, height: 170, fill: on ? 'rgba(229, 180, 67, 0.14)' : C.surface, stroke: on ? C.goldDeep : C.edge, 'stroke-width': on ? 1.5 : 1});
        label(host, x0, 2, `${title}${on ? '（当前）' : ''}`, {bold: on, color: on ? C.goldDeep : C.ink2, width: half});
        const px = x0 + half * 0.72, py = 120, cx = x0 + half * 0.22, cy = 100;
        svgEl(svg, 'circle', {cx: px, cy: py, r: 4, fill: C.verdigrisDeep});
        svgEl(svg, 'rect', {x: px - 10, y: py - 10, width: 20, height: 52, rx: 10, fill: 'rgba(229, 154, 74, 0.28)', stroke: C.ochre, 'stroke-width': 1});
        const cam = svgEl(svg, 'rect', {x: cx - 6, y: cy - 5, width: 12, height: 10, fill: C.bronze});
        const ray = svgEl(svg, 'line', {x1: cx, y1: cy, x2: cx + 150, y2: cy, stroke: 'rgba(232, 207, 156, 0.72)', 'stroke-width': 1.2});
        svgEl(svg, 'path', {d: `M${cx - 8} 180h${half * 0.5}`, stroke: C.ink3, 'stroke-width': 1, fill: 'none'});
        return {mode, px, py, cx, cy, cam, ray};
      });
      label(host, 0, 200, '同一旋转偏转（放大 5×）：左图绕相机自身原地偏转，画面整体晃动且无穿墙位移；右图绕跟随支点球面公转，角色近乎静止而背景晃动', {width: width - 10, wrap: true});
      state = {panels};
    });
    draw(ctx);
    return {
      update: draw,
      frame(time) {
        if (!state) return;
        const angle = 10 * Math.sin(time * 2 * Math.PI * 0.8) * Math.exp(-((time % 2.5) * 1.2));
        for (const p of state.panels) {
          if (p.mode === 'Camera') {
            const r = angle * DEG;
            p.ray.setAttribute('x2', p.cx + 150 * Math.cos(r));
            p.ray.setAttribute('y2', p.cy - 150 * Math.sin(r));
          } else {
            const r = angle * DEG;
            const dx = p.cx - p.px, dy = p.cy - p.py;
            const nx = p.px + dx * Math.cos(r) + dy * Math.sin(r), ny = p.py - dx * Math.sin(r) + dy * Math.cos(r);
            p.cam.setAttribute('x', nx - 6);
            p.cam.setAttribute('y', ny - 5);
            // 视线跟着相机一起转同一个角度
            p.ray.setAttribute('x1', nx);
            p.ray.setAttribute('y1', ny);
            p.ray.setAttribute('x2', nx + 150 * Math.cos(r));
            p.ray.setAttribute('y2', ny - 150 * Math.sin(r));
          }
        }
      },
    };
  };

  // ----- 条件：连续量的轴，标出常见的速度与阈值，成立的那一段着色

  kinds.condition = (host, ctx) => {
    const width = canvasWidth(host);
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const cond = c.condition();
      if (!cond) return;
      const move = c.move;
      let marks = [], hi = 1;
      if (/HorizontalSpeed|Speed/.test(cond.scalar)) {
        marks = [['蹲走', move.crouchSpeed || 158.75], ['跑', move.runSpeed || 476.25], ['能起滑铲', move.slideStartSpeed || 508], ['冲刺', move.sprintSpeed || 762],
          ['滑铲上限', move.slideBoostCap || 1168.4]];
        hi = 1250;
      } else if (/SlideBoost/.test(cond.scalar)) {
        const slide = CameraMath.slide(move);
        marks = [['疲劳', 0], ['满速冲刺一铲', slide.boostGained], ['起步加速', move.slideBoost || 438.15]];
        hi = Math.max(500, (move.slideBoost || 438) * 1.15);
      } else {
        marks = [['0', 0], ['1', 1]];
        hi = 1.2;
      }
      const threshold = cond.threshold;
      const a = area({l: 30, t: 60, w: width - 60, h: 40}, [0, hi], [0, 1]);
      svgEl(svg, 'line', {x1: a.X(0), y1: a.Y(0.5), x2: a.X(hi), y2: a.Y(0.5), stroke: C.ink2, 'stroke-width': 1.5});
      const holdsFrom = cond.atLeast ? threshold : 0, holdsTo = cond.atLeast ? hi : threshold;
      svgEl(svg, 'rect', {x: a.X(clamp(holdsFrom, 0, hi)), y: a.Y(1), width: Math.max(a.X(clamp(holdsTo, 0, hi)) - a.X(clamp(holdsFrom, 0, hi)), 0), height: a.box.h,
        fill: 'rgba(229, 180, 67, 0.28)'});
      marks.forEach(([name, v], index) => {
        svgEl(svg, 'line', {x1: a.X(v), y1: a.Y(0.5) - 6, x2: a.X(v), y2: a.Y(0.5) + 6, stroke: C.ink2, 'stroke-width': 1});
        label(host, a.X(v), a.Y(0.5) + (index % 2 ? 26 : 10), `${name} ${fmt(v, 0)}`, {align: 'center', width: 110});
      });
      svgEl(svg, 'line', {x1: a.X(clamp(threshold, 0, hi)), y1: a.Y(1) - 10, x2: a.X(clamp(threshold, 0, hi)), y2: a.Y(0) + 10, stroke: C.vermilion, 'stroke-width': 2});
      label(host, a.X(clamp(threshold, 0, hi)), a.Y(1) - 28, `阈值 ${fmt(threshold, 1)}${cond.thresholdName ? `（${cond.thresholdName}）` : ''}`, {align: 'center', color: C.vermilion, bold: true, width: 220});
      label(host, 30, 150, `高亮区间满足条件：${cond.scalarName} ${cond.atLeast ? '≥' : '<'} ${fmt(threshold, 1)}`, {width: width - 40, bold: true});
      label(host, 30, 170, '管线按前后两帧余量线性反推跨越阈值的精确亚帧时刻，确保淡入淡出起点严格帧率无关', {width: width - 40});
    });
    draw(ctx);
    return {update: draw};
  };

  // ----- 没有专门预览的修改器：示意的一段激活（0.3–1.3 s），权重与参数

  kinds.timeline = (host, ctx) => {
    const width = canvasWidth(host);
    let state = null;
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const m = c.modifiers().find(x => x.path === c.groupPath);
      if (!m) return;
      const scenario = {query: t => t >= 0.3 && t < 1.3, scalars: () => ({}), key: 'window|0.3|1.3'};
      const plain = {...m, conditions: []};
      const T = 2.2;
      const run = simulateModifier(plain, scenario, c.springOmega(m.param), T);
      const full = Math.abs(m.value) > 1e-6 ? m.value : 1;
      const a = area({l: 44, t: 24, w: width - 60, h: 160}, [0, T], [-0.05, 1.1]);
      axes(svg, host, a, {xTicks: niceTicks(0, T, 5), xFormat: x => `${fmt(x, 1)} s`, yTicks: [0, 1], yFormat: y => y ? `${fmt(m.value, 1)}${m.unit}` : '0',
        yTitle: '激活窗口 0.3–1.3 s：线性权重（虚线）与参数弹簧输出（实线）'});
      svgEl(svg, 'path', {d: a.line(run.filter((p, i) => i % 6 === 0).map(p => [p.t, p.weight])), fill: 'none', stroke: C.goldDeep, 'stroke-width': 1, 'stroke-dasharray': '3 3'});
      svgEl(svg, 'path', {d: a.line(run.filter((p, i) => i % 6 === 0).map(p => [p.t, p.value / full])), fill: 'none', stroke: C.goldDeep, 'stroke-width': 2.2});
      state = {run, T, a, full, head: playhead(svg, a, C.ink1)};
    });
    draw(ctx);
    return {
      update: draw,
      frame(time) {
        if (!state) return;
        const t = time % (state.T + 0.4);
        const p = state.run[clamp(Math.round(Math.min(t, state.T) / MODIFIER_DT), 0, state.run.length - 1)];
        state.head.set(p.t, p.value / state.full);
      },
    };
  };

  globalThis.CameraMath = CameraMath;
  globalThis.CameraPreviews = {
    kinds,
    create(kind, host, ctx) {
      const make = kinds[kind];
      return make ? make(host, ctx) : null;
    },
    simulateModifier,
    slideScenario,
  };
})();
