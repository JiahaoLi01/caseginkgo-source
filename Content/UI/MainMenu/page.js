// 开屏双镜动画与实时 3D 画面主菜单（UI/GinkgoMainMenu）：
//   - 开屏阶段：极简纯黑黑白灰双镜缓慢推镜（Shot 1 Unreal Engine 5；Shot 2 Weft UI Framework & MetaSound 同屏）
//     + 黑幕渐散揭幕进入实时 3D 游戏场景；每一镜切入与揭幕均触发裁剪后的 opening.wav 立体声转场音效。
//   - 主菜单阶段：左侧暗黑云雾停靠「新游戏」「继续游戏」「设置」「特性画廊」「退出游戏」五项，
//     点击「新游戏」原地替换为难度列表；「特性画廊」提供 5 分类 20 项技术展板（示意图 + KaTeX 公式 + 关键代码预览器 + 设计结构）。
//
// 游戏发：
//   "title-state"：{phase: 'splash'|'menu', fromGamepad, hasCheckpoint, locationName, mapName, difficulty}
//   "title-return"：{}（从设置子页退回时刷新焦点并播放单次轻快回场过渡）
//   "title-skip-splash"：{}（调试或测试强制跳过开屏直达主菜单）
//   "title-entering-game"：{}（新游戏无缝入场时同步播放退场淡出）
// 页面发：
//   ready、sound、log、splashShot {shot: 1|2|3}、splashComplete、galleryState {open}、
//   newGame {difficulty}、continueGame、openSettings {fromGamepad}、quitGame。
(() => {
  const engine = weft.engine;
  if (globalThis.Paper && typeof Paper.useDarkTheme === 'function') {
    Paper.useDarkTheme();
  }
  const root = document.getElementById('root');
  const SVG_NS = 'http://www.w3.org/2000/svg';
  const now = () => performance.now();

  const GalleryData = globalThis.MainMenuGalleryData || {CATEGORIES: [], FEATURES: []};
  const GalleryViews = globalThis.MainMenuGalleryViews;

  const el = (parent, tag, cls, text) => {
    const node = document.createElement(tag);
    if (cls) node.className = cls;
    if (text !== undefined && text !== null) node.textContent = String(text);
    if (parent) parent.appendChild(node);
    return node;
  };

  const clear = node => {
    while (node.firstChild) node.removeChild(node.firstChild);
  };

  // Weft 的 SVG 不支持 viewBox 与独立 opacity 属性：所有几何均使用精确逻辑 px 与 rgba 颜色
  const svgEl = (parent, tag, attrs) => {
    const node = document.createElementNS(SVG_NS, tag);
    if (attrs) {
      for (const [k, v] of Object.entries(attrs)) {
        node.setAttribute(k, String(v));
      }
    }
    if (parent) parent.appendChild(node);
    return node;
  };

  // 将标准绝对路径（M/L/C/H/V/Z）按比例与偏移换算到目标逻辑 px 空间，用于高保真绘制官方引擎矢量标
  const scaleSvgPath = (d, s, ox, oy) => {
    const tokens = d.match(/[MLCHVZ]|-?\d*\.?\d+(?:e[-+]?\d+)?/gi) || [];
    const out = [];
    let i = 0;
    let cmd = '';
    const fmt = v => Number(v.toFixed(2));
    while (i < tokens.length) {
      const t = tokens[i];
      if (/^[MLCHVZ]$/i.test(t)) {
        cmd = t;
        out.push(cmd);
        i++;
        continue;
      }
      if (cmd === 'M' || cmd === 'L') {
        out.push(fmt(parseFloat(tokens[i]) * s + ox), fmt(parseFloat(tokens[i + 1]) * s + oy));
        i += 2;
      } else if (cmd === 'C') {
        for (let k = 0; k < 6; k += 2) {
          out.push(fmt(parseFloat(tokens[i + k]) * s + ox), fmt(parseFloat(tokens[i + k + 1]) * s + oy));
        }
        i += 6;
      } else if (cmd === 'H') {
        out.push(fmt(parseFloat(tokens[i]) * s + ox));
        i += 1;
      } else if (cmd === 'V') {
        out.push(fmt(parseFloat(tokens[i]) * s + oy));
        i += 1;
      } else {
        i += 1;
      }
    }
    return out.join(' ');
  };

  const sound = kind => engine.send('sound', kind);
  const clamp01 = v => (v < 0 ? 0 : v > 1 ? 1 : v);
  const easeOutCubic = t => {
    const u = 1 - clamp01(t);
    return 1 - u * u * u;
  };
  const easeInOutSine = t => 0.5 - 0.5 * Math.cos(Math.PI * clamp01(t));
  const lerp = (a, b, t) => a + (b - a) * t;

  // ===== 官方矢量路径源（UE 5.8 unreal-circle-thin.svg 与 metasound_plugin.svg） =====
  const UE_MARK_100 =
    'M80.222 57.162C79.3533 61.1265 75.6497 71.99 63.5095 77.8003L58.39 72.1126L49.7926 80.6795C44.8184 80.704 33.6612 78.7846 25.846 68.145C25.846 68.145 27.4783 68.5528 28.6345 68.5742C29.9886 68.5988 31.4601 68.1051 31.4601 65.8454V43.5543C31.6054 41.7023 29.8093 39.2003 26.8043 39.881C22.9678 40.7518 20 50.2447 20 50.2447C20.0124 38.8079 25.9851 30.9983 30.5327 27.1166C36.2117 22.2659 41.7393 20.6623 45.9777 20C41.7918 22.3671 39.4361 26.2305 39.4361 29.4653C39.4361 34.6655 42.6018 34.0492 43.5385 33.2826V63.3648C43.5385 63.3648 43.724 63.8493 44.163 64.4257C44.7937 65.2567 45.9499 66.2716 47.8233 66.2716C50.9952 66.2716 55.1068 62.6841 55.1068 62.6841V38.3265C55.1068 35.849 53.221 32.8564 51.3383 31.8293C51.3383 31.8293 54.8286 31.2068 57.5275 33.2612C58.1117 32.559 58.6589 31.9703 59.1381 31.5012C65.4138 25.3842 71.334 23.6518 76.2649 22.7718H76.2711C76.2711 22.7718 67.2965 29.7596 67.2965 39.1329C67.2965 46.1054 67.4789 63.1103 67.4789 63.1103C70.8054 66.3022 75.7517 61.6999 80.222 57.162Z';

  const METASOUND_WAVE_64 =
    'M64 36.8735C58.0722 37.4356 52.1239 37.7525 46.1756 37.9569C44.7141 33.5213 41.1983 22.6979 41.1983 22.6979L31.9999 49.8637L22.8016 22.7081C22.8016 22.7081 19.7355 32.3561 17.8038 38.0387C11.8556 37.8036 5.92782 37.4663 0 36.8735C5.40658 36.3829 16.2503 35.9333 16.2503 35.9333L22.6687 13.98L31.9897 41.1252L41.3107 13.9902L47.7087 35.8719C47.7087 35.8719 58.297 36.3421 63.9795 36.894L64 36.8735Z';

  // ===== 开屏纯黑白灰徽标 SVG 构造 =====

  const buildUe5Emblem = parent => {
    const svg = svgEl(parent, 'svg', {width: 124, height: 124});
    svgEl(svg, 'circle', {cx: 62, cy: 62, r: 58.5, fill: 'none', stroke: 'rgba(255, 255, 255, 0.92)', 'stroke-width': 2.0});
    svgEl(svg, 'path', {d: scaleSvgPath(UE_MARK_100, 1.20, 2, 2), fill: '#ffffff', 'fill-rule': 'evenodd'});
  };

  // 原创 Weft UI Framework 纯黑白灰徽标：精密视口框 + 经纬编织布局栅格 + 明暗界面组件块
  const buildWeftFrameworkEmblem = parent => {
    const svg = svgEl(parent, 'svg', {width: 112, height: 112});
    svgEl(svg, 'rect', {x: 13, y: 13, width: 86, height: 86, rx: 3, fill: 'none', stroke: 'rgba(255, 255, 255, 0.24)', 'stroke-width': 1.3});
    const corners = [
      'M8 24V8H24',
      'M88 8H104V24',
      'M104 88V104H88',
      'M24 104H8V88',
    ];
    corners.forEach(d => {
      svgEl(svg, 'path', {d, fill: 'none', stroke: '#ffffff', 'stroke-width': 2.1, 'stroke-linecap': 'square'});
    });
    svgEl(svg, 'line', {x1: 38, y1: 19, x2: 38, y2: 93, stroke: 'rgba(255, 255, 255, 0.36)', 'stroke-width': 1.5});
    svgEl(svg, 'line', {x1: 74, y1: 19, x2: 74, y2: 93, stroke: 'rgba(255, 255, 255, 0.36)', 'stroke-width': 1.5});
    svgEl(svg, 'path', {d: 'M19 38H34 M42 38H93', fill: 'none', stroke: 'rgba(255, 255, 255, 0.68)', 'stroke-width': 1.6});
    svgEl(svg, 'path', {d: 'M19 60H70 M78 60H93', fill: 'none', stroke: 'rgba(255, 255, 255, 0.90)', 'stroke-width': 1.8});
    svgEl(svg, 'path', {d: 'M19 80H34 M42 80H70 M78 80H93', fill: 'none', stroke: 'rgba(255, 255, 255, 0.50)', 'stroke-width': 1.5});
    svgEl(svg, 'rect', {x: 22, y: 23, width: 68, height: 10, rx: 1, fill: 'rgba(255, 255, 255, 0.16)', stroke: 'rgba(255, 255, 255, 0.56)', 'stroke-width': 1});
    svgEl(svg, 'rect', {x: 22, y: 43, width: 12, height: 31, rx: 1, fill: 'rgba(255, 255, 255, 0.22)', stroke: 'rgba(255, 255, 255, 0.62)', 'stroke-width': 1});
    svgEl(svg, 'rect', {x: 44, y: 43, width: 26, height: 13, rx: 1, fill: 'rgba(255, 255, 255, 0.94)'});
    svgEl(svg, 'rect', {x: 44, y: 66, width: 46, height: 10, rx: 1, fill: 'rgba(255, 255, 255, 0.14)', stroke: 'rgba(255, 255, 255, 0.44)', 'stroke-width': 1});
    svgEl(svg, 'path', {d: 'M74 34L78 38L74 42L70 38Z', fill: '#ffffff'});
    svgEl(svg, 'path', {d: 'M38 56L42 60L38 64L34 60Z', fill: '#cfcfcf'});
  };

  // MetaSound 纯黑白灰徽标：官方谐振环 + 锐利 M 波形
  const buildMetaSoundEmblem = parent => {
    const svg = svgEl(parent, 'svg', {width: 112, height: 112});
    svgEl(svg, 'circle', {cx: 56, cy: 56, r: 47.5, fill: 'none', stroke: 'rgba(255, 255, 255, 0.62)', 'stroke-width': 1.9});
    svgEl(svg, 'path', {d: scaleSvgPath(METASOUND_WAVE_64, 1.62, 4.16, 5.6), fill: '#ffffff'});
  };

  // 主菜单银杏叶 + 战术菱形微标
  const buildGinkgoCrest = parent => {
    const svg = svgEl(parent, 'svg', {width: 34, height: 34});
    svgEl(svg, 'path', {d: 'M17 2L32 17L17 32L2 17Z', fill: 'rgba(229, 180, 67, 0.12)', stroke: 'rgba(229, 180, 67, 0.78)', 'stroke-width': 1.2});
    svgEl(svg, 'path', {
      d: 'M17 25V17 M17 18C12 17 8.5 13 9.5 9.5C12.5 8.5 15.6 10.5 17 14C18.4 10.5 21.5 8.5 24.5 9.5C25.5 13 22 17 17 18Z',
      fill: '#e5b443',
      stroke: '#fffbf4',
      'stroke-width': 0.9,
      'stroke-linejoin': 'round',
    });
  };

  // ===== 状态机 =====

  const CATEGORIES = GalleryData.CATEGORIES;
  const FEATURES = GalleryData.FEATURES;

  const state = {
    phase: 'splash', // 'splash' | 'menu'
    fromGamepad: false,
    device: 'keyboard',
    hasCheckpoint: false,
    locationName: '',
    mapName: '',
    focusedId: 'newGame',
    menuMode: 'main', // 'main' | 'difficulty'
    galleryOpen: false,
    galleryCategory: CATEGORIES.length ? CATEGORIES[0].id : 'movement',
    galleryItemId: FEATURES.length ? FEATURES[0].id : '',
    snippetIdx: 0,
    selectedDifficulty: 'balanced',
    modal: null,
    enteringGame: false,
  };

  // 新游戏难度档位定义（与 C++ EGinkgoDifficulty 及 Ginkgo::Difficulty::GetPlayerIncomingDamageScale 保持一致）
  const DIFFICULTIES = [
    {
      id: 'casual',
      label: '娱乐',
      enabled: true,
      danger: false,
      tag: '承受伤害 -90%',
      tagCls: '',
      caption: '适用于仅想探索游戏的玩家，该难度几乎不提供任何挑战。玩家承受伤害降低 90%。',
    },
    {
      id: 'balanced',
      label: '平衡',
      enabled: true,
      danger: false,
      tag: '推荐 · 承受伤害 -50%',
      tagCls: 'mm-item-tag-gold',
      caption: '推荐难度，提供娱乐性与挑战性的平衡。玩家承受伤害降低 50%。',
    },
    {
      id: 'hell',
      label: '地狱',
      enabled: true,
      danger: true,
      tag: '无减伤',
      tagCls: 'mm-item-tag-danger',
      caption: '极具挑战性的难度，适用于拥有丰富战斗技巧与理念的大师玩家。不提供任何减伤。',
    },
  ];

  // 开屏时序参数（毫秒）：两镜采用统一克制的极慢平滑推镜缩放，无弹跳与侧滑重坠感
  const SPLASH = {
    SHOT1_START: 0,
    SHOT1_END: 4350,
    SHOT2_START: 4600,
    SHOT2_END: 9350,
    REVEAL_START: 9350,
    REVEAL_END: 10550,
    ZOOM_FROM: 0.986,
    ZOOM_TO: 1.016,
  };

  const splash = {
    active: false,
    startedAt: 0,
    shot1Sent: false,
    shot2Sent: false,
    revealSent: false,
    finished: false,
  };

  const ITEM_STRIDE = 56; // 50px 行高 + 6px 下间距

  const getCategoryFeatures = catId => FEATURES.filter(f => f.category === catId);

  const getCurrentFeature = () => {
    const list = getCategoryFeatures(state.galleryCategory);
    const hit = list.find(f => f.id === state.galleryItemId);
    if (hit) return hit;
    if (list.length) {
      state.galleryItemId = list[0].id;
      return list[0];
    }
    return FEATURES[0] || null;
  };

  const getMenuItems = () => {
    if (state.menuMode === 'difficulty') {
      return [
        ...DIFFICULTIES,
        {
          id: 'back',
          label: '返回',
          enabled: true,
          danger: false,
          isReturn: true,
          tag: '',
          tagCls: '',
          caption: '返回主菜单。',
        },
      ];
    }
    return [
      {
        id: 'newGame',
        label: '新游戏',
        enabled: true,
        danger: false,
        tag: '',
        tagCls: '',
        caption: state.hasCheckpoint
          ? '从起点重新踏入「银杏方案」，开始一段全新的旅程。原有存档将被覆盖。'
          : '选择战斗难度，踏入罗马街巷，正式开启「银杏方案」。',
      },
      {
        id: 'continueGame',
        label: '继续游戏',
        enabled: state.hasCheckpoint,
        danger: false,
        tag: state.hasCheckpoint ? (state.locationName || '上次进度') : '无存档',
        tagCls: state.hasCheckpoint ? 'mm-item-tag-gold' : '',
        caption: state.hasCheckpoint
          ? (state.locationName
              ? `回到「${state.locationName}」，从最近一次自动存档处继续行动。`
              : '从最近一次自动存档处继续你的旅程。')
          : '尚无冒险存档，请从「新游戏」开始你的第一次行动。',
      },
      {
        id: 'settings',
        label: '设置',
        enabled: true,
        danger: false,
        tag: '',
        tagCls: '',
        caption: '调整操控灵敏度、按键布局、辅助瞄准与画面表现，按你的习惯定制手感。',
      },
      {
        id: 'gallery',
        label: '特性画廊',
        enabled: true,
        danger: false,
        tag: `${CATEGORIES.length} 分类 · ${FEATURES.length} 项`,
        tagCls: 'mm-item-tag-gold',
        caption: '深入「银杏方案」幕后：浏览运动、动画、相机与战斗等核心系统的设计图解与源码手记。',
      },
      {
        id: 'quit',
        label: '退出游戏',
        enabled: true,
        danger: true,
        tag: '',
        tagCls: '',
        caption: '结束本次游玩并退出至桌面。',
      },
    ];
  };

  // ===== 构建 DOM 骨架 =====

  const cloudEl = el(root, 'div', 'mm-cloud');

  // Layer 1：实时 3D 场景主菜单
  const menuLayer = el(root, 'div', 'mm-menu-layer mm-menu-hidden');
  const dock = el(menuLayer, 'div', 'mm-dock');

  const header = el(dock, 'div', 'mm-header');
  const brandRow = el(header, 'div', 'mm-brand-row');
  const crestBox = el(brandRow, 'div', 'mm-crest');
  buildGinkgoCrest(crestBox);
  el(brandRow, 'div', 'mm-eyebrow', 'CASE FILE // ROME');
  el(header, 'div', 'mm-title', 'CASE GINKGO');
  const cnRow = el(header, 'div', 'mm-cn-row');
  el(cnRow, 'div', 'mm-cn-gem');
  el(cnRow, 'div', 'mm-cn-title', '银杏方案');
  el(header, 'div', 'mm-hairline');
  const statusEl = el(header, 'div', 'mm-status', '');

  const center = el(dock, 'div', 'mm-center');
  const diamond = el(center, 'div', 'mm-diamond');
  el(diamond, 'div', 'mm-diamond-outer');
  el(diamond, 'div', 'mm-diamond-core');
  const listEl = el(center, 'div', 'mm-list');
  const captionEl = el(center, 'div', 'mm-caption', '');

  const footerEl = el(dock, 'div', 'mm-footer');

  const right = el(menuLayer, 'div', 'mm-right');
  const nowPlaying = el(right, 'div', 'mm-now-playing');
  el(nowPlaying, 'div', 'mm-np-dot');
  el(nowPlaying, 'div', '', "BROTHER'S KEEPER");
  el(right, 'div', 'mm-tech-line', 'UNREAL ENGINE 5 · WEFT UI · METASOUND');

  // Layer 2：分类技术特性画廊（Feature Gallery）
  const galleryEl = el(root, 'div', 'mm-gallery');
  const galHeader = el(galleryEl, 'div', 'mm-gal-header');
  const galHeadLeft = el(galHeader, 'div', 'mm-gal-head-left');
  const galEyeRow = el(galHeadLeft, 'div', 'mm-gal-eyebrow-row');
  el(galEyeRow, 'div', 'mm-cn-gem');
  el(galEyeRow, 'div', 'mm-gal-eyebrow', 'TECHNICAL GALLERY // CASE GINKGO');
  el(galHeadLeft, 'div', 'mm-gal-heading', '特性画廊');

  const galCatBar = el(galHeader, 'div', 'mm-gal-cats');

  const galHeadRight = el(galHeader, 'div', 'mm-gal-head-right');
  const galCounterEl = el(galHeadRight, 'div', 'mm-gal-counter', '01 / 05');
  const galBackBtn = el(galHeadRight, 'div', 'mm-gal-back', '返回主菜单  ESC');

  const galBody = el(galleryEl, 'div', 'mm-gal-body');
  const galSide = el(galBody, 'div', 'mm-gal-side');
  const galNavEl = el(galSide, 'div', 'mm-gal-nav');
  const galSideHints = el(galSide, 'div', 'mm-gal-side-hints');

  const galStageEl = el(galBody, 'div', 'mm-gal-stage');

  const galTopWrap = el(galStageEl, 'div', 'mm-gal-top');
  const galMetaRow = el(galTopWrap, 'div', 'mm-gal-meta-row');
  const galBadgeEl = el(galMetaRow, 'div', 'mm-gal-badge', '');
  const galSubcodeEl = el(galMetaRow, 'div', 'mm-gal-subcode', '');
  const galTitleEl = el(galTopWrap, 'div', 'mm-gal-title', '');
  const galDescHost = el(galTopWrap, 'div', 'mm-gal-desc-host');

  // 中部双栏技术展区：左栏（矢量示意图 + KaTeX 公式），右栏（关键代码预览器 + 设计结构）
  const galWork = el(galStageEl, 'div', 'mm-gal-work');
  const galLeftCol = el(galWork, 'div', 'mm-gal-col-l');
  const galDiagramBox = el(galLeftCol, 'div', 'mm-gal-diagram-box');
  const galFormulaBox = el(galLeftCol, 'div', 'mm-gal-formula-box');

  const galRightCol = el(galWork, 'div', 'mm-gal-col-r');
  const galCodeBox = el(galRightCol, 'div', 'mm-gal-code-box');
  const galStructBox = el(galRightCol, 'div', 'mm-gal-struct-box');

  const galFoot = el(galStageEl, 'div', 'mm-gal-foot');
  const galTagsEl = el(galFoot, 'div', 'mm-gal-tags');
  const galPager = el(galFoot, 'div', 'mm-gal-pager');
  const galPrevBtn = el(galPager, 'div', 'mm-gal-arrow', '← 上一项');
  const galNextBtn = el(galPager, 'div', 'mm-gal-arrow', '下一项 →');

  // Layer 3：提示条与二次确认弹窗（仅退出游戏使用）
  const noticeEl = el(root, 'div', 'mm-notice', '');

  const scrimEl = el(root, 'div', 'mm-modal-scrim');
  const modalCard = el(scrimEl, 'div', 'mm-modal');
  const modalHead = el(modalCard, 'div', 'mm-modal-head');
  const modalGem = el(modalHead, 'div', 'mm-modal-gem');
  const modalTitle = el(modalHead, 'div', 'mm-modal-title', '');
  el(modalCard, 'div', 'mm-modal-rule');
  const modalBody = el(modalCard, 'div', 'mm-modal-body', '');
  const modalActions = el(modalCard, 'div', 'mm-modal-actions');
  const modalCancelBtn = el(modalActions, 'div', 'mm-btn', '取消');
  const modalConfirmBtn = el(modalActions, 'div', 'mm-btn mm-btn-primary', '确认');

  // Layer 4：极简纯黑双镜开屏帷幕（无烟雾、无装饰线、无扫光、无说明性副文）
  const splashEl = el(root, 'div', 'mm-splash mm-splash-hidden');

  // 镜头 1：Unreal Engine 5
  const shot1El = el(splashEl, 'div', 'mm-shot');
  const ueEmblemBox = el(shot1El, 'div', 'mm-ue-emblem');
  buildUe5Emblem(ueEmblemBox);
  const ueWordmark = el(shot1El, 'div', 'mm-ue-wordmark');
  el(ueWordmark, 'div', 'mm-ue-text', 'UNREAL ENGINE');
  el(ueWordmark, 'div', 'mm-ue-five', '5');

  // 镜头 2：Weft UI Framework 与 MetaSound 同屏
  const shot2El = el(splashEl, 'div', 'mm-shot');
  const coRow = el(shot2El, 'div', 'mm-co-row');

  const coLeft = el(coRow, 'div', 'mm-co-col');
  const weftEmblemBox = el(coLeft, 'div', 'mm-co-emblem');
  buildWeftFrameworkEmblem(weftEmblemBox);
  el(coLeft, 'div', 'mm-co-name', 'WEFT');
  el(coLeft, 'div', 'mm-co-role', 'UI FRAMEWORK');

  const coRight = el(coRow, 'div', 'mm-co-col');
  const msEmblemBox = el(coRight, 'div', 'mm-co-emblem');
  buildMetaSoundEmblem(msEmblemBox);
  el(coRight, 'div', 'mm-co-name', 'METASOUND');
  el(coRight, 'div', 'mm-co-role', 'AUDIO ENGINE');

  const rowNodes = new Map();
  let noticeUntil = 0;
  let shakeRow = null;
  let shakeUntil = 0;
  let modalCloseUntil = 0;
  let menuSwapUntil = 0;
  let galleryCloseUntil = 0;
  let stageSwapUntil = 0;
  let animUntil = 0;

  // ===== 通用界面渲染 =====

  const showNotice = text => {
    noticeEl.textContent = text;
    noticeEl.className = 'mm-notice mm-notice-show';
    noticeUntil = now() + 2400;
  };

  const renderGalleryHints = () => {
    clear(galSideHints);
    const pairs = state.device === 'gamepad'
      ? [['LB / RB', '切换分类'], ['↑ ↓', '切换条目'], ['Y', '切换代码片段'], ['B', '返回主菜单']]
      : [['Q / E', '切换分类'], ['↑ ↓', '切换条目'], ['TAB', '切换代码片段'], ['ESC', '返回主菜单']];
    pairs.forEach(([k, v]) => {
      const row = el(galSideHints, 'div', 'mm-gal-hint-row');
      el(row, 'div', 'mm-gal-hint-key', k);
      el(row, 'div', 'mm-gal-hint-val', v);
    });
  };

  const renderFooter = () => {
    clear(footerEl);
    const addHint = (capCls, capText, label) => {
      const h = el(footerEl, 'div', 'mm-hint');
      el(h, 'div', capCls, capText);
      el(h, 'div', '', label);
    };
    if (state.device === 'gamepad') {
      addHint('mm-cap-pad', 'A', '选择');
      if (state.menuMode === 'difficulty') {
        addHint('mm-cap-pad', 'B', '返回');
      }
      galBackBtn.textContent = '返回主菜单  B';
    } else {
      addHint('mm-cap-kb', 'ENTER', '选择');
      if (state.menuMode === 'difficulty') {
        addHint('mm-cap-kb', 'ESC', '返回');
      } else {
        addHint('mm-cap-kb', '↑↓', '导航');
      }
      galBackBtn.textContent = '返回主菜单  ESC';
    }
    renderGalleryHints();
  };

  const renderModal = () => {
    if (!state.modal) {
      if (modalCloseUntil <= 0 && scrimEl.className !== 'mm-modal-scrim') {
        scrimEl.className = 'mm-modal-scrim';
      }
      return;
    }
    modalCloseUntil = 0;
    const m = state.modal;
    modalTitle.textContent = m.title;
    modalBody.textContent = m.body;
    modalConfirmBtn.textContent = m.confirmLabel;
    modalGem.className = m.danger ? 'mm-modal-gem mm-modal-gem-danger' : 'mm-modal-gem';
    const cancelFocus = m.focus === 'cancel' && state.device !== 'mouse';
    const confirmFocus = m.focus === 'confirm' && state.device !== 'mouse';
    modalCancelBtn.className = `mm-btn${cancelFocus ? ' mm-btn-focus' : ''}`;
    modalConfirmBtn.className = `mm-btn ${m.danger ? 'mm-btn-danger' : 'mm-btn-primary'}${confirmFocus ? ' mm-btn-focus' : ''}`;
    if (scrimEl.className !== 'mm-modal-scrim mm-modal-open') {
      scrimEl.className = 'mm-modal-scrim mm-modal-open';
    }
  };

  const renderMenu = () => {
    const items = getMenuItems();
    if (state.menuMode === 'difficulty') {
      statusEl.textContent = state.hasCheckpoint
        ? '选择难度 · 开启新篇章将覆盖当前存档'
        : '选择适合你的战斗难度';
    } else {
      statusEl.textContent = state.hasCheckpoint
        ? (state.locationName
            ? `最近进度 · ${state.locationName}`
            : '上次进度已就绪')
        : '整装待发 · 罗马';
    }

    let focusIdx = items.findIndex(item => item.id === state.focusedId);
    if (focusIdx < 0) {
      focusIdx = 0;
      state.focusedId = items[0].id;
    }

    clear(listEl);
    rowNodes.clear();

    items.forEach(item => {
      const focused = item.id === state.focusedId;
      const cls = 'mm-item'
        + (focused ? ' mm-item-focused' : '')
        + (!item.enabled ? ' mm-item-disabled' : '')
        + (item.danger ? ' mm-item-danger' : '')
        + (item.isReturn ? ' mm-item-return' : '');
      const row = el(listEl, 'div', cls);
      el(row, 'div', 'mm-item-glow');
      el(row, 'div', 'mm-item-label', item.label);
      if (item.tag) {
        el(row, 'div', `mm-item-tag${item.tagCls ? ' ' + item.tagCls : ''}`, item.tag);
      }

      row.addEventListener('mouseenter', () => {
        if (state.enteringGame || state.modal || state.galleryOpen || splash.active) return;
        if (state.focusedId !== item.id) {
          state.focusedId = item.id;
          sound('Focus');
          renderMenu();
        }
      });
      row.addEventListener('click', () => {
        if (state.enteringGame || state.modal || state.galleryOpen || splash.active) return;
        state.focusedId = item.id;
        triggerMenuItem(item);
      });
      rowNodes.set(item.id, row);
    });

    const active = items[focusIdx];
    const offsetY = focusIdx * ITEM_STRIDE + (active.isReturn ? 10 : 0);
    diamond.style.transform = `translateY(${offsetY}px)`;
    diamond.className = 'mm-diamond'
      + (!active.enabled ? ' mm-diamond-muted' : active.danger ? ' mm-diamond-danger' : '');
    captionEl.textContent = active.caption;

    renderFooter();
    renderModal();
  };

  // ===== 分类技术特性画廊 =====

  const pad2 = n => (n < 10 ? `0${n}` : `${n}`);

  const selectGalleryCategory = (catId, silent) => {
    if (state.galleryCategory === catId) return;
    state.galleryCategory = catId;
    const list = getCategoryFeatures(catId);
    state.galleryItemId = list.length ? list[0].id : '';
    state.snippetIdx = 0;
    if (!silent) sound('Select');
    renderGallery(true);
  };

  const stepGalleryCategory = delta => {
    if (!CATEGORIES.length) return;
    const cur = CATEGORIES.findIndex(c => c.id === state.galleryCategory);
    const next = (cur + delta + CATEGORIES.length) % CATEGORIES.length;
    selectGalleryCategory(CATEGORIES[next].id, false);
  };

  const stepGalleryItemInCategory = delta => {
    const list = getCategoryFeatures(state.galleryCategory);
    if (!list.length) return;
    const idx = list.findIndex(f => f.id === state.galleryItemId);
    const next = (idx + delta + list.length) % list.length;
    if (list[next].id !== state.galleryItemId) {
      state.galleryItemId = list[next].id;
      state.snippetIdx = 0;
      sound('Focus');
      renderGallery(true);
    }
  };

  // 左右翻页支持跨分类连续浏览所有 20 项
  const stepGalleryGlobal = delta => {
    if (!FEATURES.length) return;
    const idx = FEATURES.findIndex(f => f.id === state.galleryItemId);
    const next = (idx + delta + FEATURES.length) % FEATURES.length;
    const feat = FEATURES[next];
    state.galleryCategory = feat.category;
    state.galleryItemId = feat.id;
    state.snippetIdx = 0;
    sound('Focus');
    renderGallery(true);
  };

  const cycleCodeSnippet = () => {
    const feat = getCurrentFeature();
    if (!feat || !feat.snippets || feat.snippets.length <= 1) return;
    state.snippetIdx = (state.snippetIdx + 1) % feat.snippets.length;
    sound('Focus');
    renderGalleryCode(feat);
  };

  const renderGalleryCode = feat => {
    GalleryViews.renderCodePreviewer(galCodeBox, feat.snippets, state.snippetIdx, idx => {
      if (state.snippetIdx === idx) return;
      state.snippetIdx = idx;
      sound('Focus');
      renderGalleryCode(feat);
    });
  };

  const renderGallery = triggerSwapAnim => {
    const feat = getCurrentFeature();
    if (!feat) return;
    const catList = getCategoryFeatures(state.galleryCategory);
    const catPos = Math.max(0, catList.findIndex(f => f.id === feat.id));
    const globalPos = Math.max(0, FEATURES.findIndex(f => f.id === feat.id));
    galCounterEl.textContent = `${pad2(globalPos + 1)} / ${pad2(FEATURES.length)}`;

    // 顶部 5 分类标签条
    clear(galCatBar);
    CATEGORIES.forEach(cat => {
      const active = cat.id === state.galleryCategory;
      const count = getCategoryFeatures(cat.id).length;
      const pill = el(galCatBar, 'div', `mm-gal-cat${active ? ' mm-gal-cat-active' : ''}`);
      el(pill, 'div', 'mm-gal-cat-label', cat.label);
      el(pill, 'div', 'mm-gal-cat-count', pad2(count));
      pill.addEventListener('click', () => selectGalleryCategory(cat.id, false));
    });

    // 左侧当前分类条目索引
    clear(galNavEl);
    catList.forEach((f, idx) => {
      const active = f.id === feat.id;
      const tab = el(galNavEl, 'div', `mm-gal-tab${active ? ' mm-gal-tab-active' : ''}`);
      const top = el(tab, 'div', 'mm-gal-tab-top');
      el(top, 'div', 'mm-gal-tab-num', pad2(idx + 1));
      el(top, 'div', 'mm-gal-tab-code', f.badge);
      el(tab, 'div', 'mm-gal-tab-name', f.title);
      tab.addEventListener('click', () => {
        if (state.galleryItemId !== f.id) {
          state.galleryItemId = f.id;
          state.snippetIdx = 0;
          sound('Focus');
          renderGallery(true);
        }
      });
    });

    // 右侧技术展板头部
    galBadgeEl.textContent = feat.badge;
    galSubcodeEl.textContent = `${feat.code}  ·  ${pad2(catPos + 1)} / ${pad2(catList.length)}`;
    galTitleEl.textContent = feat.title;

    clear(galDescHost);
    if (globalThis.Paper && typeof Paper.rich === 'function') {
      Paper.rich(galDescHost, feat.desc, 'mm-gal-desc');
    } else {
      el(galDescHost, 'div', 'mm-gal-desc', feat.desc);
    }

    // 四大技术视图：矢量示意图 + KaTeX 公式 + 关键代码预览器 + 设计结构
    GalleryViews.renderDiagram(galDiagramBox, feat.diagram);
    GalleryViews.renderFormulas(galFormulaBox, feat.formulas);
    renderGalleryCode(feat);
    GalleryViews.renderStructure(galStructBox, feat.structure);

    // 底部关键词标签
    clear(galTagsEl);
    (feat.tags || []).forEach(tag => el(galTagsEl, 'div', 'mm-gal-tag', tag));

    if (triggerSwapAnim) {
      galStageEl.className = 'mm-gal-stage mm-gal-stage-swap';
      stageSwapUntil = now() + 210;
    }
  };

  const openGallery = () => {
    if (state.enteringGame || state.galleryOpen) return;
    sound('Open');
    galleryCloseUntil = 0;
    state.galleryOpen = true;
    animUntil = 0;
    root.className = '';
    cloudEl.className = 'mm-cloud mm-cloud-full';
    menuLayer.className = 'mm-menu-layer mm-menu-hidden';
    galleryEl.className = 'mm-gallery mm-gallery-open';
    engine.send('galleryState', {open: true});
    renderGalleryHints();
    renderGallery(false);
  };

  const closeGallery = silent => {
    if (!state.galleryOpen) return;
    state.galleryOpen = false;
    if (!silent) sound('Close');
    cloudEl.className = 'mm-cloud';
    menuLayer.className = 'mm-menu-layer';
    galleryEl.className = 'mm-gallery mm-gallery-closing';
    galleryCloseUntil = now() + 185;
    animUntil = now() + 250;
    root.className = 'mm-returning';
    engine.send('galleryState', {open: false});
    renderMenu();
  };

  galBackBtn.addEventListener('click', () => closeGallery(false));
  galPrevBtn.addEventListener('click', () => {
    if (state.galleryOpen) stepGalleryGlobal(-1);
  });
  galNextBtn.addEventListener('click', () => {
    if (state.galleryOpen) stepGalleryGlobal(1);
  });

  // ===== 二次确认弹窗（退出游戏） =====

  const openModal = spec => {
    if (state.enteringGame) return;
    sound('Select');
    modalCloseUntil = 0;
    state.modal = {...spec, focus: 'cancel'};
    renderModal();
  };

  const closeModal = silent => {
    if (!state.modal) return;
    state.modal = null;
    if (!silent) {
      sound('Deny');
      modalCloseUntil = now() + 135;
      scrimEl.className = 'mm-modal-scrim mm-modal-closing';
    } else {
      modalCloseUntil = 0;
      scrimEl.className = 'mm-modal-scrim';
    }
  };

  const confirmModal = () => {
    if (!state.modal || state.enteringGame) return;
    const onConfirm = state.modal.onConfirm;
    closeModal(true);
    if (onConfirm) onConfirm();
  };

  modalCancelBtn.addEventListener('click', () => closeModal(false));
  modalConfirmBtn.addEventListener('click', () => confirmModal());

  // ===== 左侧菜单原地切换：主菜单 ↔ 新游戏难度 =====

  const playCenterSwap = () => {
    center.className = 'mm-center mm-center-swap';
    menuSwapUntil = now() + 215;
  };

  const enterDifficultyMenu = () => {
    if (state.enteringGame || state.menuMode === 'difficulty') return;
    sound('Select');
    state.menuMode = 'difficulty';
    state.focusedId = DIFFICULTIES.some(d => d.id === state.selectedDifficulty)
      ? state.selectedDifficulty
      : 'balanced';
    playCenterSwap();
    renderMenu();
  };

  const leaveDifficultyMenu = silent => {
    if (state.menuMode !== 'difficulty') return;
    if (!silent) sound('Deny');
    state.menuMode = 'main';
    state.focusedId = 'newGame';
    playCenterSwap();
    renderMenu();
  };

  const startEnteringGameVisual = () => {
    if (state.enteringGame) return;
    state.enteringGame = true;
    animUntil = 0;
    root.className = 'mm-exiting';
  };

  const launchNewGameWithDifficulty = diffItem => {
    state.selectedDifficulty = diffItem.id;
    state.menuMode = 'main';
    startEnteringGameVisual();
    engine.send('newGame', {difficulty: diffItem.id});
  };

  const triggerMenuItem = item => {
    if (state.enteringGame) return;
    if (!item.enabled) {
      sound('Invalid');
      const row = rowNodes.get(item.id);
      if (row) {
        if (shakeRow && shakeRow !== row) {
          shakeRow.className = shakeRow.className.replace(' mm-item-shake', '');
        }
        row.className = row.className.replace(' mm-item-shake', '') + ' mm-item-shake';
        shakeRow = row;
        shakeUntil = now() + 260;
      }
      showNotice('尚无可供继续的游戏存档');
      return;
    }

    if (state.menuMode === 'difficulty') {
      if (item.id === 'back') {
        leaveDifficultyMenu(false);
      } else {
        launchNewGameWithDifficulty(item);
      }
      return;
    }

    if (item.id === 'newGame') {
      enterDifficultyMenu();
    } else if (item.id === 'continueGame') {
      startEnteringGameVisual();
      engine.send('continueGame');
    } else if (item.id === 'settings') {
      animUntil = 0;
      root.className = '';
      sound('Select');
      engine.send('openSettings', {fromGamepad: state.device === 'gamepad'});
    } else if (item.id === 'gallery') {
      openGallery();
    } else if (item.id === 'quit') {
      openModal({
        title: '退出游戏',
        body: '确定要退出「银杏方案」并返回桌面吗？',
        confirmLabel: '退出游戏',
        danger: true,
        onConfirm: () => engine.send('quitGame'),
      });
    }
  };

  // ===== 极简缓慢推镜开屏时间轴 =====

  const startSplashSequence = () => {
    state.phase = 'splash';
    splash.active = true;
    splash.startedAt = now();
    splash.shot1Sent = false;
    splash.shot2Sent = false;
    splash.revealSent = false;
    splash.finished = false;
    animUntil = 0;
    root.className = '';
    cloudEl.className = 'mm-cloud';
    menuLayer.className = 'mm-menu-layer mm-menu-hidden';
    splashEl.className = 'mm-splash';
    splashEl.style.opacity = '1';
    shot1El.style.opacity = '0';
    shot2El.style.opacity = '0';
  };

  const showMenuDirectly = withEnterAnim => {
    state.phase = 'menu';
    splash.active = false;
    splash.finished = true;
    splashEl.className = 'mm-splash mm-splash-hidden';
    menuLayer.className = 'mm-menu-layer';
    if (withEnterAnim) {
      animUntil = now() + 680;
      root.className = 'mm-entering';
    } else {
      animUntil = 0;
      root.className = '';
    }
    renderMenu();
  };

  const triggerRevealToMenu = () => {
    if (!splash.active) return;
    const elapsed = now() - splash.startedAt;
    if (elapsed < SPLASH.REVEAL_START) {
      splash.startedAt = now() - SPLASH.REVEAL_START;
    }
  };

  const advanceSplash = skipAll => {
    if (!splash.active) return;
    const elapsed = now() - splash.startedAt;
    if (skipAll || elapsed >= SPLASH.SHOT2_START - 120) {
      triggerRevealToMenu();
    } else {
      splash.startedAt = now() - SPLASH.SHOT2_START;
    }
  };

  splashEl.addEventListener('click', () => {
    if (splash.active) advanceSplash(false);
  });

  const updateSplashFrame = tNow => {
    if (!splash.active) return;
    const t = tNow - splash.startedAt;

    // ----- Shot 1：Unreal Engine 5（缓慢线性推镜缩放，无上下拖拽） -----
    if (t >= SPLASH.SHOT1_START && t <= SPLASH.SHOT1_END) {
      if (!splash.shot1Sent) {
        splash.shot1Sent = true;
        engine.send('splashShot', {shot: 1});
      }
      const lt = t - SPLASH.SHOT1_START;
      const dur = SPLASH.SHOT1_END - SPLASH.SHOT1_START;
      const p = clamp01(lt / dur);
      const fadeIn = easeOutCubic(clamp01(lt / 950));
      const fadeOut = 1 - easeInOutSine(clamp01((lt - (dur - 860)) / 860));
      const alpha = fadeIn * fadeOut;
      const scale = lerp(SPLASH.ZOOM_FROM, SPLASH.ZOOM_TO, p);

      shot1El.style.opacity = alpha.toFixed(3);
      shot1El.style.transform = `scale(${scale.toFixed(4)})`;
    } else {
      shot1El.style.opacity = '0';
    }

    // ----- Shot 2：Weft UI Framework 与 MetaSound 同屏（与第 1 镜一致的缓慢推镜缩放，移除 ping-pong） -----
    if (t >= SPLASH.SHOT2_START && t <= SPLASH.SHOT2_END) {
      if (!splash.shot2Sent) {
        splash.shot2Sent = true;
        engine.send('splashShot', {shot: 2});
      }
      const lt = t - SPLASH.SHOT2_START;
      const dur = SPLASH.SHOT2_END - SPLASH.SHOT2_START;
      const p = clamp01(lt / dur);
      const fadeIn = easeOutCubic(clamp01(lt / 980));
      const fadeOut = 1 - easeInOutSine(clamp01((lt - (dur - 880)) / 880));
      const alpha = fadeIn * fadeOut;
      const scale = lerp(SPLASH.ZOOM_FROM, SPLASH.ZOOM_TO, p);

      shot2El.style.opacity = alpha.toFixed(3);
      shot2El.style.transform = `scale(${scale.toFixed(4)})`;
    } else {
      shot2El.style.opacity = '0';
    }

    // ----- Shot 3 / 纯黑幕散开揭幕 → 实时 3D 游戏主菜单 -----
    if (t >= SPLASH.REVEAL_START) {
      if (!splash.revealSent) {
        splash.revealSent = true;
        state.phase = 'menu';
        menuLayer.className = 'mm-menu-layer';
        animUntil = now() + 760;
        root.className = 'mm-entering';
        renderMenu();
        engine.send('splashShot', {shot: 3});
      }
      const rp = clamp01((t - SPLASH.REVEAL_START) / (SPLASH.REVEAL_END - SPLASH.REVEAL_START));
      const veilAlpha = 1 - easeInOutSine(rp);
      splashEl.style.opacity = veilAlpha.toFixed(3);

      if (rp >= 1) {
        splash.active = false;
        splash.finished = true;
        splashEl.className = 'mm-splash mm-splash-hidden';
        engine.send('splashComplete');
      }
    }
  };

  // ===== 输入：键盘 + 鼠标 + 手柄 =====

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

  const setDevice = dev => {
    if (state.device === dev) return;
    state.device = dev;
    renderFooter();
    renderModal();
  };

  let lastPointerX = -1;
  let lastPointerY = -1;
  document.addEventListener('mousemove', e => {
    if (lastPointerX >= 0 && (Math.abs(e.clientX - lastPointerX) > 2 || Math.abs(e.clientY - lastPointerY) > 2)) {
      setDevice('mouse');
    }
    lastPointerX = e.clientX;
    lastPointerY = e.clientY;
  });

  document.addEventListener('keydown', e => {
    const k = e.key;
    const isNav = [
      'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight',
      'w', 'W', 's', 'S', 'a', 'A', 'd', 'D',
      'q', 'Q', 'e', 'E', 'PageUp', 'PageDown', 'Tab',
      'Enter', ' ', 'Escape',
    ].includes(k);
    if (!isNav) return;
    e.preventDefault();
    if (state.enteringGame) return;
    setDevice(padActive() ? 'gamepad' : 'keyboard');

    // 开屏期按键控制：Esc 直接进主菜单，Space/Enter 切下一镜
    if (splash.active) {
      if (e.repeat) return;
      if (k === 'Escape') advanceSplash(true);
      else if (k === 'Enter' || k === ' ' || k === 'ArrowRight') advanceSplash(false);
      return;
    }

    // 二次确认弹窗
    if (state.modal) {
      if (k === 'Escape' && !e.repeat) {
        closeModal(false);
      } else if (k === 'ArrowLeft' || k === 'a' || k === 'A') {
        if (state.modal.focus !== 'cancel') {
          state.modal.focus = 'cancel';
          sound('Focus');
          renderModal();
        }
      } else if (k === 'ArrowRight' || k === 'd' || k === 'D') {
        if (state.modal.focus !== 'confirm') {
          state.modal.focus = 'confirm';
          sound('Focus');
          renderModal();
        }
      } else if ((k === 'Enter' || k === ' ') && !e.repeat) {
        if (state.modal.focus === 'confirm') confirmModal();
        else closeModal(false);
      }
      return;
    }

    // 分类特性画廊导航：Q/E/PgUp/PgDn 切分类，↑↓ 切本类条目，←→ 全览翻页，Tab 切代码片段
    if (state.galleryOpen) {
      if (k === 'Escape' && !e.repeat) {
        closeGallery(false);
      } else if (k === 'q' || k === 'Q' || k === 'PageUp') {
        stepGalleryCategory(-1);
      } else if (k === 'e' || k === 'E' || k === 'PageDown') {
        stepGalleryCategory(1);
      } else if (k === 'ArrowDown' || k === 's' || k === 'S') {
        stepGalleryItemInCategory(1);
      } else if (k === 'ArrowUp' || k === 'w' || k === 'W') {
        stepGalleryItemInCategory(-1);
      } else if (k === 'ArrowRight' || k === 'd' || k === 'D') {
        stepGalleryGlobal(1);
      } else if (k === 'ArrowLeft' || k === 'a' || k === 'A') {
        stepGalleryGlobal(-1);
      } else if (k === 'Tab' && !e.repeat) {
        cycleCodeSnippet();
      }
      return;
    }

    // 左侧主菜单 / 难度选项导航
    if (k === 'Escape' && !e.repeat && state.menuMode === 'difficulty') {
      leaveDifficultyMenu(false);
      return;
    }
    const items = getMenuItems();
    const idx = items.findIndex(item => item.id === state.focusedId);
    if (k === 'ArrowDown' || k === 's' || k === 'S') {
      state.focusedId = items[(idx + 1) % items.length].id;
      sound('Focus');
      renderMenu();
    } else if (k === 'ArrowUp' || k === 'w' || k === 'W') {
      state.focusedId = items[(idx - 1 + items.length) % items.length].id;
      sound('Focus');
      renderMenu();
    } else if ((k === 'Enter' || k === ' ') && !e.repeat) {
      triggerMenuItem(items[idx]);
    }
  });

  // ===== 主动画循环与手柄监测 =====

  let padHeld = null;
  const loop = () => {
    const t = now();
    updateSplashFrame(t);

    if (noticeUntil > 0 && t >= noticeUntil) {
      noticeUntil = 0;
      noticeEl.className = 'mm-notice';
    }
    if (shakeUntil > 0 && t >= shakeUntil) {
      shakeUntil = 0;
      if (shakeRow) {
        shakeRow.className = shakeRow.className.replace(' mm-item-shake', '');
        shakeRow = null;
      }
    }
    if (modalCloseUntil > 0 && t >= modalCloseUntil) {
      modalCloseUntil = 0;
      if (!state.modal) scrimEl.className = 'mm-modal-scrim';
    }
    if (menuSwapUntil > 0 && t >= menuSwapUntil) {
      menuSwapUntil = 0;
      center.className = 'mm-center';
    }
    if (galleryCloseUntil > 0 && t >= galleryCloseUntil) {
      galleryCloseUntil = 0;
      if (!state.galleryOpen) galleryEl.className = 'mm-gallery';
    }
    if (stageSwapUntil > 0 && t >= stageSwapUntil) {
      stageSwapUntil = 0;
      galStageEl.className = 'mm-gal-stage';
    }
    if (animUntil > 0 && t >= animUntil) {
      animUntil = 0;
      if (!state.enteringGame) root.className = '';
    }

    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    let pad = null;
    for (let i = 0; i < pads.length && !pad; ++i) pad = pads[i];
    if (pad) {
      const pressed = pad.buttons.map(b => b.pressed);
      if (padHeld === null) padHeld = pressed.slice();
      const justPressed = idx => !!pressed[idx] && !padHeld[idx];
      if (!state.enteringGame && pressed.some((p, i) => p && !padHeld[i])) {
        setDevice('gamepad');
      }
      if (state.galleryOpen && !state.enteringGame) {
        if (justPressed(4)) stepGalleryCategory(-1); // LB
        else if (justPressed(5)) stepGalleryCategory(1); // RB
        else if (justPressed(3)) cycleCodeSnippet(); // Y
      }
      padHeld = pressed;
    }

    requestAnimationFrame(loop);
  };

  // ===== 游戏事件 =====

  engine.on('title-state', data => {
    state.enteringGame = false;
    state.menuMode = 'main';
    state.fromGamepad = !!data.fromGamepad;
    state.device = data.fromGamepad ? 'gamepad' : 'keyboard';
    state.hasCheckpoint = !!data.hasCheckpoint;
    state.locationName = data.locationName || '';
    state.mapName = data.mapName || '';
    if (data.difficulty && DIFFICULTIES.some(d => d.id === data.difficulty)) {
      state.selectedDifficulty = data.difficulty;
    }
    if (!state.galleryOpen && !state.modal && (state.focusedId === 'newGame' || state.focusedId === 'continueGame')) {
      state.focusedId = state.hasCheckpoint ? 'continueGame' : 'newGame';
    }
    if (data.phase === 'splash') {
      startSplashSequence();
    } else {
      showMenuDirectly(true);
    }
  });

  engine.on('title-return', () => {
    state.enteringGame = false;
    state.focusedId = 'settings';
    animUntil = now() + 260;
    root.className = 'mm-returning';
    renderMenu();
  });

  engine.on('title-skip-splash', () => {
    showMenuDirectly(true);
  });

  engine.on('title-entering-game', startEnteringGameVisual);

  requestAnimationFrame(loop);
  engine.send('ready');
})();
