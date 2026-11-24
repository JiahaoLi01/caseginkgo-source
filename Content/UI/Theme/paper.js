// 纸页的组件（Content/UI/Theme）：C++ 的 Ginkgo::UI::LoadThemedPage 把它排在页面自己的脚本前面，样式在同目录的 theme.css。
// 页面用 Paper.* 搭界面：目录（rail）、类别（section）、选项（option：洗染的标签条加控件）、滑块、数、开关、分段的选择、按钮、一排动作（actionBar）、
// 问一句（dialog）、描述区的富文本与公式、图标、预览的画图（sketch）、指针停住才算数（restIntent）。相机的调参面板与设置界面共用。
//
// weft 的限制（Docs/Design/UIArt.md「在 weft 里落地」与 weft 的 compatibility.md）：
// - range、checkbox、select 不能用：滑块、开关是 div，拖动的 mousemove、mouseup 挂在 document 上（没有 setPointerCapture）；
// - SVG 没有 viewBox（坐标就是 px），子元素不能带 opacity（半透明用 rgba 的颜色）；图标在 24 的格子里画，用 <g transform="scale()"> 缩放；
// - url() 的背景图只能写在样式表里：换图就换类，不写内联的 background-image；
// - innerHTML 不全：结构都用 createElement 搭。
// 公式：页面带了 KaTeX（page.json 的 scripts 里有 katex.min.js、"styles" 里有 katex.css，字体在它旁边的 fonts/）、并且 weft 认得它的
// 排版（document.compatMode 是 'CSS1Compat'）时用 KaTeX，只出 HTML；不然用这里的回退版：TeX 的一个子集（分数、上下标、根号、
// 希腊字母、\text、常见的运算符与函数名）排成横竖的 flex，变量用 Cambria 的斜体。调参面板写的公式两边都排得出。
(() => {
  const SVG_NS = 'http://www.w3.org/2000/svg';

  const el = (parent, tag, cls, text) => {
    const node = document.createElement(tag);
    if (cls) node.className = cls;
    if (text !== undefined && text !== null) node.textContent = String(text);
    if (parent) parent.appendChild(node);
    return node;
  };

  const svgEl = (parent, tag, attrs) => {
    const node = document.createElementNS(SVG_NS, tag);
    for (const key in attrs || {}) node.setAttribute(key, String(attrs[key]));
    if (parent) parent.appendChild(node);
    return node;
  };

  const svgRoot = (parent, width, height, cls) => {
    const node = svgEl(parent, 'svg', {width, height});
    if (cls) node.setAttribute('class', cls);
    return node;
  };

  const clear = node => {
    while (node.firstChild) node.removeChild(node.firstChild);
  };

  const clamp = (v, lo, hi) => Math.min(Math.max(v, lo), hi);

  // 数的写法：整数照写，小数最多 digits 位、去掉末尾的 0；负号用真的减号
  const fmt = (v, digits = 2) => {
    if (typeof v !== 'number' || !Number.isFinite(v)) return String(v);
    const scale = Math.pow(10, digits);
    let text = String(Math.round(v * scale) / scale);
    if (text === '-0') text = '0';
    return text.replace('-', '−');
  };

  // ===== 颜色（令牌的值：SVG 的属性读不到 CSS 变量；默认使用现代暗黑扁平主题，设置界面与 F7 相机调参共用） =====

  const DARK_C = {
    paper0: '#0b0b0c', paper1: '#111113', paper2: '#1a1a1d', paper3: 'rgba(229, 180, 67, 0.18)', edge: 'rgba(255, 251, 244, 0.16)',
    ink1: '#fffbf4', ink2: 'rgba(255, 251, 244, 0.66)', ink3: 'rgba(255, 251, 244, 0.38)', rule: 'rgba(255, 251, 244, 0.22)', hairline: 'rgba(255, 251, 244, 0.14)',
    goldTitle: '#fffbf4', goldDeep: '#e8cf9c', goldLo: 'rgba(232, 207, 156, 0.56)', goldMid: '#e5b443', goldHi: '#fbe296',
    bronze: '#e5b443', ginkgo: '#e5b443', ginkgoDeep: '#e5b443', ginkgoPale: '#fbe296',
    verdigris: '#5fe08a', verdigrisDeep: '#3ea868', vermilion: '#e0654a', wisteriaDeep: '#a68ce0', ochre: '#e59a4a',
    grid: 'rgba(255, 251, 244, 0.12)', faint: 'rgba(255, 251, 244, 0.07)', inkFaint: 'rgba(255, 251, 244, 0.34)',
    surface: 'rgba(255, 251, 244, 0.045)', rowHighlight: 'rgba(229, 180, 67, 0.18)',
  };

  const C = {...DARK_C};

  let darkTheme = true;

  /** 兼容旧调用：主题已默认启用暗黑现代扁平风格，保持幂等 */
  const useDarkTheme = () => {
    darkTheme = true;
    Object.assign(C, DARK_C);
  };

  // ===== 图标：24 的格子、圆头圆角的线（UIArt.md 资产清单：单色线性 SVG，24 px 网格、1.5 px 线宽） =====
  // 每个图标是几笔：['p', d] 线、['pd', d] 虚线、['pf', d] 填充、['c', cx, cy, r] 圆圈、['cf', cx, cy, r] 实心圆点

  const ICONS = {
    // ----- 通用
    leaf: [['pf', 'M12 21V13.6M12 13.6C7.2 13.6 3.6 9.9 4 5.2c2.6 1.1 5.3 1.5 8 1.5s5.4-.4 8-1.5c.4 4.7-3.2 8.4-8 8.4z'], ['p', 'M12 21V13.6'], ['p', 'M12 6.7v3.2']],
    close: [['p', 'M7 7l10 10M17 7L7 17']],
    reset: [['p', 'M5.5 9.5A7 7 0 1 1 5 13'], ['p', 'M5.5 4.5v5h5']],
    chevronDown: [['p', 'M7 10l5 5 5-5']],
    chevronRight: [['p', 'M10 7l5 5-5 5']],
    dot: [['cf', 12, 12, 2.5]],
    more: [['cf', 6, 12, 1.4], ['cf', 12, 12, 1.4], ['cf', 18, 12, 1.4]],
    // ----- 相机的目录
    frame: [['p', 'M3 8V4h4M17 4h4v4M21 16v4h-4M7 20H3v-4'], ['c', 9, 9.2, 2], ['p', 'M5.6 17c0-2.3 1.5-4 3.4-4s3.4 1.7 3.4 4'], ['p', 'M16.5 10.5v3.4M14.8 12.2h3.4']],
    follow: [['p', 'M2.5 9h4M2 12h5M2.5 15h4'], ['c', 14.5, 12, 4.5], ['cf', 14.5, 12, 1.4]],
    collision: [['p', 'M19.5 4v16'], ['p', 'M19.5 6.5l2-2M19.5 10.5l2-2M19.5 14.5l2-2M19.5 18.5l2-2'], ['c', 14.5, 12, 4], ['p', 'M3.5 12h6.5'], ['cf', 3.5, 12, 1.5]],
    slide: [['c', 17.5, 6.5, 2], ['p', 'M15.8 8.6L10 13.6l8.3 3.2M10 13.6l-4.3 3.9M13.5 10.6l3.6 1.8'], ['p', 'M3 20.5h18'], ['p', 'M2.5 11h3M2 14h2.5']],
    sprint: [['pf', 'M6.3 11.4c-.2-2.4.9-4.6 2.5-4.7s2.6 2 2.4 4.4-1.2 3.6-2.6 3.6-2.1-1.1-2.3-3.3z'], ['pf', 'M7.6 16.1c.1-1 .9-1.6 1.8-1.5s1.4.9 1.3 1.9-.8 1.9-1.7 1.8-1.5-1.2-1.4-2.2z'],
      ['pf', 'M13.2 6.4c-.2-2.4.9-4.6 2.5-4.7s2.6 2 2.4 4.4-1.2 3.6-2.6 3.6-2.1-1.1-2.3-3.3z'], ['pf', 'M14.5 11.1c.1-1 .9-1.6 1.8-1.5s1.4.9 1.3 1.9-.8 1.9-1.7 1.8-1.5-1.2-1.4-2.2z']],
    aim: [['c', 12, 12, 7], ['p', 'M12 2.5V7M12 17v4.5M2.5 12H7M17 12h4.5'], ['cf', 12, 12, 1.1]],
    smoothing: [['cf', 3.5, 12, 1.5], ['p', 'M5 12h1.5l1.5-4.5 3 9 3-9 3 9 1.5-4.5H20'], ['cf', 20.5, 12, 1.5]],
    // ----- 参数
    arm: [['p', 'M4 12h16M4 8v8M20 8v8M7.5 9.5L5 12l2.5 2.5M16.5 9.5L19 12l-2.5 2.5']],
    shoulderRight: [['c', 6, 12, 2.3], ['p', 'M10 12h10M16 8l4 4-4 4']],
    shoulderUp: [['c', 12, 18, 2.3], ['p', 'M12 14V4M8 8l4-4 4 4']],
    pivotForward: [['c', 8, 16, 2.6], ['cf', 8, 16, .9], ['p', 'M10.2 13.8L19 5M13.5 5H19v5.5']],
    pivotRight: [['c', 6, 12, 2.6], ['cf', 6, 12, .9], ['p', 'M10 12h10M16 8l4 4-4 4']],
    pivotUp: [['c', 12, 18, 2.6], ['cf', 12, 18, .9], ['p', 'M12 14V4M8 8l4-4 4 4']],
    fov: [['cf', 4, 12, 1.4], ['p', 'M4 12L20 5M4 12l16 7'], ['p', 'M15.5 7a11 11 0 0 1 0 10']],
    roll: [['p', 'M3.5 16l17-8'], ['p', 'M7 6.8A8 8 0 0 1 17.6 6'], ['p', 'M17.8 3.2l-.2 2.8-2.8-.4'], ['cf', 12, 12, 1.2]],
    post: [['p', 'M3.5 6.5h17v11h-17z'], ['c', 12, 12, 3.6], ['p', 'M5.5 8.5l2.2 1.6M18.5 8.5l-2.2 1.6M5.5 15.5l2.2-1.6M18.5 15.5l-2.2-1.6']],
    spring: [['cf', 3.5, 12, 1.5], ['p', 'M5 12h1.5l1.5-4.5 3 9 3-9 3 9 1.5-4.5H20'], ['cf', 20.5, 12, 1.5]],
    feedforward: [['c', 5.5, 12, 2.4], ['p', 'M10.5 8l4 4-4 4M15.5 8l4 4-4 4']],
    leash: [['pd', 'M12 4a8 8 0 1 1 0 16a8 8 0 1 1 0-16z'], ['cf', 12, 12, 1.2], ['p', 'M12 12l4.2-2.6'], ['c', 17, 9, 1.8]],
    filter: [['pd', 'M3 17h6V7h12'], ['p', 'M3 17c5.5 0 5.5-10 18-10']],
    probe: [['c', 12, 12, 7], ['cf', 12, 12, 1.2], ['p', 'M12 12h7']],
    pushOut: [['p', 'M5 4v16M3 6.5l2-2M3 10.5l2-2M3 14.5l2-2M3 18.5l2-2'], ['p', 'M9 12h11M16 8l4 4-4 4']],
    power: [['p', 'M12 3.5v8'], ['p', 'M7.2 6.8a7 7 0 1 0 9.6 0']],
    gauge: [['p', 'M4 16a8 8 0 0 1 16 0'], ['p', 'M12 16l4.2-5.2'], ['cf', 12, 16, 1.3], ['p', 'M6 10.5l1 .8M12 8v1.2M18 10.5l-1 .8']],
    compare: [['p', 'M6.5 6.5l11 4.5-11 4.5M6.5 19h11']],
    lock: [['p', 'M6 11h12v9H6z'], ['p', 'M8.5 11V8a3.5 3.5 0 0 1 7 0v3'], ['cf', 12, 15.5, 1.2]],
    layers: [['p', 'M12 4l8 4-8 4-8-4z'], ['p', 'M4 12l8 4 8-4M4 16l8 4 8-4']],
    value: [['p', 'M12 4.5l7.5 14h-15z'], ['p', 'M12 10v4.5']],
    priority: [['p', 'M5 19v-4M10 19v-7.5M15 19V8.5M20 19V5']],
    rampIn: [['p', 'M3 18h5l8-12h5'], ['pd', 'M8 18V6']],
    rampOut: [['p', 'M3 6h5l8 12h5'], ['pd', 'M16 18V6']],
    impulse: [['p', 'M13 3L6 13.5h5.2L10 21l7.2-10.5H12z']],
    axisPitch: [['p', 'M12 3.5v17M8.5 7L12 3.5 15.5 7M8.5 17l3.5 3.5 3.5-3.5'], ['c', 12, 12, 2]],
    axisYaw: [['p', 'M3.5 12h17M7 8.5L3.5 12 7 15.5M17 8.5l3.5 3.5-3.5 3.5'], ['c', 12, 12, 2]],
    axisRoll: [['p', 'M18.6 8.6A7.5 7.5 0 1 0 19.5 12'], ['p', 'M19.2 4.6v4.2H15'], ['cf', 12, 12, 1.4]],
    magnitude: [['p', 'M4 19h16'], ['p', 'M7 19v-5M11 19v-9M15 19v-3'], ['pd', 'M3 11.5h18']],
    mirror: [['pd', 'M12 3v18'], ['p', 'M9 7L4 12l5 5zM15 7l5 5-5 5z']],
    interval: [['p', 'M6 5v14M18 5v14M9 12h6M10.8 10l-2 2 2 2M13.2 10l2 2-2 2']],
    damping: [['p', 'M3 12c1.4-8 2.8-8 4.2 0s2.8 5.2 4.2 0 2.8-3 4.2 0 2.4 1.6 4.4 0']],
    wave: [['p', 'M3 12c.9-4 1.9-4 2.8 0s1.9 4 2.8 0 1.9-4 2.8 0 1.9 4 2.8 0 1.9-4 2.8 0 1.9 4 2.8 0']],
    amplitude: [['p', 'M3 12c2.2-7 4.4-7 6.6 0s4.4 7 6.6 0'], ['p', 'M20 5v14M18 7l2-2 2 2M18 17l2 2 2-2']],
    envelope: [['pd', 'M3 18c4 0 5-12 9-12s5 12 9 12'], ['p', 'M6 14c1-3 2-3 3 0s2 3 3 0 2-3 3 0 2 3 3 0']],
    ceiling: [['pd', 'M3 6h18'], ['p', 'M3 19c6.5 0 8.5-13 18-13']],
    dice: [['p', 'M5 5h14v14H5z'], ['cf', 9, 9, 1.3], ['cf', 15, 15, 1.3], ['cf', 12, 12, 1.3]],
    orbit: [['pd', 'M12 5a7 7 0 1 1 0 14a7 7 0 1 1 0-14z'], ['cf', 12, 12, 1.5], ['c', 19, 12, 2], ['p', 'M15.5 5.6l2.6.8-.6 2.6']],
    flag: [['p', 'M6 21V4'], ['p', 'M6 4.5h11l-2.5 3.5 2.5 3.5H6']],
    clock: [['c', 12, 12, 8], ['p', 'M12 7.5V12l3 2']],
    // ----- 设置的目录
    gamepad: [['p', 'M8 7.5h8c3 0 4.6 2.2 5.2 5.6.5 2.9.2 5-1.4 5.4-1.4.3-2.6-1-3.8-2.7-.5-.7-1-1-1.8-1h-4.4c-.8 0-1.3.3-1.8 1-1.2 1.7-2.4 3-3.8 2.7-1.6-.4-1.9-2.5-1.4-5.4C3.4 9.7 5 7.5 8 7.5z'],
      ['p', 'M8 10.2v3M6.5 11.7h3'], ['cf', 15.3, 12.8, 1], ['cf', 17.3, 10.8, 1]],
    mouse: [['p', 'M12 3c-3.6 0-6 2.6-6 6v6c0 3.4 2.4 6 6 6s6-2.6 6-6V9c0-3.4-2.4-6-6-6z'], ['p', 'M12 3v6.5M6 9.5h12']],
    // ----- 输入的设置
    speedYaw: [['p', 'M3.5 12h17M16 7.5l4.5 4.5-4.5 4.5M8 7.5L3.5 12 8 16.5']],
    speedPitch: [['p', 'M12 3.5v17M7.5 8L12 3.5 16.5 8M7.5 16l4.5 4.5 4.5-4.5']],
    scope: [['c', 12, 12, 7.5], ['p', 'M12 4.5v5M12 14.5v5M4.5 12h5M14.5 12h5']],
    fovMatch: [['p', 'M4 12L20 5.5M4 12l16 6.5'], ['pd', 'M4 12L20 8.5M4 12l16 3.5'], ['cf', 4, 12, 1.3]],
    curve: [['p', 'M4 20V4M4 20h16'], ['p', 'M4 20c8 0 9-4 11-8s3-6 5-8']],
    curveBend: [['p', 'M4 20c6-1 9-3 11-7s3-7 5-9'], ['pd', 'M4 20L20 4']],
    invertY: [['p', 'M8 4v16M4.5 7.5L8 4l3.5 3.5'], ['p', 'M16 20V4M12.5 16.5L16 20l3.5-3.5']],
    invertX: [['p', 'M4 8h16M7.5 4.5L4 8l3.5 3.5'], ['p', 'M20 16H4M16.5 12.5L20 16l-3.5 3.5']],
    swap: [['c', 6.5, 12, 3], ['c', 17.5, 12, 3], ['p', 'M10 8.5c1.3-1.3 2.7-1.3 4 0M14 15.5c-1.3 1.3-2.7 1.3-4 0'], ['p', 'M14.2 6.2l-.2 2.3-2.2-.4M9.8 17.8l.2-2.3 2.2.4']],
    deadZone: [['c', 12, 12, 8.5], ['pd', 'M12 8.5a3.5 3.5 0 1 1 0 7a3.5 3.5 0 1 1 0-7z'], ['cf', 12, 12, 1]],
    outerRing: [['pd', 'M12 3.5a8.5 8.5 0 1 1 0 17a8.5 8.5 0 1 1 0-17z'], ['c', 12, 12, 5.8], ['cf', 12, 12, 1]],
    walkZone: [['c', 12, 12, 8.5], ['pd', 'M12 8.5a3.5 3.5 0 1 1 0 7a3.5 3.5 0 1 1 0-7z'], ['p', 'M12 2.5v4M10.2 4.3L12 2.5l1.8 1.8']],
    turnBoost: [['p', 'M18.6 8.6A7.5 7.5 0 1 0 19.5 12'], ['p', 'M19.2 4.6v4.2H15'], ['p', 'M12.8 8l-2.4 4h3.2l-2.4 4']],
    ramp: [['p', 'M3 18h6l6-12h6']],
    hold: [['p', 'M9 11V5.5a1.5 1.5 0 0 1 3 0V11m0-2.5a1.5 1.5 0 0 1 3 0V11m0-1.5a1.5 1.5 0 0 1 3 0V15a6 6 0 0 1-6 6h-1a5 5 0 0 1-4-2l-3-4a1.5 1.5 0 0 1 2.3-1.9L9 15V11']],
    accel: [['p', 'M4 16a8 8 0 0 1 16 0'], ['p', 'M12 16l5-6'], ['cf', 12, 16, 1.3]],
    sens: [['c', 12, 12, 2.2], ['p', 'M12 4v3M12 17v3M4 12h3M17 12h3M6.3 6.3l2.1 2.1M15.6 15.6l2.1 2.1M6.3 17.7l2.1-2.1M15.6 8.4l2.1-2.1']],
    ratio: [['p', 'M5 19L19 5'], ['p', 'M5 5h5v5M14 14h5v5']],
    // ----- 操作提示
    keyMenu: [['p', 'M6 8h12M6 12h12M6 16h12']],
  };

  const iconStroke = 1.6;

  /**
   * 画一个图标：size 是边长（px），color 是线色。opts.rotate（度）绕中心转，opts.stroke 改线宽（屏幕上的 px）
   * 返回 svg 根节点；换颜色用 recolor
   */
  const icon = (parent, name, size = 20, color = C.ink1, opts = {}) => {
    const root = svgRoot(parent, size, size, opts.cls || '');
    const s = size / 24;
    const group = svgEl(root, 'g', {transform: opts.rotate ? `scale(${s}) rotate(${opts.rotate} 12 12)` : `scale(${s})`});
    const width = (opts.stroke || iconStroke) / s;
    for (const shape of ICONS[name] || ICONS.dot) {
      const kind = shape[0];
      if (kind === 'p' || kind === 'pd') {
        svgEl(group, 'path', {d: shape[1], fill: 'none', stroke: color, 'stroke-width': width, 'stroke-linecap': 'round', 'stroke-linejoin': 'round',
          ...(kind === 'pd' ? {'stroke-dasharray': '2.4 2.6'} : {})});
      } else if (kind === 'pf') {
        svgEl(group, 'path', {d: shape[1], fill: color, stroke: 'none'});
      } else if (kind === 'c') {
        svgEl(group, 'circle', {cx: shape[1], cy: shape[2], r: shape[3], fill: 'none', stroke: color, 'stroke-width': width});
      } else if (kind === 'cf') {
        svgEl(group, 'circle', {cx: shape[1], cy: shape[2], r: shape[3], fill: color, stroke: 'none'});
      }
    }
    root.__paperColor = color;
    return root;
  };

  const recolor = (root, color) => {
    if (!root || root.__paperColor === color) return;
    const visit = node => {
      for (let child = node.firstChild; child; child = child.nextSibling) {
        if (child.nodeType !== 1) continue;
        if (child.getAttribute('fill') && child.getAttribute('fill') !== 'none') child.setAttribute('fill', color);
        if (child.getAttribute('stroke') && child.getAttribute('stroke') !== 'none') child.setAttribute('stroke', color);
        visit(child);
      }
    };
    visit(root);
    root.__paperColor = color;
  };

  const registerIcons = defs => Object.assign(ICONS, defs);

  // ===== 指针停住才算数：描述区换东西用 =====
  // 算的是指针停住多久，不是进了这一格多久：目录一格 84 px，手稍慢一点（低于约 900 px/s）每过一格都待够了 90 ms，扫一趟换一路描述。
  // 离开选项时等得更久（移到下一行时不先回去一趟）。set(目标或 null) 开始等，指针动了超过 slop 重新等；页面每帧 due(现在)、到了 take()

  const restIntent = (opts = {}) => {
    const delay = opts.delay || 90;        // ms：指针停住这么久才换
    const leave = opts.leave || 180;       // ms：离开（目标是 null）时指针停住多久
    const slop = opts.slop || 3;           // px：这么大的晃动还算停着（真的鼠标停下时有一两个 px 的抖动）
    let pending = null;
    let x = 0;
    let y = 0;
    const dueAt = target => performance.now() + (target ? delay : leave);
    document.addEventListener('mousemove', e => {
      x = e.clientX;
      y = e.clientY;
      if (!pending || (Math.abs(x - pending.x) <= slop && Math.abs(y - pending.y) <= slop)) return;
      pending.x = x;
      pending.y = y;
      pending.due = dueAt(pending.target);
    });
    return {
      set(target) {
        pending = {target, due: dueAt(target), x, y};
      },
      cancel() {
        pending = null;
      },
      due(now) {
        return !!pending && now >= pending.due;
      },
      take() {
        const target = pending ? pending.target : null;
        pending = null;
        return target;
      },
    };
  };

  // ===== 预览的画图：原地更新与小工具（相机的调参面板、设置界面共用） =====
  // 预览的 draw(c) 每次都从头画（paint 包着）。第一次在空的画布里新建；值变了再画时，按调用的顺序复用上一遍同一位置、同一种的元素，
  // 只改变了的属性、样式与字，这一遍没用到的旧元素删掉。weft 里改属性几乎不要钱，新建的元素要排样式、布局、进场景
  // （2026-11-02 量的：滑铲预览整个重建一次 weft 要 13–21 ms）。frame() 每帧直接改的属性（播放头等）不进记录，下一帧它自己会再改

  const sketch = (() => {
    let painting = null;  // {next: Map(父元素 → 下一个可复用的旧子元素)}

    const paint = (host, body) => {
      const outer = painting;
      painting = {next: new Map()};
      try {
        body();
      } finally {
        for (const [parent, first] of painting.next) {
          for (let node = first; node;) {
            const following = node.nextSibling;
            parent.removeChild(node);
            node = following;
          }
        }
        painting = outer;
      }
    };

    /** 正在画时 parent 里下一个可复用的旧元素（match 认得的）；没有返回 null，新建的放在它前面 */
    const reusable = (parent, match) => {
      if (!painting || !parent) return null;
      if (!painting.next.has(parent)) painting.next.set(parent, parent.firstChild);
      const node = painting.next.get(parent);
      if (node && match(node)) {
        painting.next.set(parent, node.nextSibling);
        return node;
      }
      return null;
    };

    const place = (parent, node) => {
      const next = painting ? painting.next.get(parent) : null;
      if (next) parent.insertBefore(node, next);
      else parent.appendChild(node);
    };

    /** SVG 元素：同一位置、同一种、同一套属性名的旧元素只改变了的属性 */
    const svg = (parent, tag, attrs = {}) => {
      const keys = Object.keys(attrs).join(',');
      const old = reusable(parent, node => node.__tag === tag && node.__keys === keys);
      if (old) {
        for (const key in attrs) {
          const value = String(attrs[key]);
          if (old.__attrs[key] !== value) {
            old.setAttribute(key, value);
            old.__attrs[key] = value;
          }
        }
        return old;
      }
      const node = svgEl(null, tag, attrs);
      node.__tag = tag;
      node.__keys = keys;
      node.__attrs = {};
      for (const key in attrs) node.__attrs[key] = String(attrs[key]);
      if (parent) place(parent, node);
      return node;
    };

    const root = (parent, width, height) => svg(parent, 'svg', {width, height});

    /** HTML 的块：同一位置、同一个类、同一套样式名的旧块只改变了的样式与字 */
    const div = (parent, cls, style, text) => {
      const keys = `${cls}|${Object.keys(style).join(',')}`;
      const old = reusable(parent, node => node.__keys === keys);
      const node = old || el(null, 'div', cls);
      if (!old) {
        node.__keys = keys;
        node.__style = {};
        node.__text = undefined;
      }
      for (const key in style) {
        if (node.__style[key] !== style[key]) {
          node.style[key] = style[key];
          node.__style[key] = style[key];
        }
      }
      if (text !== undefined && node.__text !== text) {
        node.textContent = text;
        node.__text = text;
      }
      if (!old && parent) place(parent, node);
      return node;
    };

    // 画布的宽：只在第一次量（getBoundingClientRect 会让 weft 把整页排一遍，换描述时量一次就是几毫秒），之后用记下的；窗口变了再量
    let knownWidth = 0;
    const canvasWidth = host => {
      if (knownWidth > 100) return knownWidth;
      const rect = host.getBoundingClientRect();
      knownWidth = rect.width > 100 ? rect.width : 0;
      return knownWidth || 520;
    };
    window.addEventListener('resize', () => {
      knownWidth = 0;
    });

    const GRID = C.grid;

    const label = (host, x, y, text, opts = {}) => {
      const width = opts.width || 120;
      const style = {};
      if (opts.align === 'center') {
        style.width = `${width}px`;
        style.textAlign = 'center';
        style.left = `${x - width / 2}px`;
      } else if (opts.align === 'right') {
        style.width = `${width}px`;
        style.textAlign = 'right';
        style.left = `${x - width}px`;
      } else {
        style.left = `${x}px`;
      }
      style.top = `${y}px`;
      if (opts.color) style.color = opts.color;
      if (opts.bold) style.fontWeight = '600';
      // 长的说明折行（给了宽度时）
      if (opts.wrap) {
        style.whiteSpace = 'normal';
        style.width = `${width}px`;
      }
      return div(host, 'pp-figure-label', style, String(text));
    };

    /** 一块坐标区：box {l, t, w, h}，xDomain、yDomain；X、Y 把值换成 px，line 把点列换成路径 */
    const area = (box, xDomain, yDomain) => {
      const [x0, x1] = xDomain, [y0, y1] = yDomain;
      const X = x => box.l + (x - x0) / (x1 - x0 || 1) * box.w;
      const Y = y => box.t + (1 - (y - y0) / (y1 - y0 || 1)) * box.h;
      const line = points => points.map((p, i) => `${i ? 'L' : 'M'}${X(p[0]).toFixed(1)} ${Y(p[1]).toFixed(1)}`).join('');
      return {box, X, Y, line, x0, x1, y0, y1};
    };

    const sample = (fn, t0, t1, n = 160) => {
      const points = [];
      for (let i = 0; i <= n; ++i) {
        const t = t0 + (t1 - t0) * i / n;
        points.push([t, fn(t)]);
      }
      return points;
    };

    /** 轴：底边与左边的线、几道横的网格、刻度的字 */
    const axes = (root_, host, a, opts = {}) => {
      const {box} = a;
      svg(root_, 'line', {x1: box.l, y1: box.t + box.h, x2: box.l + box.w, y2: box.t + box.h, stroke: C.ink2, 'stroke-width': 1});
      svg(root_, 'line', {x1: box.l, y1: box.t, x2: box.l, y2: box.t + box.h, stroke: C.ink2, 'stroke-width': 1});
      for (const y of opts.yTicks || []) {
        svg(root_, 'line', {x1: box.l, y1: a.Y(y), x2: box.l + box.w, y2: a.Y(y), stroke: C.grid, 'stroke-width': 1});
        label(host, box.l - 6, a.Y(y) - 7, opts.yFormat ? opts.yFormat(y) : fmt(y), {align: 'right', width: 60});
      }
      for (const x of opts.xTicks || []) {
        svg(root_, 'line', {x1: a.X(x), y1: box.t + box.h, x2: a.X(x), y2: box.t + box.h + 4, stroke: C.ink2, 'stroke-width': 1});
        label(host, a.X(x), box.t + box.h + 6, opts.xFormat ? opts.xFormat(x) : fmt(x), {align: 'center', width: 60});
      }
      if (opts.xTitle) label(host, box.l + box.w, box.t + box.h + 20, opts.xTitle, {align: 'right', width: 160});
      if (opts.yTitle) label(host, box.l + 4, box.t - 16, opts.yTitle, {width: 240});
    };

    const niceTicks = (lo, hi, count = 4) => {
      const span = hi - lo;
      if (!(span > 0)) return [lo];
      const raw = span / count;
      const mag = Math.pow(10, Math.floor(Math.log10(raw)));
      const step = [1, 2, 2.5, 5, 10].map(m => m * mag).find(s => s >= raw) || raw;
      const ticks = [];
      for (let v = Math.ceil(lo / step) * step; v <= hi + 1e-9; v += step) ticks.push(Math.round(v * 1e6) / 1e6);
      return ticks;
    };

    return {paint, svgEl: svg, svgRoot: root, div, label, area, sample, axes, niceTicks, canvasWidth, GRID};
  })();

  // ===== 拖动：mousemove、mouseup 挂在 document 上，同一时刻只有一个 =====

  let drag = null;  // {move(x, y), end()}
  document.addEventListener('mousemove', e => {
    if (!drag) return;
    // 只认按着左键的移动：宿主补发的移动（UE 的 Slate 按真的指针位置合成的）不带按键，拖着时收到它说明键已经松开了
    if ((e.buttons & 1) === 0) {
      const ending = drag;
      drag = null;
      ending.end();
      return;
    }
    drag.move(e.clientX, e.clientY);
  });
  document.addEventListener('mouseup', () => {
    if (drag) {
      const ending = drag;
      drag = null;
      ending.end();
    }
  });
  const isDragging = () => drag !== null;

  // ===== 目录：图标加字；选中的那一格是从选项栏鼓出来的书签（上下两个反向圆角接进选项栏） =====

  const RAIL_W = 112;
  const RAIL_ITEM_H = 84;
  const RAIL_EMBLEM_H = 112;  // 与 theme.css 的 .pp-rail-emblem、.pp-rail-item 的高一致：书签按格子的序号算位置，不去量
  const TAB_R = 12;
  const TAB_X = 10;

  const tabPath = (closed) => {
    const W = RAIL_W, H = RAIL_ITEM_H, r = TAB_R, x0 = TAB_X;
    const outline = `M${W} 0A${r} ${r} 0 0 1 ${W - r} ${r}L${x0 + r} ${r}A${r} ${r} 0 0 0 ${x0} ${2 * r}L${x0} ${H}A${r} ${r} 0 0 0 ${x0 + r} ${H + r}`
      + `L${W - r} ${H + r}A${r} ${r} 0 0 1 ${W} ${H + 2 * r}`;
    return closed ? `${outline}L${W + 2} ${H + 2 * r}L${W + 2} 0Z` : outline;
  };

  /**
   * 目录：{emblem: {icon, text}, items: [{id, icon, label}], foot, onSelect(id)}；返回 {select(id), root}
   * 书签只有一张，换目录时滑到新的一格（theme.css 里 .pp-rail-tab 的 transform 过渡）
   */
  const rail = (parent, opts) => {
    const root = el(parent, 'div', 'pp-rail');
    const tab = el(root, 'div', 'pp-rail-tab');
    if (darkTheme) {
      const tabShape = svgRoot(tab, RAIL_W, RAIL_ITEM_H);
      svgEl(tabShape, 'rect', {x: 0, y: 12, width: 2, height: RAIL_ITEM_H - 24, fill: C.ginkgo});
      svgEl(tabShape, 'rect', {x: 2, y: 6, width: RAIL_W - 2, height: RAIL_ITEM_H - 12, fill: 'rgba(229, 180, 67, 0.11)'});
    } else {
      const tabShape = svgRoot(tab, RAIL_W + 2, RAIL_ITEM_H + 2 * TAB_R);
      svgEl(tabShape, 'path', {d: tabPath(true), fill: C.paper0, stroke: 'none'});
      svgEl(tabShape, 'path', {d: tabPath(false), fill: 'none', stroke: C.edge, 'stroke-width': 1});
    }
    const top = opts.emblem ? RAIL_EMBLEM_H : 0;
    if (opts.emblem) {
      const emblem = el(root, 'div', 'pp-rail-emblem');
      icon(emblem, opts.emblem.icon || 'leaf', 30, C.ginkgo, {stroke: 1.2});
      el(emblem, 'div', 'pp-rail-emblem-text', opts.emblem.text || '');
    }
    const entries = new Map();
    opts.items.forEach((item, index) => {
      const node = el(root, 'div', 'pp-rail-item');
      const glyph = icon(node, item.icon, 28, C.ink2, {cls: 'pp-rail-icon', stroke: 1.5});
      el(node, 'div', 'pp-rail-label', item.label);
      node.addEventListener('click', () => opts.onSelect && opts.onSelect(item.id));
      if (opts.onHover) {
        node.addEventListener('mouseenter', () => opts.onHover(item.id));
        node.addEventListener('mouseleave', () => opts.onHover(null));
      }
      entries.set(item.id, {node, glyph, index});
    });
    if (opts.foot) el(root, 'div', 'pp-rail-foot', opts.foot);
    let selected = null;
    const select = id => {
      if (id === selected) return;
      selected = id;
      for (const [key, entry] of entries) {
        const on = key === id;
        entry.node.className = on ? 'pp-rail-item pp-rail-item-on' : 'pp-rail-item';
        recolor(entry.glyph, on ? C.bronze : C.ink2);
      }
      const entry = entries.get(id);
      tab.style.display = entry ? 'block' : 'none';
      if (entry) tab.style.transform = `translateY(${top + entry.index * RAIL_ITEM_H - (darkTheme ? 0 : TAB_R)}px)`;
    };
    return {root, select, entries};
  };

  // ===== 小装饰：纸页为银杏叶，暗黑扁平为旋转香槟金菱形 + 发丝线 =====

  const ornament = (parent, width = 220) => {
    const box = el(parent, 'div', 'pp-ornament');
    if (darkTheme) {
      const root = svgRoot(box, width, 16);
      svgEl(root, 'path', {d: 'M5 3L10 8L5 13L0 8Z', fill: C.ginkgo, stroke: 'none'});
      svgEl(root, 'path', {d: `M18 8H${width}`, stroke: 'rgba(232, 207, 156, 0.34)', 'stroke-width': 1, fill: 'none'});
      return box;
    }
    const root = svgRoot(box, width, 16);
    const leaf = svgEl(root, 'g', {transform: 'translate(0 0) scale(0.6667)'});
    svgEl(leaf, 'path', {d: ICONS.leaf[0][1], fill: C.ginkgo, stroke: C.goldLo, 'stroke-width': 1.2});
    svgEl(root, 'path', {d: `M18 8.5H${width - 8}`, stroke: C.goldLo, 'stroke-width': 1, fill: 'none'});
    svgEl(root, 'circle', {cx: width - 4, cy: 8.5, r: 2, fill: C.goldLo});
    return box;
  };

  // ===== 类别：中文名、英文副标、拖到栏边的发丝线，点标题折起、展开 =====

  /** {title, en, note, open, count, onToggle(open), onHover(entering)}；返回 {root, body, setOpen(open), setCount(text), setNote(text)} */
  const section = (parent, opts) => {
    const root = el(parent, 'div', 'pp-section');
    const head = el(root, 'div', 'pp-section-head');
    const chevron = el(head, 'div', 'pp-section-chevron');
    el(head, 'div', 'pp-section-title', opts.title);
    if (opts.en) el(head, 'div', 'pp-section-en', opts.en);
    el(head, 'div', 'pp-section-rule');
    let countText = opts.count || '';
    const count = el(head, 'div', 'pp-section-count', countText);
    const body = el(root, 'div', 'pp-section-body');
    let noteText = opts.note || '';
    let note = noteText ? el(body, 'div', 'pp-section-note', noteText) : null;
    let open = opts.open !== false;
    const setOpen = value => {
      open = value;
      root.className = open ? 'pp-section' : 'pp-section pp-section-closed';
      clear(chevron);
      icon(chevron, open ? 'chevronDown' : 'chevronRight', 14, C.ink2, {stroke: 1.6});
    };
    setOpen(open);
    head.addEventListener('click', () => {
      setOpen(!open);
      if (opts.onToggle) opts.onToggle(open);
    });
    if (opts.onHover) {
      head.addEventListener('mouseenter', () => opts.onHover(true));
      head.addEventListener('mouseleave', () => opts.onHover(false));
    }
    // 设置器在值没变时不碰 DOM：weft 里写同样的值也算改动，整页的样式要重算
    const setCount = text => {
      if ((text || '') === countText) return;
      countText = text || '';
      count.textContent = countText;
    };
    const setNote = text => {
      if ((text || '') === noteText) return;
      noteText = text || '';
      if (!note && noteText) {
        note = el(null, 'div', 'pp-section-note');
        body.insertBefore(note, body.firstChild);
      }
      if (note) note.textContent = noteText;
    };
    return {root, body, setOpen, setCount, setNote};
  };

  // ===== 选项：洗染的标签条（图标 + 名字 + 改过的小点）+ 控件 =====

  // 标签条的颜色（一个类别一种，Scripts/UI/bake_wash.py 烘的，theme.css 里按"颜色-种子"取图）与每种颜色的种子数
  const STRIP_PALETTES = ['tan', 'jade', 'plum', 'slate', 'rust', 'moss'];
  const STRIP_SEEDS = 6;

  /** {icon, rotate, label, seed, palette}；返回 {root, label, control, setState({on, changed, off})} */
  const option = (parent, opts) => {
    const root = el(parent, 'div', 'pp-option');
    const seed = (opts.seed || 0) % STRIP_SEEDS;
    const palette = STRIP_PALETTES.includes(opts.palette) ? opts.palette : STRIP_PALETTES[0];
    const label = el(root, 'div', darkTheme ? 'pp-label' : `pp-label pp-strip-${palette}-${seed} pp-seed-${seed}`);
    const glyph = icon(label, opts.icon || 'dot', 20, darkTheme ? C.ink2 : C.ink1, {cls: 'pp-label-icon', rotate: opts.rotate});
    el(label, 'div', 'pp-label-text', opts.label);
    el(label, 'div', 'pp-label-mark');
    const control = el(root, 'div', 'pp-control');
    const state = {on: false, changed: false, off: false};
    let className = 'pp-option';
    const setState = next => {
      Object.assign(state, next);
      const name = 'pp-option' + (state.on ? ' pp-option-on' : '') + (state.changed ? ' pp-option-changed' : '') + (state.off ? ' pp-option-off' : '');
      if (name === className) return;
      className = name;
      root.className = name;
      if (darkTheme) recolor(glyph, state.on ? C.ginkgo : C.ink2);
    };
    return {root, label, glyph, control, setState, state};
  };

  // ===== 滑块：轨道、从零（或下限）到值的填充、默认值的刻线、钮 =====

  /**
   * {lo, hi, value, def, step, onInput(v)（拖动中）, onCommit(v)（按下与松开）}；返回 {root, set(v), setRange(lo, hi)}
   * 值超出范围时滑块放大到装得下（可夹的一头不放大，由调用者给 hard 范围）
   */
  const slider = (parent, opts) => {
    const root = el(parent, 'div', 'pp-slider');
    el(root, 'div', 'pp-slider-track');
    const fill = el(root, 'div', 'pp-slider-fill');
    const tick = el(root, 'div', 'pp-slider-tick');
    const knob = el(root, 'div', 'pp-slider-knob');
    let lo = opts.lo, hi = opts.hi, value = opts.value;
    const hard = opts.hard || {};
    const fraction = v => hi > lo ? clamp((v - lo) / (hi - lo), 0, 1) : 0;
    let placed = '';
    const place = () => {
      if (value < lo && hard.lo === undefined) lo = value - 0.25 * (hi - value);
      if (value > hi && hard.hi === undefined) hi = value + 0.25 * (value - lo);
      // 位置没变就不写（weft 里写同样的值也算改动）
      const key = `${value}|${lo}|${hi}`;
      if (key === placed) return;
      placed = key;
      const t = fraction(value);
      const origin = fraction(lo <= 0 && hi >= 0 ? 0 : lo);
      knob.style.left = `${t * 100}%`;
      fill.style.left = `${Math.min(t, origin) * 100}%`;
      fill.style.width = `${Math.abs(t - origin) * 100}%`;
      if (opts.def !== undefined && Number.isFinite(opts.def)) {
        tick.style.left = `${fraction(opts.def) * 100}%`;
        tick.style.visibility = 'visible';
      } else {
        tick.style.visibility = 'hidden';
      }
    };
    const snap = v => {
      const step = opts.step || Math.pow(10, Math.floor(Math.log10(Math.max(hi - lo, 1e-6) / 1000)));
      let out = Math.round(v / step) * step;
      out = Math.round(out * 1e6) / 1e6;
      if (hard.lo !== undefined) out = Math.max(out, hard.lo);
      if (hard.hi !== undefined) out = Math.min(out, hard.hi);
      return out;
    };
    const fromX = x => {
      const rect = root.getBoundingClientRect();
      if (rect.width <= 0) return value;
      return snap(lo + clamp((x - rect.left) / rect.width, 0, 1) * (hi - lo));
    };
    root.addEventListener('mousedown', e => {
      value = fromX(e.clientX);
      place();
      if (opts.onInput) opts.onInput(value);
      drag = {
        move: x => {
          const next = fromX(x);
          if (next === value) return;
          value = next;
          place();
          if (opts.onInput) opts.onInput(value);
        },
        end: () => {
          if (opts.onCommit) opts.onCommit(value);
        },
      };
    });
    place();
    return {
      root,
      set(v) {
        value = v;
        place();
      },
      setRange(nextLo, nextHi) {
        lo = nextLo;
        hi = nextHi;
        place();
      },
      get value() { return value; },
    };
  };

  /** 数的输入框：回车或离开时交；{value, format(v), onCommit(v)}；返回 {root, set(v)} */
  const numberField = (parent, opts) => {
    const input = el(parent, 'input', 'pp-number');
    input.setAttribute('type', 'text');
    const format = opts.format || (v => fmt(v, 3));
    let shown = null;
    const set = v => {
      if (document.activeElement === input) return;
      const text = format(v);
      if (text === shown && input.value === text) return;
      shown = text;
      input.value = text;
    };
    input.addEventListener('change', () => {
      const parsed = parseFloat(String(input.value).replace('−', '-'));
      if (Number.isFinite(parsed) && opts.onCommit) opts.onCommit(parsed);
    });
    set(opts.value);
    return {root: input, set};
  };

  /** 开关：{value, onText, offText, onChange(v)}；返回 {root, set(v)} */
  const toggle = (parent, opts) => {
    const root = el(parent, 'div', 'pp-toggle');
    el(root, 'div', 'pp-toggle-knob');
    const text = el(parent, 'div', 'pp-toggle-text');
    let value = !!opts.value;
    let shown = null;
    const set = v => {
      value = !!v;
      if (value === shown) return;
      shown = value;
      root.className = value ? 'pp-toggle pp-toggle-on' : 'pp-toggle';
      text.textContent = value ? (opts.onText || '开') : (opts.offText || '关');
    };
    root.addEventListener('click', () => opts.onChange && opts.onChange(!value));
    set(value);
    return {root, set};
  };

  /** 分段的选择：{options: [{value, label}], value, onChange(v)}；返回 {root, set(v)} */
  const segmented = (parent, opts) => {
    const root = el(parent, 'div', 'pp-seg');
    const nodes = opts.options.map((option, index) => {
      const node = el(root, 'div', 'pp-seg-item', option.label);
      node.addEventListener('click', () => opts.onChange && opts.onChange(option.value));
      return {node, option, first: index === 0};
    });
    let shown;
    const set = v => {
      if (v === shown) return;
      shown = v;
      for (const entry of nodes) {
        entry.node.className = 'pp-seg-item' + (entry.first ? ' pp-seg-first' : '') + (entry.option.value === v ? ' pp-seg-on' : '');
      }
    };
    set(opts.value);
    return {root, set};
  };

  const resetButton = (parent, onClick) => {
    const root = el(parent, 'div', 'pp-reset');
    icon(root, 'reset', 16, C.goldDeep, {stroke: 1.8});
    root.addEventListener('click', e => {
      e.stopPropagation();
      onClick();
    });
    return root;
  };

  const button = (parent, text, primary, onClick) => {
    const root = el(parent, 'div', primary ? 'pp-btn pp-btn-primary' : 'pp-btn', text);
    root.addEventListener('click', onClick);
    return root;
  };

  // ===== 页面底部的一排动作（设置界面右下角的恢复默认、确认、撤销修改）与问一句的对话框 =====

  /**
   * 一排按钮：[{id, label, primary, hint}]，hint 是手柄上按哪个键（默认不显示，setHints(true) 时显示）。onPress(id)。
   * 返回 {root, setDisabled(id, bool), setFocus(id 或 null), setHints(bool), setStatus(文字, 是否警告)}；设置器值没变时不碰 DOM
   */
  const actionBar = (parent, buttons, onPress) => {
    const root = el(parent, 'div', 'pp-actions');
    const status = el(root, 'div', 'pp-actions-status', '');
    status.style.display = 'none';
    const entries = new Map();
    for (const item of buttons) {
      const node = el(root, 'div', item.primary ? 'pp-action pp-action-primary' : 'pp-action');
      const hint = item.hint ? el(node, 'div', 'pp-action-hint', item.hint) : null;
      if (hint) hint.style.display = 'none';
      el(node, 'div', 'pp-action-text', item.label);
      const entry = {node, hint, item, disabled: false, focused: false};
      node.addEventListener('click', () => {
        if (!entry.disabled) onPress(item.id);
      });
      entries.set(item.id, entry);
    }
    const restyle = entry => {
      entry.node.className = `pp-action${entry.item.primary ? ' pp-action-primary' : ''}${entry.disabled ? ' pp-action-disabled' : ''}${entry.focused ? ' pp-action-focus' : ''}`;
    };
    let statusText = '';
    let statusWarn = false;
    let hints = false;
    return {
      root,
      setDisabled(id, disabled) {
        const entry = entries.get(id);
        if (!entry || entry.disabled === !!disabled) return;
        entry.disabled = !!disabled;
        restyle(entry);
      },
      isDisabled: id => !!(entries.get(id) && entries.get(id).disabled),
      setFocus(id) {
        for (const [key, entry] of entries) {
          const focused = key === id;
          if (entry.focused === focused) continue;
          entry.focused = focused;
          restyle(entry);
        }
      },
      setHints(show) {
        if (hints === !!show) return;
        hints = !!show;
        for (const entry of entries.values()) {
          if (entry.hint) entry.hint.style.display = hints ? 'flex' : 'none';
        }
      },
      setStatus(text, warn) {
        if (text === statusText && !!warn === statusWarn) return;
        statusText = text;
        statusWarn = !!warn;
        status.textContent = text;
        status.className = warn ? 'pp-actions-status pp-actions-status-warn' : 'pp-actions-status';
        status.style.display = text ? 'block' : 'none';
      },
    };
  };

  /**
   * 问一句：盖在页面上的一张纸，{title, text, buttons: [{id, label, primary}]}；onPress(id)。返回 {root, setFocus(id), close()}。
   * 焦点与方向键由页面管（页面的键盘、手柄是自己接的）
   */
  const dialog = (parent, opts, onPress) => {
    const root = el(parent, 'div', 'pp-dialog-scrim');
    const card = el(root, 'div', 'pp-dialog');
    el(card, 'div', 'pp-dialog-title', opts.title);
    el(card, 'div', 'pp-dialog-text', opts.text);
    const row = el(card, 'div', 'pp-dialog-actions');
    const nodes = new Map();
    for (const item of opts.buttons) {
      const node = el(row, 'div', item.primary ? 'pp-action pp-action-primary' : 'pp-action');
      el(node, 'div', 'pp-action-text', item.label);
      node.addEventListener('click', () => onPress(item.id));
      nodes.set(item.id, {node, item});
    }
    let closed = false;
    const close = immediate => {
      if (closed) return;
      closed = true;
      if (!darkTheme || immediate || !root.parentNode) {
        if (root.parentNode) root.parentNode.removeChild(root);
        return;
      }
      // 暗黑现代主题（设置页）播放轻快退场过渡，同时禁用指针以免阻挡底层交互
      root.className = 'pp-dialog-scrim pp-dialog-closing';
      const finishAt = performance.now() + 135;
      const finish = () => {
        if (performance.now() >= finishAt) {
          if (root.parentNode) root.parentNode.removeChild(root);
          return;
        }
        requestAnimationFrame(finish);
      };
      requestAnimationFrame(finish);
    };
    return {
      root,
      setFocus(id) {
        for (const [key, entry] of nodes) {
          entry.node.className = `pp-action${entry.item.primary ? ' pp-action-primary' : ''}${key === id ? ' pp-action-focus' : ''}`;
        }
      },
      close,
    };
  };

  /** 右上角的关闭：细双圈的圆钮，下面挂写热键的小签 */
  const closeButton = (parent, key, onClick) => {
    const root = el(parent, 'div', 'pp-close');
    const ring = el(root, 'div', 'pp-close-ring');
    icon(ring, 'close', 18, darkTheme ? C.ink1 : C.bronze, {stroke: 1.8});
    root.addEventListener('click', onClick);
    const label = el(parent, 'div', 'pp-close-key', key || '');
    return {root, label};
  };

  // ===== 富文本：说明里的 **粗**、`代码`、[[按当前值算的数]]、$行内公式$ =====

  const rich = (parent, text, cls = 'pp-p') => {
    const node = el(parent, 'div', cls);
    const pattern = /(\*\*[^*]+\*\*|`[^`]+`|\[\[[^\]]+\]\]|\$[^$]+\$)/g;
    let last = 0;
    let match;
    const source = String(text);
    while ((match = pattern.exec(source))) {
      if (match.index > last) el(node, 'span', null, source.slice(last, match.index));
      const token = match[0];
      if (token.startsWith('**')) el(node, 'span', 'pp-b', token.slice(2, -2));
      else if (token.startsWith('`')) el(node, 'span', 'pp-code', token.slice(1, -1));
      else if (token.startsWith('[[')) el(node, 'span', 'pp-v', token.slice(2, -2));
      else inlineMath(node, token.slice(1, -1));
      last = match.index + token.length;
    }
    if (last < source.length) el(node, 'span', null, source.slice(last));
    return node;
  };

  // ===== 公式 =====

  const GREEK = {
    alpha: 'α', beta: 'β', gamma: 'γ', delta: 'δ', epsilon: 'ε', varepsilon: 'ε', zeta: 'ζ', eta: 'η', theta: 'θ', vartheta: 'ϑ', iota: 'ι',
    kappa: 'κ', lambda: 'λ', mu: 'μ', nu: 'ν', xi: 'ξ', pi: 'π', rho: 'ρ', sigma: 'σ', tau: 'τ', upsilon: 'υ', phi: 'ϕ', varphi: 'φ',
    chi: 'χ', psi: 'ψ', omega: 'ω', Gamma: 'Γ', Delta: 'Δ', Theta: 'Θ', Lambda: 'Λ', Xi: 'Ξ', Pi: 'Π', Sigma: 'Σ', Phi: 'Φ',
    Psi: 'Ψ', Omega: 'Ω',
  };
  // 关系符两边宽、二元运算符两边窄、其余不加
  const RELATIONS = {
    '=': '=', '<': '<', '>': '>', le: '≤', leq: '≤', ge: '≥', geq: '≥', ne: '≠', neq: '≠', approx: '≈', equiv: '≡', propto: '∝', sim: '∼',
    to: '→', rightarrow: '→', Rightarrow: '⇒', gets: '←', mapsto: '↦', in: '∈', coloneqq: '≔',
  };
  const BINARIES = {'+': '+', '-': '−', '*': '∗', cdot: '·', times: '×', pm: '±', mp: '∓', div: '÷', circ: '∘'};
  const SYMBOLS = {
    infty: '∞', partial: '∂', nabla: '∇', sum: '∑', prod: '∏', int: '∫', ldots: '…', cdots: '⋯', prime: '′', degree: '°',
    lvert: '|', rvert: '|', vert: '|', lfloor: '⌊', rfloor: '⌋', lceil: '⌈', rceil: '⌉', langle: '⟨', rangle: '⟩', '{': '{', '}': '}',
    '|': '‖', '%': '%', '#': '#', '&': '&', '_': '_',
  };
  const FUNCTIONS = ['sin', 'cos', 'tan', 'exp', 'ln', 'log', 'max', 'min', 'arctan', 'atan', 'sgn', 'clamp', 'lerp', 'smoothstep', 'deg', 'rad'];
  // 附加符：组合字符（行内的回退用）与单独排在上面的那个字（行间的回退用）
  const ACCENTS = {dot: '̇', ddot: '̈', bar: '̄', hat: '̂', tilde: '̃', vec: '⃗'};
  const ACCENT_TEXT = {dot: '˙', ddot: '¨', bar: '¯', hat: 'ˆ', tilde: '˜', vec: '→'};
  const SPACES = {',': 0.17, ':': 0.22, ';': 0.28, quad: 1, qquad: 2, ' ': 0.25, '!': -0.17};

  const tokenize = tex => {
    const tokens = [];
    for (let i = 0; i < tex.length;) {
      const ch = tex[i];
      if (ch === '\\') {
        const match = /^\\([a-zA-Z]+|.)/.exec(tex.slice(i));
        tokens.push({cmd: match[1]});
        i += match[0].length;
      } else if (/\s/.test(ch)) {
        i += 1;
      } else {
        tokens.push({ch});
        i += 1;
      }
    }
    return tokens;
  };

  // 语法树：{t: 'row', items} | {t: 'frac', num, den} | {t: 'sqrt', body} | {t: 'scripts', base, sup, sub} | {t: 'atom', kind, s} | {t: 'space', w}
  const parse = tex => {
    const tokens = tokenize(tex);
    let pos = 0;
    const peek = () => tokens[pos];
    const textArg = () => {
      // \text{...}：原样取到配对的右括号（含空格），从源文字里取
      let depth = 0;
      let out = '';
      if (!peek() || peek().ch !== '{') return out;
      pos += 1;
      depth = 1;
      while (pos < tokens.length) {
        const token = tokens[pos++];
        if (token.ch === '{') depth += 1;
        if (token.ch === '}') {
          depth -= 1;
          if (depth === 0) break;
        }
        out += token.ch !== undefined ? token.ch : (token.cmd === ' ' || token.cmd === ',' ? ' ' : (GREEK[token.cmd] || ''));
      }
      return out;
    };
    const group = () => {
      const token = peek();
      if (!token) return {t: 'row', items: []};
      if (token.ch === '{') {
        pos += 1;
        const row = rowUntil('}');
        pos += 1;
        return row;
      }
      return atom();
    };
    const atom = () => {
      const token = tokens[pos++];
      if (token.ch !== undefined) {
        const ch = token.ch;
        if (/[a-zA-Z]/.test(ch)) return {t: 'atom', kind: 'mi', s: ch};
        if (/[0-9.]/.test(ch)) {
          let s = ch;
          while (peek() && peek().ch !== undefined && /[0-9.]/.test(peek().ch)) s += tokens[pos++].ch;
          return {t: 'atom', kind: 'mn', s};
        }
        if (RELATIONS[ch]) return {t: 'atom', kind: 'rel', s: RELATIONS[ch]};
        if (BINARIES[ch]) return {t: 'atom', kind: 'bin', s: BINARIES[ch]};
        if (ch === ',' || ch === ';') return {t: 'atom', kind: 'punct', s: ch};
        if (ch === '\'') return {t: 'atom', kind: 'mo', s: '′'};
        if (ch === '~') return {t: 'space', w: 0.25};
        return {t: 'atom', kind: 'mo', s: ch};
      }
      const cmd = token.cmd;
      if (cmd === 'frac' || cmd === 'dfrac' || cmd === 'tfrac') return {t: 'frac', num: group(), den: group()};
      if (cmd === 'sqrt') return {t: 'sqrt', body: group()};
      if (cmd === 'text' || cmd === 'mathrm' || cmd === 'textrm' || cmd === 'operatorname' || cmd === 'mathsf') {
        const text = textArg();
        return {t: 'atom', kind: cmd === 'operatorname' ? 'fn' : 'text', s: text};
      }
      if (cmd === 'mathbf' || cmd === 'boldsymbol' || cmd === 'mathit') return group();
      if (cmd === 'mathrel' || cmd === 'mathbin' || cmd === 'mathop' || cmd === 'mathord') return group();
      if (cmd === 'displaystyle' || cmd === 'textstyle') return {t: 'row', items: []};
      if (cmd === 'left' || cmd === 'right' || cmd === 'big' || cmd === 'Big' || cmd === 'bigl' || cmd === 'bigr') {
        const next = tokens[pos++];
        if (!next || next.ch === '.') return {t: 'row', items: []};
        return {t: 'atom', kind: 'fence', s: next.ch !== undefined ? next.ch : (SYMBOLS[next.cmd] || '')};
      }
      if (ACCENTS[cmd]) {
        const base = group();
        return {t: 'accent', base, mark: ACCENTS[cmd], markText: ACCENT_TEXT[cmd]};
      }
      if (GREEK[cmd]) return {t: 'atom', kind: /^[A-Z]/.test(cmd) ? 'mn' : 'mi', s: GREEK[cmd]};
      if (RELATIONS[cmd]) return {t: 'atom', kind: 'rel', s: RELATIONS[cmd]};
      if (BINARIES[cmd]) return {t: 'atom', kind: 'bin', s: BINARIES[cmd]};
      if (SPACES[cmd] !== undefined) return {t: 'space', w: SPACES[cmd]};
      if (FUNCTIONS.includes(cmd)) return {t: 'atom', kind: 'fn', s: cmd};
      if (cmd === 'sum' || cmd === 'prod' || cmd === 'int') return {t: 'atom', kind: 'big', s: SYMBOLS[cmd]};
      if (SYMBOLS[cmd]) return {t: 'atom', kind: 'mo', s: SYMBOLS[cmd]};
      return {t: 'atom', kind: 'text', s: cmd};
    };
    const rowUntil = close => {
      const items = [];
      while (pos < tokens.length && !(close && peek().ch === close)) {
        let node = atom();
        // 上下标：base^{…}_{…}，次序随意
        let sup = null, sub = null;
        while (peek() && (peek().ch === '^' || peek().ch === '_')) {
          const which = tokens[pos++].ch;
          const arg = group();
          if (which === '^') sup = arg;
          else sub = arg;
        }
        if (sup || sub) node = {t: 'scripts', base: node, sup, sub};
        items.push(node);
      }
      return {t: 'row', items};
    };
    return rowUntil(null);
  };

  const MIN_FONT = 10;

  // 排版：每个节点给出明确的字号（px），分数里缩小、上下标缩小，最小 10 px
  const layout = (parent, node, size, display) => {
    const box = (cls) => el(parent, 'div', cls);
    if (node.t === 'row') {
      const row = box('pp-mrow');
      node.items.forEach((item, index) => layout(row, item, size, display));
      return row;
    }
    if (node.t === 'space') {
      const space = box('');
      space.style.width = `${Math.max(node.w * size, 0)}px`;
      space.style.height = '1px';
      if (node.w < 0) space.style.marginLeft = `${node.w * size}px`;
      return space;
    }
    if (node.t === 'atom') {
      const kind = node.kind;
      const cls = kind === 'mi' ? 'pp-mi' : kind === 'mn' ? 'pp-mn' : kind === 'text' ? 'pp-mtext' : kind === 'fn' ? 'pp-mn' : 'pp-mo';
      const span = el(parent, 'span', cls, node.s);
      let fontSize = size;
      if (kind === 'big') fontSize = size * 1.35;
      if (kind === 'fence') fontSize = size * 1.15;
      if (kind === 'text') fontSize = size * 0.8;
      span.style.fontSize = `${fontSize}px`;
      span.style.lineHeight = `${Math.round(fontSize * 1.2)}px`;
      const pad = kind === 'rel' ? 0.28 : kind === 'bin' ? 0.22 : kind === 'fn' ? 0.08 : 0;
      if (pad) {
        span.style.paddingLeft = `${pad * size}px`;
        span.style.paddingRight = `${pad * size}px`;
      }
      if (kind === 'punct') span.style.paddingRight = `${0.17 * size}px`;
      if (kind === 'fn') span.style.paddingRight = `${0.17 * size}px`;
      if (kind === 'text') {
        span.style.paddingLeft = `${0.12 * size}px`;
        span.style.paddingRight = `${0.12 * size}px`;
      }
      return span;
    }
    if (node.t === 'accent') {
      // 附加符（点、两点、横、帽）单独一层压在字的上面：weft 不画组合附加符（U+0307 这些）。
      // 字照常排，符号绝对定位在字的上方、不占行高；斜体的字往右靠一点
      const wrap = box('pp-mrow');
      wrap.style.position = 'relative';
      layout(wrap, node.base, size, display);
      const mark = el(wrap, 'div', 'pp-mo', node.markText);
      Object.assign(mark.style, {position: 'absolute', left: `${size * 0.12}px`, right: '0px', top: `${-size * 0.3}px`, textAlign: 'center',
        fontSize: `${size * 0.9}px`, lineHeight: `${Math.round(size)}px`});
      return wrap;
    }
    if (node.t === 'frac') {
      const frac = box('pp-mfrac');
      const inner = Math.max(size * (display ? 0.92 : 0.78), MIN_FONT);
      frac.style.marginLeft = `${0.1 * size}px`;
      frac.style.marginRight = `${0.1 * size}px`;
      const num = el(frac, 'div', 'pp-mrow');
      num.style.padding = `0 ${0.18 * size}px ${Math.max(1, 0.08 * size)}px ${0.18 * size}px`;
      layout(num, node.num, inner, false);
      const den = el(frac, 'div', 'pp-mrow pp-mfrac-den');
      den.style.padding = `${Math.max(1, 0.1 * size)}px ${0.18 * size}px 0 ${0.18 * size}px`;
      layout(den, node.den, inner, false);
      return frac;
    }
    if (node.t === 'sqrt') {
      const row = box('pp-mrow');
      const sign = el(row, 'span', 'pp-mo', '√');
      sign.style.fontSize = `${size * 1.1}px`;
      sign.style.lineHeight = `${Math.round(size * 1.3)}px`;
      const body = el(row, 'div', 'pp-msqrt-body');
      body.style.padding = `${Math.max(1, 0.06 * size)}px ${0.1 * size}px 0 ${0.06 * size}px`;
      layout(body, node.body, size, display);
      return row;
    }
    if (node.t === 'scripts') {
      const row = box('pp-mrow');
      layout(row, node.base, size, display);
      const scripts = el(row, 'div', 'pp-mscripts');
      scripts.style.marginLeft = `${0.05 * size}px`;
      const small = Math.max(size * 0.7, MIN_FONT);
      // 只有上标：上标下面垫一块，整列居中时上标在中线上方；只有下标反过来
      const spacer = h => {
        const s = el(scripts, 'div', '');
        s.style.height = `${h}px`;
      };
      if (node.sup) layout(el(scripts, 'div', 'pp-mrow'), node.sup, small, false);
      if (node.sup && !node.sub) spacer(size * 0.75);
      if (node.sub && !node.sup) spacer(size * 0.55);
      if (node.sub) layout(el(scripts, 'div', 'pp-mrow'), node.sub, small, false);
      return row;
    }
    return box('');
  };

  // KaTeX：页面带了 katex.min.js、样式表前面接了 katex.css，并且 weft 认得它的排版（新版 weft 的 document.compatMode 是 'CSS1Compat'，
  // 旧版没有这个属性：旧版一碰到 KaTeX 的样式就整帧不画，所以那时不建 KaTeX 的 DOM）
  const katexAvailable = () => typeof globalThis.katex === 'object' && typeof globalThis.katex.render === 'function'
    && document.compatMode === 'CSS1Compat';

  /**
   * 排一条公式：有 KaTeX 用 KaTeX，没有用回退版。{display: 行间（默认）或行内, size: 字号 px}；返回用了哪一个（'katex' | 'fallback'）
   */
  const math = (parent, tex, opts = {}) => {
    const display = opts.display !== false;
    const size = opts.size || (display ? 19 : 15);
    if (katexAvailable()) {
      // 只出 HTML（weft 不画 MathML）；行间的公式用行内模式加 \displaystyle：.katex-display 的上下外边距在 weft 里不折叠，位置会差 1em
      const host = el(parent, 'span', 'pp-katex');
      host.style.fontSize = `${size / 1.21}px`;
      try {
        globalThis.katex.render(display ? `\\displaystyle ${tex}` : tex, host, {output: 'html', throwOnError: false});
        return 'katex';
      } catch (error) {
        parent.removeChild(host);
      }
    }
    const root = el(parent, 'div', 'pp-math');
    layout(root, parse(tex), size, display);
    return 'fallback';
  };

  // 行内的公式：回退版不做上下标的上下错位（weft 的行内盒只按基线对齐），下标写成小一号的字，读得懂即可
  const inlineMath = (parent, tex) => {
    if (katexAvailable()) {
      // 行内的公式小一号：KaTeX 的字是外面的 1.21 倍，13 px 时约 15.7 px，与 15 px 的正文相当
      const host = el(parent, 'span', 'pp-katex-inline');
      host.style.fontSize = '13px';
      try {
        globalThis.katex.render(tex, host, {output: 'html', throwOnError: false});
        return;
      } catch (error) {
        parent.removeChild(host);
      }
    }
    const flatten = node => {
      if (node.t === 'row') return node.items.map(flatten).join('');
      if (node.t === 'atom') return node.s;
      if (node.t === 'space') return node.w > 0 ? ' ' : '';
      if (node.t === 'frac') return `${flatten(node.num)}/${flatten(node.den)}`;
      if (node.t === 'sqrt') return `√${flatten(node.body)}`;
      if (node.t === 'accent') return flatten(node.base) + node.mark;
      return '';
    };
    const walk = node => {
      if (node.t === 'row') {
        node.items.forEach(walk);
      } else if (node.t === 'scripts') {
        walk(node.base);
        if (node.sub) el(parent, 'span', 'pp-mi pp-msub-inline', flatten(node.sub)).style.fontSize = '0.72em';
        if (node.sup) el(parent, 'span', 'pp-mn pp-msup-inline', flatten(node.sup)).style.fontSize = '0.72em';
      } else if (node.t === 'atom') {
        const cls = node.kind === 'mi' ? 'pp-mi' : node.kind === 'text' ? '' : 'pp-mn';
        const pad = node.kind === 'rel' || node.kind === 'bin';
        el(parent, 'span', cls, pad ? ` ${node.s} ` : node.s);
      } else if (node.t === 'frac' || node.t === 'sqrt' || node.t === 'accent') {
        el(parent, 'span', 'pp-mn', flatten(node));
      } else if (node.t === 'space' && node.w > 0) {
        el(parent, 'span', null, ' ');
      }
    };
    walk(parse(tex));
  };

  globalThis.Paper = {
    C, useDarkTheme, el, svgEl, svgRoot, clear, clamp, fmt, icon, recolor, registerIcons, isDragging, rail, ornament, section, option, slider, numberField, toggle,
    segmented, resetButton, button, closeButton, rich, math, inlineMath, katexAvailable, RAIL_W, STRIP_PALETTES, restIntent, sketch, actionBar, dialog,
  };
})();
