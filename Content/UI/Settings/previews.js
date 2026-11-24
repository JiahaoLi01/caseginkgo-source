// 描述区的预览：按当前的值画（公式同 C++ 的 Control/GinkgoInputMath，ginkgo.settings.input selfcheck 比过）。手柄连着时摇杆现在的位置画在上面，改死区、曲线时可以直接试。
// SettingsMath：说明里的数与预览共用。SettingsPreviews.create(kind, host, c)：在 host 里画一种预览，返回 {update(c), frame(time)}；画图的工具在主题里（Paper.sketch：原地更新）。
(() => {
  const {C, clamp, fmt} = Paper;
  const {paint, svgEl, svgRoot, div, label, area, sample, axes, niceTicks, canvasWidth} = Paper.sketch;

  const SettingsMath = {
    /** 径向死区：偏转的大小 → 过了死区以后的大小 */
    zone: (size, inner, outer) => size <= inner ? 0 : Math.min(1, (size - inner) / Math.max(outer - inner, 1e-8)),
    curve(x, kind, strength) {
      const v = clamp(x, 0, 1);
      if (kind === 'power') return Math.pow(v, strength);
      if (kind === 'scurve') {
        const low = Math.pow(v, strength), high = Math.pow(1 - v, strength);
        return low + high > 0 ? low / (low + high) : v;
      }
      return v;
    },
    adsScale: (alpha, scale, match, zoom) => {
      const target = match && zoom > 1 ? scale / zoom : scale;
      return 1 + (target - 1) * clamp(alpha, 0, 1);
    },
    /** 倍镜（同 C++ 的 Ginkgo::Scope::ScopeLookScale）：开镜到位时 倍镜灵敏度 ÷ 倍率 */
    scopeScale: (alpha, scale, zoom) => 1 + (scale / Math.max(zoom, 1) - 1) * clamp(alpha, 0, 1),
    gain: (speed, strength, offset, cap) => Math.min(1 + strength * Math.max(speed - offset, 0), Math.max(cap, 1)),
    /** 按住满偏 t 秒时转身加速的权重 */
    rampWeight: (t, delay, ramp) => ramp > 0 ? clamp((t - delay) / ramp, 0, 1) : (t >= delay ? 1 : 0),
    /** 转一圈手要移动多少 cm */
    cm360: (sensitivity, dpi) => 360 / (sensitivity * 0.022) / dpi * 2.54,
    /** 辅助瞄准的强度随离中线的距离淡掉（同 C++ 的 Ginkgo::AimAssist::Falloff）：inner 以内 1，outer 以外 0，中间两头缓过 */
    assistFalloff(distance, inner, outer) {
      if (distance <= inner) return 1;
      if (distance >= outer) return 0;
      const t = (distance - inner) / (outer - inner);
      return 1 - t * t * (3 - 2 * t);
    },
    /** 在目标那里量的 cm，distance（cm）外在画面上是多少度（同 C++ 换半径的） */
    assistAngle: (cm, distance) => Math.atan(cm / distance) * 180 / Math.PI,
  };
  const M = SettingsMath;

  /** 摇杆现在的位置（手柄连着、按过键之后才读得到）：{x, y}，y 向上为正；没有时为空 */
  const padStick = which => {
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    for (let i = 0; i < pads.length; ++i) {
      const pad = pads[i];
      if (pad) return which === 'left' ? {x: pad.axes[0], y: -pad.axes[1]} : {x: pad.axes[2], y: -pad.axes[3]};
    }
    return null;
  };

  /** 每帧改的字：没变不写（weft 里写同样的值也算改动） */
  const setText = (node, text) => {
    if (node.__shown === text) return;
    node.__shown = text;
    node.textContent = text;
  };

  const lookStick = c => padStick(c.v('gamepad.swapSticks') ? 'left' : 'right');
  const moveStick = c => padStick(c.v('gamepad.swapSticks') ? 'right' : 'left');

  const readouts = (host, top, pairs) => {
    pairs.forEach(([key, value, warn], index) => {
      label(host, index * 180, top, `${key}  ${value}`, {color: warn ? C.vermilion : C.ink1, width: 176});
    });
  };

  const kinds = {};

  kinds.subtitles = (host, ctx) => {
    host.className += ' gk-caption-preview';
    const render = GinkgoCaption.create(host);
    const line = {name: '守卫', text: '刚刚那是什么声音？'};
    const update = c => render(c.v('accessibility.subtitles') ? line : null, {
      speaker: c.v('accessibility.subtitleSpeaker'),
      size: c.v('accessibility.subtitleSize'),
      backdrop: c.v('accessibility.subtitleBackdrop'),
    });
    update(ctx);
    return {update};
  };

  // ----- 摇杆偏转 → 转速：不开镜实线，开镜金褐的虚线，默认值灰线；死区与外圈以外是阴影

  kinds.lookCurve = (host, ctx) => {
    const width = canvasWidth(host);
    let state = null;
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const inner = c.v('gamepad.lookInner'), outer = c.v('gamepad.lookOuter'), kind = c.v('gamepad.curve'), strength = c.v('gamepad.curveStrength');
      // 讲垂直转速时画竖直的，别的画水平的
      const speedId = c.focusId === 'gamepad.pitchSpeed' ? 'gamepad.pitchSpeed' : 'gamepad.yawSpeed';
      const yaw = c.v(speedId), ads = M.adsScale(1, c.v('gamepad.adsScale'), c.v('gamepad.adsMatchFov'), c.zoom);
      const top = Math.max(yaw, c.d(speedId)) * 1.08;
      const a = area({l: 44, t: 24, w: width - 60, h: 170}, [0, 1], [0, top]);
      svgEl(svg, 'rect', {x: a.X(0), y: a.box.t, width: a.X(inner) - a.X(0), height: a.box.h, fill: C.faint});
      if (outer < 1) svgEl(svg, 'rect', {x: a.X(outer), y: a.box.t, width: a.X(1) - a.X(outer), height: a.box.h, fill: C.faint});
      axes(svg, host, a, {xTicks: [0, 0.25, 0.5, 0.75, 1], xFormat: x => `${fmt(x * 100, 0)}%`, yTicks: [0, Math.round(top / 2 / 10) * 10, Math.round(top * 0.9 / 10) * 10], yFormat: y => `${fmt(y, 0)}°/s`, yTitle: `${speedId === 'gamepad.pitchSpeed' ? '垂直' : '水平'}转速（度 / 秒）`, xTitle: '摇杆偏转'});
      const shape = (x, speed, i, o, k, s) => M.curve(M.zone(x, i, o), k, s) * speed;
      svgEl(svg, 'path', {d: a.line(sample(x => shape(x, c.d(speedId), c.d('gamepad.lookInner'), c.d('gamepad.lookOuter'), c.d('gamepad.curve'), c.d('gamepad.curveStrength')), 0, 1, 80)), fill: 'none', stroke: C.inkFaint, 'stroke-width': 1, 'stroke-dasharray': '3 3'});
      if (Math.abs(ads - 1) > 1e-3) svgEl(svg, 'path', {d: a.line(sample(x => shape(x, yaw * ads, inner, outer, kind, strength), 0, 1, 80)), fill: 'none', stroke: C.goldDeep, 'stroke-width': 1.5, 'stroke-dasharray': '6 3'});
      svgEl(svg, 'path', {d: a.line(sample(x => shape(x, yaw, inner, outer, kind, strength), 0, 1, 120)), fill: 'none', stroke: C.bronze, 'stroke-width': 2.2});
      const dot = svgEl(svg, 'circle', {cx: a.X(0), cy: a.Y(0), r: 4.5, fill: C.vermilion});
      const dotValue = label(host, a.box.l + a.box.w - 200, a.box.t + 2, '', {align: 'right', width: 200, color: C.vermilion, bold: true});
      state = {c, a, dot, dotValue, shape: x => shape(x, yaw, inner, outer, kind, strength)};
    });
    draw(ctx);
    return {
      update: draw,
      frame(time) {
        if (!state) return;
        const stick = lookStick(state.c);
        // 没有手柄时点沿曲线走，看得出形状；手柄连着就是摇杆
        const size = stick ? Math.min(Math.hypot(stick.x, stick.y), 1) : 0.5 - 0.5 * Math.cos(time * 1.2);
        state.dot.setAttribute('cx', state.a.X(size));
        state.dot.setAttribute('cy', state.a.Y(state.shape(size)));
        setText(state.dotValue, stick ? `摇杆 ${fmt(size * 100, 0)}% → ${fmt(state.shape(size), 0)}°/s` : '');
      },
    };
  };

  // ----- 死区：摇杆的圆、内死区与外圈，点是摇杆的位置，右边是偏转 → 输出

  const zonePreview = (innerId, outerId, stickOf, what) => (host, ctx) => {
    const width = canvasWidth(host);
    let state = null;
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const inner = c.v(innerId), outer = c.v(outerId);
      const cx = 110, cy = 118, R = 96;
      svgEl(svg, 'circle', {cx, cy, r: R, fill: C.surface, stroke: C.ink2, 'stroke-width': 1});
      svgEl(svg, 'circle', {cx, cy, r: R * outer, fill: 'none', stroke: C.bronze, 'stroke-width': 1.5, 'stroke-dasharray': '4 3'});
      svgEl(svg, 'circle', {cx, cy, r: R * inner, fill: 'rgba(224, 101, 74, 0.2)', stroke: C.vermilion, 'stroke-width': 1});
      svgEl(svg, 'line', {x1: cx - R, y1: cy, x2: cx + R, y2: cy, stroke: C.grid, 'stroke-width': 1});
      svgEl(svg, 'line', {x1: cx, y1: cy - R, x2: cx, y2: cy + R, stroke: C.grid, 'stroke-width': 1});
      const out = svgEl(svg, 'line', {x1: cx, y1: cy, x2: cx, y2: cy, stroke: C.bronze, 'stroke-width': 2.5, 'stroke-linecap': 'round'});
      const dot = svgEl(svg, 'circle', {cx, cy, r: 5, fill: C.vermilion});
      const a = area({l: 276, t: 24, w: width - 292, h: 170}, [0, 1], [0, 1]);
      axes(svg, host, a, {xTicks: [0, 0.5, 1], xFormat: x => `${fmt(x * 100, 0)}%`, yTicks: [0, 0.5, 1], yFormat: y => `${fmt(y * 100, 0)}%`, yTitle: `${what}的输出`, xTitle: '摇杆偏转'});
      svgEl(svg, 'path', {d: a.line(sample(x => M.zone(x, inner, outer), 0, 1, 80)), fill: 'none', stroke: C.bronze, 'stroke-width': 2.2});
      const mark = svgEl(svg, 'circle', {cx: a.X(0), cy: a.Y(0), r: 4.5, fill: C.vermilion});
      label(host, cx - 100, 6, `红圈内归零（内死区），虚线外满偏`, {width: 210});
      const text = label(host, a.box.l + 8, a.box.t + 2, '', {width: 200, color: C.vermilion, bold: true});
      state = {c, inner, outer, cx, cy, R, out, dot, a, mark, text};
    });
    draw(ctx);
    return {
      update: draw,
      frame(time) {
        if (!state) return;
        const s = state;
        const stick = stickOf(s.c);
        const x = stick ? stick.x : (0.5 - 0.5 * Math.cos(time * 0.9)) * Math.cos(time * 0.35);
        const y = stick ? stick.y : (0.5 - 0.5 * Math.cos(time * 0.9)) * Math.sin(time * 0.35);
        const size = Math.min(Math.hypot(x, y), 1);
        const output = M.zone(size, s.inner, s.outer);
        s.dot.setAttribute('cx', s.cx + x * s.R);
        s.dot.setAttribute('cy', s.cy - y * s.R);
        const scale = size > 1e-6 ? output / size : 0;
        s.out.setAttribute('x2', s.cx + x * scale * s.R);
        s.out.setAttribute('y2', s.cy - y * scale * s.R);
        s.mark.setAttribute('cx', s.a.X(size));
        s.mark.setAttribute('cy', s.a.Y(output));
        setText(s.text, stick ? `${fmt(size * 100, 0)}% → ${fmt(output * 100, 0)}%` : '');
      },
    };
  };

  kinds.zoneLook = zonePreview('gamepad.lookInner', 'gamepad.lookOuter', lookStick, '视角');
  kinds.zoneMove = zonePreview('gamepad.moveInner', 'gamepad.moveOuter', moveStick, '移动');

  // ----- 开镜：同样的动作转多少度、画面上走多远，不开镜与开镜比

  const adsPreview = (scaleId, matchId) => (host, ctx) => {
    const width = canvasWidth(host);
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const degrees = M.adsScale(1, c.v(scaleId), c.v(matchId), c.zoom);
      const screen = degrees * c.zoom;
      const max = Math.max(1, degrees, screen) * 1.15;
      const W = width - 180;
      const bar = (y, name, value, color) => {
        label(host, 0, y + 2, name, {width: 150});
        svgEl(svg, 'rect', {x: 150, y, width: 1, height: 20, fill: C.ink2});
        svgEl(svg, 'rect', {x: 150, y: y + 3, width: Math.max(value / max * W, 1), height: 14, fill: color});
        label(host, 156 + value / max * W, y + 2, `${fmt(value, 2)} 倍`, {width: 70, bold: true});
      };
      bar(20, '腰射基准', 1, C.inkFaint);
      bar(66, '开镜 · 实际转角', degrees, C.bronze);
      bar(112, '开镜 · 屏幕像移', screen, C.goldDeep);
      label(host, 0, 160, `当前武器视野放大 ${fmt(c.zoom, 2)} 倍：屏幕像移 = 实际转角 × ${fmt(c.zoom, 2)}。屏幕像移为 1.00 倍时，准星附近的画面移动速度与腰射一致。`, {width: width - 8, wrap: true});
    });
    draw(ctx);
    return {update: draw};
  };
  kinds.adsPad = adsPreview('gamepad.adsScale', 'gamepad.adsMatchFov');
  kinds.adsMouse = adsPreview('mouse.adsScale', 'mouse.adsMatchFov');

  // ----- 倍镜：只有镜内放大，转的角度是 倍率 ÷ 放大，镜内的像走的就是倍率

  const scopePreview = scaleId => (host, ctx) => {
    const width = canvasWidth(host);
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const zoom = c.scopeZoom;
      const degrees = M.scopeScale(1, c.v(scaleId), zoom);
      const screen = degrees * zoom;
      const max = Math.max(1, degrees, screen) * 1.15;
      const W = width - 180;
      const bar = (y, name, value, color) => {
        label(host, 0, y + 2, name, {width: 150});
        svgEl(svg, 'rect', {x: 150, y, width: 1, height: 20, fill: C.ink2});
        svgEl(svg, 'rect', {x: 150, y: y + 3, width: Math.max(value / max * W, 1), height: 14, fill: color});
        label(host, 156 + value / max * W, y + 2, `${fmt(value, 2)} 倍`, {width: 70, bold: true});
      };
      bar(20, '腰射基准', 1, C.inkFaint);
      bar(66, '倍镜 · 实际转角', degrees, C.bronze);
      bar(112, '倍镜 · 镜内像移', screen, C.goldDeep);
      label(host, 0, 160, `瞄具光学放大 ${fmt(zoom, 0)} 倍（镜外视野不变）：镜内像移 = 实际转角 × ${fmt(zoom, 0)}。镜内像移为 1.00 倍时，分划内目标移动速度与腰射一致。`, {width: width - 8, wrap: true});
    });
    draw(ctx);
    return {update: draw};
  };
  kinds.scopePad = scopePreview('gamepad.scopeScale');
  kinds.scopeMouse = scopePreview('mouse.scopeScale');

  // ----- 方向：设备往哪边，视角往哪边

  const invertPreview = (pitchId, yawId, device) => (host, ctx) => {
    const width = canvasWidth(host);
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const row = (y, name, inverted, vertical) => {
        label(host, 0, y + 22, name, {width: 110});
        const arrow = (cx, flip, color) => {
          const s = flip ? -1 : 1;
          const d = vertical ? `M${cx} ${y + 30 + 24 * s}L${cx} ${y + 30 - 24 * s}M${cx - 9} ${y + 30 - 12 * s}L${cx} ${y + 30 - 24 * s}L${cx + 9} ${y + 30 - 12 * s}`
            : `M${cx - 24 * s} ${y + 30}L${cx + 24 * s} ${y + 30}M${cx + 12 * s} ${y + 21}L${cx + 24 * s} ${y + 30}L${cx + 12 * s} ${y + 39}`;
          svgEl(svg, 'path', {d, fill: 'none', stroke: color, 'stroke-width': 2.4, 'stroke-linecap': 'round', 'stroke-linejoin': 'round'});
        };
        arrow(170, false, C.ink2);
        label(host, 138, y + 62, device, {align: 'center', width: 80});
        label(host, 240, y + 22, '→', {width: 30});
        arrow(320, inverted, inverted ? C.vermilion : C.bronze);
        label(host, 288, y + 62, '视角', {align: 'center', width: 80});
        label(host, 372, y + 22, inverted ? '反转' : '不反转', {width: 80, color: inverted ? C.vermilion : C.ink2, bold: inverted});
      };
      row(10, '上下', c.v(pitchId), true);
      row(120, '左右', c.v(yawId), false);
    });
    draw(ctx);
    return {update: draw};
  };
  kinds.invertPad = invertPreview('gamepad.invertPitch', 'gamepad.invertYaw', '摇杆');
  kinds.invertMouse = invertPreview('mouse.invertPitch', 'mouse.invertYaw', '鼠标');

  // ----- 摇杆互换

  kinds.swap = (host, ctx) => {
    const width = canvasWidth(host);
    let state = null;
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const swapped = c.v('gamepad.swapSticks');
      const stick = (cx, name, role, color) => {
        svgEl(svg, 'circle', {cx, cy: 100, r: 60, fill: C.surface, stroke: C.ink2, 'stroke-width': 1});
        label(host, cx, 170, name, {align: 'center', width: 120});
        label(host, cx, 92, role, {align: 'center', width: 100, color, bold: true});
        return svgEl(svg, 'circle', {cx, cy: 100, r: 5, fill: C.vermilion});
      };
      const left = stick(120, '左摇杆', swapped ? '转视角' : '移动', swapped ? C.vermilion : C.bronze);
      const right = stick(330, '右摇杆', swapped ? '移动' : '转视角', swapped ? C.bronze : C.vermilion);
      state = {left, right};
    });
    draw(ctx);
    return {
      update: draw,
      frame() {
        if (!state) return;
        const l = padStick('left'), r = padStick('right');
        if (l) {
          state.left.setAttribute('cx', 120 + l.x * 60);
          state.left.setAttribute('cy', 100 - l.y * 60);
        }
        if (r) {
          state.right.setAttribute('cx', 330 + r.x * 60);
          state.right.setAttribute('cy', 100 - r.y * 60);
        }
      },
    };
  };

  // ----- 转身加速：推到底并按住时转速随时间

  kinds.turn = (host, ctx) => {
    const width = canvasWidth(host);
    let state = null;
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const yaw = c.v('gamepad.yawSpeed'), extra = c.v('gamepad.extraYaw'), delay = c.v('gamepad.extraDelay'), ramp = c.v('gamepad.extraRamp');
      const T = Math.max(1.2, (delay + ramp) * 1.5);
      const speed = t => yaw + extra * M.rampWeight(t, delay, ramp);
      const a = area({l: 44, t: 24, w: width - 60, h: 170}, [0, T], [0, Math.max(yaw + extra, c.d('gamepad.yawSpeed')) * 1.1]);
      axes(svg, host, a, {xTicks: [0, T / 2, T], xFormat: x => `${fmt(x, 2)} s`, yTicks: [yaw, yaw + extra], yFormat: y => `${fmt(y, 0)}°/s`, yTitle: '水平转速', xTitle: '按住的时间'});
      if (extra > 0) {
        svgEl(svg, 'line', {x1: a.X(delay), y1: a.box.t, x2: a.X(delay), y2: a.box.t + a.box.h, stroke: C.grid, 'stroke-width': 1, 'stroke-dasharray': '3 3'});
        svgEl(svg, 'line', {x1: a.X(delay + ramp), y1: a.box.t, x2: a.X(delay + ramp), y2: a.box.t + a.box.h, stroke: C.grid, 'stroke-width': 1, 'stroke-dasharray': '3 3'});
      }
      svgEl(svg, 'path', {d: a.line(sample(speed, 0, T, 120)), fill: 'none', stroke: C.bronze, 'stroke-width': 2.2});
      const dot = svgEl(svg, 'circle', {cx: a.X(0), cy: a.Y(yaw), r: 4.5, fill: C.vermilion});
      // 转半圈、转一圈要多久（数值积分，不用很细）
      const reach = target => {
        let turned = 0;
        for (let t = 0; t < 8; t += 0.005) {
          turned += speed(t) * 0.005;
          if (turned >= target) return t;
        }
        return 8;
      };
      const text = `转半圈 ${fmt(reach(180), 2)} s，转一圈 ${fmt(reach(360), 2)} s`;
      label(host, a.box.l + a.box.w, a.box.t + a.box.h - 22, text, {align: 'right', width: 300, bold: true});
      state = {a, speed, T, dot};
    });
    draw(ctx);
    return {
      update: draw,
      frame(time) {
        if (!state) return;
        const t = (time % (state.T + 0.5));
        const at = Math.min(t, state.T);
        state.dot.setAttribute('cx', state.a.X(at));
        state.dot.setAttribute('cy', state.a.Y(state.speed(at)));
      },
    };
  };

  // ----- 鼠标的灵敏度：转一圈手要移动多远

  kinds.mouseSens = (host, ctx) => {
    const width = canvasWidth(host);
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const sens = c.v('mouse.sensitivity');
      const W = width - 176;
      const dpis = [400, 800, 1600];
      const max = Math.max(...dpis.map(d => M.cm360(Math.min(sens, c.d('mouse.sensitivity')), d))) * 1.1;
      dpis.forEach((dpi, index) => {
        const y = 22 + index * 42;
        const cm = M.cm360(sens, dpi);
        const fallback = M.cm360(c.d('mouse.sensitivity'), dpi);
        label(host, 0, y + 2, `${dpi} DPI`, {width: 80});
        svgEl(svg, 'rect', {x: 80, y: y + 3, width: Math.max(cm / max * W, 1), height: 14, fill: C.bronze});
        svgEl(svg, 'rect', {x: 80 + fallback / max * W, y: y - 2, width: 1, height: 24, fill: C.ink1});
        label(host, 86 + cm / max * W, y + 2, `${fmt(cm, 1)} cm`, {width: 80, bold: true});
      });
      label(host, 0, 156, `水平旋转一周（360°）所需的手部平移距离（竖线为默认值）。每 count 水平偏转 ${fmt(sens * 0.022, 3)}°，垂直偏转 ${fmt(sens * 0.022 * c.v('mouse.pitchRatio'), 3)}°。`, {width: width - 8, wrap: true});
    });
    draw(ctx);
    return {update: draw};
  };

  // ----- 鼠标加速度：速度 → 倍率

  kinds.mouseAccel = (host, ctx) => {
    const width = canvasWidth(host);
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const on = c.v('mouse.accelEnabled');
      const strength = c.v('mouse.accelStrength'), offset = c.v('mouse.accelOffset'), cap = c.v('mouse.accelCap');
      const a = area({l: 44, t: 24, w: width - 60, h: 170}, [0, 60], [0.8, Math.max(cap, 2) * 1.08]);
      axes(svg, host, a, {xTicks: [0, 15, 30, 45, 60], xFormat: x => `${fmt(x, 0)}`, yTicks: [1, cap], yFormat: y => `${fmt(y, 1)}×`, yTitle: '灵敏度增益倍率', xTitle: '鼠标瞬时速率（count/ms）'});
      const curve = v => on ? M.gain(v, strength, offset, cap) : 1;
      svgEl(svg, 'path', {d: a.line(sample(curve, 0, 60, 120)), fill: 'none', stroke: on ? C.bronze : C.inkFaint, 'stroke-width': 2.2});
      label(host, a.box.l + a.box.w, a.box.t + a.box.h - 22, on ? `800 DPI 下：10 cm/s ${fmt(curve(10 / 2.54 * 800 / 1000), 2)}×，50 cm/s ${fmt(curve(50 / 2.54 * 800 / 1000), 2)}×，1 m/s ${fmt(curve(100 / 2.54 * 800 / 1000), 2)}×` : '未启用：任意移速下均保持 1.00× 恒定倍率',
        {align: 'right', width: 420, bold: true});
    });
    draw(ctx);
    return {update: draw};
  };

  // ----- 辅助瞄准：离目标中线多远（cm，在目标那里量）→ 强度；下面一条是准星匀速扫过 10 m 外的靶子，有辅助（朱红）与没有（灰圈）

  kinds.aimAssist = (host, ctx) => {
    const width = canvasWidth(host);
    const DISTANCE = 1000;      // 10 m 外（cm）
    const SWEEP = 28.125;       // 扫过去的转速（度 / 秒），同 aim_assist.py 扫靶的
    const STEP = 1 / 240;
    let state = null;
    const draw = c => paint(host, () => {
      const dir = String(c.focusId).split('.')[0];
      const id = name => `${dir}.aimAssist${name}`;
      // 讲开镜强度时画开镜的，别的画不开镜的
      const which = c.focusId === id('Ads') ? 'Ads' : 'Hip';
      const deg = cm => M.assistAngle(cm, DISTANCE);
      const inner = c.v(id('Inner')), outer = Math.max(c.v(id('Outer')), inner);
      const defInner = c.d(id('Inner')), defOuter = Math.max(c.d(id('Outer')), defInner);
      const strength = c.v(id(which));
      const enabled = !!c.v(id('Enabled'));
      const reach = enabled && c.v(id('Range')) * 100 >= DISTANCE;
      // 同 C++：强度在角度里淡，所以按 10 m 外换成角度算；横轴还是 cm
      const k = (cm, s, i, o) => (reach ? s * M.assistFalloff(deg(cm), deg(i), deg(o)) : 0);
      const X = Math.max(outer, defOuter, 40) * 1.15;
      const svg = svgRoot(host, width, 240);
      const a = area({l: 44, t: 24, w: width - 60, h: 100}, [0, X], [0, 1]);
      // 半个身体宽（约 20 cm）
      svgEl(svg, 'rect', {x: a.X(0), y: a.box.t, width: a.X(Math.min(20, X)) - a.X(0), height: a.box.h, fill: C.faint});
      axes(svg, host, a, {xTicks: niceTicks(0, X, 4), xFormat: x => fmt(x, 0), yTicks: [0, 0.5, 1], yFormat: y => `${fmt(y * 100, 0)}%`, yTitle: `${which === 'Ads' ? '开镜' : '腰射'}阻尼强度`, xTitle: '距躯干中线（cm）'});
      svgEl(svg, 'path', {d: a.line(sample(x => k(x, c.d(id(which)), defInner, defOuter), 0, X, 120)), fill: 'none', stroke: C.inkFaint, 'stroke-width': 1, 'stroke-dasharray': '3 3'});
      svgEl(svg, 'path', {d: a.line(sample(x => k(x, strength, inner, outer), 0, X, 160)), fill: 'none', stroke: C.bronze, 'stroke-width': 2.2});
      label(host, a.X(0) + 4, a.box.t + a.box.h - 18, '身体', {width: 60, color: C.ink2});
      const note = !enabled ? '辅助瞄准已关闭' : reach ? `10 m 处张角：满强度 ±${fmt(deg(inner), 1)}°，淡出外沿 ±${fmt(deg(outer), 1)}°` : `最远作用距离 ${fmt(c.v(id('Range')), 0)} m（小于 10 m，超出范围）`;
      label(host, a.box.l + a.box.w, a.box.t + 2, note, {align: 'right', width: 320, color: reach ? C.ink1 : C.vermilion, bold: !reach});

      // 扫靶：角度里积分（同 C++：靠近时不修，过了中线往外的速度乘 1 − k），画的时候换回 cm
      const sx = cm => a.box.l + (cm + X) / (2 * X) * a.box.w;
      const toX = theta => sx(DISTANCE * Math.tan(theta * Math.PI / 180));
      const thetaMax = deg(X);
      const kAt = theta => (theta > 0 && reach ? strength * M.assistFalloff(theta, deg(inner), deg(outer)) : 0);
      const path = [];
      for (let theta = -thetaMax; theta < thetaMax && path.length * STEP < 8;) {
        path.push(theta);
        theta += SWEEP * (1 - kAt(theta)) * STEP;
      }
      path.push(thetaMax);
      const plainTime = 2 * thetaMax / SWEEP;
      const assistedTime = (path.length - 1) * STEP;
      const y0 = 178;
      svgEl(svg, 'line', {x1: sx(-X), y1: y0, x2: sx(X), y2: y0, stroke: C.grid, 'stroke-width': 1});
      svgEl(svg, 'rect', {x: sx(-20), y: y0 - 15, width: sx(20) - sx(-20), height: 30, fill: C.faint});
      svgEl(svg, 'line', {x1: sx(0), y1: y0 - 17, x2: sx(0), y2: y0 + 17, stroke: C.ink2, 'stroke-width': 1});
      const plain = svgEl(svg, 'circle', {cx: sx(-X), cy: y0, r: 4.5, fill: 'none', stroke: C.inkFaint, 'stroke-width': 1.5});
      const assisted = svgEl(svg, 'circle', {cx: sx(-X), cy: y0, r: 4.5, fill: C.vermilion});
      const extra = assistedTime - plainTime;
      label(host, a.box.l, 202, `准星以 ${fmt(SWEEP, 0)}°/s 匀速扫过 10 m 处目标：朱红点有辅助，灰圈无辅助。${extra > 0.005 ? `通过目标窗口时额外驻留 ${fmt(extra, 2)} s${path.length * STEP >= 8 ? ' 以上' : ''}。` : ''}`,
        {width: a.box.w, wrap: true});
      state = {path, plainTime, thetaMax, toX, plain, assisted, period: Math.max(assistedTime, plainTime) + 0.8};
    });
    draw(ctx);
    return {
      update: draw,
      frame(time) {
        if (!state) return;
        const s = state;
        const t = time % s.period;
        s.plain.setAttribute('cx', s.toX(Math.min(-s.thetaMax + SWEEP * t, s.thetaMax)));
        s.assisted.setAttribute('cx', s.toX(s.path[Math.min(Math.floor(t / STEP), s.path.length - 1)]));
      },
    };
  };

  // ----- 按住与切换：两个键位各按两下，下面是动作开着没有（同 C++ 的 FGinkgoHoldActions：按住的键位按着算开，切换的按一下翻一下，任何一个开着就开）

  kinds.holdKeys = (host, ctx) => {
    const width = canvasWidth(host);
    const draw = c => paint(host, () => {
      const svg = svgRoot(host, width, 240);
      const W = width - 140;
      const read = (slot, presses) => {
        const key = c.v(`${c.focusId}.${slot}`);
        return {key, mode: c.v(`${c.focusId}.${slot}.mode`), presses: key ? presses : []};
      };
      const keys = [read('primary', [[0.05, 0.22], [0.58, 0.72]]), read('secondary', [[0.32, 0.38], [0.84, 0.9]])];
      const isOn = t => {
        const held = keys.some(k => k.mode === 'hold' && k.presses.some(([s, e]) => t >= s && t < e));
        const flips = keys.reduce((count, k) => count + (k.mode === 'toggle' ? k.presses.filter(([s]) => t >= s).length : 0), 0);
        return held || flips % 2 === 1;
      };
      const spans = [];
      for (let i = 0, start = -1; i <= 400; ++i) {
        const on = i < 400 && isOn(i / 400);
        if (on && start < 0) start = i;
        if (!on && start >= 0) {
          spans.push([start / 400, i / 400]);
          start = -1;
        }
      }
      const track = (y, name, sub, list, color, bold) => {
        label(host, 0, y - 6, name, {width: 134, bold, color: bold ? C.ink1 : C.ink2});
        if (sub) label(host, 0, y + 10, sub, {width: 134, color: C.ink2});
        svgEl(svg, 'line', {x1: 136, y1: y + 20, x2: 136 + W, y2: y + 20, stroke: C.ink2, 'stroke-width': 1});
        for (const [s, e] of list) svgEl(svg, 'rect', {x: 136 + s * W, y: y + 2, width: (e - s) * W, height: 18, fill: color});
      };
      const name = k => (k.key ? c.keyName(k.key) : '未绑定');
      track(24, name(keys[0]), keys[0].key ? c.optionName(keys[0].mode) : '', keys[0].presses, C.inkFaint, false);
      track(84, name(keys[1]), keys[1].key ? c.optionName(keys[1].mode) : '', keys[1].presses, C.inkFaint, false);
      track(144, '动作状态', '是否激活', spans, C.bronze, true);
      label(host, 0, 190, '上方两行为主键与辅助键的按下区间（各触发两次）；下方为逻辑或合成后的动作有效时段。', {width: width - 8, wrap: true});
    });
    draw(ctx);
    return {update: draw};
  };

  // ----- 键位一览：这个目录的所有动作，讲到的那一行标出来

  kinds.keyList = (host, ctx) => {
    const width = canvasWidth(host);
    const draw = c => paint(host, () => {
      const directory = SettingsCatalog.DIRECTORIES.find(d => d.id === String(c.focusId).split('.')[0]);
      const ids = directory.categories.find(category => category.keys).items;
      const step = 25;
      // 画布默认 240 高，这张的行数按目录变
      const height = 22 + ids.length * step + 4;
      host.style.height = `${height}px`;
      const svg = svgRoot(host, width, height);
      const slotText = (id, slot) => {
        const key = c.v(`${id}.${slot}`);
        const modeId = `${id}.${slot}.mode`;
        return key && c.has(modeId) ? `${c.keyName(key)} · ${c.optionName(c.v(modeId))}` : c.keyName(key);
      };
      label(host, 110, 0, '主键', {width: 160});
      label(host, 280, 0, '辅助键', {width: 160});
      ids.forEach((id, index) => {
        const y = 22 + index * step;
        const on = id === c.focusId;
        svgEl(svg, 'rect', {x: 0, y: y - 3, width, height: step - 2, fill: on ? C.rowHighlight : 'rgba(0, 0, 0, 0)'});
        label(host, 6, y, SettingsCatalog.ITEMS[id].label, {width: 100, bold: on, color: C.ink1});
        label(host, 110, y, slotText(id, 'primary'), {width: 164, bold: on, color: c.v(`${id}.primary`) ? C.ink1 : C.ink2});
        label(host, 280, y, slotText(id, 'secondary'), {width: 164, bold: on, color: c.v(`${id}.secondary`) ? C.ink1 : C.ink2});
      });
    });
    draw(ctx);
    return {update: draw};
  };

  globalThis.SettingsMath = SettingsMath;
  globalThis.SettingsPreviews = {
    create(kind, host, c) {
      const make = kinds[kind];
      return make ? make(host, c) : null;
    },
  };
})();
