// 设置界面（Docs/Design/Control.md）：纸页，同相机的调参面板（目录、约四成宽的选项、描述区），样式与组件是共用的主题（Content/UI/Theme），不同的是右下角的三个按钮。
// 目录与说明在 catalog.js，预览在 previews.js。名字与单位都在页面里：打包的游戏里没有元数据。
//
// 游戏发（C++ 的 UI/GinkgoSettingsPage）：
//   "settings"：整张表 {rows: [{id, dir, kind: number | bool | choice | key | mode, value, def, saved, min?, max?, step?, options?}], keyLabels, state, fromGamepad, key, file, adsZoom, dirty}
//   "values"：变了的行 {rows: [{id, value, saved}], keyLabels, dirty, status}
//   "capture"：等键的结果 {id, key, label, result: done | clear | cancel | conflict | reserved | wrongDevice, conflict?}
//   "settings-closing"：{}（C++ 启动退场模糊过渡时同步播放页面退场动画）
// 页面发：ready、set {id, value}、reset {id}、resetDirectory {dir}、revert、confirm、close、state {dir, focus}、log、capture {id}、captureCancel、assignKey {id, key}。
// 键位一个键位两行：mouse.crouch.primary（键）、mouse.crouch.primary.mode（方式，只有冲刺、蹲、开镜有）；选项里的一项 mouse.crouch 管两个键位。
//
// 输入设置实时预览，画质与显示确认时应用；撤销和恢复默认均可在确认前反悔。
//
// 键盘与手柄：页面开了 spatialNavigation（手柄的 D-pad、左摇杆、A、B 变成方向键、Enter、Escape），但焦点是这里自己管的（方向键都 preventDefault）：
// 上下在选项与按钮之间走，左右调整（滑块一格一格、按住变快；按键那一类是在键位与方式之间走），不用元素的焦点，所以滑块、开关不用是能拿焦点的控件。LB、RB、X、Y、菜单键读 navigator.getGamepads()。
// 改键时等键的是 C++（UI/GinkgoKeyCapture，Slate 的预处理器）：等键时键盘、鼠标键与手柄的键到不了这里。
//
// 不卡（做法同调参面板，Docs/Design/CameraPipeline.md「调参面板：切换不卡」）：weft 把新接进文档的节点排样式、布局、进场景，一个约 0.1 ms，改已有节点的属性几乎不要钱。
// 每个目录的选项在文档外搭好，换到它时一帧两行接进来；描述按讲的东西各留一份，说明当帧、预览下一帧；指针停住 90 ms 才换描述；一帧最多做一件活。
(() => {
  const engine = weft.engine;
  Paper.useDarkTheme();
  const {C, el, clear, fmt} = Paper;
  const Catalog = SettingsCatalog;
  const root = document.getElementById('root');
  root.className = 'st-dark';
  const now = () => performance.now();

  // ===== 表 =====

  const rows = new Map();          // id → 行
  const owners = new Map();        // 行 id → 选项里的一项（改键的一项有好几行）
  const keyLabels = new Map();     // 键名 → 引擎给的名字（catalog.js 的 KEYS 没写的键显示它）
  let table = null;
  let model = null;                // {directories: [{dir, categories: [{id, title, en, palette, items: [item]}], items: [item]}], items: Map(id → item)}
  const ui = {
    dir: null,
    focus: null,                   // 键盘、手柄停在哪一项（点过标签条的也算）
    cell: {slot: 0, part: 'key'},  // 改键的一项里停在哪一格：第几个键位，键还是方式
    capture: null,                 // 正在等键：{item, id, pad, message}
    zone: 'list',                  // 键盘、手柄在哪里：list 选项、actions 按钮
    action: 'confirm',             // 按钮区停在哪一个
    hover: null,                   // 鼠标停着的（{type: item | category | directory, …}）
    dragging: null,
    device: 'mouse',               // 最后用的设备：mouse、keyboard、gamepad（提示跟着换）
    dialog: null,                  // {view, focus, order, press, back, primary}
  };
  let valuesVersion = 0;
  let dirty = 0;
  let statusText = '';
  let closing = false;

  const startExit = () => {
    if (closing) return;
    closing = true;
    root.className = 'st-dark st-exiting';
  };

  const requestClose = () => {
    if (closing) return;
    startExit();
    engine.send('close');
  };

  const row = id => rows.get(id);
  const valueOf = id => row(id).value;
  // 数的比较留余量：页面的值是 double，游戏存的是 float（0.2 回来是 0.20000000298），拖完滑块时页面还拿着自己的值
  const same = (a, b) => (typeof a === 'number' && typeof b === 'number' ? Math.abs(a - b) < 1e-5 : a === b);
  const isChanged = id => !same(row(id).value, row(id).def);
  const isDirty = id => !same(row(id).value, row(id).saved);
  const itemChanged = item => item.ids.some(isChanged);
  const itemDirtyCount = item => item.ids.filter(isDirty).length;
  const keyName = name => (!name ? '未绑定' : Catalog.KEYS[name] || keyLabels.get(name) || name);

  const ctx = {
    v: id => (rows.has(id) ? valueOf(id) : undefined),
    d: id => row(id).def,
    has: id => rows.has(id),
    keyName,
    optionName: value => Catalog.OPTIONS[value] || String(value),
    get zoom() { return table ? table.adsZoom : 1; },
    get scopeZoom() { return table && table.scopeZoom ? table.scopeZoom : 4; },
    focusId: null,
  };

  const log = text => engine.send('log', text);
  // 菜单的声音（Docs/Design/Control.md「菜单的声音」）：Focus、Select、Confirm、Deny、Invalid；打开、关闭、存没存下来由 C++ 放
  const sound = kind => engine.send('sound', kind);

  // ===== 模型 =====

  const buildModel = () => {
    const items = new Map();
    owners.clear();
    let seed = 0;
    let palette = 0;
    const directories = Catalog.DIRECTORIES.map(dir => {
      const categories = dir.categories.map(category => {
        const entry = {...category, dirId: dir.id, palette: Paper.STRIP_PALETTES[palette++ % Paper.STRIP_PALETTES.length], items: []};
        for (const id of category.items) {
          const info = Catalog.ITEMS[id] || {label: id};
          // 改键的一项：两个键位，每个键位一行键、可能还有一行方式
          const slots = info.binding ? ['primary', 'secondary'].map((name, index) => ({
            id: `${id}.${name}`, modeId: rows.has(`${id}.${name}.mode`) ? `${id}.${name}.mode` : null, title: index === 0 ? '主键' : '辅助键',
          })) : null;
          if (slots ? !slots.every(slot => rows.has(slot.id)) : !rows.has(id)) {
            log(`catalog.js 里的 ${id} 游戏没有`);
            continue;
          }
          const ids = slots ? slots.flatMap(slot => (slot.modeId ? [slot.id, slot.modeId] : [slot.id])) : [id];
          const item = {id, ids, slots, binding: !!slots, row: slots ? null : row(id), info, category: entry, seed: seed++};
          items.set(id, item);
          for (const rowId of ids) owners.set(rowId, item);
          entry.items.push(item);
        }
        return entry;
      });
      return {dir, categories, items: categories.flatMap(category => category.items)};
    });
    for (const id of rows.keys()) {
      if (!owners.has(id)) log(`游戏的 ${id} 没写进 catalog.js 的目录`);
    }
    return {directories, items};
  };

  const entryOf = id => model.directories.find(d => d.dir.id === id);
  const activeEntry = () => entryOf(ui.dir) || model.directories[0];
  const itemOff = item => (item.row && item.row.disabled) || (item.info.off ? item.info.off(ctx) : '');
  /** 收起来的行（catalog 的 hide，比如辅助瞄准的总开关关着时的细节项）：不显示，键盘、手柄跳过 */
  const itemHidden = item => !!(item.info.hide && item.info.hide(ctx));
  const display = (item, value) => value * (item.info.scale || 1);
  const digitsOf = item => (item.info.digits !== undefined ? item.info.digits : 2);
  const unitOf = item => item.info.unit || '';
  const optionName = value => Catalog.OPTIONS[value] || String(value);

  /** 显示的值：数带单位，开关是开、关，选择是中文名 */
  const showValue = (item, value) => {
    if (item.row.kind === 'bool') return value ? '开' : '关';
    if (item.row.kind === 'choice') return optionName(value);
    const unit = unitOf(item);
    return `${fmt(display(item, value), digitsOf(item))}${unit ? ` ${unit}` : ''}`;
  };

  // ===== 分帧的活：每帧最多做一件（页面时钟在一次执行里不走，没法按毫秒切，只能按件切） =====

  const revealJobs = [];
  const descJobs = [];
  const idleJobs = [];
  let idleAfter = 0;
  let busyFrame = false;

  const runJobs = () => {
    for (const queue of [revealJobs, descJobs]) {
      while (queue.length) {
        if (queue.shift()() !== false) return;
      }
    }
    if (now() < idleAfter) return;
    while (idleJobs.length) {
      if (idleJobs.shift()() !== false) return;
    }
  };

  // ===== 界面 =====

  el(root, 'div', 'pp-cloud');
  const page = el(root, 'div', 'pp-page');
  let rail = null;
  const column = el(null, 'div', 'pp-column');
  const desc = el(null, 'div', 'pp-desc');
  const imageLayer = el(desc, 'div', 'pp-desc-image');
  const descScroll = el(desc, 'div', 'pp-desc-scroll st-desc-scroll');
  let hintFoot = null;

  const ACTIONS = [
    {id: 'reset', label: '恢复默认', hint: 'X'},
    {id: 'confirm', label: '确认', primary: true, hint: '≡'},
    {id: 'revert', label: '撤销修改', hint: 'Y'},
  ];
  const actions = Paper.actionBar(desc, ACTIONS, id => pressAction(id));
  const closeButton = Paper.closeButton(desc, 'Esc', () => goBack());

  const sendState = () => engine.send('state', {dir: ui.dir, focus: ui.focus});

  const refreshActions = () => {
    const entry = activeEntry();
    actions.setDisabled('reset', !entry || entry.items.every(item => !itemChanged(item)));
    actions.setDisabled('revert', dirty === 0);
    const capture = ui.capture ? ui.capture.message || (ui.capture.pad ? '按手柄上的新键：菜单键取消，视图键清空' : '按新的键：Esc 取消，Delete 清空') : '';
    actions.setStatus(statusText || capture || (dirty > 0 ? `有 ${dirty} 项修改还没确认` : ''), !!statusText || !!(ui.capture && ui.capture.message));
    actions.setFocus(!ui.dialog && ui.zone === 'actions' && ui.device !== 'mouse' ? ui.action : null);
    actions.setHints(ui.device === 'gamepad');
  };

  const HINTS = {
    gamepad: 'LB RB 换目录\n十字键 选择 调整\nA 确定  B 返回',
    keyboard: 'PgUp PgDn 换目录\n↑↓ 选择\n←→ 调整',
    mouse: '',
  };
  const KEY_HINTS = {
    gamepad: 'LB RB 换目录\n十字键 选择 换格\nA 改键  B 返回',
    keyboard: 'PgUp PgDn 换目录\n↑↓ 选择  ←→ 换格\nEnter 改键',
    mouse: '',
  };
  const hintText = () => {
    if (ui.capture) return ui.capture.pad ? '按新的键\n菜单键 取消\n视图键 清空' : '按新的键\nEsc 取消\nDelete 清空';
    const item = ui.zone === 'list' && ui.focus && model && model.items.get(ui.focus);
    return (item && item.binding ? KEY_HINTS : HINTS)[ui.device] || '';
  };
  const refreshHints = () => {
    closeButton.label.textContent = ui.device === 'gamepad' ? 'B' : 'Esc';
    if (!hintFoot) return;
    const text = hintText();
    if (hintFoot.__text !== text) {
      hintFoot.__text = text;
      clear(hintFoot);
      text.split('\n').forEach(line => el(hintFoot, 'div', '', line));
    }
  };

  const setDevice = device => {
    if (ui.device === device) return;
    ui.device = device;
    if (device !== 'mouse' && ui.zone === 'list' && !ui.focus) {
      setFocus(activeEntry().items[0].id);
    }
    if (device === 'mouse') {
      ui.hover = null;
      ui.zone = 'list';
      updateFocusRows();
    }
    refreshHints();
    refreshActions();
  };

  // ----- 目录

  const selectDirectory = id => {
    if (ui.dir === id) return;
    ui.dir = id;
    ui.hover = null;
    hoverIntent.cancel();
    rail.select(id);
    const entry = activeEntry();
    showDirView(dirView(id));
    // 键盘、手柄换目录时停在第一项；鼠标点的不选
    const keep = ui.focus && model.items.has(ui.focus) && model.items.get(ui.focus).category.dirId === id;
    if (!keep) ui.focus = ui.device === 'mouse' ? null : entry.items[0].id;
    if (ui.zone !== 'list' && ui.device !== 'mouse') ui.zone = 'list';
    updateFocusRows();
    refreshActions();
    refreshDescription();
    sendState();
  };

  const cycleDirectory = delta => {
    const list = model.directories;
    const index = list.findIndex(d => d.dir.id === ui.dir);
    sound('Select');
    selectDirectory(list[(index + delta + list.length) % list.length].dir.id);
  };

  // ----- 选项栏：每个目录一份，在文档外搭（空闲时预先搭），显示时一批批地接进文档

  const REVEAL_ROWS = 2;
  const PREBUILD_ROWS = 6;
  const dirViews = new Map();
  let activeView = null;

  const dirView = id => {
    if (dirViews.has(id)) return dirViews.get(id);
    const entry = entryOf(id);
    const head = el(null, 'div', 'pp-head pp-enter');
    const title = el(head, 'div', 'pp-title', entry.dir.label);
    el(title, 'span', 'pp-title-en', entry.dir.en);
    el(head, 'div', 'pp-intro', entry.dir.intro || '');
    Paper.ornament(head, 260);
    const list = el(null, 'div', 'pp-list');
    const view = {id, entry, head, list, steps: [], pieces: [], shown: 0};
    for (const category of entry.categories) {
      view.steps.push(() => buildSection(view, category));
      if (category.keys) view.steps.push(() => buildKeysHead(view, category));
      for (const item of category.items) view.steps.push(() => buildRow(view, category, item));
    }
    dirViews.set(id, view);
    return view;
  };

  const buildStep = view => {
    const step = view.steps.shift();
    if (!step) return false;
    step();
    return true;
  };

  const buildSection = (view, category) => {
    const section = Paper.section(null, {
      title: category.title, en: category.en, open: true,
      onToggle: open => {
        sound('Select');
        category.closed = !open;
      },
      onHover: entering => setHover(entering ? {type: 'category', category} : null),
    });
    category.section = section;
    view.pieces.push({node: section.root, parent: view.list, rows: 0});
  };

  /** 按键一类的表头：主键、辅助键两列 */
  const buildKeysHead = (view, category) => {
    const head = el(null, 'div', 'st-keys-head');
    el(head, 'div', 'st-keys-title', '主键');
    el(head, 'div', 'st-keys-title', '辅助键');
    view.pieces.push({node: head, parent: category.section.body, rows: 0});
  };

  const buildRow = (view, category, item) => {
    buildOption(item);
    updateOption(item);
    view.pieces.push({node: item.view.view.root, parent: category.section.body, rows: 1});
  };

  const revealNext = view => {
    if (view !== activeView) return false;
    while (view.steps.length && view.pieces.length - view.shown < REVEAL_ROWS + 2) buildStep(view);
    if (view.shown >= view.pieces.length) return false;
    let shownRows = 0;
    while (view.shown < view.pieces.length && shownRows < REVEAL_ROWS) {
      const piece = view.pieces[view.shown++];
      piece.parent.appendChild(piece.node);
      shownRows += piece.rows;
    }
    if (view.shown < view.pieces.length || view.steps.length) revealJobs.push(() => revealNext(view));
    return true;
  };

  /** 把目录接到某一项为止（键盘、手柄一下子走到还没接进来的项时） */
  const revealUpTo = item => {
    const view = dirView(item.category.dirId);
    while (view === activeView && !(item.view && item.view.view.root.parentNode)) {
      while (view.steps.length && view.pieces.length <= view.shown) buildStep(view);
      if (view.shown >= view.pieces.length) break;
      const piece = view.pieces[view.shown++];
      piece.parent.appendChild(piece.node);
    }
  };

  const showDirView = view => {
    if (activeView === view) return;
    if (activeView) {
      const old = activeView;
      column.removeChild(old.head);
      column.removeChild(old.list);
      for (let i = old.shown - 1; i >= 0; --i) old.pieces[i].parent.removeChild(old.pieces[i].node);
      old.shown = 0;
    }
    column.appendChild(view.head);
    column.appendChild(view.list);
    activeView = view;
    revealJobs.length = 0;
    revealJobs.push(() => revealNext(view));
  };

  const queuePrebuild = () => {
    for (const entry of model.directories) {
      const view = dirView(entry.dir.id);
      const job = () => {
        if (!view.steps.length) return false;
        for (let i = 0; i < PREBUILD_ROWS && view.steps.length; ++i) buildStep(view);
        if (view.steps.length) idleJobs.push(job);
        return true;
      };
      idleJobs.push(job);
    }
  };

  const buildOption = item => {
    const info = item.info;
    const id = item.id;
    const view = Paper.option(null, {icon: info.icon || 'dot', label: info.label || id, seed: item.seed, palette: item.category.palette});
    // 改了还没确认：标签条左边一道银杏金（小点是"和默认值不同"，同调参面板）
    const pending = el(view.label, 'div', 'st-pending');
    const controls = {pending};
    const control = view.control;
    if (item.binding) {
      const host = el(control, 'div', 'st-slots');
      controls.slots = item.slots.map((slot, index) => {
        const root = el(host, 'div', 'st-slot');
        const cap = el(root, 'div', 'st-key');
        cap.addEventListener('click', e => {
          e.stopPropagation();
          ui.zone = 'list';
          ui.cell = {slot: index, part: 'key'};
          if (ui.focus !== id) setFocus(id);
          startCapture(item, index);
        });
        const seg = slot.modeId ? Paper.segmented(root, {
          options: ['hold', 'toggle'].map(value => ({value, label: optionName(value)})),
          value: valueOf(slot.modeId),
          onChange: value => {
            ui.cell = {slot: index, part: 'mode'};
            choose(slot.modeId, value);
          },
        }) : null;
        return {cap, seg};
      });
    } else if (item.row.kind === 'bool') {
      controls.toggle = Paper.toggle(control, {value: valueOf(id), onChange: value => choose(id, value)});
    } else if (item.row.kind === 'choice' && item.row.dir === 'video') {
      const host = el(control, 'div', 'st-video-choice');
      const previous = el(host, 'div', 'st-video-arrow', '‹');
      const label = el(host, 'div', 'st-video-value', optionName(item.row.displayValue || valueOf(id)));
      const next = el(host, 'div', 'st-video-arrow', '›');
      const step = delta => {
        if (itemOff(item)) return;
        const options = item.row.options;
        choose(id, options[(options.indexOf(valueOf(id)) + delta + options.length) % options.length]);
      };
      previous.addEventListener('click', () => step(-1));
      next.addEventListener('click', () => step(1));
      controls.seg = {set: value => { label.textContent = optionName(item.row.displayValue || value); }};
    } else if (item.row.kind === 'choice') {
      controls.seg = Paper.segmented(control, {
        options: item.row.options.map(value => ({value, label: optionName(value)})),
        value: valueOf(id),
        onChange: value => choose(id, value),
      });
    } else {
      controls.slider = Paper.slider(control, {
        lo: item.row.min, hi: item.row.max, value: valueOf(id), def: item.row.def, step: item.row.step, hard: {lo: item.row.min, hi: item.row.max},
        onInput: value => {
          ui.dragging = item;
          setValue(id, value);
        },
        onCommit: () => {
          sound('Select');
          ui.dragging = null;
          ui.hover = {type: 'item', item};
          hoverIntent.cancel();
          markDirty();
        },
      });
      controls.number = Paper.numberField(control, {
        value: display(item, valueOf(id)), format: v => fmt(v, digitsOf(item)),
        onCommit: value => choose(id, Paper.clamp(value / (info.scale || 1), item.row.min, item.row.max)),
      });
      el(control, 'div', 'pp-unit', unitOf(item));
    }
    controls.reset = Paper.resetButton(control, () => {
      if (itemOff(item)) return;
      sound('Select');
      engine.send('reset', {id});
    });
    view.root.addEventListener('mouseenter', () => setHover({type: 'item', item}));
    view.root.addEventListener('mouseleave', () => setHover(null));
    view.label.addEventListener('click', () => {
      sound('Focus');
      ui.zone = 'list';
      setFocus(ui.focus === id ? null : id);
    });
    item.view = {view, controls};
  };

  /** 改一个值：先在页面上改（选项与描述区跟上），再交给游戏；游戏回 "values" 时以它为准 */
  const setValue = (id, value) => {
    const entry = row(id);
    if (entry.value === value) return;
    entry.value = value;
    statusText = '';
    engine.send('set', {id, value});
    valuesChanged([id]);
  };

  /** 玩家选了一个值（开关、分段、输入框、方向键）：真的变了才响；拖滑块时走 setValue，松开才响 */
  const choose = (id, value) => {
    if (owners.has(id) && itemOff(owners.get(id))) return;
    if (row(id).value !== value) sound('Select');
    setValue(id, value);
  };

  const valuesChanged = ids => {
    ++valuesVersion;
    // 汇总先按页面的值算一遍，拖滑块时不用等游戏回
    let count = 0;
    for (const entry of rows.values()) count += same(entry.value, entry.saved) ? 0 : 1;
    dirty = count;
    const categories = new Set();
    for (const id of ids) {
      const item = owners.get(id);
      if (item) categories.add(item.category);
    }
    for (const category of categories) updateCategory(category);
    keepFocusVisible();
    refreshActions();
    markDirty();
  };

  /** 焦点停的那一行收起来了（关了总开关、撤销修改、恢复默认）：挪到它前面最近的看得见的一项，也就是总开关 */
  const keepFocusVisible = () => {
    const item = ui.focus && model.items.get(ui.focus);
    if (!item || !itemHidden(item)) return;
    const items = entryOf(item.category.dirId).items;
    for (let i = items.indexOf(item) - 1; i >= 0; --i) {
      if (!itemHidden(items[i])) {
        setFocus(items[i].id);
        return;
      }
    }
    setFocus(null);
  };

  const updateOption = item => {
    if (!item.view) return;
    const {view, controls} = item.view;
    const id = item.id;
    const focused = ui.focus === id && ui.zone === 'list' && !ui.dialog;
    view.setState({on: focused, changed: itemChanged(item), off: !!itemOff(item)});
    // 收起来用行内的 display（setState 会整个换掉 className）；没变不写
    const hidden = itemHidden(item);
    if (controls.hidden !== hidden) {
      controls.hidden = hidden;
      view.root.style.display = hidden ? 'none' : '';
    }
    const pending = itemDirtyCount(item) > 0;
    if (controls.pendingShown !== pending) {
      controls.pendingShown = pending;
      controls.pending.className = pending ? 'st-pending st-pending-on' : 'st-pending';
    }
    if (controls.slots) updateSlots(item, controls, focused && ui.device !== 'mouse');
    if (controls.slider) controls.slider.set(valueOf(id));
    if (controls.number) controls.number.set(display(item, valueOf(id)));
    if (controls.toggle) controls.toggle.set(valueOf(id));
    if (controls.seg) controls.seg.set(valueOf(id));
  };

  /** 键帽上的字与样式、方式的分段；设置器值没变时不碰 DOM */
  const updateSlots = (item, controls, cellFocus) => {
    controls.slots.forEach((view, index) => {
      const slot = item.slots[index];
      const key = valueOf(slot.id);
      const listening = !!ui.capture && ui.capture.id === slot.id;
      // 键帽（Theme/keycap.js）：绑着的画键的样子，等按键、没绑写字
      const shown = listening ? 'listening' : key ? `key:${key}:${keyLabels.get(key) || ''}` : 'empty';
      if (view.text !== shown) {
        view.text = shown;
        view.cap.textContent = listening ? '按键…' : key ? '' : '未绑定';
        if (!listening && key) GinkgoKeycap.create(view.cap, key, keyLabels.get(key), 'dark', 24);
      }
      const onCell = part => cellFocus && ui.cell.slot === index && ui.cell.part === part;
      const name = `st-key${key ? '' : ' st-key-empty'}${isDirty(slot.id) ? ' st-key-dirty' : ''}${listening ? ' st-key-listening' : ''}${onCell('key') ? ' st-cell-on' : ''}`;
      if (view.className !== name) {
        view.className = name;
        view.cap.className = name;
      }
      if (view.seg) {
        view.seg.set(valueOf(slot.modeId));
        const segName = `pp-seg${onCell('mode') ? ' st-cell-on' : ''}`;
        if (view.segName !== segName) {
          view.segName = segName;
          view.seg.root.className = segName;
        }
      }
    });
  };

  const updateCategory = category => {
    if (!category.section) return;
    let changed = 0;
    for (const item of category.items) {
      updateOption(item);
      changed += itemDirtyCount(item);
    }
    category.section.setCount(changed ? `改了 ${changed} 项` : '');
  };

  const updateFocusRows = () => {
    for (const entry of model.directories) {
      for (const item of entry.items) updateOption(item);
    }
  };

  // ----- 键盘与手柄的焦点

  const setFocus = id => {
    const before = ui.focus && model.items.get(ui.focus);
    ui.focus = id;
    if (before) updateOption(before);
    const item = id && model.items.get(id);
    if (item) {
      // 换到没有方式的一行时停在键上
      if (item.binding && ui.cell.part === 'mode' && !item.slots[ui.cell.slot].modeId) ui.cell = {slot: ui.cell.slot, part: 'key'};
      revealUpTo(item);
      updateOption(item);
      scrollIntoView(item);
    }
    hoverIntent.cancel();
    if (ui.device !== 'mouse') ui.hover = null;
    refreshHints();
    refreshDescription();
    sendState();
  };

  /** 选中的行滚进看得见的地方：量一次几何（weft 会排一遍页面，这个页面只有几百个节点，只在键盘、手柄走动时量） */
  const scrollIntoView = item => {
    const list = activeView && activeView.list;
    const node = item.view && item.view.view.root;
    if (!list || !node || !node.parentNode) return;
    const box = list.getBoundingClientRect();
    const rect = node.getBoundingClientRect();
    if (rect.top < box.top + 8) list.scrollTop += rect.top - box.top - 44;
    else if (rect.bottom > box.bottom - 8) list.scrollTop += rect.bottom - box.bottom + 44;
  };

  /** 上下走：跳过不起作用的行、收起来的行与折起的类别；走到最下面再往下就到按钮 */
  const moveFocus = delta => {
    const items = activeEntry().items.filter(item => !item.category.closed);
    const index = items.findIndex(item => item.id === ui.focus);
    const start = index < 0 ? (delta > 0 ? 0 : items.length - 1) : index + delta;
    for (let i = start; i >= 0 && i < items.length; i += delta) {
      if (!itemOff(items[i]) && !itemHidden(items[i])) {
        sound('Focus');
        setFocus(items[i].id);
        return;
      }
    }
    if (delta > 0) {
      sound('Focus');
      ui.zone = 'actions';
      ui.action = firstEnabledAction();
      updateFocusRows();
      refreshActions();
      refreshHints();
    }
  };

  const firstEnabledAction = () => (ACTIONS.find(a => a.primary) || ACTIONS[0]).id;

  const moveAction = delta => {
    const order = ACTIONS.map(a => a.id).filter(id => !actions.isDisabled(id));
    const index = order.indexOf(ui.action);
    const next = order[Paper.clamp(index + delta, 0, order.length - 1)];
    if (next && next !== ui.action) sound('Focus');
    if (next) ui.action = next;
    refreshActions();
  };

  /** 改键的一项里左右走：主键、主键的方式、辅助键、辅助键的方式 */
  const moveCell = (item, delta) => {
    const cells = item.slots.flatMap((slot, index) => (slot.modeId ? [{slot: index, part: 'key'}, {slot: index, part: 'mode'}] : [{slot: index, part: 'key'}]));
    const at = cells.findIndex(cell => cell.slot === ui.cell.slot && cell.part === ui.cell.part);
    const next = cells[Paper.clamp(at + delta, 0, cells.length - 1)];
    if (next.slot !== ui.cell.slot || next.part !== ui.cell.part) sound('Focus');
    ui.cell = next;
    updateOption(item);
  };

  const adjust = (delta, repeatCount) => {
    const item = ui.focus && model.items.get(ui.focus);
    if (!item || itemOff(item) || itemHidden(item)) return;
    if (item.binding) {
      if (repeatCount === 0) moveCell(item, delta);
      return;
    }
    const entry = item.row;
    if (entry.kind === 'bool') {
      if (repeatCount === 0) choose(item.id, !entry.value);
    } else if (entry.kind === 'choice') {
      if (repeatCount > 0) return;
      const index = entry.options.indexOf(entry.value);
      choose(item.id, entry.options[(index + delta + entry.options.length) % entry.options.length]);
    } else {
      // 按住变快：重复 0.1 s 一次，走得久了一次多走几格
      const multiple = repeatCount > 30 ? 10 : repeatCount > 12 ? 4 : 1;
      const step = entry.step;
      const snapped = Math.round(entry.value / step) * step;
      const moved = Math.abs(snapped - entry.value) > 1e-6 && delta * (snapped - entry.value) > 0 ? snapped : snapped + delta * step * multiple;
      // 按住连走时只有第一下响
      const value = Paper.clamp(Math.round(moved * 1e6) / 1e6, entry.min, entry.max);
      if (repeatCount === 0 && value !== entry.value) sound('Select');
      setValue(item.id, value);
    }
  };

  const activate = () => {
    if (ui.dialog) {
      pressDialog(ui.dialog.focus);
    } else if (ui.zone === 'actions') {
      pressAction(ui.action);
    } else {
      const item = ui.focus && model.items.get(ui.focus);
      if (item && item.binding) {
        const slot = item.slots[ui.cell.slot];
        if (ui.cell.part === 'key') startCapture(item, ui.cell.slot);
        else choose(slot.modeId, valueOf(slot.modeId) === 'hold' ? 'toggle' : 'hold');
      } else if (item && item.row.kind !== 'number') {
        adjust(1, 0);
      }
    }
  };

  // ----- 改键：页面说"开始等键"，C++ 等到了发 "capture" 回来；等键时键盘、鼠标键与手柄的键到不了页面

  const startCapture = (item, index) => {
    if (ui.capture || ui.dialog) return;
    const active = document.activeElement;
    if (active && active.tagName && active.tagName.toUpperCase() === 'INPUT') active.blur();
    const slot = item.slots[index];
    sound('Select');
    statusText = '';
    ui.capture = {item, id: slot.id, pad: item.category.dirId === 'gamepad', message: ''};
    engine.send('capture', {id: slot.id});
    updateOption(item);
    refreshHints();
    refreshActions();
  };

  const endCapture = () => {
    const item = ui.capture && ui.capture.item;
    ui.capture = null;
    if (item) updateOption(item);
    refreshHints();
    refreshActions();
  };

  const onCapture = data => {
    if (data.label && data.key) keyLabels.set(data.key, data.label);
    if (data.result === 'reserved' || data.result === 'wrongDevice') {
      sound('Invalid');
      if (ui.capture) {
        ui.capture.message = data.result === 'reserved' ? `${keyName(data.key)} 不能绑，换一个键` : ui.capture.pad ? '这里要手柄上的键' : '这里要键盘或鼠标上的键';
        refreshActions();
      }
      return;
    }
    endCapture();
    if (data.result === 'conflict') openConflict(data);
    else sound(data.result === 'done' ? 'Confirm' : data.result === 'clear' ? 'Select' : 'Deny');
  };

  const slotTitle = id => {
    const item = owners.get(id);
    const slot = item.slots.find(entry => entry.id === id);
    return `${item.info.label} · ${slot.title}`;
  };

  // ----- 三个按钮与有改动时退出

  const pressAction = id => {
    if (closing) return;
    if (actions.isDisabled(id)) {
      sound('Invalid');
      return;
    }
    statusText = '';
    if (id === 'confirm') {
      engine.send('confirm');
    } else if (id === 'reset') {
      sound('Select');
      engine.send('resetDirectory', {dir: ui.dir});
    } else if (id === 'revert') {
      sound('Select');
      engine.send('revert');
    }
  };

  const goBack = () => {
    if (closing) return;
    if (ui.dialog) {
      pressDialog(ui.dialog.back);
    } else if (dirty > 0) {
      openExitDialog();
    } else {
      requestClose();
    }
  };

  const DIALOG_BUTTONS = [
    {id: 'confirm', label: '确认并退出', primary: true},
    {id: 'discard', label: '放弃修改'},
    {id: 'continue', label: '继续修改'},
  ];

  /** {title, text, buttons, primary（手柄的菜单键按它）, back（Esc、B 按它）, press(id)} */
  const openDialog = spec => {
    if (closing) return;
    sound('Select');
    const view = Paper.dialog(page, spec, id => pressDialog(id));
    ui.dialog = {view, focus: spec.primary, order: spec.buttons.map(button => button.id), press: spec.press, back: spec.back, primary: spec.primary};
    view.setFocus(ui.device === 'mouse' ? null : spec.primary);
    updateFocusRows();
    refreshActions();
  };

  const openExitDialog = () => openDialog({
    title: '还有没确认的修改',
    text: `有 ${dirty} 项修改还没确认。确认了才会存下来；放弃的话回到上一次确认时的样子。`,
    buttons: DIALOG_BUTTONS,
    primary: 'confirm',
    back: 'continue',
    press: id => {
      if (id === 'confirm') engine.send('confirm');
      else if (id === 'discard') requestClose();
    },
  });

  /** 按的键别的键位在用：问一句，互换以后两个键位都有键 */
  const openConflict = data => openDialog({
    title: '这个键已经在用了',
    text: `${keyName(data.key)} 现在是「${slotTitle(data.conflict)}」的键。互换的话，「${slotTitle(data.conflict)}」换成${valueOf(data.id) ? ` ${keyName(valueOf(data.id))}` : '空的'}。`,
    buttons: [{id: 'swap', label: '互换', primary: true}, {id: 'cancel', label: '取消'}],
    primary: 'swap',
    back: 'cancel',
    press: id => {
      if (id === 'swap') engine.send('assignKey', {id: data.id, key: data.key});
    },
  });

  const closeDialog = () => {
    if (!ui.dialog) return;
    ui.dialog.view.close();
    ui.dialog = null;
    updateFocusRows();
    refreshActions();
  };

  const moveDialog = delta => {
    const order = ui.dialog.order;
    const index = order.indexOf(ui.dialog.focus);
    const next = order[Paper.clamp(index + delta, 0, order.length - 1)];
    if (next !== ui.dialog.focus) sound('Focus');
    ui.dialog.focus = next;
    ui.dialog.view.setFocus(ui.dialog.focus);
  };

  const pressDialog = id => {
    const press = ui.dialog.press;
    closeDialog();
    // 确认并退出、放弃修改由 C++ 放（确认存没存下来、关闭）
    if (id === 'continue' || id === 'cancel') sound('Deny');
    else if (id === 'swap') sound('Confirm');
    press(id);
  };

  // ----- 键盘（手柄的 D-pad、左摇杆、A、B 也从这里来）

  const padActive = () => {
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    for (let i = 0; i < pads.length; ++i) {
      const pad = pads[i];
      if (!pad) continue;
      if (pad.buttons.some(b => b.pressed)) return true;
      if (Math.abs(pad.axes[0]) > 0.5 || Math.abs(pad.axes[1]) > 0.5) return true;
    }
    return false;
  };

  let repeatCount = 0;

  document.addEventListener('keydown', e => {
    if (!model || closing) return;
    if (ui.capture) {
      // 等键时 C++ 先收了；到这里的是消息还在路上那一帧的键
      e.preventDefault();
      if (e.key === 'Escape' && !e.repeat) {
        sound('Deny');
        engine.send('captureCancel');
        endCapture();
      }
      return;
    }
    const typing = document.activeElement && document.activeElement.tagName.toUpperCase() === 'INPUT';
    if (typing) {
      // 输入框里的键归输入框（回车在 Paper.numberField 里交）；Esc 退出输入框
      if (e.key === 'Escape') {
        document.activeElement.blur();
        e.preventDefault();
      }
      return;
    }
    const key = e.key;
    const nav = ['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Enter', ' ', 'Escape', 'F10', 'PageUp', 'PageDown', 'Tab'].includes(key);
    if (!nav) return;
    e.preventDefault();
    setDevice(padActive() ? 'gamepad' : 'keyboard');
    repeatCount = e.repeat ? repeatCount + 1 : 0;
    const vertical = key === 'ArrowUp' ? -1 : key === 'ArrowDown' ? 1 : 0;
    const horizontal = key === 'ArrowLeft' ? -1 : key === 'ArrowRight' ? 1 : 0;
    if (key === 'Escape' || key === 'F10') {
      // 按住 Esc 打开界面时，系统的按键重复不能又把它关上
      if (!e.repeat) goBack();
    } else if (ui.dialog) {
      if (horizontal) moveDialog(horizontal);
      if ((key === 'Enter' || key === ' ') && !e.repeat) activate();
    } else if (key === 'PageUp' || key === 'PageDown') {
      if (!e.repeat) cycleDirectory(key === 'PageUp' ? -1 : 1);
    } else if (key === 'Tab') {
      if (!e.repeat) {
        sound('Focus');
        ui.zone = ui.zone === 'list' ? 'actions' : 'list';
        if (ui.zone === 'actions') ui.action = firstEnabledAction();
        else if (!ui.focus) ui.focus = activeEntry().items[0].id;
        updateFocusRows();
        refreshActions();
        refreshHints();
      }
    } else if (key === 'Enter' || key === ' ') {
      if (!e.repeat) activate();
    } else if (ui.zone === 'actions') {
      if (horizontal) moveAction(horizontal);
      if (vertical < 0) {
        sound('Focus');
        ui.zone = 'list';
        if (!ui.focus) ui.focus = activeEntry().items[activeEntry().items.length - 1].id;
        updateFocusRows();
        refreshActions();
        refreshHints();
        const item = model.items.get(ui.focus);
        if (item) scrollIntoView(item);
      }
    } else if (vertical) {
      moveFocus(vertical);
    } else if (horizontal) {
      adjust(horizontal, repeatCount);
    }
  });

  // ----- 手柄：LB、RB、X、Y、菜单键（按下的沿）

  const PAD_LB = 4, PAD_RB = 5, PAD_X = 2, PAD_Y = 3, PAD_START = 9;
  let padHeld = null;   // 上一帧按着的
  let openedAt = 0;     // 页面拿到表的时候

  const pollPad = () => {
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    let pad = null;
    for (let i = 0; i < pads.length && !pad; ++i) pad = pads[i];
    if (!pad || !model || closing) return;
    const pressed = pad.buttons.map(b => b.pressed);
    // 手柄要按过键页面才看得见（weft 的规则），所以第一次读到时按着的就是真的按键，要算；只有打开界面的那一下菜单键不算（刚载入、还按着）
    if (padHeld === null) padHeld = pressed.map((held, index) => held && index === PAD_START && table.fromGamepad && now() - openedAt < 1000);
    const edge = index => pressed[index] && !padHeld[index];
    if (!ui.capture) {
      if (pressed.some((p, i) => p && !padHeld[i])) setDevice('gamepad');
      if (ui.dialog) {
        if (edge(PAD_START)) pressDialog(ui.dialog.primary);
      } else {
        if (edge(PAD_LB)) cycleDirectory(-1);
        if (edge(PAD_RB)) cycleDirectory(1);
        if (edge(PAD_X)) pressAction('reset');
        if (edge(PAD_Y)) pressAction('revert');
        if (edge(PAD_START)) pressAction('confirm');
      }
    }
    padHeld = pressed;
  };

  // 鼠标动了：提示换回鼠标的（真的鼠标停着时 Slate 也会补发移动，所以要真的动过）
  let lastPointerX = -1, lastPointerY = -1;
  document.addEventListener('mousemove', e => {
    if (lastPointerX >= 0 && (Math.abs(e.clientX - lastPointerX) > 2 || Math.abs(e.clientY - lastPointerY) > 2)) setDevice('mouse');
    lastPointerX = e.clientX;
    lastPointerY = e.clientY;
  });

  // ----- 描述区

  const UPDATE_INTERVAL = 100;   // ms：值变了以后描述区多久补一轮
  const PANEL_CACHE = 16;        // 留着的描述份数

  const hoverIntent = Paper.restIntent();
  let needsRender = false;
  let lastRender = -Infinity;
  const panels = new Map();
  let shownPanel = null;

  const markDirty = () => {
    needsRender = true;
  };

  const setHover = target => {
    if (ui.dragging) return;
    hoverIntent.set(target);
  };

  /** 指针停在一行或一个目录上算焦点动了；类别的标题、离开不算 */
  const hoverKey = target => (!target ? '' : target.type === 'item' ? `i:${target.item.id}` : target.type === 'directory' ? `d:${target.id}` : '');

  /** 描述区讲哪一个：拖着的 → 悬停的 → 键盘、手柄停着的（点过的）→ 这个目录 */
  const resolveTarget = () => {
    if (ui.dragging) return {type: 'item', item: ui.dragging};
    if (ui.hover && ui.hover.type === 'item') return {type: 'item', item: ui.hover.item};
    if (ui.hover && ui.hover.type === 'category') return {type: 'category', category: ui.hover.category};
    if (ui.hover && ui.hover.type === 'directory') {
      const hovered = entryOf(ui.hover.id);
      if (hovered) return {type: 'directory', dir: hovered.dir};
    }
    const item = ui.focus && model.items.get(ui.focus);
    if (item) return {type: 'item', item};
    return {type: 'directory', dir: activeEntry().dir};
  };

  const targetKey = target => target.type === 'item' ? `i:${target.item.id}` : target.type === 'category' ? `c:${target.category.dirId}/${target.category.id}` : `d:${target.dir.id}`;

  const describe = target => {
    if (target.type === 'item') {
      const info = target.item.info;
      const dir = entryOf(target.item.category.dirId).dir;
      return {title: info.label, en: info.en, crumb: `${dir.label} › ${target.item.category.title}`, text: info.text, tex: info.tex, preview: info.preview, figure: info.figure, image: dir.image, focusId: target.item.id};
    }
    if (target.type === 'category') {
      const dir = entryOf(target.category.dirId).dir;
      const first = target.category.items[0];
      return {title: target.category.title, en: target.category.en, crumb: dir.label, text: target.category.text, tex: target.category.tex, preview: first && first.info.preview, figure: first && first.info.figure, image: dir.image, focusId: first && first.id};
    }
    const d = target.dir.desc || {};
    return {title: target.dir.label, en: target.dir.en, crumb: '设置', text: d.text, tex: d.tex, preview: d.preview, figure: d.figure, image: target.dir.image};
  };

  const chipList = target => {
    if (target.type !== 'item') return [];
    const item = target.item;
    if (item.binding) {
      const show = read => item.slots.map(slot => ({key: read(slot.id) || '', mode: slot.modeId && read(slot.id) ? optionName(read(slot.modeId)) : ''}));
      const chips = [{key: '当前', keys: show(valueOf)}, {key: '默认', keys: show(id => row(id).def)}];
      if (itemDirtyCount(item) > 0) chips.push({key: '上次确认', keys: show(id => row(id).saved)});
      return chips;
    }
    const chips = [{key: '当前', text: showValue(item, valueOf(item.id))}, {key: '默认', text: showValue(item, item.row.def)}];
    if (isDirty(item.id)) chips.push({key: '上次确认', text: showValue(item, item.row.saved)});
    if (item.row.kind === 'number') chips.push({key: '范围', text: `${fmt(display(item, item.row.min), digitsOf(item))} – ${fmt(display(item, item.row.max), digitsOf(item))}`});
    const off = itemOff(item);
    if (off) chips.push({warn: `不起作用：${off}`});
    return chips;
  };

  const buildChip = (parent, chip) => {
    const node = el(parent, 'div', 'pp-chip');
    if (chip.warn) {
      node.style.color = C.vermilion;
      node.textContent = chip.warn;
      return;
    }
    el(node, 'span', 'pp-chip-key', chip.key);
    if (chip.keys) {
      // 键位：键帽，几个键位之间一条斜线
      const keys = el(node, 'span', 'st-chip-keys');
      chip.keys.forEach((entry, index) => {
        if (index > 0) el(keys, 'span', 'st-chip-sep', '/');
        if (entry.key) GinkgoKeycap.create(keys, entry.key, keyLabels.get(entry.key), 'dark', 18);
        else el(keys, 'span', 'pp-num', '未绑定');
        if (entry.mode) el(keys, 'span', 'st-chip-mode', entry.mode);
      });
      return;
    }
    el(node, 'span', 'pp-num', chip.text);
  };

  const panelFor = target => {
    const key = targetKey(target);
    let panel = panels.get(key);
    if (panel) {
      panels.delete(key);
      panels.set(key, panel);
      panel.target = target;
      return panel;
    }
    const info = describe(target);
    const node = el(null, 'div', 'pp-desc-panel');
    el(node, 'div', 'pp-desc-crumb', info.crumb || '');
    el(node, 'div', 'pp-desc-title', info.title || '');
    if (info.en) el(node, 'div', 'pp-desc-en', info.en);
    panel = {
      key, target, info, root: node,
      chipsHost: target.type === 'item' ? el(node, 'div', 'pp-chips') : null, chips: null,
      textHost: el(node, 'div', ''), paragraphs: [],
      formulaHost: el(node, 'div', ''), formulaBlock: null, blockAttached: false, formulas: [],
      figureHost: el(node, 'div', ''), figure: null, preview: null, previewKind: null,
      attached: 0,
      version: {text: -1, formulas: -1, preview: -1},
    };
    panels.set(key, panel);
    for (const [oldKey, old] of panels) {
      if (panels.size <= PANEL_CACHE) break;
      if (old !== shownPanel && old !== panel) panels.delete(oldKey);
    }
    return panel;
  };

  const panelContext = panel => ({...ctx, zoom: ctx.zoom, focusId: panel.info.focusId});

  const renderText = panel => {
    if (panel.chipsHost) {
      const chips = chipList(panel.target);
      const signature = JSON.stringify(chips);
      if (signature !== panel.chips) {
        clear(panel.chipsHost);
        for (const chip of chips) buildChip(panel.chipsHost, chip);
        panel.chips = signature;
      }
    }
    let paragraphs = [];
    try {
      paragraphs = panel.info.text ? panel.info.text(panelContext(panel)) : [];
    } catch (error) {
      paragraphs = [`（说明出错：${error && error.message}）`];
      log(`说明出错 ${panel.key}：${error && error.message}`);
    }
    const sources = paragraphs.filter(Boolean).map(String);
    for (let i = 0; i < sources.length; ++i) {
      const entry = panel.paragraphs[i];
      if (entry && entry.source === sources[i]) continue;
      const node = Paper.rich(null, sources[i]);
      if (entry) {
        panel.textHost.insertBefore(node, entry.node);
        panel.textHost.removeChild(entry.node);
        panel.paragraphs[i] = {source: sources[i], node};
      } else {
        panel.textHost.appendChild(node);
        panel.paragraphs.push({source: sources[i], node});
      }
    }
    while (panel.paragraphs.length > sources.length) panel.textHost.removeChild(panel.paragraphs.pop().node);
    panel.version.text = valuesVersion;
  };

  /** 一次最多替换一条节点，全部对齐后返回 true */
  const patchOne = (host, entries, sources, make) => {
    for (let i = 0; i < sources.length; ++i) {
      const entry = entries[i];
      if (entry && entry.source === sources[i]) continue;
      const node = make(sources[i]);
      if (entry) {
        host.insertBefore(node, entry.node);
        host.removeChild(entry.node);
        entries[i] = {source: sources[i], node};
      } else {
        host.appendChild(node);
        entries.push({source: sources[i], node});
      }
      return entries.length === sources.length && entries.every((e, k) => e.source === sources[k]);
    }
    while (entries.length > sources.length) host.removeChild(entries.pop().node);
    return true;
  };

  /** 行间公式：每帧最多排一条，未变动的条目直接复用 */
  const renderFormulas = panel => {
    if (panel.version.formulas === valuesVersion) return false;
    let formulas = [];
    try {
      formulas = panel.info.tex ? panel.info.tex(panelContext(panel)) : [];
    } catch (error) {
      formulas = [];
      log(`公式出错 ${panel.key}：${error && error.message}`);
    }
    if (formulas.length && !panel.formulaBlock) {
      panel.formulaBlock = el(panel.formulaHost, 'div', 'pp-formula');
      panel.blockAttached = true;
    }
    let done = true;
    if (panel.formulaBlock) {
      done = patchOne(panel.formulaBlock, panel.formulas, formulas.map(f => JSON.stringify([f.tex, f.note || ''])), source => {
        const [tex, note] = JSON.parse(source);
        const node = el(null, 'div', 'pp-formula-item');
        Paper.math(el(node, 'div', 'pp-formula-line'), tex, {size: 19});
        if (note) el(node, 'div', 'pp-formula-note', note);
        return node;
      });
      if (!formulas.length) {
        panel.formulaHost.removeChild(panel.formulaBlock);
        panel.formulaBlock = null;
        panel.blockAttached = false;
      }
    }
    panel.attached = heavyParts(panel).length;
    if (!done) return 'more';
    panel.version.formulas = valuesVersion;
    return true;
  };

  const formulasJob = panel => () => {
    if (shownPanel !== panel) return false;
    const result = renderFormulas(panel);
    if (result === 'more') descJobs.unshift(formulasJob(panel));
    return result;
  };

  const renderPreview = panel => {
    if (panel.version.preview === valuesVersion) return false;
    const c = panelContext(panel);
    const kind = panel.info.preview || null;
    if (!panel.previewKind && kind) {
      panel.previewKind = kind;
      const figure = el(panel.figureHost, 'div', 'pp-figure');
      panel.figure = figure;
      el(figure, 'div', 'pp-figure-title', panel.info.figure || '预览');
      const canvas = el(figure, 'div', 'pp-figure-canvas');
      try {
        panel.preview = SettingsPreviews.create(kind, canvas, c);
      } catch (error) {
        log(`预览出错 ${kind}：${error && error.message}`);
      }
    } else if (panel.preview) {
      try {
        panel.preview.update(c);
      } catch (error) {
        log(`预览出错 ${kind}：${error && error.message}`);
      }
    }
    panel.attached = heavyParts(panel).length;
    panel.version.preview = valuesVersion;
    return true;
  };

  const heavyParts = panel => {
    const parts = panel.formulas.map(entry => ({host: panel.formulaBlock, node: entry.node, formula: true}));
    if (panel.figure) parts.push({host: panel.figureHost, node: panel.figure});
    return parts;
  };

  const stowPanel = panel => {
    const parts = heavyParts(panel);
    for (let i = 0; i < panel.attached; ++i) parts[i].host.removeChild(parts[i].node);
    if (panel.blockAttached) {
      panel.formulaHost.removeChild(panel.formulaBlock);
      panel.blockAttached = false;
    }
    panel.attached = 0;
  };

  const attachNext = panel => () => {
    if (shownPanel !== panel) return false;
    const parts = heavyParts(panel);
    if (panel.attached >= parts.length) return false;
    const part = parts[panel.attached++];
    if (part.formula && !panel.blockAttached) {
      panel.formulaHost.appendChild(panel.formulaBlock);
      panel.blockAttached = true;
    }
    part.host.appendChild(part.node);
    return true;
  };

  /** 切换描述面板：正文当帧更新，公式与预览分帧挂载与重排 */
  const refreshDescription = () => {
    if (!model) return;
    const panel = panelFor(resolveTarget());
    if (shownPanel !== panel) {
      if (shownPanel) {
        descScroll.removeChild(shownPanel.root);
        stowPanel(shownPanel);
      }
      descScroll.appendChild(panel.root);
      shownPanel = panel;
      descJobs.length = 0;
      for (let i = panel.attached, n = heavyParts(panel).length; i < n; ++i) descJobs.push(attachNext(panel));
      const image = `pp-desc-image ${Catalog.IMAGES[panel.info.image] || ''}`;
      if (imageLayer.className !== image) imageLayer.className = image;
    }
    if (panel.version.text !== valuesVersion) renderText(panel);
    if (panel.version.formulas !== valuesVersion) descJobs.push(formulasJob(panel));
    if (panel.version.preview !== valuesVersion) descJobs.push(() => shownPanel === panel && renderPreview(panel));
    busyFrame = true;
  };

  // ----- 每帧

  const loop = time => {
    const t = now();
    pollPad();
    if (busyFrame) {
      busyFrame = false;
    } else if (hoverIntent.due(t)) {
      const before = hoverKey(ui.hover);
      ui.hover = hoverIntent.take();
      if (hoverKey(ui.hover) && hoverKey(ui.hover) !== before) sound('Focus');
      refreshDescription();
      busyFrame = false;
    } else if (needsRender && t - lastRender >= UPDATE_INTERVAL && model) {
      needsRender = false;
      lastRender = t;
      refreshDescription();
      busyFrame = false;
    } else {
      runJobs();
    }
    const preview = shownPanel && shownPanel.preview;
    if (preview && preview.frame && shownPanel.figure && shownPanel.figure.parentNode) {
      try {
        preview.frame(time / 1000);
      } catch (error) {
        log(`预览出错：${error && error.message}`);
        shownPanel.preview = null;
      }
    }
    requestAnimationFrame(loop);
  };

  // ===== 事件 =====

  const build = data => {
    table = data;
    closing = false;
    root.className = 'st-dark';
    openedAt = now();
    rows.clear();
    for (const entry of data.rows || []) rows.set(entry.id, entry);
    for (const [name, text] of Object.entries(data.keyLabels || {})) keyLabels.set(name, text);
    model = buildModel();
    ++valuesVersion;
    dirty = data.dirty || 0;
    let saved = {};
    try {
      saved = data.state ? JSON.parse(data.state) : {};
    } catch (error) {
      saved = {};
    }
    ui.device = data.fromGamepad ? 'gamepad' : 'mouse';
    // 上次停在收起来的一行（关了总开关以后没确认就关了页面之类）：不停在那里
    ui.focus = saved.focus && model.items.has(saved.focus) && !itemHidden(model.items.get(saved.focus)) ? saved.focus : null;
    clear(page);
    rail = Paper.rail(page, {
      emblem: {icon: 'leaf', text: '设置'},
      items: model.directories.map(d => ({id: d.dir.id, icon: d.dir.icon, label: d.dir.label})),
      onSelect: id => {
        if (id !== ui.dir) sound('Select');
        selectDirectory(id);
      },
      onHover: id => setHover(id ? {type: 'directory', id} : null),
    });
    hintFoot = el(rail.root, 'div', 'pp-rail-foot');
    page.appendChild(column);
    page.appendChild(desc);
    const startDir = entryOf(saved.dir) ? saved.dir : data.fromGamepad ? 'gamepad' : 'mouse';
    ui.dir = null;
    selectDirectory(startDir);
    if (ui.device !== 'mouse' && !ui.focus) setFocus(activeEntry().items[0].id);
    refreshHints();
    refreshActions();
    idleAfter = now() + 400;
    queuePrebuild();
  };

  const apply = data => {
    const ids = [];
    for (const [name, text] of Object.entries(data.keyLabels || {})) keyLabels.set(name, text);
    for (const value of data.rows || []) {
      const entry = row(value.id);
      if (!entry) continue;
      entry.saved = value.saved;
      if (value.disabled !== undefined) entry.disabled = value.disabled;
      if (value.displayValue !== undefined) entry.displayValue = value.displayValue;
      // 正在拖的那一项以页面为准（游戏回来的可能是上一下的值）
      if (!(ui.dragging && ui.dragging.id === value.id && Paper.isDragging())) entry.value = value.value;
      ids.push(value.id);
    }
    statusText = data.status || '';
    valuesChanged(ids);
  };

  // 自检（ginkgo.settings.input selfcheck）：把页面的公式算一遍发回去，C++ 与自己的比
  engine.on('selfcheck', () => {
    const M = SettingsMath;
    const samples = [];
    const kinds = [['linear', 0], ['power', 1], ['scurve', 2]];
    for (const [name, curve] of kinds) {
      for (const strength of [1, 1.5, 2, 3, 4]) {
        for (let i = 0; i <= 20; ++i) samples.push({kind: 'curve', curve, strength, in: i / 20, out: M.curve(i / 20, name, strength)});
      }
    }
    for (const [inner, outer] of [[0.2, 1], [0, 1], [0.05, 0.8], [0.5, 0.55]]) {
      for (let i = 0; i <= 20; ++i) samples.push({kind: 'zone', inner, outer, in: i / 20, out: M.zone(i / 20, inner, outer)});
    }
    for (const [strength, offset, cap] of [[0.03, 3, 2], [0.2, 0, 4], [0, 5, 1]]) {
      for (let i = 0; i <= 12; ++i) samples.push({kind: 'gain', strength, offset, cap, in: i * 5, out: M.gain(i * 5, strength, offset, cap)});
    }
    for (const [delay, ramp] of [[0.1, 0.3], [0, 1], [0.5, 0.25]]) {
      for (let i = 1; i <= 12; ++i) samples.push({kind: 'ramp', delay, ramp, in: i * 0.1, out: M.rampWeight(i * 0.1, delay, ramp)});
    }
    // 辅助瞄准的强度随离中线的距离（度）：两个半径一样时是一道坎
    for (const [inner, outer] of [[1.15, 6.28], [0, 3.4], [2, 2], [0.5, 17]]) {
      for (let i = 0; i <= 24; ++i) samples.push({kind: 'assist', inner, outer, in: i * 0.3, out: M.assistFalloff(i * 0.3, inner, outer)});
    }
    engine.send('selfcheck', samples);
  });

  engine.on('settings', build);
  engine.on('values', apply);
  engine.on('capture', onCapture);
  engine.on('settings-closing', startExit);
  requestAnimationFrame(loop);
  engine.send('ready');
})();
