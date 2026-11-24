// 游戏里的 toast：一次显示一条，排队、什么时候换下一条都在 C++（FGinkgoToastHud）。
//
// 游戏发两种事件：
//   "toast-show"：{icon, title, body, inMs}：这一条出现。icon 是 Gameplay、Tutorial、Saved
//   "toast-hide"：{outMs}：这一条消失
// 没有 toast 时元素 display: none。
(() => {
  const engine = weft.engine;
  const root = document.getElementById('root');
  const SVG_NS = 'http://www.w3.org/2000/svg';

  const SLIDE_IN = 48;    // 出现时从右边多少 px 滑进来
  const SLIDE_OUT = 28;   // 消失时往右滑多少 px
  const SEED_STEP = 7.31; // 每条换一条不同的边

  // 24 的格子、圆头圆角的线（同 Theme/paper.js 的图标）：线 p、实心圆 c
  const ICON_SIZE = 34;
  const ICON_STROKE = 1.7;
  const ICON_COLOR = '#FFD486';
  const ICONS = {
    Gameplay: {
      p: ['M7.6 8h8.8a4.4 4.4 0 0 1 4.3 3.5l.9 4.3a2.4 2.4 0 0 1-4.2 2l-1.9-2.3H8.5l-1.9 2.3a2.4 2.4 0 0 1-4.2-2l.9-4.3A4.4 4.4 0 0 1 7.6 8z', 'M7.8 10.4v3M6.3 11.9h3'],
      c: [[15.6, 12.9, 1], [17.9, 10.9, 1]],
    },
    Tutorial: {
      p: ['M12 6.6C10 5.2 7 4.7 3.5 5.1v13.1c3.5-.4 6.5.1 8.5 1.5 2-1.4 5-1.9 8.5-1.5V5.1C17 4.7 14 5.2 12 6.6z', 'M12 6.6v13.1'],
      c: [],
    },
    Saved: {
      p: ['M5 4h11.4L20 7.6V19a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V5a1 1 0 0 1 1-1z', 'M8 4v5h7V4', 'M7.5 20v-6.5h9V20'],
      c: [],
    },
  };

  const add = (parent, tag, cls) => {
    const e = document.createElement(tag);
    e.className = cls;
    parent.appendChild(e);
    return e;
  };

  const svg = (parent, tag, attrs) => {
    const e = document.createElementNS(SVG_NS, tag);
    for (const key in attrs) e.setAttribute(key, String(attrs[key]));
    parent.appendChild(e);
    return e;
  };

  const toast = add(root, 'div', 'toast');
  const iconRoot = svg(toast, 'svg', {width: ICON_SIZE, height: ICON_SIZE, class: 'toast-icon'});
  const text = add(toast, 'div', 'toast-text');
  const title = add(text, 'div', 'toast-title');
  const body = add(text, 'div', 'toast-body');

  const drawIcon = name => {
    while (iconRoot.firstChild) iconRoot.removeChild(iconRoot.firstChild);
    const icon = ICONS[name] || ICONS.Gameplay;
    const scale = ICON_SIZE / 24;
    const group = svg(iconRoot, 'g', {transform: `scale(${scale})`});
    for (const d of icon.p) {
      svg(group, 'path', {d, fill: 'none', stroke: ICON_COLOR, 'stroke-width': ICON_STROKE / scale, 'stroke-linecap': 'round', 'stroke-linejoin': 'round'});
    }
    for (const [cx, cy, r] of icon.c) {
      svg(group, 'circle', {cx, cy, r, fill: ICON_COLOR, stroke: 'none'});
    }
  };

  const place = (opacity, x) => {
    toast.style.opacity = String(opacity);
    toast.style.transform = `translateX(${x}px)`;
  };

  // 一次只有一段动画：新的一段开始时旧的自己停
  let running = 0;
  const animate = (ms, frame, done) => {
    const token = ++running;
    const start = performance.now();
    const step = () => {
      if (token !== running) return;
      const t = ms > 0 ? Math.min((performance.now() - start) / ms, 1) : 1;
      frame(t);
      if (t < 1) requestAnimationFrame(step);
      else if (done) done();
    };
    step();
  };

  let seed = 0;

  engine.on('toast-show', message => {
    title.textContent = message.title;
    body.textContent = message.body;
    drawIcon(message.icon);
    seed = (seed + SEED_STEP) % 1000;
    toast.style.setProperty('--toast-seed', String(seed));
    toast.className = 'toast toast-on';
    animate(message.inMs, t => {
      const eased = 1 - Math.pow(1 - t, 3);
      place(Math.min(t * 1.4, 1), SLIDE_IN * (1 - eased));
    });
  });

  engine.on('toast-hide', message => {
    animate(message.outMs, t => place(1 - t, SLIDE_OUT * t * t), () => { toast.className = 'toast'; });
  });
})();
