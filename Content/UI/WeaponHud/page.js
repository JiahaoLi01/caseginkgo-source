// 武器的界面（Docs/Design/Weapons.md「界面：武器栏与准星」）：右下角的武器栏与画面中间的准星。样子照《最后生还者2》：半透明、小。
//
// 游戏在武器栏变了时发一条事件 "weapon"（C++ 的 FGinkgoWeaponHud），值是
//   {lift, weapon: false}：没有枪；或
//   {lift, weapon: true, name, caliber, icon, attachments, reticle, inHand, raised,
//    mechanism: {feed, capacity, detachable, magazineIn, chamber, action, reserve}}（没有机构的枪不带 mechanism）
//   lift：窗口时间轴开着时武器栏往上挪多少（px，挪到时间轴上面），关着时为 0；
//   name：游戏里的名字；caliber：口径（写在膛里那一发旁边）；icon：枪的图的名字（page.css 里 .wh-icon-<名字> 指两张图）；
//   attachments：当前显示的枪上，哪个锚点挂哪张配件图，如 {muzzle: 'AK47Suppressor'}；
//   reticle：开镜准星的名字（RETICLES 里的一项，空的开镜时也是点）；inHand：枪在手上（否则在背上，武器栏最淡）；
//   raised：枪抬起来了（腰射、开镜），武器栏亮起来；
//   feed、capacity：供弹里几发、最多几发；chamber：empty、live、spent；action：closed、open；reserve：这把枪的弹种的后备；
//   openBolt：开膛待击（MG3：膛空、枪机在后面是常态，膛那里画弹链上的下一发，不写"栓开"）；reloadable：能装填（弹链不能，不写后备）。
//   弹种（Docs/Design/Weapons.md「切换弹种」）：ammo 是这把枪的几种弹（同一口径，枪的弹种第一）[{short, label, round, special}]，
//   mechanism 里的 rounds（供弹从下往上每一发是第几种）、chamberAmmo（膛里那一发，空膛 -1）、nextAmmo（下一枪）、load（装填压的那种）都是它的下标；
//   reserve 是 load 那种的后备。特殊弹（special，不是枪的弹种）的格子、膛里那一发、后备按特殊弹的颜色。
// 子弹的状态变了以后武器栏也亮 PULSE_MS（页面自己的计时）。
// 打中了可打击的目标，每一枪发一条 "confirm"（Docs/Design/Damage.md「命中的白 X」）：
//   {serial, hits, kill, head, armor, armorBroken, health, armorDamage}
//   hits：这一枪打中几下（霰弹几颗弹丸）；kill：打死了；head：有一下打在头上；armor：有一下打在护甲上；armorBroken：有一下把护甲打穿了；
//   health、armorDamage：这一枪一共扣的血、护甲。
// 准星的位置与大小走锚点：1 是不开镜的点，2 是开镜准星中间的竖柱，3 是红 X，4、5 是两边的横线，6、7 是开镜准星的两根横柱，8 是命中的白 X（准星的中心），
// 9 是弹种轮盘（屏幕中心，"wheel" 见最后一节），10 是 MG3 的扩散圈。
// 缩放由游戏按开镜进度给（点收拢、准星张开，同相机拉近），横线与横柱张开多少是扩散，都由游戏给，页面不做这些动画。
// 没有框架，直接写 DOM。weft 的 SVG 不认 viewBox，坐标就是 px。
(() => {
  const engine = weft.engine;
  const root = document.getElementById('root');
  const SVG = 'http://www.w3.org/2000/svg';
  const PULSE_MS = 2500;

  const add = (parent, tag, cls) => {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    parent.appendChild(e);
    return e;
  };

  const addSvg = (parent, tag, attributes) => {
    const e = document.createElementNS(SVG, tag);
    for (const [name, value] of Object.entries(attributes)) e.setAttribute(name, value);
    parent.appendChild(e);
    return e;
  };

  // ===== 准星 =====

  // 红 X：枪口那条线这一帧打到的地方，扩散的圆锥的中心（准星画的是预期方向），两者对不上才有。14 × 14，中心对着锚点，差得越多越大（游戏给锚点的缩放）
  const hit = add(root, 'div', 'wh-hit');
  hit.setAttribute('data-anchor', '3');
  addSvg(addSvg(hit, 'svg', {width: 14, height: 14}), 'path', {d: 'M1.4 2.6L2.6 1.4L7 5.8L11.4 1.4L12.6 2.6L8.2 7L12.6 11.4L11.4 12.6L7 8.2L2.6 12.6L1.4 11.4L5.8 7Z'});

  const dot = add(root, 'div', 'wh-dot');
  dot.setAttribute('data-anchor', '1');
  add(dot, 'i');

  const spreadRing = add(root, 'div', 'wh-spread-ring');
  spreadRing.setAttribute('data-anchor', '10');
  const ringDot = add(root, 'div', 'wh-dot');
  ringDot.setAttribute('data-anchor', '10');
  ringDot.setAttribute('data-anchor-scale', 'off');
  add(ringDot, 'i');

  // 命中的白 X（照 COD）：准星的中心，四条斜线、中间留空。40 × 40，中心对着锚点 8。
  // 弹出来（大一点再缩回）、停一下、淡掉；前一个还看得见时又打中，一次比一次大一点。打在护甲上偏蓝，护甲被打穿那一下外面另有一圈往外扩、一闪；
  // 爆头线粗一点。打死是红的，画在自己的一块上（下面的 KILL）。动画由页面按自己的时钟每帧算，只动 transform 与 opacity
  const CONFIRM = {
    inner: 6, outer: 13, width: 2, headWidth: 3,            // 斜线从中心多远到多远、多宽（px）
    ringInner: 15.5, ringOuter: 19, ringWidth: 1.6,         // 护甲打穿的那一圈（缩放前），从 1.2 倍扩到 1.6 倍
    pop: 60, popScale: 1.45,                                // 弹出：从 1.45 倍缩回，60 ms
    hold: 110, holdArmor: 130,                              // 停多久（ms）
    fade: 170,                                              // 淡掉多久（ms），淡的时候缩到 0.92 倍
    stackStep: 0.07, stackMax: 4,                           // 挨得近的命中：每次大 7%，最多四次
    pelletStep: 0.025, pelletMax: 8,                        // 霰弹多打中一颗大 2.5%，最多八颗
    ring: 240,                                              // 那一圈往外扩、淡掉的时间（ms）
  };
  // 打死的 X（你说的，2026-11-08：加一个缩放动画、一个透明度动画，起手是现有的 X 的两倍，不要再大；Docs/Design/Damage.md「打死的确认」）。
  // 红的，停着的大小同原来；画在自己的一块上，后面的命中盖不掉
  const KILL = {
    scale: 1.3,                                             // 停着时比普通的 X 大多少（同原来）
    fromScale: 2, fromOpacity: 0.25, in: 120,               // 进场：起手是停着时的两倍大、25% 不透明，这么久（ms）缩回来、变不透明（先快后慢）
    hold: 320, fade: 260,                                   // 同原来：停多久、淡掉多久（ms），淡的时候缩到 0.92 倍
  };
  const confirm = add(root, 'div', 'wh-confirm');
  confirm.setAttribute('data-anchor', '8');
  // 那一圈画在斜线下面，从斜线弹出时的外端往外扩：两者不叠
  const confirmRing = add(confirm, 'div', 'wh-confirm-ring');
  const confirmX = add(confirm, 'div', 'wh-confirm-x');
  // 一条斜线是细长的四边形（不靠描边画线）：沿对角线从中心 inner 到 outer，宽 width，四个方向各一条
  const confirmBars = (inner, outer, width) => [[1, 1], [1, -1], [-1, 1], [-1, -1]].map(([sx, sy]) => {
    const ux = sx * Math.SQRT1_2, uy = sy * Math.SQRT1_2;
    const px = -uy * width / 2, py = ux * width / 2;
    const at = (r, side) => `${(20 + ux * r + side * px).toFixed(2)} ${(20 + uy * r + side * py).toFixed(2)}`;
    return `M${at(inner, 1)}L${at(outer, 1)}L${at(outer, -1)}L${at(inner, -1)}Z`;
  }).join('');
  const confirmShapes = (parent, d) => {
    const svg = addSvg(parent, 'svg', {width: 40, height: 40});
    return [addSvg(svg, 'path', {class: 'shade', d}), addSvg(svg, 'path', {class: 'white', d})];
  };
  const confirmPaths = confirmShapes(confirmX, confirmBars(CONFIRM.inner, CONFIRM.outer, CONFIRM.width));
  confirmShapes(confirmRing, confirmBars(CONFIRM.ringInner, CONFIRM.ringOuter, CONFIRM.ringWidth));

  const killX = add(confirm, 'div', 'wh-confirm-x wh-kill-x');
  const killPaths = confirmShapes(killX, confirmBars(CONFIRM.inner, CONFIRM.outer, CONFIRM.width));
  let killShot = null;      // 正在放的这一下：{start, base}
  let killFrame = 0;
  let killHead = false;

  let confirmHead = false;
  let confirmShot = null;   // 正在放的这一下：{start, base, hold, fade, ring, x}
  let confirmStack = 0;
  let confirmFrame = 0;

  const easeOut = t => 1 - (1 - t) * (1 - t) * (1 - t);
  const setLook = (element, scale, opacity) => {
    const transform = `scale(${scale.toFixed(3)})`;
    const alpha = opacity.toFixed(3);
    if (element.style.transform !== transform) element.style.transform = transform;
    if (element.style.opacity !== alpha) element.style.opacity = alpha;
  };
  const confirmLength = shot => CONFIRM.pop + shot.hold + shot.fade;

  const stepConfirm = () => {
    confirmFrame = 0;
    const shot = confirmShot;
    if (!shot) return;
    const t = performance.now() - shot.start;
    let scale = shot.base;
    let opacity = 1;
    if (t < CONFIRM.pop) {
      scale = shot.base * (CONFIRM.popScale + (1 - CONFIRM.popScale) * easeOut(Math.max(t, 0) / CONFIRM.pop));
    } else if (t > CONFIRM.pop + shot.hold) {
      const f = Math.min((t - CONFIRM.pop - shot.hold) / shot.fade, 1);
      opacity = 1 - f;
      scale = shot.base * (1 - 0.08 * f);
    }
    if (shot.x) setLook(confirmX, scale, opacity);
    else setLook(confirmX, 1, 0);
    const ringDone = !shot.ring || t >= CONFIRM.ring;
    if (ringDone) {
      setLook(confirmRing, 1, 0);
    } else {
      const f = Math.max(t, 0) / CONFIRM.ring;
      setLook(confirmRing, shot.base * (1.2 + 0.4 * easeOut(f)), 1 - f);
    }
    if (t >= confirmLength(shot) && ringDone) {
      confirmShot = null;
      confirmStack = 0;
      setLook(confirmX, 1, 0);
      return;
    }
    confirmFrame = requestAnimationFrame(stepConfirm);
  };

  const stepKill = () => {
    killFrame = 0;
    const shot = killShot;
    if (!shot) return;
    const t = Math.max(performance.now() - shot.start, 0);
    let scale = 1;
    let opacity = 1;
    if (t < KILL.in) {
      const f = easeOut(t / KILL.in);
      scale = KILL.fromScale + (1 - KILL.fromScale) * f;
      opacity = KILL.fromOpacity + (1 - KILL.fromOpacity) * f;
    } else if (t > KILL.in + KILL.hold) {
      const f = Math.min((t - KILL.in - KILL.hold) / KILL.fade, 1);
      opacity = 1 - f;
      scale = 1 - 0.08 * f;
    }
    setLook(killX, shot.base * KILL.scale * scale, opacity);
    if (t >= KILL.in + KILL.hold + KILL.fade) {
      killShot = null;
      setLook(killX, 1, 0);
      return;
    }
    killFrame = requestAnimationFrame(stepKill);
  };

  const startKill = (now, base, head) => {
    killShot = {start: now, base};
    if (killHead !== head) {
      killHead = head;
      const d = confirmBars(CONFIRM.inner, CONFIRM.outer, head ? CONFIRM.headWidth : CONFIRM.width);
      for (const path of killPaths) path.setAttribute('d', d);
    }
    if (!killFrame) stepKill();
  };

  engine.on('confirm', message => {
    const now = performance.now();
    const showing = confirmShot && now - confirmShot.start < confirmLength(confirmShot);
    confirmStack = showing ? Math.min(confirmStack + 1, CONFIRM.stackMax) : 0;
    const pellets = Math.min(Math.max((message.hits || 1) - 1, 0), CONFIRM.pelletMax);
    const base = (1 + CONFIRM.stackStep * confirmStack) * (1 + CONFIRM.pelletStep * pellets);
    if (message.kill) startKill(now, base, !!message.head);
    confirmShot = {
      start: now,
      base,
      hold: message.armor ? CONFIRM.holdArmor : CONFIRM.hold,
      fade: CONFIRM.fade,
      ring: !!message.armorBroken,
      x: !message.kill,     // 打死的那一下画红 X，普通的 X 让开（护甲打穿的那一圈照常）
    };
    // 颜色：打在护甲上的蓝，别的白；爆头另加粗
    const className = message.armor && !message.kill ? 'wh-confirm wh-confirm-armor' : 'wh-confirm';
    if (confirm.className !== className) confirm.className = className;
    if (confirmHead !== !!message.head) {
      confirmHead = !!message.head;
      const d = confirmBars(CONFIRM.inner, CONFIRM.outer, confirmHead ? CONFIRM.headWidth : CONFIRM.width);
      for (const path of confirmPaths) path.setAttribute('d', d);
    }
    if (!confirmFrame) stepConfirm();
  });

  // 准星两边的横线（"- · -"）：枪配了才有（CrosshairGap）。内端画在扩散的圆锥上，子弹落在两根横线之间。框的中心是内端，离点多远由游戏给锚点的位置
  for (const [key, side] of [['4', 'left'], ['5', 'right']]) {
    const dash = add(root, 'div', `wh-dash wh-dash-${side}`);
    dash.setAttribute('data-anchor', key);
    add(dash, 'i');
  }

  // 开镜准星，按名字画，40 × 40，中心对着锚点。每一项分三块：中间的（不动）和左右两块（圆锥比它们之间的空隙大时张开，反馈扩散，空隙的一半写在枪的 AdsReticleGap）。
  // 每一块是一组形：深色的细边（描边往外 1 px）先画，白色后画
  const RETICLES = {
    // ZF39 的 Absehen 1：左右两根横柱、下面一根竖柱，竖柱的尖就是瞄准点，与横柱同高。
    // 蔡司 Ziel 4/S 的表：两根横柱之间空 70 cm/100 m（7 mrad），横柱与竖柱遮住 20 cm/100 m（2 mrad），空隙是柱宽的 3.5 倍。
    // 缩小版照这个比例：柱宽 2.5 px、空隙 9 px；横柱各 15.5 px，竖柱 20 px，最上面 4 px 收成尖，一共 40 px。
    // 1080p 下比过：柱宽 2 px 时尖被晕冲淡、看不出是尖；尖长 6 px 时靠近中心的几个像素不到 1 px 宽，也淡；4 px 实，仍看得出收尖
    ZF39: {
      center: [['path', {d: 'M20 20L21.25 24V40H18.75V24Z'}]],
      left: [['rect', {x: 0, y: 18.75, width: 15.5, height: 2.5}]],
      right: [['rect', {x: 24.5, y: 18.75, width: 15.5, height: 2.5}]],
    },
    // 霰弹枪的 "( · )"（你说的）：中间一个点，两边两段弧。弧是以中心为心、半径 12 的圆上左右各 70° 的一段（宽 1.6 px），
    // 弧的中点就是圆锥的边：圆锥比 12 px 大时两段弧张开（枪的 AdsReticleGap 是 12），弹丸落在两段弧之间
    Shotgun: {
      center: [['path', {d: 'M18.5 20A1.5 1.5 0 1 0 21.5 20A1.5 1.5 0 1 0 18.5 20Z'}]],
      left: [['path', {d: 'M10.17 13.12A12 12 0 0 0 10.17 26.88L8.86 27.8A13.6 13.6 0 0 1 8.86 12.2Z'}]],
      right: [['path', {d: 'M29.83 13.12A12 12 0 0 1 29.83 26.88L31.14 27.8A13.6 13.6 0 0 0 31.14 12.2Z'}]],
    },
    // M79 立框梯形标尺：中心瞄准点加下方三条下坠横刻线（对应 76 m/s 低速榴弹抛物线抬高参考），左右短横翼随扩散张开
    M79: {
      center: [
        ['path', {d: 'M18.6 20A1.4 1.4 0 1 0 21.4 20A1.4 1.4 0 1 0 18.6 20Z'}],
        ['rect', {x: 16.5, y: 26, width: 7, height: 1.6}],
        ['rect', {x: 17.5, y: 31.5, width: 5, height: 1.5}],
        ['rect', {x: 18.5, y: 37, width: 3, height: 1.5}],
      ],
      left: [['rect', {x: 4, y: 19.1, width: 8, height: 1.8}]],
      right: [['rect', {x: 28, y: 19.1, width: 8, height: 1.8}]],
    },
  };
  const reticleParts = {};
  for (const [part, key] of [['center', '2'], ['left', '6'], ['right', '7']]) {
    const element = add(root, 'div', 'wh-reticle');
    element.setAttribute('data-anchor', key);
    reticleParts[part] = element;
  }
  let reticleName = null;

  const lookupCI = (table, key) => {
    if (!key) return undefined;
    if (table[key] !== undefined) return table[key];
    const lower = String(key).toLowerCase();
    for (const [k, v] of Object.entries(table)) {
      if (k.toLowerCase() === lower) return v;
    }
    return undefined;
  };

  const drawReticle = name => {
    if (name === reticleName) return;
    reticleName = name;
    const spec = lookupCI(RETICLES, name);
    for (const [part, element] of Object.entries(reticleParts)) {
      element.textContent = '';
      const shapes = spec && spec[part];
      if (!shapes) continue;
      const svg = addSvg(element, 'svg', {width: 40, height: 40});
      for (const layer of ['shade', 'white']) {
        const group = addSvg(svg, 'g', {class: layer});
        for (const [tag, attributes] of shapes) addSvg(group, tag, attributes);
      }
    }
  };

  // ===== 武器栏 =====

  const dock = add(root, 'div', 'wh-dock');
  const panel = add(dock, 'div', 'wh-panel wh-none');
  add(panel, 'div', 'wh-backdrop');

  const nameText = add(panel, 'div', 'wh-name');

  const rifle = add(panel, 'div', 'wh-rifle');
  add(rifle, 'i', 'wh-bolt-closed');
  add(rifle, 'i', 'wh-bolt-open');
  const parts = add(rifle, 'div', 'wh-parts');
  let shownParts = '';
  const drawParts = (attachments = {}, layers = []) => {
    const key = JSON.stringify([attachments, layers]);
    if (key === shownParts) return;
    shownParts = key;
    parts.textContent = '';
    if (layers.length) {
      // 尺寸和枢轴来自与模型同时导出的元数据，所有模块保持同一取景比例。
      for (const layer of layers) {
        const sprite = add(parts, 'span', 'wh-part wh-layer-' + String(layer.mount || '').toLowerCase());
        sprite.style.setProperty('left', layer.x + 'px');
        sprite.style.setProperty('top', layer.y + 'px');
        sprite.style.setProperty('width', layer.w + 'px');
        sprite.style.setProperty('height', layer.h + 'px');
        sprite.style.setProperty('background-image', 'url(../Interaction/' + layer.icon + '_Part.png)');
      }
      return;
    }
    for (const [mount, part] of Object.entries(attachments)) {
      const anchor = add(parts, 'span', 'wh-mount wh-mount-' + String(mount || '').toLowerCase());
      const sprite = add(anchor, 'span', 'wh-part wh-part-' + part);
      sprite.style.setProperty('background-image', 'url(../Interaction/' + part + '_Part.png)');
    }
  };

  const ammo = add(panel, 'div', 'wh-ammo');
  const count = add(ammo, 'span', 'wh-count');
  add(ammo, 'span', 'wh-plus').textContent = '+1';
  add(ammo, 'i', 'wh-sep');
  const reserve = add(ammo, 'span', 'wh-reserve');

  const info = add(panel, 'div', 'wh-info');
  const pips = add(info, 'div', 'wh-pips');
  let pipCount = -1;
  // 膛里那一发：每把枪自己的子弹，弹头朝左（同枪口），活弹、空壳、空膛各一张小图，按类换。
  // 尺寸同游戏里的 3D 子弹（从底往弹尖的距离、半径，mm），同一个比例，所以长短胖瘦就是真的差别。枪换了才重画
  const ROUND_PX_PER_MM = 0.6;
  const ROUND_HEIGHT = 8;
  const ROUNDS = {
    // 7.92 × 57：最长，瓶颈，长尖头
    Mauser98k: {
      case: [[0, 5.97], [1.25, 5.97], [1.25, 5.15], [2.4, 5.15], [3.3, 5.9], [45, 5.45], [48.6, 4.5], [57, 4.5]],
      bullet: [[57, 4.11], [62, 4.11], [67, 3.75], [72, 3], [77, 1.9], [80.5, 1], [82, 0.4]],
    },
    // 7.62 × 39：短一截，壳的锥度大，尖头
    AK47: {
      case: [[0, 5.67], [1.5, 5.67], [1.6, 4.9], [2.6, 4.9], [3.2, 5.67], [30.5, 5.03], [33, 4.3], [38.7, 4.3]],
      bullet: [[38.7, 3.96], [45, 3.96], [52, 2.6], [56, 0.9]],
    },
    // .45 ACP：又短又胖，直筒，圆头
    M1911: {
      case: [[0, 6.09], [1.25, 6.09], [1.25, 5.2], [2.2, 5.2], [3.1, 6.04], [22.81, 6]],
      bullet: [[22.81, 5.74], [25, 5.74], [28, 5.2], [30.5, 4], [32, 2.6], [32.39, 1.5]],
    },
    // 12 号 2 3/4 英寸（压口后 61 mm）：又粗又长的筒，底缘 22.3、铜底 20.7、塑料壳 20.3 mm，铜底 14 mm 长；压口那一截算"弹头"（淡一点），空壳就没有它
    Shotgun: {
      case: [[0, 11.15], [1.5, 11.15], [1.5, 10.35], [14, 10.35], [14.3, 10.15], [58.5, 10.15]],
      bullet: [[58.5, 10.15], [59.8, 9.4], [61, 7.6]],
    },
    // 12 号独头弹：同样的壳，口是卷边（圆的，不是星形压口往里收），弹头的圆顶露一点出来（Foster 式的铅弹头；真的大多缩在口里，画出来好认）
    ShotgunSlug: {
      case: [[0, 11.15], [1.5, 11.15], [1.5, 10.35], [14, 10.35], [14.3, 10.15], [58.5, 10.15]],
      bullet: [[58.5, 10.15], [59.6, 9.7], [60.3, 8.6], [60.3, 7.4], [61.4, 6.7], [62.3, 5.2], [62.8, 3.1], [63, 0.8]],
    },
    // S.m.K.：外形同 s.S.（钢芯在里面，真枪靠底火那一圈红漆认），武器栏按特殊弹的颜色画
    Mauser98kSmK: {
      case: [[0, 5.97], [1.25, 5.97], [1.25, 5.15], [2.4, 5.15], [3.3, 5.9], [45, 5.45], [48.6, 4.5], [57, 4.5]],
      bullet: [[57, 4.11], [62, 4.11], [67, 3.75], [72, 3], [77, 1.9], [80.5, 1], [82, 0.4]],
    },
    // 7.62 × 51 NATO（C.I.P.，同游戏里的壳 SM_MG3_Case）：底缘 12.01、抽壳槽 10.26、壳底 11.96，39.62 mm 处的肩 11.53，20° 的肩收到 8.77 的颈，
    // 壳长 51.18；弹头 7.82，全长 71.1。比 7.92 × 57 短一截、同样是瓶颈
    MG3: {
      case: [[0, 6.0], [1.27, 6.0], [1.27, 5.13], [2.4, 5.13], [3.3, 5.98], [39.62, 5.765], [43.41, 4.385], [51.18, 4.385]],
      bullet: [[51.18, 3.91], [57, 3.91], [62, 3.45], [66, 2.6], [69.5, 1.3], [71.1, 0.5]],
    },
    // 40 × 46mm M406 高爆榴弹（全长 99 mm，底缘 41.3 mm，M118 直筒铝壳 46 mm，圆拱弹头 + M551 平顶引信）：口径是 12 号近两倍，按 0.36 px/mm 等比缩进信息栏高度
    M79: {
      scale: 0.36,
      case: [[0, 20.65], [1.7, 20.65], [1.7, 19.9], [46.0, 19.9]],
      bullet: [[46.0, 19.9], [50.5, 20.2], [53.5, 20.2], [56.0, 19.85], [68.0, 18.4], [80.0, 14.8], [90.5, 8.8], [95.5, 5.2], [99.0, 3.8]],
    },
    // 40 × 46mm 碰炸闪光弹（全长 97 mm，直筒泄压窗弹体 + 圆顶瞬发触碰引信，轮廓方直饱满，与卵形高爆弹区分鲜明）
    M79Flash: {
      scale: 0.36,
      case: [[0, 20.65], [1.7, 20.65], [1.7, 19.9], [46.0, 19.9]],
      bullet: [[46.0, 19.6], [51.5, 19.8], [60.0, 19.6], [60.8, 18.9], [79.2, 18.9], [80.0, 19.6], [85.5, 19.4], [91.5, 15.2], [95.2, 9.2], [97.0, 4.2]],
    },
    // 40 × 46mm XM576 16 钢镖霰弹（全长 68 mm，短平头塑料弹托筒伸出 46 mm 铝壳 22 mm，端面平齐如巨型霰弹）
    M79Flechette: {
      scale: 0.36,
      case: [[0, 20.65], [1.7, 20.65], [1.7, 19.9], [46.0, 19.9]],
      bullet: [[46.0, 19.5], [52.0, 19.3], [65.0, 18.9], [68.0, 17.6], [68.0, 2.0]],
    },
  };
  const round = add(info, 'div', 'wh-round');
  let roundName = null;

  // 一段外形：沿上边从底走到尖，再沿下边回来。scale 是 px/mm
  const roundPath = (points, length, height, scale = ROUND_PX_PER_MM) => {
    const at = (point, side) => `${((length - point[0]) * scale).toFixed(2)} ${(height / 2 + side * point[1] * scale).toFixed(2)}`;
    return `M${points.map(point => at(point, -1)).concat(points.slice().reverse().map(point => at(point, 1))).join('L')}Z`;
  };

  // 一发的几张图（活弹、空壳、空的虚线）画进 container：kinds 里有哪几种画哪几种。返回图的大小
  const drawRoundShapes = (container, name, kinds, scale = ROUND_PX_PER_MM, minHeight = ROUND_HEIGHT) => {
    const shape = lookupCI(ROUNDS, name) || ROUNDS.AK47;
    const usedScale = shape.scale ? shape.scale * (scale / ROUND_PX_PER_MM) : scale;
    const length = shape.bullet[shape.bullet.length - 1][0];
    // 霰弹枪的壳比 8 px 高：图高一点，上下居中伸出去，比例不变
    const radius = Math.max(...shape.case.concat(shape.bullet).map(point => point[1]));
    const height = Math.max(minHeight, Math.ceil(radius * 2 * usedScale + 1));
    const size = {width: Math.ceil(length * usedScale), height};
    const svgs = [];
    if (kinds.includes('live')) {
      const live = addSvg(container, 'svg', {class: 'live', ...size});
      addSvg(live, 'path', {class: 'bullet', d: roundPath(shape.bullet, length, height, usedScale)});
      addSvg(live, 'path', {class: 'case', d: roundPath(shape.case, length, height, usedScale)});
      svgs.push(live);
    }
    if (kinds.includes('spent')) {
      const spent = addSvg(container, 'svg', {class: 'spent', ...size});
      addSvg(spent, 'path', {class: 'case', d: roundPath(shape.case, length, height, usedScale)});
      svgs.push(spent);
    }
    if (kinds.includes('empty')) {
      const empty = addSvg(container, 'svg', {class: 'empty', ...size});
      addSvg(empty, 'path', {class: 'outline', d: roundPath(shape.case.concat(shape.bullet), length, height, usedScale)});
      svgs.push(empty);
    }
    return {size, svgs};
  };

  const drawRound = name => {
    if (name === roundName) return;
    roundName = name;
    round.textContent = '';
    const {size, svgs} = drawRoundShapes(round, name, ['live', 'spent', 'empty']);
    round.style.width = `${size.width}px`;
    for (const svg of svgs) svg.style.top = `${(ROUND_HEIGHT - size.height) / 2}px`;
  };
  const caliber = add(info, 'div', 'wh-caliber');
  const actionNote = add(info, 'div', 'wh-boltnote');
  // 枪机开着时的小字，按枪说：手枪是空仓挂机
  const ACTION_NOTE = {M1911: '空仓挂机', AK47: '枪机在后', Shotgun: '护木在后', M79: '膛开'};

  const setText = (element, text) => {
    if (element.textContent !== text) element.textContent = text;
  };

  // 弹仓的格子：容量变了才重建；多于 10 发的不画（一个数字就够）。从左到右是供弹从下往上（最右边亮着的是下一发进膛的），
  // 特殊弹（不是枪的弹种，Docs/Design/Weapons.md「切换弹种」）按特殊弹的颜色
  const setPips = (filled, capacity, special) => {
    const shown = capacity <= 10 ? capacity : 0;
    if (shown !== pipCount) {
      pipCount = shown;
      pips.textContent = '';
      for (let index = 0; index < shown; ++index) add(pips, 'i');
    }
    Array.from(pips.children).forEach((pip, index) => {
      const className = index < filled ? (special && special[index] ? 'on alt' : 'on') : '';
      if (pip.className !== className) pip.className = className;
    });
  };

  const CHAMBER_LABEL = {live: '', spent: '空壳', empty: '空膛'};
  let caliberState = null;
  // 子弹的状态（供弹、膛、枪机、后备）变了：武器栏亮一会儿
  let lastMechanism = null;
  let pulseUntil = 0;
  let pulseTimer = 0;
  let lastMessage = null;

  const render = () => {
    const message = lastMessage;
    if (!message || !message.weapon) {
      panel.className = 'wh-panel wh-none';
      return;
    }
    const m = message.mechanism;
    const classes = ['wh-panel', 'wh-icon-' + message.icon];
    if (!message.inHand) {
      classes.push('wh-stowed');
    } else if (message.raised || performance.now() < pulseUntil) {
      classes.push('wh-active');
    }
    if (m) {
      if (m.openBolt) {
        // 开膛待击：膛空、枪机在后面是常态，不按它们画。膛那里是弹链上的下一发，打空了是虚线
        classes.push('wh-action-closed', m.feed > 0 ? 'wh-next-live' : 'wh-next-empty');
      } else {
        classes.push('wh-chamber-' + m.chamber, 'wh-action-' + m.action);
      }
      const singleShot = m.capacity === 0 && !m.openBolt;
      if (singleShot) classes.push('wh-single-shot');
      if (!m.magazineIn) classes.push('wh-magazine-out');
      else if (singleShot ? m.chamber !== 'live' : m.feed === 0) classes.push('wh-feed-empty');
      if (m.reloadable === false) classes.push('wh-no-reserve');
      else if (m.reserve === 0) classes.push('wh-reserve-empty');
      // 膛里那一发、装填压的那种是特殊弹：按特殊弹的颜色
      const ammo = message.ammo || [];
      const shownAmmo = ammo[m.openBolt ? m.nextAmmo : (m.chamber === 'empty' ? m.load : m.chamberAmmo)];
      if (shownAmmo && shownAmmo.special) classes.push('wh-round-special');
      if (ammo[m.load] && ammo[m.load].special) classes.push('wh-load-special');
    } else {
      classes.push('wh-no-mechanism');
    }
    const className = classes.join(' ');
    if (panel.className !== className) panel.className = className;
  };

  engine.on('weapon', message => {
    const lift = `translateY(${-(message.lift || 0)}px)`;
    if (dock.style.transform !== lift) dock.style.transform = lift;
    lastMessage = message;
    drawParts(message.attachments, message.layers);
    if (!message.weapon) {
      drawReticle(null);
      lastMechanism = null;
      render();
      return;
    }
    drawReticle(message.reticle);
    const m = message.mechanism;
    // 膛里那一发画它自己的弹种（独头弹的壳口是卷边、露着弹头）；膛空时画装填压的那种的虚线，开膛待击画弹链上的下一发
    const ammo = message.ammo || [];
    const shownIndex = m ? (m.openBolt ? m.nextAmmo : (m.chamber === 'empty' ? m.load : m.chamberAmmo)) : -1;
    const shownAmmo = ammo[shownIndex];
    drawRound(shownAmmo && shownAmmo.round ? shownAmmo.round : message.icon);
    const mechanism = m ? [m.feed, m.chamber, m.action, m.reserve, m.magazineIn, m.chamberAmmo, m.load, (m.rounds || []).join(',')].join('|') : '';
    if (lastMechanism !== null && mechanism !== lastMechanism) {
      pulseUntil = performance.now() + PULSE_MS;
      clearTimeout(pulseTimer);
      pulseTimer = setTimeout(render, PULSE_MS + 20);
    }
    lastMechanism = mechanism;
    render();

    setText(nameText, message.name);
    setText(actionNote, ACTION_NOTE[message.icon] || '栓开');
    const singleShot = m && m.capacity === 0 && !m.openBolt;
    setText(count, m ? (singleShot ? (m.chamber === 'live' ? '1' : '0') : (m.magazineIn ? String(m.feed) : '—')) : '');
    setText(reserve, m && m.reloadable !== false ? String(m.reserve) : '');
    setPips(m && m.magazineIn ? m.feed : 0, m ? m.capacity : 0, m && m.rounds ? m.rounds.map(index => !!(ammo[index] && ammo[index].special)) : null);
    // 膛里那一发的口径：活弹写口径，空壳在前面标"空壳"，空膛只写"空膛"。开膛待击的写弹链上那一发的口径，打空了什么也不写。
    // 几种弹的枪写那一发的弹种（"12 Gauge Slug"）
    const label = m && !m.openBolt ? CHAMBER_LABEL[m.chamber] : '';
    const text = m && (m.openBolt ? m.feed > 0 : m.chamber !== 'empty') ? (shownAmmo && shownAmmo.label ? shownAmmo.label : message.caliber) : '';
    if (caliberState !== label + '|' + text) {
      caliberState = label + '|' + text;
      caliber.textContent = '';
      if (label) add(caliber, 'em').textContent = label;
      caliber.appendChild(document.createTextNode(text));
    }
  });

  // ===== 弹种轮盘（Docs/Design/Weapons.md「切换弹种」） =====
  // 同一口径有几种弹的枪（霰弹枪、98k）长按装填打开：一圈平分成几块，第 0 块在正上方、往右排；鼠标相对运动的方向、视角那根摇杆的方向选（选的是方向，屏幕上没有指针），松开装填键确认，开镜键取消。
  // 游戏在开、关、选的变了时发 "wheel"：
  //   {open, result, selected, current, weapon, entries: [{short, label, round, special}], details: [{name, desc, count}], prompts: {device, confirm: {key, label}, cancel: {key, label}}}
  //   result：上一次怎么关的（confirmed、cancelled、unavailable）；current：打开时正在装的那一种；prompts：底下的键位提示（打开轮盘的那个设备上的键）。
  // 轮盘在锚点 9（屏幕中心）。样子（2026-11-08 你说的：大一些，环用毛玻璃）：圈后面一层毛玻璃（backdrop-filter：游戏画面模糊、压暗一点），圈上每块薄薄一层白；
  // 选中的那块亮、描白边，后备是 0 的灰掉，正在装的那种外沿一道短线。中间写选着的那一种，底下画键位（键帽，Theme/keycap.js）。
  const WHEEL = {
    size: 560, inner: 112, outer: 204, gap: 2.5,   // 框、圈的里外半径（px）、两块之间空几度
    label: 158,                                    // 每块的名字、子弹画在这个半径上
    roundScale: 1.0,                               // 轮盘里的子弹（px/mm）：12 号的壳 63 px 长、23 px 粗
    labelWidth: 170, labelHeight: 68,              // 每块的名字、子弹的框（page.css 的 .wh-wheel-entry）
    keySize: 30,                                   // 键位提示的键帽多高
    longName: 14,                                  // 中间的名字比这么多字长时字小一号
  };
  // 跟着锚点的框不动它的变换（锚点自己算位置与缩放），开、关的动画在里面那一层
  const wheelAnchor = add(root, 'div', 'wh-wheel-anchor');
  wheelAnchor.setAttribute('data-anchor', '9');
  // 毛玻璃不能放进淡入淡出的那一层：祖先的不透明度小于 1 时 backdrop-filter 读不到游戏画面（背景根），所以它同 wheel 并排，自己淡入淡出、缩放
  const wheelGlass = add(wheelAnchor, 'div', 'wh-wheel-glass');
  const wheel = add(wheelAnchor, 'div', 'wh-wheel');
  const wheelSvg = addSvg(wheel, 'svg', {width: WHEEL.size, height: WHEEL.size});
  const wheelLabels = add(wheel, 'div', 'wh-wheel-labels');
  const wheelCenter = add(wheel, 'div', 'wh-wheel-center');
  const wheelName = add(wheelCenter, 'div', 'wh-wheel-name');
  const wheelDesc = add(wheelCenter, 'div', 'wh-wheel-desc');
  const wheelCount = add(wheelCenter, 'div', 'wh-wheel-count');
  const wheelPrompts = add(wheel, 'div', 'wh-wheel-prompts');
  let wheelPromptKey = null;
  let wheelKey = null;
  let wheelSectors = [];
  let wheelEntryLabels = [];

  // 圆上的点：角从正上方往右量（度），y 往下
  const wheelAt = (radius, degrees) => {
    const angle = degrees * Math.PI / 180;
    const c = WHEEL.size / 2;
    return [c + radius * Math.sin(angle), c - radius * Math.cos(angle)];
  };

  // 一块圆环：从 a0 到 a1（度），只有一块时是整圈（两个半圆拼起来，弧画不了 360°）
  const sectorPath = (a0, a1) => {
    const f = ([x, y]) => `${x.toFixed(2)} ${y.toFixed(2)}`;
    if (a1 - a0 >= 359.9) {
      const o = WHEEL.outer, i = WHEEL.inner;
      return `M${f(wheelAt(o, 0))}A${o} ${o} 0 1 1 ${f(wheelAt(o, 180))}A${o} ${o} 0 1 1 ${f(wheelAt(o, 360))}`
        + `M${f(wheelAt(i, 0))}A${i} ${i} 0 1 0 ${f(wheelAt(i, 180))}A${i} ${i} 0 1 0 ${f(wheelAt(i, 360))}Z`;
    }
    const large = a1 - a0 > 180 ? 1 : 0;
    return `M${f(wheelAt(WHEEL.outer, a0))}A${WHEEL.outer} ${WHEEL.outer} 0 ${large} 1 ${f(wheelAt(WHEEL.outer, a1))}`
      + `L${f(wheelAt(WHEEL.inner, a1))}A${WHEEL.inner} ${WHEEL.inner} 0 ${large} 0 ${f(wheelAt(WHEEL.inner, a0))}Z`;
  };

  // 几项变了才重画（打开时）：每块一个形（深色的边在下面，白的在上面），每块的子弹与名字
  const buildWheel = message => {
    const entries = message.entries || [];
    const key = entries.map(entry => entry.short + entry.round).join('|');
    if (key === wheelKey) return;
    wheelKey = key;
    wheelSvg.textContent = '';
    wheelLabels.textContent = '';
    wheelSectors = [];
    wheelEntryLabels = [];
    const count = Math.max(entries.length, 1);
    const step = 360 / count;
    // 圈里面一个深色的圆（中间的字的底），留出 6 px 的缝
    const hubRadius = WHEEL.inner - 6;
    const c = WHEEL.size / 2;
    addSvg(wheelSvg, 'path', {class: 'hub', d: `M${c - hubRadius} ${c}A${hubRadius} ${hubRadius} 0 1 1 ${c + hubRadius} ${c}A${hubRadius} ${hubRadius} 0 1 1 ${c - hubRadius} ${c}Z`});
    entries.forEach((entry, index) => {
      const center = index * step;
      const gap = count > 1 ? WHEEL.gap / 2 : 0;
      const d = sectorPath(center - step / 2 + gap, center + step / 2 - gap);
      const group = addSvg(wheelSvg, 'g', {class: 'sector'});
      addSvg(group, 'path', {class: 'shade', d});
      addSvg(group, 'path', {class: 'fill', d});
      // 正在装的那种：外沿一道短线
      const [mx0, my0] = wheelAt(WHEEL.outer + 3, center - Math.min(step / 2 - 6, 9));
      const [mx1, my1] = wheelAt(WHEEL.outer + 3, center + Math.min(step / 2 - 6, 9));
      const markD = `M${mx0.toFixed(2)} ${my0.toFixed(2)}A${WHEEL.outer + 3} ${WHEEL.outer + 3} 0 0 1 ${mx1.toFixed(2)} ${my1.toFixed(2)}`;
      addSvg(group, 'path', {class: 'mark', d: markD});
      wheelSectors.push({group, className: 'sector'});

      const [x, y] = wheelAt(WHEEL.label, center);
      const label = add(wheelLabels, 'div', 'wh-wheel-entry' + (entry.special ? ' special' : ''));
      label.style.left = `${(x - WHEEL.labelWidth / 2).toFixed(1)}px`;
      label.style.top = `${(y - WHEEL.labelHeight / 2).toFixed(1)}px`;
      const roundBox = add(label, 'div', 'wh-wheel-round');
      const {size} = drawRoundShapes(roundBox, entry.round, ['live'], WHEEL.roundScale, 6);
      roundBox.style.width = `${size.width}px`;
      roundBox.style.height = `${size.height}px`;
      const text = add(label, 'div', 'wh-wheel-text');
      const name = add(text, 'span', 'wh-wheel-short');
      name.textContent = entry.short;
      const reserveText = add(text, 'span', 'wh-wheel-reserve');
      wheelEntryLabels.push({label, reserveText});
    });
  };

  // 底下的键位提示：键变了才重画（换了设备、改了键）
  const buildPrompts = prompts => {
    const confirm = prompts && prompts.confirm;
    const cancel = prompts && prompts.cancel;
    const key = `${confirm ? confirm.key : ''}|${cancel ? cancel.key : ''}`;
    if (key === wheelPromptKey) return;
    wheelPromptKey = key;
    wheelPrompts.textContent = '';
    const addPrompt = (info, text) => {
      if (!info) return;
      const item = add(wheelPrompts, 'div', 'wh-wheel-prompt');
      GinkgoKeycap.create(item, info.key, info.label, 'hud', WHEEL.keySize);
      add(item, 'span', 'wh-wheel-prompt-text').textContent = text;
    };
    addPrompt(confirm, '松开确认');
    addPrompt(cancel, '取消');
  };

  engine.on('wheel', message => {
    if (!message.open) {
      // 确认的那一下：选的那块亮一下再随整个轮盘淡掉；取消、没有这种弹的直接淡掉
      const confirmed = message.result === 'confirmed';
      const closing = confirmed ? 'wh-wheel wh-wheel-confirmed' : 'wh-wheel';
      if (wheel.className !== closing) wheel.className = closing;
      const glassClosing = confirmed ? 'wh-wheel-glass wh-wheel-confirmed' : 'wh-wheel-glass';
      if (wheelGlass.className !== glassClosing) wheelGlass.className = glassClosing;
      if (root.className.indexOf('wh-wheel-open') >= 0) root.className = root.className.replace(/\s*wh-wheel-open/, '');
      return;
    }
    buildWheel(message);
    buildPrompts(message.prompts);
    const details = message.details || [];
    const entries = message.entries || [];
    wheelSectors.forEach((sector, index) => {
      const available = details[index] && details[index].count > 0;
      const classes = ['sector'];
      if (index === message.selected) classes.push('selected');
      if (!available) classes.push('empty');
      if (index === message.current) classes.push('current');
      const className = classes.join(' ');
      // SVG 的元素只用 setAttribute 改类（className 在 SVG 上不是字符串），上一次的记在这里
      if (sector.className !== className) {
        sector.className = className;
        sector.group.setAttribute('class', className);
      }
    });
    wheelEntryLabels.forEach(({label, reserveText}, index) => {
      const detail = details[index] || {};
      setText(reserveText, `×${detail.count || 0}`);
      const classes = ['wh-wheel-entry'];
      if (entries[index] && entries[index].special) classes.push('special');
      if (index === message.selected) classes.push('selected');
      if (!(detail.count > 0)) classes.push('empty');
      const className = classes.join(' ');
      if (label.className !== className) label.className = className;
    });
    const chosen = details[message.selected] || {};
    setText(wheelName, chosen.name || '');
    // 长的名字（7.92×57 mm S.m.K. 穿甲弹）字小一号，一行放得下，不从词中间断开
    const nameClass = 'wh-wheel-name' + ((chosen.name || '').length > WHEEL.longName ? ' long' : '');
    if (wheelName.className !== nameClass) wheelName.className = nameClass;
    setText(wheelDesc, chosen.desc || '');
    setText(wheelCount, chosen.count > 0 ? `后备 ${chosen.count} 发${message.selected === message.current ? ' · 正在装' : ''}` : '后备里没有');
    const centerClass = 'wh-wheel-center' + (chosen.count > 0 ? '' : ' empty');
    if (wheelCenter.className !== centerClass) wheelCenter.className = centerClass;
    if (wheel.className !== 'wh-wheel open') wheel.className = 'wh-wheel open';
    if (wheelGlass.className !== 'wh-wheel-glass open') wheelGlass.className = 'wh-wheel-glass open';
    // 准星让开：中间写字
    if (root.className.indexOf('wh-wheel-open') < 0) root.className = (root.className + ' wh-wheel-open').trim();
  });
})();
