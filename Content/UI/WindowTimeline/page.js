// 窗口时间轴（ginkgo.showWindowTimeline、F6）：本地玩家的移动窗口，画在屏幕下方的一条面板上。
//
// 游戏每帧在所有 Actor 更新之后，有内容时发一条事件 "timeline"，值是一组消息（C++ 的 FGinkgoWindowTimeline）：
//   config  {hotkey, command, history}：开关的键、命令名（没有控制台的发行版为空）、轨道上显示最近几秒
//   lanes   {lanes: [{name, tag, push, pushFlag, behavior, endEvents, abilities, blocked, cleared, multipliers: [{category, phase, value}],
//            phases: [{label, duration, next, transitions: [{event, target}]}]}]}：清空，换成这些轨道。
//            abilities、blocked 与 cleared（推入时清除的窗口）是完整的标签名；倍率随时间变（曲线）时没有 value。
//            pushFlag（set / clear）是推入时对事件标志的要求，不限时没有；behavior 是有行为的窗口的类名（lurch），只用数据的窗口没有
//   clock   {paused, t, offset}：世界暂停与继续。页面的时钟减去 offset 是时间轴的时钟，暂停时停在 t
//   frame   {t, ends: [{id, t, elapsed, ok}], starts: [{id, lane, phase, t, duration, count, chain, ok}],
//            events: [{type, value, flag, chain, links: [{lane, kind}]}], corrections: [error]}
// 时刻都在页面的时钟上（秒，performance.now() 的千分之一）。页面自己每帧按时钟滚动、把还开着的段画到现在，游戏不用每帧发数据。
//
// 最上面一条是事件，下面每种窗口一条轨道、一个阶段一段：实心的画到现在或结束为止，还开着的定长阶段另有一个空心的画出整个时长。
// 事件与它推入、结束、换阶段的段之间画竖线；一条因果链（chain）一种颜色，三种颜色轮流用。面板是透明的（page.css）：
// 只有字、段与事件（链色的淡彩加同链的笔线）、现在那道线和轨道下面的时间横轴。
// ok 为假的边界画一道红色短线：客户端上是校正放回服务器的状态改的窗口。没有框架，直接写 DOM。
(() => {
  const engine = weft.engine;
  const root = document.getElementById('root');
  const add = (parent, tag, cls, text) => {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined && text !== null) e.textContent = String(text);
    parent.appendChild(e);
    return e;
  };
  const clearChildren = e => {
    while (e.children.length) e.removeChild(e.children[0]);
  };

  // ---------------------------------------------------------------- 常量

  // 链色（与现代暗黑主题令牌同源：香槟金、翠绿、紫藤）：笔色用于段描边、因果竖线与段外时间；
  // 填充采用同色系高对比不透明深底，避免背后因果竖线与校正线透出干扰文字，白字在深底上清晰易读。不属于任一链的用中性炭灰。
  const CHAIN_PENS = ['#e5b443', '#5fe08a', '#a68ce0'];
  const CHAIN_WASHES = ['#332914', '#132b20', '#241d36'];
  const NEUTRAL_PEN = 'rgba(255, 251, 244, 0.48)';
  const NEUTRAL_WASH = '#1e2024';
  const WINDOW_LEFT = 248;  // 轨道区在轨道说明右边的位置（page.css 的 .wt-window、.wt-axis）
  const SCALE_EDGE = 24;    // 横轴上的字离轨道区左端至少这么远（半个标签宽），免得写到轨道说明下面
  const AXIS_STEP = 0.5;    // 横轴每多少秒一个刻度，整秒的长一些、写上时刻
  const PITCH = 64;     // 一条窗口轨道的高度（px）：放得下名字与三行说明
  const PILL = 28;      // 胶囊的高度
  const PILL_TOP = 18;  // 胶囊在轨道里的上边：上下居中
  // 事件轨道：最上面一行放校正的标签，下面两层事件。和上一组挨得太近（走下边缘后很快起跳）的放到上面一层：
  // 它的竖线在下一层那组的右边落下，不从别的事件后面穿过
  const EVENT_ROW = 72;
  const CHIP = 26;               // 事件胶囊的高度（page.css 的 .wt-events、.wt-chip）
  const CHIP_LEVELS = [44, 16];  // 下层、上层在事件轨道里的上边
  const CHIP_GAP = 4;            // 同一层里两组之间至少留的距离
  const LABEL_NAME = 17;  // 轨道名字与一行说明的行高（page.css 的 .wt-label-name、.wt-label-sub）
  const LABEL_SUB = 13;
  const WARP_STEP = 3;  // 同一组里几个事件都有连线时，竖线向左错开的距离
  const EVENTS_BOX = 480;  // 事件组的盒子宽度（page.css 的 .wt-events）：组在盒子里靠右排
  const NOW_AT = 0.82;  // 现在的位置在轨道宽度的这一处，右边留给还没走完的时长
  // 位置都是 (t - origin) × pps，开着久了一直涨。weft 读几何（measure 里的 getBoundingClientRect）时检查整个文档，任何一个元素的偏移
  // 超过 100000 px 就抛异常（WEFT_GEOMETRY_LIMIT: offset），这一帧的 animate 断在那里、不再排下一帧，时间轴就停住了：
  // 每秒约 400 px 时开 4 分多钟就到。现在走出这么远（px）就把 origin 挪到现在、重排所有东西（每秒 400 px 时约 50 s 一次）
  const REBASE_PX = 20000;
  const EVENT_LABELS = {
    Jumped: '起跳', Landed: '落地', WalkedOffLedge: '未起跳离地', SlideStarted: '滑铲开始', SlideEnded: '滑铲结束',
    DirectionPressed: '方向键', TraversalStarted: '攀爬开始',
  };
  const eventLabel = name => EVENT_LABELS[name] || name;
  // 推入时要求事件标志为真（set）或假（clear）的写法：起跳的标志是"疲劳"，滑铲开始的是"拿到了起步加速"
  const FLAG_LABELS = {
    Jumped: {set: '疲劳的起跳', clear: '不疲劳的起跳'},
    SlideStarted: {set: '拿到加速的滑铲开始', clear: '没拿到加速的滑铲开始'},
  };
  const pushLabel = (name, flag) => (flag && FLAG_LABELS[name] && FLAG_LABELS[name][flag]) || eventLabel(name);
  // 有行为的窗口（类名）做什么
  const BEHAVIOR_LABELS = {
    GinkgoLurchWindow: '方向键按下时转向，用掉一次',
  };
  // 能力与倍率类别；没列出的用标签的最后一节
  const TAG_LABELS = {
    'Movement.Window.Ability.JumpWhileFalling': '空中起跳',
    'Movement.Window.Effect.JumpPenalty': '跳跃惩罚',
    'Movement.Window.Effect.SlideBoost': '起步加速',
  };
  const tagLabel = tag => TAG_LABELS[tag] || String(tag).split('.').pop();
  const pen = chain => chain >= 0 ? CHAIN_PENS[chain % CHAIN_PENS.length] : NEUTRAL_PEN;
  const wash = chain => chain >= 0 ? CHAIN_WASHES[chain % CHAIN_WASHES.length] : NEUTRAL_WASH;
  // 胶囊：淡彩的底，同链的笔线（inset 的 box-shadow，不占宽度）
  const paint = (element, chain) => {
    element.style.backgroundColor = wash(chain);
    element.style.boxShadow = `inset 0 0 0 1.5px ${pen(chain)}`;
  };
  const rowTop = row => row === 0 ? 0 : EVENT_ROW + (row - 1) * PITCH;
  const rowHeight = row => row === 0 ? EVENT_ROW : PITCH;
  const px = v => `${v.toFixed(1)}px`;

  // 估算文字宽度（12px 粗体）：中文一个字约 12px，其余约 7px；放不下的标签不显示
  const textWidth = text => {
    let width = 0;
    for (const ch of text) width += ch.codePointAt(0) >= 0x2e80 ? 12 : 7;
    return width;
  };
  // 依次试几种写法，取第一种放得下的；都放不下时不写字。返回写上的
  const setLabel = (element, texts, width) => {
    const text = texts.find(t => textWidth(t) + 20 <= width) || '';
    if (element.textContent !== text) element.textContent = text;
    return text;
  };

  // ---------------------------------------------------------------- 结构

  const panel = add(root, 'div', 'wt');
  const head = add(panel, 'div', 'wt-head');
  const title = add(head, 'div', 'wt-title', '窗口时间轴');
  add(title, 'span', 'wt-en', 'WINDOW TIMELINE');
  const subtitle = add(head, 'div', 'wt-sub');
  const hint = add(head, 'div', 'wt-hint');
  const body = add(panel, 'div', 'wt-body');
  const labels = add(body, 'div', 'wt-labels');
  const win = add(body, 'div', 'wt-window');
  const strip = add(win, 'div', 'wt-strip');
  // 竖线在胶囊之下：跨过中间轨道的连线不压住那条轨道上的段
  const warpLayer = add(strip, 'div', 'wt-strip');
  const pillLayer = add(strip, 'div', 'wt-strip');
  const markLayer = add(strip, 'div', 'wt-strip');
  const nowLine = add(win, 'div', 'wt-now');
  const empty = add(win, 'div', 'wt-empty');
  // 轨道下面的时间横轴，不随轨道走（drawScale）
  const scale = add(panel, 'div', 'wt-scale');

  // ---------------------------------------------------------------- 状态

  let history = 4;   // 显示最近几秒
  let width = 0;     // 轨道区的宽度（px），量到之前为 0
  let pps = 100;     // 每秒多少 px
  let origin = 0;    // 位置相对这个时刻；走出 REBASE_PX 时挪到现在（rebase），数值不会很大
  let lanes = [];
  let rows = 2;
  let levelRight = [-Infinity, -Infinity];  // 事件轨道的两层上最后一组的右端（px，与 x() 同一坐标）
  let offset = 0;
  let paused = false;
  let frozen = 0;
  const now = () => paused ? frozen : performance.now() / 1000 - offset;
  const x = t => (t - origin) * pps;

  const segments = new Map();  // 段的编号 → 段
  const open = new Set();      // 还开着的段：每帧画到现在
  let items = [];              // 轨道上的东西（段、事件、校正），每个有 place()、remove() 与 last（最右端的时刻）
  let laneNames = new Map();   // 窗口标签 → 轨道的名字，写"清除"时用

  // ---------------------------------------------------------------- 轨道

  // 轨道的说明，最多三行：哪些事件推入、推入时清除哪些窗口；各阶段的时长与哪些事件结束它；行为、给出的能力、倍率与屏蔽的类别
  const summary = lane => {
    if (lane.missing) return ['定义已经不在配置里'];
    const phaseList = lane.phases || [];
    // 用窗口名字的阶段（没有标签、只有一个阶段）只写时长；不定长的写换到下一个阶段的事件
    const phases = phaseList
      .map(p => {
        const label = p.label === lane.name ? '' : `${p.label} `;
        if (p.duration > 0) return `${label}${p.duration.toFixed(2)} s`;
        const next = (p.transitions || []).filter(transition => transition.target >= 0).map(transition => eventLabel(transition.event));
        return next.length ? `${label}到${next.join('、')}` : `${label}直到事件`;
      })
      .join(' → ');
    const ends = new Set(lane.endEvents || []);
    for (const phase of phaseList) {
      for (const transition of phase.transitions || []) {
        if (transition.target < 0) ends.add(transition.event);
      }
    }
    const pushedBy = (lane.push || []).length ? `${lane.push.map(name => pushLabel(name, lane.pushFlag)).join('、')}推入` : '';
    const cleared = (lane.cleared || []).map(tag => laneNames.get(tag) || tagLabel(tag));
    const push = [pushedBy, cleared.length ? `清除${cleared.join('、')}` : ''].filter(text => text).join(' · ');
    const life = ends.size ? `${phases} · ${[...ends].map(eventLabel).join('、')}结束` : phases;
    const effects = [];
    if (lane.behavior) effects.push(BEHAVIOR_LABELS[lane.behavior] || lane.behavior);
    if ((lane.abilities || []).length) effects.push(`给出${lane.abilities.map(tagLabel).join('、')}`);
    for (const m of lane.multipliers || []) {
      const value = typeof m.value === 'number' ? `×${m.value.toFixed(2)}` : '随时间变';
      const phase = m.phase >= 0 && phaseList[m.phase] ? `（${phaseList[m.phase].label}）` : '';
      effects.push(`${tagLabel(m.category)} ${value}${phase}`);
    }
    if ((lane.blocked || []).length) effects.push(`屏蔽${lane.blocked.map(tagLabel).join('、')}`);
    return [push, life, effects.join(' · ')].filter(line => line);
  };

  const clear = () => {
    for (const item of items) item.remove();
    items = [];
    segments.clear();
    open.clear();
    levelRight = [-Infinity, -Infinity];
  };
  const bodyHeight = () => rowTop(rows);

  const buildLanes = list => {
    clear();
    lanes = list || [];
    laneNames = new Map(lanes.filter(lane => lane.tag).map(lane => [lane.tag, lane.name]));
    origin = now();
    clearChildren(labels);
    // 没有窗口时也留一条空轨道写说明
    rows = 1 + Math.max(lanes.length, 1);
    body.style.height = px(bodyHeight());
    const lane = (row, name, subs) => {
      const box = add(labels, 'div', 'wt-label');
      // 名字与说明在轨道里上下居中
      box.style.top = px(rowTop(row) + (rowHeight(row) - LABEL_NAME - LABEL_SUB * subs.length) / 2);
      add(box, 'div', 'wt-label-name', name);
      for (const sub of subs) add(box, 'div', 'wt-label-sub', sub);
    };
    lane(0, '事件', ['起跳 · 落地 · 未起跳离地', '滑铲 · 方向键 · 攀爬']);
    lanes.forEach((definition, index) => lane(index + 1, definition.name, summary(definition)));
    subtitle.textContent = lanes.length ? `${lanes.length} 种窗口 · 最近 ${history} 秒` : `最近 ${history} 秒`;
    empty.textContent = lanes.length ? '' : '移动配置里还没有窗口';
    empty.style.top = px(rowTop(1) + PILL_TOP);
    width = 0;
  };

  // ---------------------------------------------------------------- 段

  // 红色短线：没有对应窗口变化的边界
  const mark = top => {
    const tick = add(markLayer, 'div', 'wt-tick');
    tick.style.top = px(top - 4);
    return tick;
  };

  const startSegment = s => {
    const lane = lanes[s.lane];
    const phase = lane && lane.phases ? lane.phases[s.phase] : null;
    const name = (phase ? phase.label : `阶段 ${s.phase}`) + (s.count > 1 ? ` ×${s.count}` : '');
    const top = rowTop(s.lane + 1) + PILL_TOP;
    const color = pen(s.chain);
    const seg = {lane: s.lane, start: s.t, end: null, elapsed: 0, duration: s.duration, top, last: Infinity, followed: false};
    if (s.duration > 0) {
      seg.ghost = add(pillLayer, 'div', 'wt-ghost');
      seg.ghost.style.top = px(top);
      seg.ghost.style.borderColor = color;
    }
    seg.solid = add(pillLayer, 'div', 'wt-seg');
    seg.solid.style.top = px(top);
    paint(seg.solid, s.chain);
    seg.startTick = s.ok ? null : mark(top);
    // 实心的位置、宽度与标签：还开着时每帧画到现在；结束了标上模拟里经过的时间，放不下名字时先保留时间。
    // 只画看得见的那部分：从结束（还开着时是现在）往前 history + 1 秒画起。等事件的阶段（如 Armed 到落地）开得再久，
    // 宽度与偏移也不涨（见 REBASE_PX）；开头滚出去以后，开头的红线跟着拿掉
    seg.grow = () => {
      const end = seg.end === null ? now() : seg.end;
      const from = Math.max(seg.start, end - history - 1);
      const w = Math.max(2, (end - from) * pps);
      seg.solid.style.left = px(x(from));
      seg.solid.style.width = px(w);
      if (seg.ghost) {
        // 空心的画出整个时长，同样只画到现在往后 history + 1 秒；边框各 2px 在宽度之外
        seg.ghost.style.left = px(x(from));
        seg.ghost.style.width = px(Math.max(0, (Math.min(seg.start + seg.duration, end + history + 1) - from) * pps - 4));
      }
      if (seg.startTick && from > seg.start) {
        seg.startTick.remove();
        seg.startTick = null;
      }
      const elapsed = `${seg.elapsed.toFixed(3)} s`;
      const texts = seg.end === null
        ? (seg.duration > 0 ? [`${name} · ${seg.duration.toFixed(2)} s`, name] : [name])
        : [`${name} · ${elapsed}`, elapsed, name];
      const inside = setLabel(seg.solid, texts, w);
      // 结束了的短段（如 83 ms 后起跳结束的土狼时间）里写不下经过的时间：写在右边。
      // 同一帧里这条轨道接着开始新的段时不写，新段从这里画起，会盖住它
      const outside = seg.end !== null && !seg.followed && !inside.includes(elapsed);
      if (outside && !seg.outside) {
        seg.outside = add(pillLayer, 'div', 'wt-out', elapsed);
        seg.outside.style.top = px(top);
        seg.outside.style.color = color;
      } else if (!outside && seg.outside) {
        seg.outside.remove();
        seg.outside = null;
      }
      if (seg.outside) seg.outside.style.left = px(x(seg.end) + 4);
    };
    seg.place = () => {
      seg.grow();
      if (seg.startTick) seg.startTick.style.left = px(x(seg.start) - 1);
      if (seg.endTick) seg.endTick.style.left = px(x(seg.end) - 2);
    };
    seg.remove = () => {
      seg.solid.remove();
      if (seg.ghost) seg.ghost.remove();
      if (seg.outside) seg.outside.remove();
      if (seg.startTick) seg.startTick.remove();
      if (seg.endTick) seg.endTick.remove();
    };
    segments.set(s.id, seg);
    open.add(seg);
    items.push(seg);
    seg.place();
  };

  // restarted：这一帧里有新段开始的轨道
  const endSegment = (e, restarted) => {
    const seg = segments.get(e.id);
    if (!seg) return;
    seg.end = e.t;
    seg.elapsed = e.elapsed;
    seg.last = e.t;
    seg.followed = restarted.has(seg.lane);
    open.delete(seg);
    segments.delete(e.id);
    if (seg.ghost) {
      seg.ghost.remove();
      seg.ghost = null;
    }
    if (!e.ok) seg.endTick = mark(seg.top);
    seg.place();
  };

  // ---------------------------------------------------------------- 事件与校正

  const describe = ev => {
    const label = eventLabel(ev.type);
    if (ev.type === 'SlideStarted') return ev.flag ? `${label} +${(ev.value / 100).toFixed(1)} m/s` : `${label}（无加速）`;
    // 起跳的数值是跳跃高度的倍率，标志是"疲劳"（倍率小于 1）
    if (ev.type === 'Jumped' && ev.flag) return `${label} ×${ev.value.toFixed(2)}`;
    return label;
  };

  // 同一次移动里的事件排成一组，右端对着事件的时刻（同参考图里一段的末端接下一段的开头），竖线从这里落到它改变的段上。
  // 有连线的事件排在右边；几个都有连线时，最后一个的竖线正对时刻，前面的依次向左错开，一种颜色一条。
  // 默认在下层；会压住下层上一组时放到上层，两层都压住时仍放下层
  const addEvents = (t, events) => {
    const linked = events.filter(ev => (ev.links || []).length);
    const ordered = [...events.filter(ev => !(ev.links || []).length), ...linked];
    const texts = ordered.map(describe);
    // 胶囊的左右内边距各 12px、左外边距 4px（page.css 的 .wt-chip）
    const groupWidth = texts.reduce((sum, text) => sum + textWidth(text) + 28, 0);
    const right = x(t);
    const fits = level => right - groupWidth >= levelRight[level] + CHIP_GAP;
    const level = fits(0) || !fits(1) ? 0 : 1;
    levelRight[level] = right;
    const group = add(pillLayer, 'div', 'wt-events');
    group.style.top = px(rowTop(0) + CHIP_LEVELS[level]);
    ordered.forEach((ev, index) => {
      paint(add(group, 'div', 'wt-chip', texts[index]), ev.chain);
    });
    const warps = [];
    linked.forEach((ev, index) => {
      for (const link of ev.links) {
        const warp = add(warpLayer, 'div', 'wt-warp');
        const top = rowTop(0) + CHIP_LEVELS[level] + CHIP;
        warp.style.top = px(top);
        warp.style.height = px(Math.max(0, rowTop(link.lane + 1) + PILL_TOP - top));
        warp.style.backgroundColor = pen(ev.chain);
        warps.push({warp, shift: WARP_STEP * (linked.length - 1 - index)});
      }
    });
    const item = {
      last: t,
      place() {
        group.style.left = px(x(t) - EVENTS_BOX);
        for (const {warp, shift} of warps) warp.style.left = px(x(t) - 1 - shift);
      },
      remove() {
        group.remove();
        for (const {warp} of warps) warp.remove();
      },
    };
    items.push(item);
    item.place();
  };

  // 校正：竖线在胶囊后面（校正多的时候不压住段），标签在事件胶囊上方；同一帧几次校正合成一个标签，写最大的位置差
  const addCorrections = (t, errors) => {
    const line = add(warpLayer, 'div', 'wt-fix');
    line.style.height = px(bodyHeight());
    const worst = Math.max(...errors);
    const count = errors.length > 1 ? ` ×${errors.length}` : '';
    const box = add(markLayer, 'div', 'wt-fix-box');
    add(box, 'span', 'wt-fix-tag', worst < 0 ? `校正${count}` : `校正${count} ${worst.toFixed(2)} cm`);
    const item = {
      last: t,
      place() {
        line.style.left = px(x(t) - 1);
        box.style.left = px(x(t) + 3);
      },
      remove() {
        line.remove();
        box.remove();
      },
    };
    items.push(item);
    item.place();
  };

  // ---------------------------------------------------------------- 消息

  const configure = config => {
    history = config.history || history;
    clearChildren(hint);
    add(hint, 'span', 'wt-key', config.hotkey || 'F6');
    // 没有控制台的版本（发行版）不给命令名
    add(hint, 'span', '', config.command ? `开关 · ${config.command}` : '开关');
    width = 0;
  };

  const handle = message => {
    switch (message.op) {
      case 'config':
        configure(message);
        break;
      case 'lanes':
        buildLanes(message.lanes);
        break;
      case 'clock':
        offset = message.offset;
        paused = message.paused;
        frozen = message.t;
        break;
      case 'frame':
        // 先结束、再开始：同一种窗口换阶段时旧的段在新的段开始处结束
        const restarted = new Set(message.starts.map(start => start.lane));
        for (const end of message.ends) endSegment(end, restarted);
        for (const start of message.starts) startSegment(start);
        if (message.events.length) addEvents(message.t, message.events);
        if (message.corrections.length) addCorrections(message.t, message.corrections);
        break;
    }
  };
  engine.on('timeline', messages => {
    for (const message of messages) handle(message);
  });

  // ---------------------------------------------------------------- 每帧

  // 时间横轴：轨道下面一道墨线，刻度对着"现在"，每 AXIS_STEP 秒一个，整秒的长一些、写上时刻（现在右边的不写）。
  // 轨道区里的东西往左滚，横轴不动；太靠近轨道区左端的字不写
  const drawScale = () => {
    clearChildren(scale);
    add(scale, 'div', 'wt-axis');
    const nowX = width * NOW_AT;
    const first = -Math.floor(nowX / (pps * AXIS_STEP));
    const last = Math.floor((width - nowX) / (pps * AXIS_STEP));
    for (let k = first; k <= last; k++) {
      const left = nowX + k * AXIS_STEP * pps;
      const seconds = -k * AXIS_STEP;
      const whole = Math.abs(seconds - Math.round(seconds)) < 1e-6;
      add(scale, 'div', whole ? 'wt-scale-mark' : 'wt-scale-mark wt-scale-minor').style.left = px(WINDOW_LEFT + left);
      if (!whole || k > 0 || (k < 0 && left < SCALE_EDGE)) continue;
      const label = add(scale, 'div', k === 0 ? 'wt-scale-label wt-scale-now' : 'wt-scale-label', k === 0 ? '现在' : `−${Math.round(seconds)} s`);
      label.style.left = px(WINDOW_LEFT + left - 24);
    }
  };

  // 轨道区的宽度：变了就重新算比例、重排所有东西
  const measure = () => {
    const w = win.getBoundingClientRect().width;
    if (w > 0 && Math.abs(w - width) > 0.5) {
      width = w;
      pps = width * NOW_AT / history;
      nowLine.style.left = px(width * NOW_AT - 1);
      drawScale();
      for (const item of items) item.place();
    }
  };

  // 把 origin 挪到 t：所有东西按新的 origin 重排，事件两层的右端（px）跟着挪
  const rebase = t => {
    const shift = x(t);
    origin = t;
    levelRight = levelRight.map(right => right - shift);
    for (const item of items) item.place();
  };

  // 滚出左边的东西扔掉
  const collect = t => {
    const gone = t - history - 1;
    const keep = [];
    for (const item of items) {
      if (item.last < gone) item.remove();
      else keep.push(item);
    }
    items = keep;
  };

  let lastMaintenance = -1;
  const animate = () => {
    const t = now();
    if (width === 0 || t - lastMaintenance > 0.5) {
      // 在 measure 读几何之前：两次之间新加的东西最多再往右 0.5 秒，偏移远不到 weft 的上限
      if (x(t) > REBASE_PX) rebase(t);
      measure();
      collect(t);
      lastMaintenance = t;
    }
    if (width > 0) {
      strip.style.transform = `translateX(${px(width * NOW_AT - x(t))})`;
      for (const seg of open) seg.grow();
    }
    requestAnimationFrame(animate);
  };
  requestAnimationFrame(animate);
})();
