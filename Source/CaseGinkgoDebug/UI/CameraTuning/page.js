// 相机的调参面板（Docs/Design/CameraPipeline.md「调试工具」）：纸页，左边是目录，中间约四成宽是这一类的选项，右边是描述区。
// 样式与组件是共用的主题（Content/UI/Theme 的 theme.css、paper.js，C++ 接在这个页面前面），目录与说明在 catalog.js，预览在 previews.js。
//
// 游戏发两种事件（C++ 的 GinkgoCameraTuning.cpp）：
//   "tuning"：整张表 {file, key, config, state, context, changed, unsaved, status, groups: [组], rows: [行]}
//     组 {path, kind: struct | element | config, property, propertyName, type, typeName, element, param, paramName, paramUnits, event,
//         envelopeScalar, query, conditions: [{scalar, thresholdScalar}]}
//     行 {path, group, kind: number | int | bool | enum | rays, value, def, changed, unsaved, chain: [{field, owner, name, index?}],
//         clampmin?, clampmax?, uimin?, uimax?, units?, tooltip?, options?, optionNames?}
//   "values"：变了的行 {rows: [{path, value, changed, unsaved}], changed, unsaved, status}
// 页面发：ready、set {path, value}、reset {path}、resetAll、save、close、state {dir, pinned, collapsed}（关了再开回到原处）。
//
// 描述区讲的是：鼠标停着的那一项（或类别、目录）；不停在选项上时讲点过的那一项（标签条变成银杏金），都没有时讲这个目录。
// 描述图按"具体的项 → 类别 → 目录 → 相机"逐级回退，现在默认均使用 camera.png；图的不透明度不到 100%（theme.css）。
//
// 帧预算与防卡顿（实测一次性重建单行描述约 20–70 ms、切换目录约 84–168 ms）。weft 把新接进文档的节点排样式、布局、进场景，一个约 0.07–0.1 ms
// （滑铲那一栏约 1200 个节点一下接进来 85–118 ms）；改已有节点的属性几乎不要钱（一帧约 0.3 ms）；脚本在文档外搭节点也便宜（一行选项
// 约 0.5 ms）。所以：
// - 指针停住 90 ms 才换描述（选项、类别、目录都是），扫过去的不重画；离开选项、指针停住 180 ms 才回到点过的项或目录，移到下一行时不先回去一趟；
// - 描述区按讲的东西各搭一份、留着最近的 24 份；说明当帧搭，公式一条一帧、预览一帧，排在后面（一帧最多做一件活）；
//   值变了时每 100 ms 补一轮（拖滑块时说明与公式 300 ms 一轮），同一位置的段落、公式没变的不重排，预览原地改属性；
// - 每个目录的选项栏在文档外搭好（空闲时预先搭），换到它时一帧接两行、从上往下接进来；不显示的目录不在文档里；
//   描述区换走时公式与预览从那一份上拿下来，回来时一帧一块地接回去。
(() => {
  const engine = weft.engine;
  // 菜单的声音（Docs/Design/Control.md「菜单的声音」）：Focus、Select；打开、关闭、存没存下来由 C++ 放
  const sound = kind => engine.send('sound', kind);
  const {C, el, clear, fmt} = Paper;
  const Catalog = CameraCatalog;
  const root = document.getElementById('root');
  const now = () => performance.now();

  // ===== 表 =====

  let table = null;
  const values = new Map();       // 路径 → 当前值
  const rowsByPath = new Map();   // 路径 → 行
  const groups = new Map();       // 组的路径 → 组
  let model = null;               // {directories: [...], items: Map(id → item), categories: Map(id → category), itemsByPath: Map(路径 → item)}
  const ui = {dir: null, pinned: null, hover: null, collapsed: new Set(), dragging: null};
  let valuesVersion = 0;          // 值每变一次加一：描述区的每一段记着自己是按哪一版画的

  const valueOf = path => values.get(path);
  const numberOf = path => {
    const v = values.get(path);
    return typeof v === 'number' ? v : (typeof v === 'boolean' ? (v ? 1 : 0) : 0);
  };

  // ===== 读值的接口（说明与预览共用） =====

  const groupInfo = path => groups.get(path) || {path, kind: 'config'};

  const conditionInfo = (groupPath, index) => {
    const group = groupInfo(groupPath);
    const entry = (group.conditions || [])[index];
    if (!entry) return null;
    const prefix = `${groupPath}.Activation.Conditions[${index}]`;
    const compare = valueOf(`${prefix}.Compare`);
    const thresholdScalar = entry.thresholdScalar || '';
    const move = table.context || {};
    const scalarValue = tag => (tag === 'Movement.Value.RunSpeed' ? move.runSpeed || 476.25 : 0);
    return {
      prefix, scalar: entry.scalar, scalarName: Catalog.tagName(entry.scalar), thresholdScalar, thresholdName: Catalog.tagName(thresholdScalar),
      atLeast: compare !== 'Below', threshold: thresholdScalar ? scalarValue(thresholdScalar) : numberOf(`${prefix}.Threshold`),
      holds(scalars) {
        const value = scalars[entry.scalar] || 0;
        const limit = thresholdScalar ? (scalars[thresholdScalar] || 0) : numberOf(`${prefix}.Threshold`);
        return compare === 'Below' ? value < limit : value >= limit;
      },
    };
  };

  const modifierSummaries = () => {
    const out = [];
    for (const group of groups.values()) {
      if (group.property !== 'Modifiers') continue;
      const element = Catalog.ELEMENTS[group.element];
      const conditions = (group.conditions || []).map((entry, index) => conditionInfo(group.path, index)).filter(Boolean);
      out.push({
        path: group.path, element: group.element, title: element ? element.title : (group.element || group.path), param: group.param,
        unit: element ? element.unit : (Catalog.UNITS[group.paramUnits] || ''), value: numberOf(`${group.path}.Value.Value`),
        enabled: valueOf(`${group.path}.bEnabled`) !== false, latch: !!valueOf(`${group.path}.bLatchOff`),
        blendIn: numberOf(`${group.path}.BlendInTime`), blendOut: numberOf(`${group.path}.BlendOutTime`), conditions,
        slide: queryHas(group.query, 'Movement.Slide'),
        // 各项的默认值：预览把"都是默认值时"画成灰的虚线
        defaults: {
          value: defaultOf(`${group.path}.Value.Value`, 0), enabled: defaultOf(`${group.path}.bEnabled`, true) !== false,
          latch: !!defaultOf(`${group.path}.bLatchOff`, false), blendIn: defaultOf(`${group.path}.BlendInTime`, 0), blendOut: defaultOf(`${group.path}.BlendOutTime`, 0),
        },
      });
    }
    return out;
  };

  const queryHas = (expr, tag) => !!expr && ((expr.tags || []).includes(tag) && expr.op !== 'none' || (expr.exprs || []).some(e => queryHas(e, tag)));

  const defaultOf = (path, fallback) => {
    const row = rowsByPath.get(path);
    return row && row.def !== undefined ? row.def : fallback;
  };

  const springOmega = param => {
    for (const group of groups.values()) {
      if (group.property === 'ParamSprings' && group.param === param) return numberOf(`${group.path}.Omega`);
    }
    return 0;
  };

  const springOmegaDef = param => {
    for (const group of groups.values()) {
      if (group.property === 'ParamSprings' && group.param === param) return defaultOf(`${group.path}.Omega`, 0);
    }
    return 0;
  };

  /** 说明与预览的上下文：target 是 {type: item | category | directory, …} */
  const context = target => {
    const groupPath = target.groupPath || '';
    const item = target.type === 'item' ? target.item : null;
    const rel = path => (groupPath ? `${groupPath}.${path}` : path);
    const c = {
      groupPath,
      group: groupInfo(groupPath),
      move: table.context || {},
      v: item ? itemValue(item) : undefined,
      d: item ? itemDefault(item) : undefined,
      value: () => c.v,
      def: () => c.d,
      get: path => numberOf(rel(path)),
      abs: path => numberOf(path),
      raw: path => valueOf(rel(path)),
      params: () => {
        const p = {};
        for (const field of ['ArmLength', 'ShoulderOffsetRight', 'ShoulderOffsetUp', 'PivotOffsetForward', 'PivotOffsetRight', 'PivotOffsetUp', 'FieldOfView', 'Roll',
          'PostProcessWeight']) p[field] = numberOf(`BaseParams.${field}`);
        return p;
      },
      groupBlend: () => valueOf(rel('Blend')),
      springOmega,
      springOmegaDef,
      modifierValue: element => numberOf(`Modifiers[${element}].Value.Value`),
      modifierGroup: element => {
        const path = `OverlayEffects[${element}]`;
        return groups.has(path) ? {get: field => numberOf(`${path}.${field}`)} : null;
      },
      modifiers: modifierSummaries,
      condition: () => (item && item.kind === 'condition' ? conditionInfo(item.groupPath, item.conditionIndex) : null),
      conditionThreshold: index => {
        const cond = conditionInfo(groupPath, index);
        return cond ? cond.threshold : 0;
      },
      tag: Catalog.tagName,
      focusIn: fields => !!item && fields.some(field => field && (item.relPath === field || item.lastField === field)),
    };
    // 通用的弹簧预览：目录"通用"讲的是第一个参数弹簧
    if (target.type === 'directory' && target.dir.desc && target.dir.desc.valueFrom === 'springs') {
      const first = [...groups.values()].find(g => g.property === 'ParamSprings');
      if (first) {
        c.v = numberOf(`${first.path}.Omega`);
        c.d = (rowsByPath.get(`${first.path}.Omega`) || {}).def;
      }
    }
    if (target.type === 'directory' && target.dir.desc && target.dir.desc.groupFor) {
      const path = [...groups.values()].find(g => g.element === target.dir.desc.groupFor);
      if (path) {
        c.groupPath = path.path;
        c.group = path;
        c.get = field => numberOf(`${path.path}.${field}`);
      }
    }
    return c;
  };

  // ===== 行 → 目录、类别、选项 =====

  const itemValue = item => (item.kind === 'condition' ? numberOf(`${item.prefix}.Threshold`) : valueOf(item.row.path));
  const itemDefault = item => (item.kind === 'condition' ? (rowsByPath.get(`${item.prefix}.Threshold`) || {}).def : item.row.def);

  const buildModel = () => {
    const categories = new Map();
    const items = new Map();
    const itemsByPath = new Map();
    const directories = Catalog.DIRECTORIES.map(dir => ({dir, categories: []}));
    const dirEntry = id => directories.find(d => d.dir.id === id) || directories.find(d => d.dir.id === 'other');
    const categoryFor = (dirId, catId, group) => {
      const id = `${dirId}/${catId}`;
      if (categories.has(id)) return categories.get(id);
      const entry = dirEntry(dirId);
      const fixed = (entry.dir.categories || []).find(category => category.id === catId);
      const element = Catalog.ELEMENTS[group.element];
      const category = {
        id, dirId: entry.dir.id, groupPath: fixed ? '' : group.path, fixed, element, group, items: [],
        title: fixed ? fixed.title : element ? element.title : `${group.typeName || group.type || ''} ${group.element || group.path}`.trim(),
        en: fixed ? fixed.en : element ? element.en : (group.element || '').toUpperCase(),
        icon: element ? element.icon : (fixed && fixed.icon) || 'dot',
        order: fixed ? (fixed.afterElements ? 200 + (entry.dir.categories || []).indexOf(fixed) : (entry.dir.categories || []).indexOf(fixed)) : element ? (entry.dir.elements || []).indexOf(group.element) + 100 : 1000 + categories.size,
      };
      categories.set(id, category);
      entry.categories.push(category);
      return category;
    };
    for (const row of table.rows) {
      const group = groupInfo(row.group);
      const where = Catalog.place(row, group);
      const category = categoryFor(where.dir, where.cat, group);
      const relPath = row.group ? row.path.slice(row.group.length + 1) : row.path;
      if (Catalog.isCondition(row)) {
        // 条件：比较与阈值合成一行
        const segment = row.chain.find(s => s.field === 'Conditions');
        const index = segment ? segment.index : 0;
        const prefix = `${row.group}.Activation.Conditions[${index}]`;
        let item = items.get(prefix);
        if (!item) {
          item = {id: prefix, kind: 'condition', prefix, conditionIndex: index, groupPath: row.group, group, category, relPath: 'Condition', lastField: 'Condition',
            key: 'Condition', info: Catalog.ITEMS.Condition};
          items.set(item.id, item);
          category.items.push(item);
        }
        itemsByPath.set(row.path, item);
        continue;
      }
      const key = Catalog.keyOf(row);
      const info = Catalog.ITEMS[`${group.element}:${key}`] || Catalog.ITEMS[key] || null;
      if (info && info.showIn && !info.showIn(group)) continue;
      const item = {id: row.path, kind: row.kind, row, groupPath: row.group, group, category, relPath, lastField: Catalog.lastField(row), key, info};
      items.set(item.id, item);
      itemsByPath.set(row.path, item);
      category.items.push(item);
    }
    // 类别里先放开关、条件与锁住（C++ 按声明的顺序收，基类的字段排在最后），其余照声明的顺序。
    // 标签条：每个类别一种颜色，按显示的顺序轮着给（相邻的类别不同色）；种子按排好的行轮着给
    const rank = item => (item.lastField === 'bEnabled' ? 0 : item.kind === 'condition' ? 1 + item.conditionIndex * 0.01 : item.lastField === 'bLatchOff' ? 2 : 10);
    let seed = 0;
    let palette = 0;
    for (const entry of directories) {
      entry.categories.sort((a, b) => a.order - b.order);
      for (const category of entry.categories) {
        category.palette = Paper.STRIP_PALETTES[palette++ % Paper.STRIP_PALETTES.length];
        category.items = category.items.map((item, index) => ({item, index})).sort((a, b) => rank(a.item) - rank(b.item) || a.index - b.index).map(x => x.item);
        for (const item of category.items) item.seed = seed++;
      }
    }
    return {directories: directories.filter(d => d.categories.length > 0), items, categories, itemsByPath};
  };

  const labelOf = item => {
    if (item.kind === 'condition') {
      const cond = conditionInfo(item.groupPath, item.conditionIndex);
      return cond ? cond.scalarName : '条件';
    }
    if (item.info && item.info.label) return item.info.label(context({type: 'item', item, groupPath: item.groupPath}));
    const names = item.row.chain.map(s => s.name).filter(Boolean);
    return names.length ? names.join(' · ') : item.row.path;
  };

  const unitOf = item => {
    const info = item.info || {};
    if (item.kind === 'condition') {
      const cond = conditionInfo(item.groupPath, item.conditionIndex);
      return cond && /Speed|SlideBoost/.test(cond.scalar) ? 'cm/s' : '';
    }
    if (info.unitFor) return info.unitFor(context({type: 'item', item, groupPath: item.groupPath}));
    if (info.unit) return info.unit;
    return Catalog.UNITS[item.row.units] || '';
  };

  const rangeOf = item => {
    const info = item.info || {};
    if (item.kind === 'condition') {
      const cond = conditionInfo(item.groupPath, item.conditionIndex);
      if (cond && /SlideBoost/.test(cond.scalar)) return {lo: 0, hi: 500, step: 1};
      if (cond && /Speed/.test(cond.scalar)) return {lo: 0, hi: 1200, step: 1};
      return {lo: 0, hi: 1, step: 0.01};
    }
    const row = item.row;
    const hard = {lo: row.clampmin, hi: row.clampmax};
    let range = info.rangeFor ? info.rangeFor(context({type: 'item', item, groupPath: item.groupPath})) : info.range;
    if (!range) {
      const lo = row.uimin !== undefined ? row.uimin : row.clampmin;
      const hi = row.uimax !== undefined ? row.uimax : row.clampmax;
      const span = Math.max(Math.abs(row.def || 0) * 2, 1);
      range = [lo !== undefined ? lo : -span, hi !== undefined ? hi : (lo !== undefined ? lo + span : span)];
    }
    const step = info.step || (row.kind === 'int' ? 1 : undefined);
    return {lo: range[0], hi: range[1], step, hard};
  };

  const digitsOf = item => {
    const range = rangeOf(item);
    if (item.kind === 'int') return 0;
    return range.step ? Math.max(0, Math.min(4, -Math.floor(Math.log10(range.step) + 1e-9))) : 3;
  };

  const rowChanged = path => {
    const row = rowsByPath.get(path);
    return !!(row && row.changed);
  };

  const itemChanged = item => (item.kind === 'condition' ? rowChanged(`${item.prefix}.Compare`) || rowChanged(`${item.prefix}.Threshold`) : rowChanged(item.row.path));

  // ===== 分帧的活 =====
  // 页面时钟在一次执行里不走，没法按毫秒切，只能按件切：每帧最多做一件。先接选项栏的下一批行，再做描述区排在后面的段（公式、预览），
  // 都没有时做空闲的活（在文档外预先搭别的目录）。活返回 false 表示这次没事可做（比如讲的东西已经换了），接着看下一件。
  // 这一帧的事件里已经做过一件（换描述、换目录）时，循环这一帧不再做

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
  const card = el(column, 'div', 'pp-card');
  const imageLayer = el(desc, 'div', 'pp-desc-image');
  const descScroll = el(desc, 'div', 'pp-desc-scroll');
  let closeButton = null;

  // 状态卡：和默认值不同几项、没存几项、最近一次的结果；保存与全部恢复
  const cardTitleRow = el(card, 'div', 'pp-card-row');
  el(cardTitleRow, 'div', 'pp-card-title', '本地调参');
  const cardCount = el(cardTitleRow, 'div', 'pp-card-count', '');
  const cardStatus = el(card, 'div', 'pp-card-line', '');
  const cardFile = el(card, 'div', 'pp-card-line pp-mono', '');
  const cardActions = el(card, 'div', 'pp-card-actions');
  Paper.button(cardActions, '保存到本地', true, () => engine.send('save'));
  Paper.button(cardActions, '全部恢复默认', false, () => {
    sound('Select');
    engine.send('resetAll');
  });

  let summary = {changed: 0, unsaved: 0, status: ''};
  const showSummary = () => {
    cardCount.textContent = `和默认值不同 ${summary.changed} 项 · 没存的 ${summary.unsaved} 项`;
    cardStatus.textContent = summary.status || '';
    cardFile.textContent = table ? table.file : '';
  };

  const sendState = () => engine.send('state', {dir: ui.dir, pinned: ui.pinned, collapsed: [...ui.collapsed]});

  // ----- 目录

  const selectDirectory = id => {
    ui.dir = id;
    ui.hover = null;
    hoverIntent.cancel();
    if (ui.pinned && (!model.items.has(ui.pinned) || model.items.get(ui.pinned).category.dirId !== id)) ui.pinned = null;
    rail.select(id);
    showDirView(dirView(id) || dirView(model.directories[0].dir.id));
    refreshDescription();
    sendState();
  };

  // ----- 选项栏：每个目录一份，在文档外搭（空闲时预先搭），显示时一批批地接进文档

  const REVEAL_ROWS = 2;       // 一帧接进文档的选项行（一行约 25 个节点，weft 排一行约 5–7 ms）
  const PREBUILD_ROWS = 6;     // 空闲时一帧在文档外搭的行
  const dirViews = new Map();  // 目录的 id → {entry, head, list, steps: [还没搭的], pieces: [搭好的小节与行，按顺序], shown: 接进去了几块}
  let activeView = null;

  const dirView = id => {
    if (dirViews.has(id)) return dirViews.get(id);
    const entry = model.directories.find(d => d.dir.id === id);
    if (!entry) return null;
    const dir = entry.dir;
    // 不显示的目录不在文档里（weft 只处理文档里的节点；在文档外搭只花脚本的时间）
    const head = el(null, 'div', 'pp-head pp-enter');
    const title = el(head, 'div', 'pp-title', dir.label);
    el(title, 'span', 'pp-title-en', dir.en);
    el(head, 'div', 'pp-intro', dir.intro || '');
    Paper.ornament(head, 260);
    const list = el(null, 'div', 'pp-list');
    // 搭的步骤：每个类别先搭小节，再一行一行地搭选项；搭好的按顺序记成"块"（小节接到列表里，行接到小节里）
    const view = {id, entry, head, list, steps: [], pieces: [], shown: 0};
    for (const category of entry.categories) {
      view.steps.push(() => buildSection(view, category));
      for (const item of category.items) view.steps.push(() => buildRow(view, category, item));
    }
    dirViews.set(id, view);
    return view;
  };

  /** 搭下一步；搭了返回 true */
  const buildStep = view => {
    const step = view.steps.shift();
    if (!step) return false;
    step();
    return true;
  };

  const buildSection = (view, category) => {
    const section = Paper.section(null, {
      title: category.title, en: category.en, open: !ui.collapsed.has(category.id), note: activationNote(category),
      onToggle: open => {
        sound('Select');
        if (open) ui.collapsed.delete(category.id);
        else ui.collapsed.add(category.id);
        sendState();
      },
      onHover: entering => setHover(entering ? {type: 'category', category} : null),
    });
    category.section = section;
    view.pieces.push({node: section.root, parent: view.list, rows: 0});
  };

  const buildRow = (view, category, item) => {
    buildOption(null, item);
    updateOption(item);
    view.pieces.push({node: item.view.view.root, parent: category.section.body, rows: 1});
    if (item === category.items[category.items.length - 1]) updateCategory(category);
  };

  /** 接下一批（小节与行按顺序，一批 REVEAL_ROWS 行）；还没搭到的先搭。还有剩的就再排一件 */
  const revealNext = view => {
    if (view !== activeView) return false;
    while (view.steps.length && view.pieces.length - view.shown < REVEAL_ROWS + 2) buildStep(view);
    if (view.shown >= view.pieces.length) return false;
    let rows = 0;
    while (view.shown < view.pieces.length && rows < REVEAL_ROWS) {
      const piece = view.pieces[view.shown++];
      piece.parent.appendChild(piece.node);
      rows += piece.rows;
    }
    if (view.shown < view.pieces.length || view.steps.length) revealJobs.push(() => revealNext(view));
    return true;
  };

  /** 换目录：拿下原来那一份（拿下来的行放回文档外，不在文档里拆开不要钱），接上标题与空的列表，行从下一帧起一批批地接 */
  const showDirView = view => {
    if (activeView === view) return;
    if (activeView) {
      const old = activeView;
      column.removeChild(old.head);
      column.removeChild(old.list);
      for (let i = old.shown - 1; i >= 0; --i) old.pieces[i].parent.removeChild(old.pieces[i].node);
      old.shown = 0;
    }
    column.insertBefore(view.head, card);
    column.appendChild(view.list);
    activeView = view;
    revealJobs.length = 0;
    revealJobs.push(() => revealNext(view));
  };

  /** 空闲时在文档外把别的目录搭好：一件活几行 */
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

  /** 类别下面一行小字：修改器、叠加效果什么时候起作用（标签查询、连续量的条件、事件） */
  const activationNote = category => {
    const group = category.group;
    if (!group || category.fixed || (group.kind !== 'element')) return '';
    const parts = [];
    if (group.event) parts.push(`事件「${Catalog.tagName(group.event)}」`);
    const query = describeQuery(group.query);
    if (query) parts.push(query);
    (group.conditions || []).forEach((entry, index) => {
      const cond = conditionInfo(group.path, index);
      if (cond) parts.push(`${cond.scalarName} ${cond.atLeast ? '≥' : '<'} ${cond.thresholdScalar ? cond.thresholdName : fmt(cond.threshold, 2)}`);
    });
    if (group.property === 'Modifiers' && valueOf(`${group.path}.bLatchOff`)) parts.push('单次失效锁死');
    if (group.param) parts.push(`作用于${group.paramName || group.param}`);
    return parts.length ? `激活条件：${parts.join('，')}` : '';
  };

  const describeQuery = expr => {
    if (!expr) return '';
    const names = (expr.tags || []).map(Catalog.tagName);
    switch (expr.op) {
      case 'all': case 'allExact': return names.join('、');
      case 'any': case 'anyExact': return names.join('或');
      case 'none': return names.map(name => `非「${name}」`).join('、');
      case 'allOf': return (expr.exprs || []).map(describeQuery).filter(Boolean).join('、');
      case 'anyOf': return (expr.exprs || []).map(describeQuery).filter(Boolean).join('或');
      case 'noneOf': return `非（${(expr.exprs || []).map(describeQuery).filter(Boolean).join('、')}）`;
      default: return '';
    }
  };

  const buildOption = (parent, item) => {
    const info = item.info || {};
    const view = Paper.option(parent, {icon: info.icon || 'dot', rotate: info.rotate, label: labelOf(item), seed: item.seed, palette: item.category.palette});
    const controls = {};
    const control = view.control;
    if (item.kind === 'rays') {
      // 仍沿用纸页的标签/恢复按钮；多行编辑器作为整个选项的一部分，不受标准 40px 行高裁切。
      view.root.style.height = 'auto';
      view.root.style.display = 'block';
      const header = el(view.root, 'div', 'ct-ray-header');
      header.appendChild(view.label);
      header.appendChild(control);
      el(control, 'div', 'pp-control-text', '相机起点 · 平视示意');
      controls.rays = CameraRayEditor.create(view.root, {
        value: valueOf(item.row.path), maxItems: item.row.maxItems || 64,
        onChange: value => choose(item.row.path, value),
      });
    } else if (item.kind === 'condition') {
      const cond = conditionInfo(item.groupPath, item.conditionIndex);
      const compareRow = rowsByPath.get(`${item.prefix}.Compare`);
      const options = (compareRow && compareRow.options) || ['AtLeast', 'Below'];
      controls.seg = Paper.segmented(control, {
        options: options.map((value, index) => ({value, label: value === 'AtLeast' ? '≥' : value === 'Below' ? '<' : (compareRow.optionNames || [])[index] || value})),
        value: valueOf(`${item.prefix}.Compare`),
        onChange: value => choose(`${item.prefix}.Compare`, value),
      });
      if (cond && cond.thresholdScalar) {
        el(control, 'div', 'pp-control-text', `${cond.thresholdName}（${fmt(cond.threshold, 1)} cm/s，同步移动配置）`);
      } else {
        numberControls(control, item, `${item.prefix}.Threshold`, controls);
      }
    } else if (item.kind === 'bool') {
      controls.toggle = Paper.toggle(control, {value: valueOf(item.row.path), onChange: value => choose(item.row.path, value)});
    } else if (item.kind === 'enum') {
      const row = item.row;
      const options = (row.options || []).map((value, index) => ({value, label: (row.optionNames || [])[index] || value}));
      if (options.length > 5) {
        const cycle = delta => {
          const index = options.findIndex(option => option.value === valueOf(row.path));
          choose(row.path, options[(index + delta + options.length) % options.length].value);
        };
        Paper.button(control, '‹', false, () => cycle(-1));
        const text = el(control, 'div', 'ct-enum-current');
        Paper.button(control, '›', false, () => cycle(1));
        controls.seg = {set: value => { text.textContent = (options.find(option => option.value === value) || {}).label || value; }};
        controls.seg.set(valueOf(row.path));
      } else {
        controls.seg = Paper.segmented(control, {options, value: valueOf(row.path), onChange: value => choose(row.path, value)});
      }
    } else {
      numberControls(control, item, item.row.path, controls);
    }
    controls.reset = Paper.resetButton(control, () => {
      sound('Select');
      if (item.kind === 'condition') {
        engine.send('reset', {path: `${item.prefix}.Compare`});
        if (rowsByPath.has(`${item.prefix}.Threshold`)) engine.send('reset', {path: `${item.prefix}.Threshold`});
      } else {
        engine.send('reset', {path: item.row.path});
      }
    });
    view.root.addEventListener('mouseenter', () => setHover({type: 'item', item}));
    view.root.addEventListener('mouseleave', () => setHover(null));
    view.label.addEventListener('click', () => {
      sound('Focus');
      const before = ui.pinned && model.items.get(ui.pinned);
      ui.pinned = ui.pinned === item.id ? null : item.id;
      if (before && before.view) updateOption(before);
      updateOption(item);
      hoverIntent.cancel();
      refreshDescription();
      sendState();
    });
    item.view = {view, controls};
  };

  const numberControls = (control, item, path, controls) => {
    const range = rangeOf(item);
    const digits = digitsOf(item);
    const row = rowsByPath.get(path) || {};
    controls.slider = Paper.slider(control, {
      lo: range.lo, hi: range.hi, value: numberOf(path), def: row.def, step: range.step, hard: range.hard || {},
      onInput: value => {
        ui.dragging = item;
        setValue(path, value);
      },
      onCommit: () => {
        // 拖着时不认悬停：松开后讲的还是这一项（指针多半还在这一行上，挪开时 mouseleave 会改回去）
        sound('Select');
        ui.dragging = null;
        ui.hover = {type: 'item', item};
        hoverIntent.cancel();
        markDirty();
      },
    });
    controls.number = Paper.numberField(control, {value: numberOf(path), format: v => fmt(v, digits), onCommit: value => choose(path, clampTo(value, range.hard, row.kind))});
    el(control, 'div', 'pp-unit', unitOf(item));
    controls.path = path;
  };

  const clampTo = (value, hard, kind) => {
    let v = value;
    if (hard && hard.lo !== undefined) v = Math.max(v, hard.lo);
    if (hard && hard.hi !== undefined) v = Math.min(v, hard.hi);
    return kind === 'int' ? Math.round(v) : v;
  };

  /** 玩家选了一个值：真的变了才响；拖滑块时走 setValue，松开才响 */
  const choose = (path, value) => {
    if (valueOf(path) !== value) sound('Select');
    setValue(path, value);
  };

  /** 改一个值：先在页面上改（选项与描述区跟上），再交给游戏；游戏回 "values" 时以它为准 */
  const setValue = (path, value) => {
    values.set(path, value);
    engine.send('set', {path, value});
    valuesChanged([path]);
  };

  /**
   * 值变了：这些路径所在的类别马上更新（开关、混合方式会让同组的别的项不起作用，所以整个类别一起），描述区记一笔、下一轮补。
   * 设置器在值没变时不碰 DOM，别的行不受影响
   */
  const valuesChanged = paths => {
    ++valuesVersion;
    if (!model) return;
    const categories = new Set();
    for (const path of paths) {
      const item = model.itemsByPath.get(path);
      if (item) categories.add(item.category);
    }
    for (const category of categories) updateCategory(category);
    markDirty();
  };

  const updateOption = item => {
    if (!item.view) return;
    const {view, controls} = item.view;
    const info = item.info || {};
    const offReason = info.off ? info.off(context({type: 'item', item, groupPath: item.groupPath})) : '';
    view.setState({on: ui.pinned === item.id, changed: itemChanged(item), off: !!offReason});
    if (controls.slider) controls.slider.set(numberOf(controls.path));
    if (controls.number) controls.number.set(numberOf(controls.path));
    if (controls.toggle) controls.toggle.set(valueOf(item.row.path));
    if (controls.rays) controls.rays.set(valueOf(item.row.path));
    if (controls.seg) controls.seg.set(item.kind === 'condition' ? valueOf(`${item.prefix}.Compare`) : valueOf(item.row.path));
  };

  /** 一个类别：各选项、"改过几项"与起作用的小字 */
  const updateCategory = category => {
    if (!category.section) return;
    let changed = 0;
    for (const item of category.items) {
      updateOption(item);
      if (itemChanged(item)) ++changed;
    }
    category.section.setCount(changed ? `改过 ${changed} 项` : '');
    category.section.setNote(activationNote(category));
  };

  // ----- 描述区

  const UPDATE_INTERVAL = 100;   // ms：值变了以后描述区多久补一轮
  const DRAG_TEXT_INTERVAL = 300;  // ms：拖滑块时说明与公式多久补一轮（预览照样 100 ms）
  const PANEL_CACHE = 24;        // 留着的描述份数

  const hoverIntent = Paper.restIntent();  // 指针停住才换描述（主题里，设置界面也用）
  let dirty = false;
  let lastRender = -Infinity;
  let lastTextRender = -Infinity;
  const panels = new Map();      // 讲的东西的键 → 搭好的一份（按最近用过的顺序）
  let shownPanel = null;

  const markDirty = () => {
    dirty = true;
  };

  const setHover = target => {
    if (ui.dragging) return;
    hoverIntent.set(target);
  };

  /** 指针停在一行或一个目录上算焦点动了；类别的标题、离开不算 */
  const hoverKey = target => (!target ? '' : target.type === 'item' ? `i:${target.item.id}` : target.type === 'directory' ? `d:${target.id}` : '');

  /** 描述区讲哪一个：拖着的 → 悬停的 → 点过的 → 这个目录 */
  const resolveTarget = () => {
    const entry = model.directories.find(d => d.dir.id === ui.dir) || model.directories[0];
    if (ui.dragging) return {type: 'item', item: ui.dragging, groupPath: ui.dragging.groupPath};
    if (ui.hover && ui.hover.type === 'item') return {type: 'item', item: ui.hover.item, groupPath: ui.hover.item.groupPath};
    if (ui.hover && ui.hover.type === 'category') return {type: 'category', category: ui.hover.category, groupPath: ui.hover.category.groupPath};
    if (ui.hover && ui.hover.type === 'directory') {
      const hovered = model.directories.find(d => d.dir.id === ui.hover.id);
      if (hovered) return {type: 'directory', dir: hovered.dir, groupPath: ''};
    }
    if (ui.pinned && model.items.has(ui.pinned)) {
      const item = model.items.get(ui.pinned);
      return {type: 'item', item, groupPath: item.groupPath};
    }
    return {type: 'directory', dir: entry.dir, groupPath: ''};
  };

  const targetKey = target => target.type === 'item' ? `i:${target.item.id}` : target.type === 'category' ? `c:${target.category.id}` : `d:${target.dir.id}`;

  /** 描述的内容：标题、英文、面包屑、说明、公式、预览的种类与标题、图 */
  const describe = target => {
    if (target.type === 'directory') {
      const d = target.dir.desc || {};
      return {title: target.dir.label, en: target.dir.en, crumb: '相机调参', text: d.text, tex: d.tex, preview: d.preview, figure: d.figure, image: target.dir.image};
    }
    const directory = Catalog.DIRECTORIES.find(d => d.id === (target.type === 'item' ? target.item.category.dirId : target.category.dirId));
    const category = target.type === 'item' ? target.item.category : target.category;
    const element = category.element;
    const categoryDesc = element ? element : (category.fixed ? (category.fixed.text ? category.fixed : directory.desc) : null);
    const categoryPreview = element ? element.preview : category.fixed ? (category.fixed.preview !== undefined ? category.fixed.preview : (directory.desc ? directory.desc.preview : null)) : defaultPreview(category.group);
    const categoryImage = (element && element.image) || (category.fixed && category.fixed.image) || null;
    if (target.type === 'category') {
      return {
        title: category.title, en: category.en, crumb: directory.label,
        text: categoryDesc ? categoryDesc.text : (() => [category.group.typeName ? `${category.group.typeName}：${category.group.element || ''}` : '']),
        tex: categoryDesc ? categoryDesc.tex : null, preview: categoryPreview, figure: categoryDesc ? categoryDesc.figure : '', image: categoryImage || directory.image,
      };
    }
    const item = target.item;
    const info = item.info || {};
    const groupPreview = element && element.itemPreview ? element.itemPreview : categoryPreview;
    const preview = info.preview === 'byGroup' ? groupPreview : info.preview !== undefined ? info.preview : groupPreview;
    const tooltip = item.row && item.row.tooltip ? [item.row.tooltip.replace(/\s+/g, ' ')] : [];
    return {
      title: labelOf(item), en: (item.lastField || '').replace(/^b(?=[A-Z])/, '').replace(/([a-z])([A-Z])/g, '$1 $2').toUpperCase(),
      crumb: `${directory.label} › ${category.title}`, text: info.text || (() => tooltip), tex: info.tex,
      preview, figure: info.figure || (categoryDesc && categoryDesc.figure) || '', image: info.image || categoryImage || directory.image,
    };
  };

  const defaultPreview = group => {
    if (!group) return null;
    if (group.type === 'GinkgoCameraImpulseShake') return 'impulse';
    if (group.type === 'GinkgoCameraNoiseSway') return 'noise';
    if (group.property === 'Modifiers') return 'timeline';
    if (group.property === 'ParamSprings') return 'spring';
    return null;
  };

  const imageClass = key => Catalog.IMAGES[key] || Catalog.IMAGES[Catalog.ROOT_IMAGE];

  /** 一项的标签：当前、默认、范围、路径、不起作用的原因 */
  const chipList = (target, c) => {
    if (target.type !== 'item') return [];
    const item = target.item;
    const chips = [];
    const unit = unitOf(item);
    const show = v => item.kind === 'rays' ? `${Array.isArray(v) ? v.length : 0} 条射线` : typeof v === 'number' ? `${fmt(v, digitsOf(item))}${unit ? ` ${unit}` : ''}` : typeof v === 'boolean' ? (v ? '开' : '关') : enumName(item, v);
    if (item.kind === 'condition') {
      const cond = conditionInfo(item.groupPath, item.conditionIndex);
      if (cond) {
        chips.push({key: '比较', text: `${cond.scalarName} ${cond.atLeast ? '≥' : '<'}`});
        chips.push({key: '阈值', text: cond.thresholdScalar ? `${cond.thresholdName}（${fmt(cond.threshold, 1)}）` : show(cond.threshold)});
      }
    } else {
      chips.push({key: '当前', text: show(itemValue(item))});
      chips.push({key: '默认', text: show(itemDefault(item))});
    }
    const row = item.row || rowsByPath.get(`${item.prefix}.Threshold`) || {};
    if (row.clampmin !== undefined || row.clampmax !== undefined) {
      chips.push({key: '范围', text: row.clampmin !== undefined && row.clampmax !== undefined ? `${fmt(row.clampmin)} – ${fmt(row.clampmax)}`
        : row.clampmin !== undefined ? `≥ ${fmt(row.clampmin)}` : `≤ ${fmt(row.clampmax)}`});
    }
    chips.push({key: '路径', text: item.kind === 'condition' ? item.prefix : item.row.path, mono: true});
    const info = item.info || {};
    const off = info.off ? info.off(c) : '';
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
    el(node, 'span', chip.mono ? 'pp-mono' : 'pp-num', chip.text);
  };

  const enumName = (item, v) => {
    const row = item.row || rowsByPath.get(`${item.prefix}.Compare`) || {};
    const index = (row.options || []).indexOf(v);
    return index >= 0 && row.optionNames ? row.optionNames[index] : String(v);
  };

  /** 按位置比较：同一位置的源文字没变就留着原来的节点，变了的换掉，多的删、少的补 */
  const patchList = (host, entries, sources, make) => {
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
    }
    while (entries.length > sources.length) host.removeChild(entries.pop().node);
  };

  /** 讲这个东西的那一份：有就拿出来（挪到最近用过），没有就搭个架子（面包屑、标题；说明、公式、预览在后面补） */
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
      attached: 0,  // 重的部分（heavyParts）从头数接进去了几块
      version: {text: -1, formulas: -1, preview: -1},
    };
    panels.set(key, panel);
    for (const [oldKey, old] of panels) {
      if (panels.size <= PANEL_CACHE) break;
      if (old !== shownPanel && old !== panel) panels.delete(oldKey);
    }
    return panel;
  };

  /** 说明：标签与段落（这一帧搭，比公式、预览轻） */
  const renderText = panel => {
    const c = context(panel.target);
    if (panel.chipsHost) {
      const chips = chipList(panel.target, c);
      const signature = JSON.stringify(chips);
      if (signature !== panel.chips) {
        clear(panel.chipsHost);
        for (const chip of chips) buildChip(panel.chipsHost, chip);
        panel.chips = signature;
      }
    }
    let paragraphs = [];
    try {
      paragraphs = panel.info.text ? panel.info.text(c) : [];
    } catch (error) {
      paragraphs = [`（说明出错：${error && error.message}）`];
    }
    patchList(panel.textHost, panel.paragraphs, paragraphs.filter(Boolean).map(String), source => Paper.rich(null, source));
    panel.version.text = valuesVersion;
  };

  /** 同 patchList，但一次只换一处；都对上了返回 true */
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

  /** 行间公式：一次排一条（KaTeX 的一条公式几十上百个节点，weft 接进来要几毫秒到十几毫秒），没变的不重排；还有没排的返回 'more' */
  const renderFormulas = panel => {
    if (panel.version.formulas === valuesVersion) return false;
    let formulas = [];
    try {
      formulas = panel.info.tex ? panel.info.tex(context(panel.target)) : [];
    } catch (error) {
      formulas = [];
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

  /** 预览：第一次（或换了种类）搭，之后值变了 update */
  const renderPreview = panel => {
    if (panel.version.preview === valuesVersion) return false;
    const c = context(panel.target);
    const kind = panel.info.preview || null;
    if (kind !== panel.previewKind || (kind && !panel.preview)) {
      clear(panel.figureHost);
      panel.figure = null;
      panel.preview = null;
      panel.previewKind = kind;
      if (kind) {
        const figure = el(panel.figureHost, 'div', 'pp-figure');
        panel.figure = figure;
        el(figure, 'div', 'pp-figure-title', panel.info.figure || '预览（按当前的值）');
        const canvas = el(figure, 'div', 'pp-figure-canvas');
        try {
          panel.preview = CameraPreviews.create(kind, canvas, c);
        } catch (error) {
          el(canvas, 'div', 'pp-p', `（预览出错：${error && error.message}）`);
        }
      }
    } else if (panel.preview) {
      try {
        panel.preview.update(c);
      } catch (error) {
        // 画不出时保留上一帧
      }
    }
    panel.attached = heavyParts(panel).length;
    panel.version.preview = valuesVersion;
    return true;
  };

  /**
   * 一份描述里重的部分，按文档里的顺序：公式一条一块、预览一块（weft 接一条 KaTeX 公式约 15–20 ms，整份一起接回来 30–40 ms）。
   * 换走时拿下来（这一份已经不在文档里，拆开不要钱），回来时一帧一块地接；公式、预览只在全接上以后才重排（排在接的后面）
   */
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

  /** 换到该讲的那一份（接回去），说明这一帧补，公式与预览排到后面；previewOnly：只补预览（拖滑块时说明与公式慢一点补） */
  const refreshDescription = (previewOnly = false) => {
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
      previewOnly = false;
      const image = `pp-desc-image ${imageClass(panel.info.image)}`;
      if (imageLayer.className !== image) imageLayer.className = image;
    }
    if (!previewOnly) {
      if (panel.version.text !== valuesVersion) renderText(panel);
      if (panel.version.formulas !== valuesVersion) descJobs.push(formulasJob(panel));
    }
    if (panel.version.preview !== valuesVersion) descJobs.push(() => shownPanel === panel && renderPreview(panel));
    busyFrame = true;
  };

  // ----- 每帧：悬停到时换描述；值变了的补一轮最多每 100 ms 一次；不然做一件排着的活；预览的播放头与小画面

  const loop = time => {
    const t = now();
    if (busyFrame) {
      busyFrame = false;
    } else if (hoverIntent.due(t)) {
      const before = hoverKey(ui.hover);
      ui.hover = hoverIntent.take();
      if (hoverKey(ui.hover) && hoverKey(ui.hover) !== before) sound('Focus');
      refreshDescription();
      busyFrame = false;
    } else if (dirty && t - lastRender >= UPDATE_INTERVAL && model) {
      dirty = false;
      lastRender = t;
      const textToo = !ui.dragging || t - lastTextRender >= DRAG_TEXT_INTERVAL;
      if (textToo) lastTextRender = t;
      refreshDescription(!textToo);
      busyFrame = false;
    } else {
      runJobs();
    }
    const preview = shownPanel && shownPanel.preview;
    if (preview && preview.frame) {
      try {
        preview.frame(time / 1000);
      } catch (error) {
        shownPanel.preview = null;
      }
    }
    requestAnimationFrame(loop);
  };

  // ===== 事件 =====

  const build = data => {
    table = data;
    values.clear();
    rowsByPath.clear();
    groups.clear();
    for (const group of data.groups || []) groups.set(group.path, group);
    for (const row of data.rows || []) {
      rowsByPath.set(row.path, row);
      values.set(row.path, row.value);
    }
    model = buildModel();
    ++valuesVersion;
    let saved = {};
    try {
      saved = data.state ? JSON.parse(data.state) : {};
    } catch (error) {
      saved = {};
    }
    // 换肩项已归入碰撞目录；页面热重载或重开时承接原来的目录和折叠状态。
    if (saved.dir === 'shoulder') saved.dir = 'collision';
    ui.collapsed = new Set((saved.collapsed || []).map(id => id.replace(/^shoulder\//, 'collision/')));
    ui.pinned = saved.pinned || null;
    // 整张表来了：搭好的选项栏与描述都作废
    if (activeView) {
      column.removeChild(activeView.head);
      column.removeChild(activeView.list);
    }
    dirViews.clear();
    activeView = null;
    panels.clear();
    shownPanel = null;
    clear(descScroll);
    revealJobs.length = 0;
    descJobs.length = 0;
    idleJobs.length = 0;
    clear(page);
    rail = Paper.rail(page, {
      emblem: {icon: 'leaf', text: '相机调参'},
      items: model.directories.map(d => ({id: d.dir.id, icon: d.dir.icon, label: d.dir.label})),
      foot: `${data.key || 'F7'} 关闭`,
      onSelect: id => {
        if (id !== ui.dir) sound('Select');
        selectDirectory(id);
      },
      onHover: id => setHover(id ? {type: 'directory', id} : null),
    });
    page.appendChild(column);
    page.appendChild(desc);
    if (!closeButton) closeButton = Paper.closeButton(desc, data.key || 'F7', () => engine.send('close'));
    const startDir = model.directories.some(d => d.dir.id === saved.dir) ? saved.dir : model.directories[0].dir.id;
    summary = {changed: data.changed, unsaved: data.unsaved, status: data.status};
    showSummary();
    ui.dir = null;
    selectDirectory(startDir);
    // 打开以后先让第一屏的公式、预览与淡入走完，再空闲地搭别的目录
    idleAfter = now() + 600;
    queuePrebuild();
  };

  const apply = data => {
    const paths = [];
    for (const value of data.rows || []) {
      const row = rowsByPath.get(value.path);
      if (row) {
        row.changed = value.changed;
        row.unsaved = value.unsaved;
      }
      // 正在拖的那一项以页面为准（游戏回来的可能是上一下的值）
      if (value.value !== undefined && !(ui.dragging && Paper.isDragging())) values.set(value.path, value.value);
      paths.push(value.path);
    }
    summary = {...summary, changed: data.changed, unsaved: data.unsaved, status: data.status || summary.status};
    showSummary();
    valuesChanged(paths);
  };

  engine.on('tuning', build);
  engine.on('values', apply);
  requestAnimationFrame(loop);
  engine.send('ready');
})();
