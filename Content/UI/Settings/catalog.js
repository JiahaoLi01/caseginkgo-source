// 设置界面的目录、说明与数学公式（Docs/Design/Control.md）：两个输入目录、各类别及配置项的显示名、单位、图标、原理解析、公式与动态预览。
// 打包版本不包含 UPROPERTY 反射元数据，因此显示文本统一维护于此；数值范围、默认值与存档键名定义在 C++ 的 GetSettingDefs
// （自动化测试 CaseGinkgo.Control.Input.Catalog 会校验所有定义项均已在此处登记）。
//
// 配置项结构：{label, en, icon, unit, scale, digits, text(c), tex(c), preview, figure, off(c), hide(c)}；键位项带 binding: true（每行含主、辅两个槽位）。
// text(c) 返回正文段落，支持 **加粗**、`代码`、$行内公式$ 与 [[实时计算值]]；tex(c) 返回独立公式卡片 [{tex, note}]；
// off(c) 返回当前未生效的原因（非空时置灰并在键盘/手柄导航中跳过）；hide(c) 为真时折叠隐藏该行。
(() => {
  const {fmt} = Paper;
  const M = () => SettingsMath;

  const OPTIONS = {
    linear: '线性', power: '指数', scurve: 'S 形',
    hold: '按住', toggle: '切换',
    off: '关闭', on: '开启', low: '低', medium: '中', high: '高', epic: '极高', cinematic: '电影级', custom: '自定义',
    windowed: '窗口', borderless: '无边框', fullscreen: '全屏', unlimited: '不限', autoScale: '引擎默认',
    quality: '质量', balanced: '均衡', performance: '性能',
  };

  // 按键显示名映射自引擎 EKeys，与键帽图标共用 Theme/keycap.js
  const KEYS = GinkgoKeycap.NAMES;

  Paper.registerIcons({
    monitor: [['p', 'M3 4h18v13H3zM12 17v4M7 21h10']],
    moveUp: [['p', 'M12 19V5M6 11l6-6 6 6']],
    moveDown: [['p', 'M12 5v14M6 13l6 6 6-6']],
    moveLeft: [['p', 'M19 12H5M11 6l-6 6 6 6']],
    moveRight: [['p', 'M5 12h14M13 6l6 6-6 6']],
    jump: [['p', 'M12 15V4M7 9l5-5 5 5'], ['p', 'M5 20h14']],
    interact: [['c', 12, 12, 3.5], ['p', 'M12 3v3.5M12 17.5V21M3 12h3.5M17.5 12H21']],
    slot1: [['p', 'M5 4h14v16H5z'], ['p', 'M10 9l2.5-1.5V16']],
    slot2: [['p', 'M5 4h14v16H5z'], ['p', 'M9.5 10a2.5 2.5 0 0 1 5 0c0 2-5 3.5-5 6h5']],
    sidearm: [['p', 'M4 7h15v4h-8l-1 3H7l1-3H4z'], ['p', 'M7 14l-1 5h3l1-5']],
    holster: [['p', 'M12 4v10M7 10l5 5 5-5'], ['p', 'M5 20h14']],
    swap: [['p', 'M5 8h12M13 4l4 4-4 4'], ['p', 'M19 16H7M11 12l-4 4 4 4']],
    drop: [['p', 'M4 6h13v3h-6l-1 3H7l1-3H4z'], ['p', 'M18 12v6M15 15.5l3 3 3-3'], ['p', 'M4 21h17']],
    fire: [['c', 12, 12, 7], ['c', 12, 12, 1.5], ['p', 'M12 2v5M12 17v5M2 12h5M17 12h5']],
    reload: [['p', 'M20 11a8 8 0 1 0-2.3 6.2'], ['p', 'M20 5v6h-6']],
    aimAssist: [['c', 15, 12, 4.5], ['p', 'M5 4v16'], ['p', 'M10.5 12H7M9 10l-2 2 2 2']],
    assistInner: [['p', 'M12 3.5v17'], ['p', 'M8.5 8v8M15.5 8v8']],
    assistOuter: [['p', 'M12 3.5v17'], ['p', 'M8.5 8v8M15.5 8v8'], ['pd', 'M4 5.5v13M20 5.5v13']],
    assistRange: [['c', 5, 12, 2], ['pd', 'M8 12h8'], ['p', 'M19.5 6v12'], ['p', 'M14.5 9.5L17 12l-2.5 2.5']],
    assistFollow: [['c', 7, 12, 3.5], ['p', 'M14 5v14'], ['p', 'M17 12h4M19.5 10l1.5 2-1.5 2']],
  });

  // ===== 辅助瞄准（半楞次径向阻尼 + 越肩视差雅可比修正） =====

  const ASSIST_FIGURE = '径向衰减曲线与 10 m 目标匀速扫掠对比';
  const assistDeg = (cm, meters) => fmt(M().assistAngle(cm, meters * 100), 1);
  const assistOff = dir => c => c.v(`${dir}.aimAssistHip`) <= 0 && c.v(`${dir}.aimAssistAds`) <= 0 ? '腰射与开镜强度均为 0' : '';
  const assistHidden = dir => c => !c.v(`${dir}.aimAssistEnabled`);

  const assistSwitch = (dir, deviceLabel) => ({
    label: '辅助瞄准', en: 'AIM ASSIST', icon: 'power', preview: 'aimAssist', figure: ASSIST_FIGURE,
    text: c => [
      c.v(`${dir}.aimAssistEnabled`)
        ? '已启用：**去留而近不拒**（半楞次速度阻尼）。将目标骨盆至头部的躯干中线投影至屏幕角空间，仅当准星相对中线向外远离时按径向分量做速度衰减；移向目标或沿躯干切向（如纵向压枪、追头）移动时完全不施加阻力。'
        : '已关闭：跳过目标筛选与角速度修正，下方细分参数随之折叠；重新开启后保留原有参数值。',
      `越肩相机偏离角色中心，转动视角时相机会绕跟随点位移并引入近距视差。系统每步通过有限差分构建 $2\\times 2$ 视差雅可比矩阵 $J$，在屏幕角空间求得修正量后经 $J^{-1}$ 映射回控制器视角，并以 $\\le 1/960\\text{ s}$ 的中点法子步积分保证帧率无关。本组参数仅对${deviceLabel}生效，按最近实际触发的输入设备自动切换。`,
    ],
    tex: () => [
      {tex: '\\mathbf{c}_{\\text{screen}} = -k(\\rho)\\,\\max(\\mathbf{w}\\cdot\\hat{\\mathbf{r}},\\ 0)\\,\\hat{\\mathbf{r}}, \\qquad \\boldsymbol{\\omega}_{\\text{corr}} = J^{-1}\\mathbf{c}_{\\text{screen}}',
        note: 'r̂：中线最近点指向准星的径向单位向量；w：准星相对中线的屏幕角速度；J：越肩视差雅可比矩阵'},
    ],
  });

  const assistStrength = (dir, ads) => {
    const id = `${dir}.aimAssist${ads ? 'Ads' : 'Hip'}`;
    return {
      label: ads ? '开镜强度' : '不开镜强度', en: ads ? 'ADS STRENGTH' : 'HIP STRENGTH', icon: ads ? 'scope' : 'aimAssist', unit: '%', scale: 100, digits: 0,
      preview: 'aimAssist', figure: ASSIST_FIGURE, hide: assistHidden(dir),
      text: c => {
        const k = c.v(id);
        return [
          `定义准星在满强度区内远离目标中线时，径向逃逸速度被抵消的比例 $k_0$。当前${ads ? '开镜到位' : '腰射状态'}下，向外脱离的速度保留输入值的 [[${fmt((1 - k) * 100, 0)}%]]${k > 0 ? `，同手速扫过满强度区时的驻留时间约为无辅助时的 [[${fmt(1 / (1 - k), 1)} 倍]]` : '（当前不产生阻尼）'}。`,
          ads
            ? '开镜过程中，基础强度按平滑后的开镜进度 $\\alpha \\in [0, 1]$ 在腰射强度与开镜强度之间线性过渡。强度上限限制为 95%，保证准星始终具备脱离与切换目标的余量。'
            : '仅在持枪且存在视角或移动输入时生效；准星朝目标靠近或沿躯干中线上下滑动时，径向脱离分量为零，操作手感不受干扰。',
        ];
      },
      tex: () => [
        {tex: 'w_{\\text{out}}\' = \\bigl(1 - k(\\rho)\\bigr)\\,w_{\\text{out}}, \\qquad k_0 = \\mathrm{lerp}(k_{\\text{hip}},\\ k_{\\text{ads}},\\ \\alpha)',
          note: 'w_out = max(w · r̂, 0) 为径向远离速度；k_0 ∈ [0, 0.95]'},
      ],
    };
  };

  const assistInner = dir => ({
    label: '满强度半径', en: 'FULL RADIUS', icon: 'assistInner', unit: 'cm', digits: 0, preview: 'aimAssist', figure: ASSIST_FIGURE, off: assistOff(dir), hide: assistHidden(dir),
    text: c => {
      const cm = c.v(`${dir}.aimAssistInner`);
      return [
        `以目标所在位置的世界空间宽度（cm）定义满强度区半径 $R_{\\text{in}}$：准星距目标躯干线段 $AB$ 的角距离不超过 $\\rho_{\\text{in}}$ 时保持全额强度 $k_0$。按距离折算为视角张角后，在 10 m 处对应 [[±${assistDeg(cm, 10)}°]]，在 30 m 处对应 [[±${assistDeg(cm, 30)}°]]。`,
        '采用世界空间半径而非固定屏幕像素半径，可使吸附窗口随目标远近自然遵循透视缩放（角色半身宽约 20 cm）：既避免远处吸附范围过大把准星卡在目标外围，也避免近身缠斗时窗口过窄难以触发。',
      ];
    },
    tex: () => [
      {tex: '\\rho_{\\text{in}} = \\arctan\\frac{R_{\\text{in}}}{D}', note: 'R_in：世界空间满强度半径（cm）；D：相机到目标的距离（cm）'},
    ],
  });

  const assistOuter = dir => ({
    label: '淡出半径', en: 'FADE RADIUS', icon: 'assistOuter', unit: 'cm', digits: 0, preview: 'aimAssist', figure: ASSIST_FIGURE, off: assistOff(dir), hide: assistHidden(dir),
    text: c => {
      const inner = c.v(`${dir}.aimAssistInner`), outer = c.v(`${dir}.aimAssistOuter`);
      return [
        `定义阻尼强度从满值平滑衰减至零的外边界半径 $R_{\\text{out}}$，同时作为目标候选筛选的角距离外限。在 10 m 距离上，有效外沿对应视角张角 [[±${assistDeg(Math.max(outer, inner), 10)}°]]。`,
        outer <= inner
          ? '当前淡出半径不大于满强度半径：衰减过渡带宽度为零，强度在满强度边界处呈阶跃截断。'
          : '在 $\\rho_{\\text{in}}$ 到 $\\rho_{\\text{out}}$ 的过渡带内，强度按三次 Hermite 多项式（Smoothstep）平滑降至 0，两端导数均为零，使准星进出吸附区边缘时不会出现阻尼突跳。',
      ];
    },
    tex: () => [
      {tex: 'k(\\rho) = k_0\\bigl(1 - 3t^2 + 2t^3\\bigr), \\qquad t = \\mathrm{clamp}\\!\\left(\\frac{\\rho - \\rho_{\\text{in}}}{\\rho_{\\text{out}} - \\rho_{\\text{in}}},\\ 0,\\ 1\\right)',
        note: 'ρ 为准星到目标躯干线段的球面角距离；ρ_out = arctan(R_out / D)'},
    ],
  });

  const assistRange = dir => ({
    label: '最远距离', en: 'MAX DISTANCE', icon: 'assistRange', unit: 'm', digits: 0, preview: 'aimAssist', figure: ASSIST_FIGURE, off: assistOff(dir), hide: assistHidden(dir),
    text: c => [
      `辅助瞄准的最大有效距离 $D_{\\max}$，超出 [[${fmt(c.v(`${dir}.aimAssistRange`), 0)} m]] 的目标不参与候选评估。`,
      '当视野内存在多名目标时，系统仅锁定唯一最优目标：须位于该距离以内、落在各自淡出半径张角内、通过可见性遮挡检测（掩体后目标不触发吸附），并在候选者中选取距躯干中线角距离最小的一名。',
    ],
    tex: () => [
      {tex: 'D \\le D_{\\max}, \\qquad \\rho < \\rho_{\\text{out}}(D)', note: '同时满足距离上限、淡出张角与视线无遮挡的目标中，取角距离 ρ 最小者'},
    ],
  });

  const assistFollow = (dir, deviceNote) => ({
    label: '跟随目标', en: 'FOLLOW TARGET', icon: 'assistFollow', unit: '%', scale: 100, digits: 0, preview: 'aimAssist', figure: ASSIST_FIGURE, off: assistOff(dir), hide: assistHidden(dir),
    text: c => {
      const follow = c.v(`${dir}.aimAssistFollow`), hip = c.v(`${dir}.aimAssistHip`);
      return [
        `控制目标横向移动（含玩家自身横移产生的相对视差）计入相对角速度的权重 $\\lambda \\in [0, 1]$。当目标向准星外侧移动导致准星相对落后时，系统会顺势牵引视角跟进；当前腰射满强度区内，准星对目标脱离速度的跟随比例为 [[${fmt(follow * hip * 100, 0)}%]]（$\\lambda \\times k_0$）。设为 0% 时仅阻尼玩家主动外推的输入，不产生自动跟转。`,
        deviceNote,
      ];
    },
    tex: () => [
      {tex: '\\mathbf{w} = J\\,\\boldsymbol{\\omega}_{\\text{in}} - \\lambda\\,\\mathbf{v}_{\\text{target}}',
        note: 'λ：跟随系数；v_target：由目标速度与玩家走位视差共同引起的屏幕角速度'},
    ],
  });

  // ===== 键位与按住/切换双槽状态机 =====

  const binding = (label, en, icon, text) => ({binding: true, label, en, icon, preview: 'keyList', figure: '当前目录键位一览', text});
  const holdBinding = (label, en, icon, action, rule) => ({
    binding: true, label, en, icon, preview: 'holdKeys', figure: '双槽位按住 / 切换合成时序',
    text: () => [
      `主键与辅助键可分别独立设定触发方式：**按住**在按键持续按下期间保持${action}，松开即结束；**切换**在每次按下时翻转锁存状态。两个槽位支持混搭（例如一个槽位设为按住、另一个设为切换），任一槽位处于有效态时动作即保持激活。`,
      rule,
    ],
    tex: () => [
      {tex: 'A(t) = \\max\\bigl(T(t),\\ H_1(t),\\ H_2(t)\\bigr)', note: 'T(t) ∈ {0, 1} 为切换模式的锁存位（受状态打断同步清零），H_1、H_2 ∈ {0, 1} 为按住模式的实时按键状态'},
    ],
  });

  const SPRINT_RULE = '切换模式下的状态打断：角色停止移动超过 0.25 s，或进入开镜状态时，冲刺锁存位会自动清零退出。';
  const CROUCH_RULE = '切换模式下的状态打断：起跳或发起冲刺会自动解除下蹲并恢复站姿；高速冲刺途中按下蹲则会直接转入滑铲。';
  const AIM_RULE = '切换模式下的状态打断：发起冲刺会自动退出开镜状态并同步清零切换锁存位。';

  const SLOT_RULE = '若当前手持其他武器，会先执行收枪再拔出目标槽位武器；目标槽位为空或已拿在手中时不重复响应。';
  const HOLSTER_RULE = [
    '在持枪与徒手状态之间切换：持枪时将当前武器收回背负，徒手时重新拔出上一次使用的武器。',
    '每把武器独立持久化自身的弹匣余量与机件阶段：拉栓或装填中途收枪，再次拔出后会从中断处继续完成；收放过渡期间再次按键仅翻转目标去向，不会打断当前动画引起抖动。',
  ];
  const DROP_RULE = [
    '将当前手持武器抛落至地面。武器本体完整保留膛内、枪栓与弹匣状态，后备弹药仍留在角色身上。',
    '准星对准地面武器按交互键即可拾取并直接装备上手；若手持其他武器会先将其收起，对应武器槽已满时则将原槽内武器替换掉落。',
  ];
  const SWITCH_RULE = '在两把主武器槽位之间快速轮换；若当前处于徒手状态，则直接拔出上一次手持的武器。';
  const FIRE_RULE = [
    '单次击发输入：若武器处于低姿戒备，会自动先抬枪到位并立即击发；冲刺途中按下则会先解除冲刺再进入射击。',
    '栓动武器在击发后自动衔接拉栓动作；拉栓期间再次按下开火会记录预输入（Input Buffer），待闭栓到位瞬间自动补发下一枪，弹仓为空时触发空仓击锤声。',
  ];
  const RELOAD_RULE = [
    '单发装填流程：打开枪机后逐发向弹仓压入子弹，直至弹仓压满或后备弹药耗尽后闭栓结束。',
    '装填中途支持开火打断：压弹期间按下开火，会在当前这一发子弹压入后立即提前闭栓并完成击发。',
  ];

  // ===== 开镜与倍镜换算说明 =====

  const adsText = (c, scaleId, matchId, what) => {
    const scale = c.v(scaleId);
    const match = c.v(matchId);
    const degrees = M().adsScale(1, scale, match, c.zoom);
    const screen = degrees * c.zoom;
    return [
      `越肩开镜（ADS）期间对${what}施加的缩放系数，随平滑后的开镜进度 $\\alpha \\in [0, 1]$ 连续线性插值，避免开镜与收镜途中灵敏度跳变。当前开镜到位时，同等输入下的视角转角为腰射的 [[${fmt(degrees, 2)} 倍]]，准星附近的屏幕像移速度为腰射的 [[${fmt(screen, 2)} 倍]]（当前武器视野放大 $z =$ ${fmt(c.zoom, 2)} 倍）。`,
    ];
  };

  const adsTex = (c, matchId) => [
    c.v(matchId)
      ? {tex: 'S(\\alpha) = 1 + \\left(\\frac{S_{\\text{ads}}}{z} - 1\\right)\\alpha', note: '已开启按视场换算：z 为越肩开镜的画面中心角放大率，α ∈ [0, 1] 为开镜进度'}
      : {tex: 'S(\\alpha) = 1 + \\bigl(S_{\\text{ads}} - 1\\bigr)\\,\\alpha', note: '未开启按视场换算：到位倍率为 S_ads，α ∈ [0, 1] 为开镜进度'},
  ];

  const matchText = what => () => [
    `越肩开镜将相机视场角从 $\\theta_0$ 收窄至 $\\theta_{\\text{ads}}$，等效焦距拉长 $z$ 倍，相同视角偏转在屏幕中心附近扫过的像素距离也随之放大 $z$ 倍，主观上会感到开镜后${what}变快。开启本项后，系统将开镜倍率除以 $z$ 进行反向补偿，使开镜前后相同输入在屏幕中心附近产生的像移距离保持一致（即 0% 屏幕距离匹配 / 焦距正切匹配）。`,
    '在此基础上，「开镜灵敏度」作为额外偏好系数继续乘在换算结果之上。默认关闭时不随视场收窄自动折减实际转角。',
  ];

  const matchTex = () => [
    {tex: 'z = \\frac{\\tan(\\theta_0 / 2)}{\\tan(\\theta_{\\text{ads}} / 2)}, \\qquad S^* = \\frac{S_{\\text{ads}}}{z}',
      note: 'θ_0：基础视场；θ_ads：开镜视场；当 S_ads = 1 时开镜前后的屏幕中心像移速度 1:1 一致'},
  ];

  const scopeText = (c, scaleId, what) => {
    const scale = c.v(scaleId);
    const zoom = c.scopeZoom;
    return [
      `装配光学倍镜的武器（如 98k 的 ZF39 瞄具）在开镜时平滑过渡至第一人称出瞳位，采用画中画光学成像：镜外周边视野保持原尺寸，仅镜片内部按光学倍率 $M =$ ${fmt(zoom, 0)} 独立放大。系统将${what}除以 $M$ 再乘以本项系数 $S_{\\text{scope}}$；当设为 \`1.00\` 时，镜内目标像在屏幕上的移动速度与腰射完全一致。当前到位时的实际转角为腰射的 [[${fmt(scale / zoom, 2)} 倍]]，镜内像移速度为腰射的 [[${fmt(scale, 2)} 倍]]。`,
      '第一人称光学倍镜仅受本项控制，与上方越肩开镜的「开镜灵敏度」「开镜按视场换算」相互独立。',
    ];
  };

  const scopeTex = () => [
    {tex: 'S_{\\text{scope}}(\\alpha) = 1 + \\left(\\frac{S_{\\text{scope}}}{M} - 1\\right)\\alpha',
      note: 'M 为瞄具光学放大倍率，α ∈ [0, 1] 为第一人称开镜进度；S_scope = 1 时镜内像移与腰射等速'},
  ];

  const ITEMS = {
    'video.quality': {label: '整体画质', en: 'QUALITY PRESET', icon: 'monitor',
      text: () => ['统一调整阴影、光照、反射、纹理和植被等画质。场景较重时，降低一档可以减轻渲染负担。']},
    'video.scale': {label: '渲染比例', en: 'RENDER SCALE', icon: 'monitor',
      text: () => ['按输出分辨率的比例渲染 3D 场景。降低比例可节省 GPU 时间，界面仍以输出分辨率绘制。']},
    'video.dlss': {label: 'DLSS 超分辨率', en: 'SUPER RESOLUTION', icon: 'monitor',
      text: () => ['以较低分辨率渲染，再重建输出图像。质量档保留较多细节，性能档的内部渲染分辨率更低。关闭后恢复手动渲染比例。']},
    'video.frames': {label: 'DLSS 帧生成', en: 'FRAME GENERATION', icon: 'monitor',
      text: () => ['在渲染帧之间生成额外画面。2× 表示每个渲染帧增加一帧。可选倍率取决于显卡、驱动与游戏窗口。切到后台时暂停插帧。']},
    'video.window': {label: '显示模式', en: 'WINDOW MODE', icon: 'monitor',
      text: () => ['选择全屏、无边框或窗口显示。无边框模式铺满当前桌面。']},
    'video.resolution': {label: '输出分辨率', en: 'RESOLUTION', icon: 'monitor',
      text: () => ['设置窗口或全屏游戏画面的宽和高，单位为像素。无边框模式自动使用桌面尺寸。']},
    'video.limit': {label: '帧率上限', en: 'FRAME RATE LIMIT', icon: 'monitor',
      text: () => ['限制游戏每秒渲染的帧数。选择能够稳定维持的上限，可以减少帧间隔波动。']},
    'video.vsync': {label: '垂直同步', en: 'V-SYNC', icon: 'monitor',
      text: () => ['与显示器刷新同步，减少画面撕裂。']},
    // ===== 控制器 =====
    'gamepad.yawSpeed': {
      label: '水平转速', en: 'YAW SPEED', icon: 'speedYaw', unit: '°/s', digits: 0, preview: 'lookCurve', figure: '摇杆偏转幅度 → 角速度响应',
      text: c => [
        `视角摇杆水平推至外圈满偏（$u = 1$）时的基础偏航角速度 $\\omega_{\\text{yaw}}$。按当前设定，不含转身加速时水平原地旋转 360° 需要 [[${fmt(360 / c.v('gamepad.yawSpeed'), 2)} 秒]]。`,
        '基础转速需要在中远距离精细跟枪与近身大幅度转身之间权衡：若希望兼顾两者，可将基础水平转速保持在便于微调的区间，再配合下方的「转身加速」在满偏时补充快速掉头能力。',
      ],
      tex: () => [
        {tex: '\\omega_{\\text{H}}(u) = \\omega_{\\text{yaw}}\\cdot f(u), \\qquad T_{360} = \\frac{360^\\circ}{\\omega_{\\text{yaw}}}',
          note: 'u ∈ [0, 1] 为经径向死区重映射后的推杆模长，f(u) 为响应曲线输出'},
      ],
    },
    'gamepad.pitchSpeed': {
      label: '垂直转速', en: 'PITCH SPEED', icon: 'speedPitch', unit: '°/s', digits: 0, preview: 'lookCurve', figure: '摇杆偏转幅度 → 角速度响应',
      text: () => [
        '视角摇杆纵向推至外圈满偏时的基础俯仰角速度 $\\omega_{\\text{pitch}}$。由于实战中纵向视角活动范围与目标高低差通常小于水平扫视，独立设置垂直转速有助于减少水平转火时混入多余的上下晃动。',
      ],
      tex: () => [
        {tex: '\\Delta\\theta = \\omega_{\\text{pitch}}\\cdot S(\\alpha)\\cdot f(u)\\,\\hat{s}_y\\,\\Delta t',
          note: 'ŝ_y = s_y / ‖s‖ 为保留原始推杆方向的纵向单位分量'},
      ],
    },
    'gamepad.adsScale': {
      label: '开镜灵敏度', en: 'ADS SENSITIVITY', icon: 'scope', unit: '×', digits: 2, preview: 'adsPad', figure: '越肩开镜转角与屏幕像移对比',
      text: c => adsText(c, 'gamepad.adsScale', 'gamepad.adsMatchFov', '摇杆角速度'),
      tex: c => adsTex(c, 'gamepad.adsMatchFov'),
    },
    'gamepad.adsMatchFov': {
      label: '开镜按视场换算', en: 'ADS FOV MATCH', icon: 'fovMatch', preview: 'adsPad', figure: '越肩开镜转角与屏幕像移对比',
      text: matchText('转速'),
      tex: matchTex,
    },
    'gamepad.scopeScale': {
      label: '倍镜灵敏度', en: 'SCOPE SENSITIVITY', icon: 'scope', unit: '×', digits: 2, preview: 'scopePad', figure: '光学倍镜转角与镜内像移对比',
      text: c => scopeText(c, 'gamepad.scopeScale', '摇杆角速度'),
      tex: scopeTex,
    },
    'gamepad.curve': {
      label: '响应曲线', en: 'RESPONSE CURVE', icon: 'curve', preview: 'lookCurve', figure: '摇杆偏转幅度 → 角速度响应',
      text: () => [
        '定义过死区后的归一化推杆幅度 $u \\in [0, 1]$ 到转速比例 $f(u) \\in [0, 1]$ 的非线性映射。映射仅作用于二维向量的模长，推杆方向角 $\\hat{\\mathbf{s}}$ 严格保持不变，不会引入轴向畸变。',
        '**线性**：输出与推杆幅度成正比，斜率恒定，响应最为直接。**指数**：幂函数映射，压低中心附近的初始斜率以放大微调行程，接近外圈时快速拉升至满速。**S 形**：关于 $(0.5, 0.5)$ 中心对称的双端缓和曲线（半偏仍对应半速），兼顾准星附近的细腻起步与接近外圈时的速度收敛。',
      ],
      tex: () => [
        {tex: 'f_{\\text{exp}}(u) = u^k, \\qquad f_{\\text{S}}(u) = \\frac{u^k}{u^k + (1 - u)^k}',
          note: 'u ∈ [0, 1] 为过死区后的归一化推杆幅度，k ≥ 1 为曲线强度；线性模式恒为 f(u) = u'},
      ],
    },
    'gamepad.curveStrength': {
      label: '曲线强度', en: 'CURVE STRENGTH', icon: 'curveBend', digits: 1, preview: 'lookCurve', figure: '摇杆偏转幅度 → 角速度响应',
      off: c => c.v('gamepad.curve') === 'linear' ? '当前响应曲线为线性' : '',
      text: c => {
        const half = M().curve(0.5, c.v('gamepad.curve'), c.v('gamepad.curveStrength'));
        const quarter = M().curve(0.25, c.v('gamepad.curve'), c.v('gamepad.curveStrength'));
        return [
          `控制指数或 S 形响应曲线的弯曲指数 $k$：$k = 1$ 时严格退化为线性映射；$k$ 越大，小偏转区间的斜率越平缓、准星微调分辨率越高，但中等行程的响应也会相应延后。当前参数下，过死区后推至 25% 行程输出 [[${fmt(quarter * 100, 0)}%]] 转速，推至 50% 行程输出 [[${fmt(half * 100, 0)}%]] 转速。`,
        ];
      },
      tex: c => [
        c.v('gamepad.curve') === 'scurve'
          ? {tex: 'f(u) = \\frac{u^k}{u^k + (1 - u)^k}, \\qquad f(0.5) = 0.5', note: `当前为 S 形曲线，k = ${fmt(c.v('gamepad.curveStrength'), 1)}`}
          : {tex: 'f(u) = u^k', note: `当前幂指数 k = ${fmt(c.v('gamepad.curveStrength'), 1)}；k = 1 时退化为线性`},
      ],
    },
    'gamepad.invertPitch': {
      label: '反转上下', en: 'INVERT PITCH', icon: 'invertY', preview: 'invertPad', figure: '摇杆输入与视角转向对应关系',
      text: () => ['翻转手柄视角摇杆的纵向俯仰输入符号：开启时向上推杆对应低头俯视、向下拉杆对应抬头仰视（模拟飞行操纵杆习惯）。仅作用于控制器摇杆，与鼠标垂直方向互不影响。'],
    },
    'gamepad.invertYaw': {
      label: '反转左右', en: 'INVERT YAW', icon: 'invertX', preview: 'invertPad', figure: '摇杆输入与视角转向对应关系',
      text: () => ['翻转手柄视角摇杆的水平偏航输入符号：开启时向右推杆对应视角向左偏转。'],
    },
    'gamepad.swapSticks': {
      label: '左右摇杆互换', en: 'SWAP STICKS', icon: 'swap', preview: 'swap', figure: '左右摇杆功能分配',
      text: () => ['交换左右两根物理摇杆的职责（左手布局 / Southpaw）：改由左摇杆控制视角旋转、右摇杆控制角色移动。视角与移动各自的死区、外圈及响应曲线均随功能职责一并迁移，无需重新调校。'],
    },
    'gamepad.lookInner': {
      label: '视角内死区', en: 'LOOK DEAD ZONE', icon: 'deadZone', unit: '%', scale: 100, digits: 0, preview: 'zoneLook', figure: '视角摇杆径向死区与重映射（连接手柄时实时显示推杆点）',
      text: c => [
        `采用**径向死区（Radial Dead Zone）**：以二维推杆模长 $r = \\|\\mathbf{s}\\|$ 判定死区而非分别裁剪单轴，避免斜向微调时产生贴合十字坐标轴的吸附失真。模长不超过 [[${fmt(c.v('gamepad.lookInner') * 100, 0)}%]] 时输出归零，超出部分从死区外沿平滑重映射至 $[0, 1]$，不在边界处产生阶跃。`,
        '内死区用于滤除摇杆弹簧回中误差引起的视角自发零漂：发生缓慢漂移时可适当调大，微调起步滞涩时则可调小。',
      ],
      tex: () => [
        {tex: 'u = \\mathrm{clamp}\\!\\left(\\frac{\\|\\mathbf{s}\\| - d_{\\text{in}}}{d_{\\text{out}} - d_{\\text{in}}},\\ 0,\\ 1\\right), \\qquad \\mathbf{s}\' = u\\,\\frac{\\mathbf{s}}{\\|\\mathbf{s}\\|}',
          note: '‖s‖ ≤ d_in 时 u = 0；方向单位向量 s / ‖s‖ 原样保留'},
      ],
    },
    'gamepad.lookOuter': {
      label: '视角外圈', en: 'LOOK OUTER', icon: 'outerRing', unit: '%', scale: 100, digits: 0, preview: 'zoneLook', figure: '视角摇杆径向死区与重映射（连接手柄时实时显示推杆点）',
      text: c => [
        `定义达到满偏输出（$u = 1$）所需的物理推杆半径比例 $d_{\\text{out}}$，当前为 [[${fmt(c.v('gamepad.lookOuter') * 100, 0)}%]]。调低外圈可补偿外壳限位挡圈的机械误差并更快触及满速（及触发转身加速），但会压缩有效推杆行程、降低中段微调精度。`,
      ],
      tex: () => [
        {tex: 'u = 1 \\quad (\\|\\mathbf{s}\\| \\ge d_{\\text{out}})', note: '推杆模长达到外圈阈值 d_out 即视为满偏输入'},
      ],
    },
    'gamepad.moveInner': {
      label: '移动内死区', en: 'MOVE DEAD ZONE', icon: 'walkZone', unit: '%', scale: 100, digits: 0, preview: 'zoneMove', figure: '移动摇杆径向死区与重映射（连接手柄时实时显示推杆点）',
      text: c => [
        `移动摇杆的径向内死区阈值：推杆模长小于 [[${fmt(c.v('gamepad.moveInner') * 100, 0)}%]] 时视为静止输入，用于消除松手后摇杆未完全回中导致的角色缓慢自走。`,
      ],
      tex: () => [
        {tex: 'u_{\\text{move}} = \\mathrm{clamp}\\!\\left(\\frac{\\|\\mathbf{s}\\| - d_{\\text{in}}}{d_{\\text{out}} - d_{\\text{in}}},\\ 0,\\ 1\\right)',
          note: '与视角摇杆共用同一套径向线性重映射模型'},
      ],
    },
    'gamepad.moveOuter': {
      label: '移动外圈', en: 'MOVE OUTER', icon: 'outerRing', unit: '%', scale: 100, digits: 0, preview: 'zoneMove', figure: '移动摇杆径向死区与重映射（连接手柄时实时显示推杆点）',
      text: c => [
        `移动摇杆达到全速奔跑所需的物理推杆半径比例，当前推至 [[${fmt(c.v('gamepad.moveOuter') * 100, 0)}%]] 即输出满幅移动向量。`,
      ],
    },
    'gamepad.extraYaw': {
      label: '转身加速 · 水平', en: 'EXTRA YAW', icon: 'turnBoost', unit: '°/s', digits: 0, preview: 'turn', figure: '满偏保持时的水平角速度时间曲线',
      text: () => [
        '当视角摇杆推至满偏（$u \\ge 0.98$）并持续保持时，在基础水平转速之上额外叠加的偏航角速度 $\\omega_{\\text{extra}}$；一旦推杆退回满偏阈值以内，加速计时立即清零。通过将日常瞄准与满偏掉头解耦，可在保留低速精细瞄准的同时获得快速的 180° 转身能力。',
        '该附加转速随开镜进度按 $(1 - \\alpha)$ 淡出，开镜到位时不叠加以维持瞄准稳定。每帧在时间区间 $[t_0, t_1]$ 上对折线权重函数 $w(t)$ 求解析定积分均值 $\\bar{w}$，确保任意帧率下按住相同时长转过的总角度完全一致。',
      ],
      tex: () => [
        {tex: '\\bar{w} = \\frac{1}{\\Delta t}\\int_{t_0}^{t_1} \\mathrm{clamp}\\!\\left(\\frac{t - t_d}{t_r},\\ 0,\\ 1\\right)dt, \\qquad \\omega = \\omega_0 + \\omega_{\\text{extra}}\\,\\bar{w}\\,(1 - \\alpha)',
          note: 't_d：触发延迟；t_r：爬升时间；对分段线性斜坡做帧内解析积分，严格消除帧率误差'},
      ],
    },
    'gamepad.extraPitch': {
      label: '转身加速 · 垂直', en: 'EXTRA PITCH', icon: 'turnBoost', unit: '°/s', digits: 0, preview: 'turn', figure: '满偏保持时的水平角速度时间曲线',
      text: () => ['视角摇杆推至满偏（径向模长 $u \\ge 0.98$）时额外叠加的垂直俯仰角速度，与水平转身加速共用同一套满偏计时器、延迟与爬升斜坡。通常保持为 0 即可满足纵向观察需求。'],
    },
    'gamepad.extraDelay': {
      label: '转身加速 · 延迟', en: 'BOOST DELAY', icon: 'clock', unit: 's', digits: 2, preview: 'turn', figure: '满偏保持时的水平角速度时间曲线',
      off: c => c.v('gamepad.extraYaw') <= 0 && c.v('gamepad.extraPitch') <= 0 ? '水平与垂直转身加速均为 0' : '',
      text: () => ['摇杆维持满偏状态后，等待多久（$t_d$）才开始引入转身加速。适当的延迟可防止快速跟枪短促触边时意外触发高速跳变。'],
      tex: () => [
        {tex: 'w(t) = 0 \\quad (0 \\le t \\le t_d)', note: '满偏保持时长未超过延迟 t_d 前，仅输出基础转速'},
      ],
    },
    'gamepad.extraRamp': {
      label: '转身加速 · 爬升', en: 'BOOST RAMP', icon: 'ramp', unit: 's', digits: 2, preview: 'turn', figure: '满偏保持时的水平角速度时间曲线',
      off: c => c.v('gamepad.extraYaw') <= 0 && c.v('gamepad.extraPitch') <= 0 ? '水平与垂直转身加速均为 0' : '',
      text: () => ['度过延迟期后，转身加速权重从 0 线性爬升至 100% 所经历的过渡时间 $t_r$。设为 0 时在越过延迟时刻立即阶跃至满额附加转速（帧内同样按跨越时刻精确拆分积分）。'],
      tex: () => [
        {tex: 'w(t) = \\mathrm{clamp}\\!\\left(\\frac{t - t_d}{t_r},\\ 0,\\ 1\\right)', note: 't_r > 0 时线性渐进爬升，过渡平滑易控停'},
      ],
    },
    'gamepad.aimAssistEnabled': assistSwitch('gamepad', '控制器'),
    'gamepad.aimAssistHip': assistStrength('gamepad', false),
    'gamepad.aimAssistAds': assistStrength('gamepad', true),
    'gamepad.aimAssistInner': assistInner('gamepad'),
    'gamepad.aimAssistOuter': assistOuter('gamepad'),
    'gamepad.aimAssistRange': assistRange('gamepad'),
    'gamepad.aimAssistFollow': assistFollow('gamepad', '控制器默认设为 100%（即完整的转动辅助）：仅在玩家推杆或走位时生效，双手离开手柄时视角绝不会自行转动。'),
    'gamepad.jump': binding('跳跃', 'JUMP', 'jump', () => ['触发角色起跳；下蹲切换态下起跳会自动解除下蹲并站起。']),
    'gamepad.crouch': holdBinding('下蹲', 'CROUCH', 'slide', '下蹲', CROUCH_RULE),
    'gamepad.sprint': holdBinding('冲刺', 'SPRINT', 'sprint', '冲刺', `${SPRINT_RULE}控制器默认采用切换模式，避免长时间按压左摇杆造成疲劳。`),
    'gamepad.aim': holdBinding('开镜', 'AIM', 'aim', '开镜', AIM_RULE),
    'gamepad.fire': binding('开火', 'FIRE', 'fire', () => FIRE_RULE),
    'gamepad.reload': binding('装填', 'RELOAD', 'reload', () => RELOAD_RULE),
    'gamepad.interact': binding('交互', 'INTERACT', 'interact', () => ['准星对准场景内可交互物体（如地面武器、补给）出现提示时，按下执行拾取或交互。']),
    'gamepad.holster': binding('收枪', 'HOLSTER', 'holster', () => HOLSTER_RULE),
    'gamepad.switchWeapon': binding('切枪', 'SWITCH WEAPON', 'swap', () => [SWITCH_RULE]),
    'gamepad.dropWeapon': binding('丢枪', 'DROP WEAPON', 'drop', () => DROP_RULE),
    'gamepad.slot1': binding('第一把', 'WEAPON 1', 'slot1', () => [`直接切换至一号主武器槽。${SLOT_RULE}`, '控制器默认未绑定，日常通过「切枪」键在槽位间轮换，可按需自定义绑定。']),
    'gamepad.slot2': binding('第二把', 'WEAPON 2', 'slot2', () => [`直接切换至二号主武器槽。${SLOT_RULE}`, '控制器默认未绑定，日常通过「切枪」键在槽位间轮换，可按需自定义绑定。']),
    'gamepad.sidearm': binding('手枪', 'SIDEARM', 'sidearm', () => [`直接拔出副武器槽手枪。${SLOT_RULE}`, '控制器默认未绑定，可按需自定义绑定。']),

    // ===== 鼠标与键盘 =====
    'mouse.sensitivity': {
      label: '灵敏度', en: 'SENSITIVITY', icon: 'sens', digits: 2, preview: 'mouseSens', figure: '不同 DPI 下水平旋转 360° 所需物理位移',
      text: c => [
        `基于硬件原始计数（Raw Input）的线性角度标度，绕过操作系统的指针曲线修正：鼠标传感器每上报 1 count 位移，视角水平偏转 $0.022^\\circ \\times s$。当前设定下，单次计数对应 [[${fmt(c.v('mouse.sensitivity') * 0.022, 3)}°]]，在 800 DPI 下水平旋转一周（360°）需要手部平移 [[${fmt(M().cm360(c.v('mouse.sensitivity'), 800), 1)} cm]]。`,
        '未开启鼠标加速度时，视角偏转量仅取决于手部移动的累计物理距离，与挥动快慢及帧率完全无关，有利于建立稳定的肌肉记忆。',
      ],
      tex: () => [
        {tex: '\\Delta\\psi = 0.022^\\circ \\cdot s \\cdot \\Delta c_x, \\qquad L_{360} = \\frac{360^\\circ}{0.022^\\circ \\cdot s \\cdot \\mathrm{DPI}} \\times 2.54\\text{ cm}',
          note: 's：灵敏度；Δc_x：水平原始计数（count）；1 英寸 = 2.54 cm'},
      ],
    },
    'mouse.pitchRatio': {
      label: '垂直比例', en: 'PITCH RATIO', icon: 'ratio', unit: '×', digits: 2, preview: 'mouseSens', figure: '不同 DPI 下水平旋转 360° 所需物理位移',
      text: c => [
        `垂直俯仰灵敏度相对于水平偏航灵敏度的缩放比例 $r_{\\text{pitch}}$。设为 \`1.00\` 时横纵等比；当前每上报 1 count 纵向位移，视角俯仰偏转 [[${fmt(c.v('mouse.sensitivity') * 0.022 * c.v('mouse.pitchRatio'), 3)}°]]。`,
      ],
      tex: () => [
        {tex: '\\Delta\\theta = 0.022^\\circ \\cdot s \\cdot r_{\\text{pitch}} \\cdot \\Delta c_y',
          note: 'r_pitch：垂直比例；Δc_y：纵向原始计数（count）'},
      ],
    },
    'mouse.adsScale': {
      label: '开镜灵敏度', en: 'ADS SENSITIVITY', icon: 'scope', unit: '×', digits: 2, preview: 'adsMouse', figure: '越肩开镜转角与屏幕像移对比',
      text: c => adsText(c, 'mouse.adsScale', 'mouse.adsMatchFov', '鼠标灵敏度'),
      tex: c => adsTex(c, 'mouse.adsMatchFov'),
    },
    'mouse.adsMatchFov': {
      label: '开镜按视场换算', en: 'ADS FOV MATCH', icon: 'fovMatch', preview: 'adsMouse', figure: '越肩开镜转角与屏幕像移对比',
      text: matchText('灵敏度'),
      tex: matchTex,
    },
    'mouse.scopeScale': {
      label: '倍镜灵敏度', en: 'SCOPE SENSITIVITY', icon: 'scope', unit: '×', digits: 2, preview: 'scopeMouse', figure: '光学倍镜转角与镜内像移对比',
      text: c => scopeText(c, 'mouse.scopeScale', '鼠标灵敏度'),
      tex: scopeTex,
    },
    'mouse.invertPitch': {
      label: '反转上下', en: 'INVERT PITCH', icon: 'invertY', preview: 'invertMouse', figure: '鼠标位移与视角转向对应关系',
      text: () => ['翻转鼠标纵向俯仰输入符号：开启时鼠标向前推对应低头俯视、向后拉对应抬头仰视。仅作用于鼠标，与控制器设置互不影响。'],
    },
    'mouse.invertYaw': {
      label: '反转左右', en: 'INVERT YAW', icon: 'invertX', preview: 'invertMouse', figure: '鼠标位移与视角转向对应关系',
      text: () => ['翻转鼠标水平偏航输入符号：开启时鼠标向右移动对应视角向左偏转。'],
    },
    'mouse.accelEnabled': {
      label: '鼠标加速度', en: 'ACCELERATION', icon: 'accel', preview: 'mouseAccel', figure: '鼠标瞬时速率 → 灵敏度增益曲线',
      text: () => [
        '启用**基于瞬时物理速率的帧率无关动态增益**：低速移动时保持基础灵敏度以保证精细控枪，高速甩鼠时按速率线性放大灵敏度以缩短大角度掉头所需的桌面行程。默认关闭以保持纯粹的 1:1 位移映射。',
        '系统直接采集硬件原始输入，先将单帧合成计数 $\\sqrt{\\Delta c_x^2 + \\Delta c_y^2}$ 除以帧长 $\\Delta t$ 换算为速率 $v$（单位 `count/ms`），再求取增益倍率。相比按单帧位移量计算的传统做法，以速率为自变量可确保相同手速在 60 Hz 与 240 Hz 等不同帧率下获得严格一致的加速曲线。',
      ],
      tex: () => [
        {tex: 'v = \\frac{\\sqrt{\\Delta c_x^2 + \\Delta c_y^2}}{1000\\,\\Delta t}, \\qquad G(v) = \\min\\!\\bigl(1 + a\\cdot\\max(v - v_0,\\ 0),\\ G_{\\max}\\bigr)',
          note: 'v：鼠标瞬时速率（count/ms）；v_0：起效阈值；a：斜率强度；G_max：倍率上限'},
      ],
    },
    'mouse.accelStrength': {
      label: '加速度 · 强度', en: 'STRENGTH', icon: 'gauge', digits: 3, preview: 'mouseAccel', figure: '鼠标瞬时速率 → 灵敏度增益曲线',
      off: c => c.v('mouse.accelEnabled') ? '' : '鼠标加速度未开启',
      text: () => ['定义超出起效阈值后的线性增益斜率 $a$：鼠标瞬时速率每提高 `1 count/ms`（800 DPI 下约合 `3.18 cm/s`），灵敏度倍率额外增加的数值。'],
      tex: () => [
        {tex: '\\frac{dG}{dv} = a \\quad (v_0 < v < v_{\\text{cap}})', note: '斜率 a 控制从基础灵敏度向倍率上限过渡的陡峭程度'},
      ],
    },
    'mouse.accelOffset': {
      label: '加速度 · 起点', en: 'OFFSET', icon: 'flag', unit: 'c/ms', digits: 1, preview: 'mouseAccel', figure: '鼠标瞬时速率 → 灵敏度增益曲线',
      off: c => c.v('mouse.accelEnabled') ? '' : '鼠标加速度未开启',
      text: c => [
        `定义不触发加速的低速平坦区上限 $v_0$（800 DPI 下当前对应手速约 [[${fmt(c.v('mouse.accelOffset') * 1000 / 800 * 2.54, 1)} cm/s]]）。当手部移速低于该阈值时增益恒为 $1.00\\times$，使常规跟枪与微调完全保持无加速的固定手感，仅在快速大幅甩鼠时介入放大。`,
      ],
      tex: () => [
        {tex: 'G(v) = 1 \\quad (v \\le v_0)', note: '低速区内保持严格线性的 1:1 位移映射'},
      ],
    },
    'mouse.accelCap': {
      label: '加速度 · 上限', en: 'CAP', icon: 'ceiling', unit: '×', digits: 1, preview: 'mouseAccel', figure: '鼠标瞬时速率 → 灵敏度增益曲线',
      off: c => c.v('mouse.accelEnabled') ? '' : '鼠标加速度未开启',
      text: () => ['限制动态增益的最大放大倍数 $G_{\\max}$。高速甩鼠达到该上限后灵敏度重新收敛为恒定倍率，避免极快挥动时视角过度飞甩失控。'],
      tex: () => [
        {tex: 'G(v) \\le G_{\\max}', note: '当 v ≥ v_0 + (G_max − 1) / a 时增益饱和封顶'},
      ],
    },
    'mouse.aimAssistEnabled': assistSwitch('mouse', '鼠标与键盘'),
    'mouse.aimAssistHip': assistStrength('mouse', false),
    'mouse.aimAssistAds': assistStrength('mouse', true),
    'mouse.aimAssistInner': assistInner('mouse'),
    'mouse.aimAssistOuter': assistOuter('mouse'),
    'mouse.aimAssistRange': assistRange('mouse'),
    'mouse.aimAssistFollow': assistFollow('mouse', '键鼠默认较低：鼠标直接映射手部绝对位移，过高的自动跟随容易与手部主动修正产生对抗感，设为 0% 时仅保留离开目标时的柔和减速。'),
    'mouse.moveForward': binding('前进', 'MOVE FORWARD', 'moveUp', () => [
      '向前移动。主、辅两个键位可同时生效，并按时移动速度不会叠加。',
      '同轴反方向键冲突采用**后按下优先（Last-Input Priority / SOCD）**：当前后或左右两个相反方向同时按住时，取较晚按下的方向而非互相抵消归零，确保急停变向与空中转向（Lurch）瞬时响应。',
    ]),
    'mouse.moveBack': binding('后退', 'MOVE BACK', 'moveDown', () => ['向后移动。与前进键同时按住时遵循后按下优先规则。']),
    'mouse.moveLeft': binding('向左', 'MOVE LEFT', 'moveLeft', () => ['向左横移。与向右横移键同时按住时遵循后按下优先规则。']),
    'mouse.moveRight': binding('向右', 'MOVE RIGHT', 'moveRight', () => ['向右横移。与向左横移键同时按住时遵循后按下优先规则。']),
    'mouse.jump': binding('跳跃', 'JUMP', 'jump', () => ['触发角色起跳。辅助键位默认绑定鼠标滚轮下滚，便于在落地窗口内连续输入跳跃指令。']),
    'mouse.crouch': holdBinding('下蹲', 'CROUCH', 'slide', '下蹲', CROUCH_RULE),
    'mouse.sprint': holdBinding('冲刺', 'SPRINT', 'sprint', '冲刺', SPRINT_RULE),
    'mouse.aim': holdBinding('开镜', 'AIM', 'aim', '开镜', AIM_RULE),
    'mouse.fire': binding('开火', 'FIRE', 'fire', () => FIRE_RULE),
    'mouse.reload': binding('装填', 'RELOAD', 'reload', () => RELOAD_RULE),
    'mouse.interact': binding('交互', 'INTERACT', 'interact', () => ['准星对准场景内可交互物体出现提示时，按下执行拾取或交互。']),
    'mouse.slot1': binding('第一把', 'WEAPON 1', 'slot1', () => [`直接切换至一号主武器槽。${SLOT_RULE}`]),
    'mouse.slot2': binding('第二把', 'WEAPON 2', 'slot2', () => [`直接切换至二号主武器槽。${SLOT_RULE}`]),
    'mouse.sidearm': binding('手枪', 'SIDEARM', 'sidearm', () => [`直接拔出副武器槽手枪。${SLOT_RULE}`]),
    'mouse.holster': binding('收枪', 'HOLSTER', 'holster', () => HOLSTER_RULE),
    'mouse.switchWeapon': binding('切枪', 'SWITCH WEAPON', 'swap', () => [SWITCH_RULE, '键鼠默认未绑定，日常可通过数字键直接选槽，也可按习惯绑定侧键或滚轮。']),
    'mouse.dropWeapon': binding('丢枪', 'DROP WEAPON', 'drop', () => DROP_RULE),
    'mouse.flashbang': binding('投掷闪光弹', 'FLASHBANG', 'swap', () => ['轻按快速投出，按住显示轨迹，松开投掷。']),
    'gamepad.flashbang': binding('投掷闪光弹', 'FLASHBANG', 'swap', () => ['按住显示轨迹，松开投掷。']),
    'mouse.switchShoulder': binding('切换肩位', 'SWITCH SHOULDER', 'swap', () => ['在左肩与右肩视角之间平滑切换，默认按 V。手动换肩始终可用；开镜时手动选择肩位会暂缓自动换肩。']),
    'gamepad.switchShoulder': binding('切换肩位', 'SWITCH SHOULDER', 'swap', () => ['在左肩与右肩视角之间平滑切换。默认未绑定，可按习惯指定按键。']),
    'accessibility.subtitles': {
      label: '显示字幕', en: 'SUBTITLES', icon: 'power', preview: 'subtitles', figure: '字幕预览',
      text: () => ['显示附近角色的台词。关闭后仍能听到已配置的语音。'],
    },
    'accessibility.subtitleSpeaker': {
      label: '显示说话人', en: 'SPEAKER NAME', icon: 'monitor', preview: 'subtitles', figure: '字幕预览',
      text: () => ['在台词前显示说话人的名称。'],
    },
    'accessibility.subtitleSize': {
      label: '字幕字号', en: 'TEXT SIZE', icon: 'monitor', unit: 'px', digits: 0, preview: 'subtitles', figure: '字幕预览',
      text: () => ['调整字幕文字大小。较长的台词会在可用宽度内换行。'],
    },
    'accessibility.subtitleBackdrop': {
      label: '字幕衬底浓度', en: 'BACKDROP OPACITY', icon: 'monitor', unit: '%', scale: 100, digits: 0, preview: 'subtitles', figure: '字幕预览',
      text: () => ['调整文字背后暗色柔光的浓度，可用它改善明亮场景中的可读性。'],
    },
    'accessibility.autoPeek': {
      label: '自动探头', en: 'AUTO PEEK', icon: 'aim',
      text: () => [
        '抬枪时，如果枪口受到遮挡，身体会向实际命中指针偏离准星的反方向倾斜，尝试让出射击空间。默认开启，同时适用于鼠标键盘与控制器。',
        '实际命中指针已指向可伤害目标时，不会发起探头。若探头后才获得射击空间，会保持必要的倾斜，直到遮挡解除；关闭此选项会平滑回正。',
      ],
    },
    'accessibility.restoreShoulderOnAdsExit': {
      label: '取消ADS时复原', en: 'RESTORE SHOULDER ON ADS EXIT', icon: 'swap',
      hide: c => !c.v('accessibility.adsAutoShoulder'),
      text: () => [
        '退出开镜时，平滑恢复到本次进入开镜前的肩位。每次开镜都会重新记录，不固定回到右肩。',
        '开启后，本次开镜期间的自动换肩或手动换肩都会在退出时复原；关闭后保留退出时选择的肩位。仅在 ADS自动换肩开启时生效。',
      ],
    },
    'accessibility.adsAutoShoulder': {
      label: 'ADS自动换肩', en: 'ADS AUTO SHOULDER', icon: 'aim',
      text: () => [
        '开镜时，分别从左右肩的镜头位置检测前方遮挡。另一肩的遮挡得分比当前肩低，且分差超过容差时，相机会平滑移到另一肩。',
        '换肩后暂缓自动判断，退出开镜即可重新判断。你仍可随时使用换肩键选择视角；关闭此选项不影响手动换肩。',
      ],
    },
  };

  const DIRECTORIES = [
    {
      id: 'gamepad', label: '控制器', en: 'CONTROLLER', icon: 'gamepad', image: 'gamepad',
      intro: '手柄的视角管线、摇杆曲线与键位映射，与鼠标键盘设置相互独立。',
      desc: {
        text: () => [
          '控制器视角输入管线依次经过**径向死区重映射 → 无畸变模长响应曲线 → 帧内解析积分转身加速 → 开镜 / 倍镜视场缩放 → 半楞次辅助瞄准**五个阶段，每一步均严格保证帧率无关。',
          '界面导航：方向键或左摇杆选择与调节，A 确认，B 返回，LB / RB 切换目录，X 恢复当前目录默认值，Y 撤销未保存修改，菜单键保存并退出。连接手柄时，右侧预览图会实时投射物理摇杆的当前偏转点，便于直观校准死区与响应曲线。',
        ],
        tex: () => [
          {tex: '\\Delta\\psi = \\bigl(\\omega_{\\text{yaw}} + \\omega_{\\text{extra}}\\,\\bar{w}\\,(1 - \\alpha)\\bigr)\\,S(\\alpha)\\,f(u)\\,\\hat{s}_x\\,\\Delta t',
            note: 'u：死区归一化幅度；f(u)：响应曲线；S(α)：开镜缩放；w̄：帧内解析积分加速权重'},
        ],
        preview: 'lookCurve', figure: '摇杆偏转幅度 → 角速度响应',
      },
      categories: [
        {id: 'sens', title: '灵敏度', en: 'SENSITIVITY',
          text: () => ['定义视角摇杆满偏时的基础角速度，以及越肩开镜与第一人称光学倍镜下的缩放换算规则。'],
          tex: () => [{tex: '\\omega = \\omega_0 \\cdot S(\\alpha) \\cdot f(u)'}],
          items: ['gamepad.yawSpeed', 'gamepad.pitchSpeed', 'gamepad.adsScale', 'gamepad.adsMatchFov', 'gamepad.scopeScale']},
        {id: 'curve', title: '响应曲线', en: 'RESPONSE CURVE',
          text: () => ['塑造摇杆过死区后推杆幅度 $u$ 到转速比例 $f(u)$ 的非线性映射，在保留二维推杆原方向的前提下平衡中心微调精度与大角度跟枪速度。'],
          tex: () => [{tex: 'f_{\\text{exp}}(u) = u^k, \\qquad f_{\\text{S}}(u) = \\frac{u^k}{u^k + (1 - u)^k}'}],
          items: ['gamepad.curve', 'gamepad.curveStrength']},
        {id: 'zone', title: '死区', en: 'DEAD ZONE',
          text: () => ['按二维推杆向量的径向模长滤除中心回弹零漂，并将 $[d_{\\text{in}}, d_{\\text{out}}]$ 的有效物理行程连续重映射至 $[0, 1]$。视角摇杆与移动摇杆各自独立配置。'],
          tex: () => [{tex: 'u = \\mathrm{clamp}\\!\\left(\\frac{\\|\\mathbf{s}\\| - d_{\\text{in}}}{d_{\\text{out}} - d_{\\text{in}}},\\ 0,\\ 1\\right)'}],
          items: ['gamepad.lookInner', 'gamepad.lookOuter', 'gamepad.moveInner', 'gamepad.moveOuter']},
        {id: 'turn', title: '转身加速', en: 'TURN BOOST',
          text: () => ['仅在摇杆推至满偏（$u \\ge 0.98$）并保持超过延迟时间后渐进叠加额外转速，并对分段斜坡做帧内解析积分，兼顾日常低速控枪与快速转身。'],
          tex: () => [{tex: 'w(t) = \\mathrm{clamp}\\!\\left(\\frac{t - t_d}{t_r},\\ 0,\\ 1\\right)'}],
          items: ['gamepad.extraYaw', 'gamepad.extraPitch', 'gamepad.extraDelay', 'gamepad.extraRamp']},
        {id: 'axes', title: '方向', en: 'AXES',
          text: () => ['配置视角俯仰与偏航的正反方向，以及左右两根物理摇杆的职责互换。'],
          items: ['gamepad.invertPitch', 'gamepad.invertYaw', 'gamepad.swapSticks']},
        {id: 'assist', title: '辅助瞄准', en: 'AIM ASSIST',
          text: () => [
            '采用**半楞次径向阻尼**机制：仅在准星远离目标躯干中线时削弱径向逃逸速度，靠近目标与沿躯干上下修枪均不受阻碍；配合越肩视差雅可比求逆与高频子步积分，保证任意距离与帧率下的手感一致。',
            '控制器与键鼠各自独立维护一套参数，运行时自动匹配当前活跃设备；关闭总开关时下方细分项自动折叠。',
          ],
          tex: () => [{tex: '\\mathbf{c}_{\\text{screen}} = -k(\\rho)\\,\\max(\\mathbf{w}\\cdot\\hat{\\mathbf{r}},\\ 0)\\,\\hat{\\mathbf{r}}'}],
          items: ['gamepad.aimAssistEnabled', 'gamepad.aimAssistHip', 'gamepad.aimAssistAds', 'gamepad.aimAssistInner', 'gamepad.aimAssistOuter', 'gamepad.aimAssistRange', 'gamepad.aimAssistFollow']},
        {id: 'keys', keys: true, title: '按键', en: 'KEY BINDINGS',
          text: () => [
            '每个动作提供主键与辅助键两个槽位；冲刺、下蹲、开镜的每个槽位可独立设为「按住」或「切换」，按逻辑并集协同生效。选中键位按 A 后输入新按键即可完成重绑（菜单键取消，视图键清空）。',
            '控制器键位与键盘鼠标键位完全隔离；菜单键与视图键保留供系统界面使用，不可绑定为游戏动作。',
          ],
          items: ['gamepad.jump', 'gamepad.crouch', 'gamepad.sprint', 'gamepad.aim', 'gamepad.fire', 'gamepad.reload', 'gamepad.interact', 'gamepad.holster', 'gamepad.switchWeapon', 'gamepad.switchShoulder', 'gamepad.flashbang', 'gamepad.dropWeapon', 'gamepad.slot1', 'gamepad.slot2', 'gamepad.sidearm']},
      ],
    },
    {
      id: 'mouse', label: '鼠标与键盘', en: 'MOUSE & KEYBOARD', icon: 'mouse', image: 'mouse',
      intro: '鼠标的原始灵敏度、速率加速度与键位映射，与手柄设置相互独立。',
      desc: {
        text: () => [
          '鼠标视角直接采集硬件原始位移计数（Raw Input），跳过操作系统的指针加速曲线：基础偏转严格按 `0.022° × 灵敏度` 线性换算；可选的动态加速度基于瞬时物理速率（`count/ms`）计算增益，在任意帧率下均保持相同手速对应相同响应。',
          '界面导航：除鼠标点击与拖拽外，也可使用 ↑↓ 选择、←→ 微调、Enter 确认、Esc 返回、PageUp / PageDown 切换目录；在按键区域点击键帽或按 Enter 即可录入新键位。',
        ],
        tex: () => [
          {tex: '\\Delta\\psi = 0.022^\\circ \\cdot s \\cdot G(v)\\cdot S(\\alpha)\\cdot \\Delta c_x',
            note: 's：基础灵敏度；G(v)：速率加速度增益（未开启时恒为 1）；S(α)：开镜 / 倍镜缩放'},
        ],
        preview: 'mouseSens', figure: '不同 DPI 下水平旋转 360° 所需物理位移',
      },
      categories: [
        {id: 'sens', title: '灵敏度', en: 'SENSITIVITY',
          text: () => ['定义鼠标每上报 1 count 原始位移对应的视角转角、横纵灵敏度比例，以及越肩开镜与光学倍镜下的视场换算。'],
          tex: () => [{tex: '\\Delta\\psi = 0.022^\\circ \\cdot s \\cdot \\Delta c_x, \\qquad \\Delta\\theta = 0.022^\\circ \\cdot s \\cdot r_{\\text{pitch}} \\cdot \\Delta c_y'}],
          items: ['mouse.sensitivity', 'mouse.pitchRatio', 'mouse.adsScale', 'mouse.adsMatchFov', 'mouse.scopeScale']},
        {id: 'accel', title: '鼠标加速度', en: 'ACCELERATION',
          text: () => ['根据鼠标瞬时移动速率 $v$（`count/ms`）动态放大灵敏度倍率，低速微调维持 1:1 精度，高速甩鼠缩短转向行程；以速率为自变量可确保加速手感不随帧率波动。'],
          tex: () => [{tex: 'G(v) = \\min\\!\\bigl(1 + a\\cdot\\max(v - v_0,\\ 0),\\ G_{\\max}\\bigr)'}],
          items: ['mouse.accelEnabled', 'mouse.accelStrength', 'mouse.accelOffset', 'mouse.accelCap']},
        {id: 'axes', title: '方向', en: 'AXES',
          text: () => ['配置鼠标纵向与横向位移对应的视角旋转极性。'],
          items: ['mouse.invertPitch', 'mouse.invertYaw']},
        {id: 'assist', title: '辅助瞄准', en: 'AIM ASSIST',
          text: () => [
            '采用**半楞次径向阻尼**机制：仅在准星远离目标躯干中线时削弱径向逃逸速度，靠近与沿躯干切向移动均不减速。键鼠与控制器各自独立维护一套参数，运行时按最近活动的输入设备自动切换。',
            '关闭总开关时下方细分参数自动折叠隐藏。',
          ],
          tex: () => [{tex: '\\mathbf{c}_{\\text{screen}} = -k(\\rho)\\,\\max(\\mathbf{w}\\cdot\\hat{\\mathbf{r}},\\ 0)\\,\\hat{\\mathbf{r}}'}],
          items: ['mouse.aimAssistEnabled', 'mouse.aimAssistHip', 'mouse.aimAssistAds', 'mouse.aimAssistInner', 'mouse.aimAssistOuter', 'mouse.aimAssistRange', 'mouse.aimAssistFollow']},
        {id: 'keys', keys: true, title: '按键', en: 'KEY BINDINGS',
          text: () => [
            '每个动作提供主键与辅助键两个槽位（支持键盘按键、鼠标侧键及滚轮上下滚动）；冲刺、下蹲、开镜的每个槽位可独立选择「按住」或「切换」。点击槽位后输入新键即可完成绑定（Esc 取消，Delete 清空）。',
            '与控制器键位相互独立。Esc 与 F10 固定用于开关设置界面；F1–F12、Delete、Win 键与反引号（`）保留供系统与调试工具使用，不可绑定。',
          ],
          items: ['mouse.moveForward', 'mouse.moveBack', 'mouse.moveLeft', 'mouse.moveRight', 'mouse.jump', 'mouse.crouch', 'mouse.sprint', 'mouse.aim', 'mouse.fire', 'mouse.reload', 'mouse.interact', 'mouse.slot1', 'mouse.slot2', 'mouse.sidearm', 'mouse.holster', 'mouse.switchWeapon', 'mouse.switchShoulder', 'mouse.flashbang', 'mouse.dropWeapon']},
      ],
    },
    {
      id: 'accessibility', label: '辅助功能', en: 'ACCESSIBILITY', icon: 'aim',
      intro: '调整辅助行为，让观察与操作更适合自己的习惯。',
      desc: {text: () => ['这里的设置同时作用于鼠标键盘和控制器。自动辅助可以独立关闭，手动操作始终保留。']},
      categories: [
        {id: 'occlusion', title: '反遮挡', en: 'OCCLUSION ASSIST',
          text: () => ['根据周围遮挡辅助调整身体或相机的位置，改善射击与观察空间。自动探头与 ADS自动换肩可独立开关。'],
          items: ['accessibility.autoPeek', 'accessibility.adsAutoShoulder', 'accessibility.restoreShoulderOnAdsExit']},
        {id: 'subtitles', title: '字幕', en: 'SUBTITLES',
          text: () => ['按阅读习惯调整角色台词的显示效果。'],
          items: ['accessibility.subtitles', 'accessibility.subtitleSpeaker', 'accessibility.subtitleSize', 'accessibility.subtitleBackdrop']},
      ],
    },
  ];

  DIRECTORIES.push({
    id: 'video', label: '画质与显示', en: 'GRAPHICS & DISPLAY', icon: 'monitor',
    intro: '调整场景画质与显示方式。确认后应用到本机。',
    desc: {text: () => ['场景掉帧时可先降低整体画质或渲染比例。可用的 DLSS 选项由当前渲染设备决定。']},
    categories: [
      {id: 'graphics', title: '画质', en: 'GRAPHICS', text: () => ['调整场景细节与内部渲染分辨率。'],
        items: ['video.quality', 'video.scale', 'video.dlss', 'video.frames']},
      {id: 'display', title: '显示', en: 'DISPLAY', text: () => ['调整游戏窗口及帧呈现方式。'],
        items: ['video.window', 'video.resolution', 'video.limit', 'video.vsync']},
    ],
  });

  // 描述区背景插画类名（Weft 背景图统一声明在样式表中）
  const IMAGES = {gamepad: 'st-img-gamepad', mouse: 'st-img-mouse'};

  globalThis.SettingsCatalog = {ITEMS, DIRECTORIES, OPTIONS, IMAGES, KEYS};
})();
