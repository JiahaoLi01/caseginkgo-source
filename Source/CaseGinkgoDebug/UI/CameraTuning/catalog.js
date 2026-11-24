// 相机调参面板的目录与说明（Docs/Design/CameraPipeline.md）：目录划分、各小节与参数项的原理解析、数学公式、实时计算指标、图标、滑块范围及动态预览。
//
// 数据行由 C++（GinkgoCameraTuning.cpp 的 "tuning" 事件）反射收集：包含配置路径、所属分组（顶层结构或数组元素）、路径段链与 DisplayName。
// 此处按「声明结构.字段路径」（去除数组下标，如 GinkgoCameraParamModifier.Activation.Conditions.Threshold）索引通用说明；
// 数组元素的专属覆盖说明写作「元素名:键」（如 SlideArm:GinkgoCameraParamModifier.Value.Value），优先于通用说明；未登记项回退读取 C++ ToolTip。
//
// 说明接口：text(c) 返回正文段落，支持 **加粗**、`代码`、[[按当前值实时计算的指标]] 与 $行内公式$；tex(c) 返回独立公式卡片 [{tex, note}]；
// c 为 page.js 构造的 descContext：提供 c.v / c.d（当前值 / 默认值）、c.get(组内相对路径)、c.abs(完整路径)、c.params()（基础构图参数）、
// c.move（角色移动配置常量）、c.group（分组元数据）、c.springOmega(参数名)、c.modifiers()、c.tag(标签名) 与 c.condition()（条件行信息）。
(() => {
  const {fmt} = Paper;
  const M = CameraMath;

  const deg = x => `${fmt(x, 1)}°`;
  const cm = x => `${fmt(x, 0)} cm`;
  const sec = x => `${fmt(x, 2)} s`;
  const ms = x => `${fmt(x * 1000, 0)} ms`;
  const pct = x => `${fmt(x * 100, 0)}%`;
  const signed = (x, digits = 1) => `${x > 0 ? '+' : ''}${fmt(x, digits)}`;

  // ===== 标签与单位显示名 =====

  const TAGS = {
    'Movement.Slide': '滑铲中', 'Movement.Grounded': '着地状态', 'Movement.Sprint': '冲刺中', 'Movement.WallRun': '墙跑中', 'Movement.Grapple': '钩锁牵引中',
    'Movement.Value.HorizontalSpeed': '水平速度', 'Movement.Value.SlideBoost': '本次滑铲起步加速', 'Movement.Value.RunSpeed': '常态跑速',
    'Movement.Event.SlideStarted': '滑铲触发', 'Movement.Event.Landed': '着地瞬间', 'Movement.Event.Footstep': '脚步落地',
    'Weapon.ADS': '开镜状态', 'Weapon.Value.AdsAlpha': '开镜进度', 'Weapon.Value.AdsZoom': '武器开镜倍率', 'Weapon.Value.SightClearance': '拉栓避让进度',
    'Weapon.Event.Fire': '武器击发', 'Weapon.Event.Handling': '枪械操作', 'Camera.Category.Speed': '速度类',
  };
  const tagName = tag => TAGS[tag] || (tag ? tag.split('.').pop() : '');

  const UNITS = {cm: 'cm', Degrees: '°', 'rad/s': 'rad/s', s: 's', Seconds: 's', 'cm/s': 'cm/s', Hz: 'Hz'};

  // 描述区背景插画类名（Weft 背景图统一声明在样式表中）
  const IMAGES = {camera: 'ct-img-camera'};
  const ROOT_IMAGE = 'camera';

  // ===== 目录 =====

  /** 换肩射线与判断参数共用的类别说明；归入碰撞目录，保持各自的操作说明。 */
  const shoulderText = () => [
    '左侧编辑器按相机前向画出射线布局。点图中的线或列表编号选中一条，再调整角度、长度和命中分值；也可以自由增删。',
    '左右肩各发出图中的整组射线。每条命中都给发出它的肩位加分。**当前肩总分 − 另一肩总分 > 分差容差** 时自动换肩。开关位于设置界面的「辅助功能」。',
    '修改立即作用于下一次允许的采样；冷却期间保持停止检测。退出 ADS 清空冷却，重新瞄准即可试新布局。点击「保存到本地」保留配置，标签右边的恢复按钮重置整组射线。',
  ];

  /** 三环按相机高低命名；说明中的半径、离支点高度均在肩部偏移与碰撞之前计算。 */
  const orbitText = c => {
    const o = M.orbitSettings(c), l = c.params().ArmLength;
    const upper = M.orbitDistance(l, -o.UpperPitch, {...o, bEnabled: true});
    const lower = M.orbitDistance(l, o.LowerPitch, {...o, bEnabled: true});
    return [
      '平视时中环沿用「臂长」，上下两环分别调水平半径和俯仰角。**低头看时相机升到上环，抬头看时降到下环**；数值均相对跟随点，不受角色所在地图高度影响。',
      `按基础臂长换算的三环配置（${o.bEnabled ? '已启用' : '未启用，图中显示原球面'}）：中环 [[${cm(l)}]]；上环半径 [[${cm(l * o.UpperRadiusScale)}]]、高度 [[+${cm(upper * Math.sin(o.UpperPitch * Math.PI / 180))}]]；下环半径 [[${cm(l * o.LowerRadiusScale)}]]、高度 [[−${cm(lower * Math.sin(o.LowerPitch * Math.PI / 180))}]]。肩部偏移在轨道之外叠加，图中不含肩位与碰撞。`,
      '两环之间用三次缓动改变视线距离，过中环和外环时没有折点。超过外环角度仍能继续转动，沿视线的距离保持外环值。半径比例小于该角度的余弦时会比原球面更靠近角色；较大时会拉远。',
      '滑铲等修改器改变臂长时，三环一起缩放。关闭开关即可对比原来的球面轨道；修改即时生效，点击「保存到本地」保留。',
    ];
  };

  const DIRECTORIES = [
    {
      id: 'framing', label: '构图', en: 'FRAMING', icon: 'frame', image: 'camera',
      intro: '定义相机在空间中的几何架设与取景宽度：跟随点锚定于胶囊中心上方，臂长与肩部偏移将相机置于右后方，形成角色居左三分之一、中心让给准星的近距越肩构图。',
      categories: [
        {id: 'arm', title: '臂与肩', en: 'ARM & SHOULDER', fields: ['ArmLength', 'ShoulderOffsetRight', 'ShoulderOffsetUp']},
        {id: 'orbit', title: '三环轨道', en: 'THREE-RING ORBIT', preview: 'orbit', text: orbitText,
          figure: '三环与俯仰轨迹：彩线为当前轨道，灰线为原球面；不含肩部偏移和碰撞'},
        {id: 'pivot', title: '跟随点', en: 'PIVOT', fields: ['PivotOffsetForward', 'PivotOffsetRight', 'PivotOffsetUp']},
        {id: 'lens', title: '镜头', en: 'LENS', fields: ['FieldOfView', 'Roll', 'PostProcessWeight']},
      ],
      desc: {
        preview: 'framing', figure: '当前构图实时投影：左侧为 16:9 画面取景，右侧为空间视锥俯视 / 侧视',
        text: c => {
          const p = c.params(), fr = M.framing(p, c.move);
          return [
            '越肩相机位姿由两级空间变换合成：首先在经弹簧平滑后的胶囊中心 $C$ 上叠加仅随水平偏航角旋转的**跟随点偏移**，确立俯仰旋转支点 $P$；随后在完整视角坐标系 $R_{\\text{view}}$ 下按**三环轨道距离** $D(p)$ 后退，并沿右、上方向施加**肩部偏移** $(s_r, s_u)$。平视时 $D(0)=L$，关闭三环时所有角度均为臂长 $L$。相机朝向直接取控制器视角，不做旋转插值。',
            `当前基准构图：臂长 [[${cm(p.ArmLength)}]]、肩部右 / 上偏移 [[${cm(p.ShoulderOffsetRight)} / ${cm(p.ShoulderOffsetUp)}]]、跟随点高度 [[${cm(p.PivotOffsetUp)}]]、基准视场 [[${deg(p.FieldOfView)}]]（16:9 视口下实际水平视场 [[${deg(fr.fov.horizontal)}]]）。平视状态下，角色胶囊中线位于画面宽度的 [[${pct(fr.centerX)}]]，胶囊顶端位于画面高度的 [[${pct(fr.topY)}]]，屏幕中央准星区域完全无遮挡。`,
            '本页调节的是静态基准参数 $P_0$：滑铲、开镜等动态状态通过「参数修改器」在其上叠加覆盖或增量，墙体防穿模压缩则在碰撞阶段临时缩短有效臂长，均不会篡改此处的基准数值。',
          ];
        },
        tex: () => [
          {tex: 'P = C + R_{\\text{yaw}}\\,(p_f,\\ p_r,\\ p_u)', note: 'C 为平滑后的胶囊中心；跟随点偏移仅随水平偏航角旋转，作为俯仰旋转中心'},
          {tex: 'K = P + R_{\\text{view}}\\,(-D(p),\\ s_r,\\ s_u)', note: 'D(p) 为三环轨道在俯仰角 p 下的距离；平视时等于中环臂长 L'},
        ],
      },
    },
    {
      id: 'follow', label: '跟随', en: 'FOLLOW', icon: 'follow', image: 'camera',
      intro: '相机跟随胶囊体中心而非骨骼节点，避免动作摆动污染准星；通过二阶临界阻尼弹簧配合速度前馈，消除匀速移动滞后并仅对急起、急停与撞墙等速度突变做柔性缓冲。',
      categories: [
        {id: 'horizontal', title: '水平', en: 'HORIZONTAL', fields: ['Omega', 'Feedforward', 'MaxLag']},
        {id: 'vertical', title: '竖直', en: 'VERTICAL', fields: ['VerticalOmega', 'VerticalFeedforward', 'VerticalMaxLag']},
        {id: 'filter', title: '前馈的低通', en: 'FEEDFORWARD FILTER', fields: ['FeedforwardFilterOmega']},
      ],
      desc: {
        preview: 'followH', figure: '冲刺急起与急停全过程的水平跟随滞后曲线',
        text: c => {
          const omega = c.abs('Follow.Omega'), k = c.abs('Follow.Feedforward');
          const v = c.move.sprintSpeed || 762;
          return [
            '二阶临界阻尼弹簧在追踪以速度 $v$ 匀速运动的目标时，存在固有稳态滞后 $\\Delta_{\\infty} = 2v / \\omega$。引入**速度前馈（Velocity Feedforward）**将追踪目标沿移动速度方向预置提前 $k\\cdot 2v/\\omega$：当 $k = 1$ 时稳态滞后精确抵消为零，弹簧仅在加速度非零的瞬态阶段（起步、急停、空中变向、撞墙）产生平滑弹性缓冲。水平平面与竖直轴各自独立配置参数。',
            `当前水平角频率 $\\omega =$ [[${fmt(omega, 1)} rad/s]]、前馈系数 $k =$ [[${fmt(k, 2)}]]：以 ${fmt(v, 0)} cm/s 冲刺时的稳态滞后为 [[${fmt((1 - k) * 2 * v / Math.max(omega, 1e-6), 1)} cm]]（若关闭前馈则为 [[${fmt(2 * v / Math.max(omega, 1e-6), 0)} cm]]）。`,
            '管线采用解析闭式解推进弹簧状态（`StepMoving`）：将帧内胶囊位移视为匀速线性轨迹，在任意帧率（30 / 60 / 144 fps）下积分出的跟随轨迹严格一致。',
          ];
        },
        tex: () => [
          {tex: '\\ddot{x} + 2\\omega\\dot{x} + \\omega^2 x = \\omega^2 x^*, \\qquad x^* = c + k\\,\\frac{2v}{\\omega}',
            note: 'x 为平滑后的跟随中心，c 为真实胶囊中心，v 为移动组件速度，k 为前馈系数'},
        ],
      },
    },
    {
      id: 'collision', label: '碰撞', en: 'COLLISION', icon: 'collision', image: 'camera',
      intro: '当跟随点到理想相机位置之间存在几何遮挡时按比例压缩悬臂：拉近采用零延迟瞬时截断以杜绝穿模，遮挡解除后的推远则经临界阻尼弹簧平滑释放。ADS 自动换肩同样在此调节，可配置环境射线及判断与过渡参数。',
      categories: [
        {id: 'probe', title: '探测与推远', en: 'PROBE & PUSH-OUT', fields: ['ProbeRadius', 'PushOutOmega']},
        {id: 'rays', title: '换肩射线', en: 'SHOULDER RAYS', preview: null, text: shoulderText},
        {id: 'behavior', title: '换肩判断与过渡', en: 'SHOULDER DECISION & TRANSITION', preview: null, text: shoulderText},
      ],
      desc: {
        preview: 'collision', figure: '遮挡物切入与移出时的实际有效臂长响应',
        text: () => [
          '每帧从经安全校验的跟随点 $P$ 向理想相机位 $K$ 投射半径为 $r$ 的球体扫掠（Sphere Sweep），由首个命中点折算出本帧所需的最小压缩量 $c^*$。系统对拉近与推远执行**非对称更新**：当 $c^*$ 大于当前压缩量（障碍物逼近）时立即令 $c = c^*$ 并将弹簧速度清零，确保近裁剪面绝不穿入墙体；当 $c^*$ 减小时（走出掩体）则交由临界阻尼弹簧平滑回放。',
          '碰撞阶段记录并演化的是**距离压缩量** $c$ 而非绝对臂长，且沿「后退臂长 + 肩部偏移」合成向量等比例缩放：因此在无遮挡环境下，滑铲等修改器对基准臂长的动态调节会直接透传，不会被推远弹簧额外拖慢。',
        ],
        tex: () => [
          {tex: 'c(t) = \\max\\!\\bigl(c^*,\\ \\mathrm{Spring}_{\\omega_{\\text{out}}}(c \\to c^*)\\bigr)',
            note: 'c* 为本帧球体扫掠测得的遮挡压缩量；拉近瞬时夹紧，推远按解析弹簧平滑释放'},
        ],
      },
    },
    {
      id: 'slide', label: '滑铲', en: 'SLIDE', icon: 'slide', image: 'camera',
      intro: '仅在获得有效起步冲量的滑铲中触发：视场扩张与臂长缩短同步执行以构成推拉变焦，滑行全程维持向左微倾，并在起铲瞬间施加一次确定性的低频冲量震动。',
      elements: ['SlideFov', 'SlideArm', 'SlideRoll', 'SlideShake', 'SlideShakeRoll'],
      desc: {
        preview: 'slideTimeline', figure: '满速冲刺滑铲全过程：速度衰减曲线与三项参数修改器响应',
        text: c => {
          const s = M.slide(c.move);
          return [
            '滑铲镜头表现由两层协同构成：**参数层**修改器驱动视场、臂长与侧倾角，经激活条件判定、线性淡入淡出及参数弹簧平滑；**叠加层**冲量弹簧仅在起铲瞬间对画面施加确定性的阻尼振荡——武器射线与落点判定仍严格沿叠加前的控制器视角计算，准星锚定真实弹着方向。',
            `满速冲刺起铲初速为 [[${fmt(s.v0, 0)} cm/s]]（冲刺跑速 ${fmt(c.move.sprintSpeed || 762, 0)} 叠加起步冲量并受上限 ${fmt(c.move.slideBoostCap || 1168.4, 0)} 约束），随后在线性摩擦减速 $d$ 与指数空气阻力 $k$ 共同作用下衰减：历经 [[${sec(s.tRun)}]] 降至常态跑速以下时，视场与臂长修改器开始淡出并锁死；侧倾角则持续保持至 [[${sec(s.tEnd)}]] 滑铲结束（速度低于 ${fmt(s.end, 1)} cm/s）。`,
            '连续滑铲进入冷却疲劳期时起步加速冲量为 0：由于不满足参数修改器「起步加速 ≥ 1」的连续量门限与冲量弹簧的最小事件强度阈值，疲劳滑铲不会触发任何虚假的速度感镜头特效。',
          ];
        },
        tex: c => {
          const s = M.slide(c.move);
          return [
            {tex: 'v(t) = \\left(v_0 + \\frac{d}{k}\\right)e^{-kt} - \\frac{d}{k}', note: `d = ${fmt(s.d, 0)} cm/s²，k = ${fmt(s.k, 4)} /s（读取自移动配置 SlideDecel 与 SlideDecay）`},
            {tex: 't_{\\text{run}} = \\frac{1}{k}\\ln\\frac{v_0 + d/k}{v_{\\text{run}} + d/k}', note: `当前为 ${sec(s.tRun)}：滑行速度回落至常态跑速的时刻`},
          ];
        },
      },
    },
    {
      id: 'sprint', label: '冲刺', en: 'SPRINT', icon: 'sprint', image: 'camera',
      intro: '常态奔跑保持画面平稳，仅在高速冲刺的每一步足部触地瞬间注入一次高频、低幅度的欠阻尼角速度冲量：俯仰微沉，并随左右脚交替向对应侧轻倾。',
      elements: ['FootstepShake'],
      desc: {
        preview: 'footsteps', groupFor: 'FootstepShake', figure: '冲刺连续脚步事件触发的叠加阻尼响应',
        text: () => [
          '脚步触发信号直接源自角色动画同步标记（烘焙自足部着地曲线的 L / R Sync Marker）：当动画播放头跨过标记时，系统精确反算出跨过该标记的**帧内亚帧时刻**（`CrossFootMarkers`），在本地角色上下文中派发强度为 $+1$（左脚）或 $-1$（右脚）的瞬时事件。',
          '叠加层采用二阶欠阻尼冲量弹簧响应：单帧内若包含事件，管线先将弹簧解析推进至事件发生的精确亚帧时刻，注入角速度冲量 $J$ 后再推进剩余帧长，使振荡波形完全取决于物理事件时刻而与帧率无关。',
        ],
        tex: () => [
          {tex: 'x(t) = \\frac{J}{\\omega_d}\\,e^{-\\zeta\\omega t}\\sin(\\omega_d t), \\qquad \\omega_d = \\omega\\sqrt{1 - \\zeta^2}',
            note: 'J 为瞬时角速度冲量（°/s），ζ 为阻尼比，ω_d 为有阻尼固有角频率'},
        ],
      },
    },
    {
      id: 'aim', label: '开镜', en: 'AIM', icon: 'aim', image: 'camera',
      intro: '涵盖越肩开镜的视场倍率缩放、超低频呼吸漂移噪声，以及装配光学瞄具时的第一人称出瞳过渡、近裁剪面保护与拉栓手部避让参数。',
      elements: ['AdsFov', 'AdsSway'],
      categories: [
        {
          id: 'firstPerson', title: '第一人称倍镜', en: 'FIRST PERSON SCOPE', icon: 'scope', afterElements: true,
          fields: ['NearClipPlane', 'HeadHideDistance', 'BoltClearance'], preview: null,
          text: c => [
            '当手持装配独立光学瞄具的武器（如 98k 的 ZF39 倍镜）进入开镜状态时，相机位姿随平滑后的开镜进度 $\\alpha \\in [0, 1]$ 从第三人称越肩构图连续插值至瞄具后方的**出瞳位置（Exit Pupil）**，视线严格对齐瞄具光轴；收镜时沿原轨迹平滑退回。',
            `过渡至第一人称近眼观察时需协同处理三项几何约束：将视锥近裁剪面压低至 [[${fmt(c.abs('FirstPerson.NearClipPlane'), 1)} cm]] 以防切穿目镜筒；在距出瞳不足 [[${cm(c.abs('FirstPerson.HeadHideDistance'))}]] 时提前隐藏角色头部网格体以防穿模；并在拉栓退壳期间按机件行程将视点向 [[(${fmt(c.abs('FirstPerson.BoltClearance.X'), 1)}, ${fmt(c.abs('FirstPerson.BoltClearance.Y'), 1)}, ${fmt(c.abs('FirstPerson.BoltClearance.Z'), 1)}) cm]] 偏置，避开贴近眼前的拉栓手腕。`,
          ],
          tex: () => [
            {tex: 'K_{\\text{eye}} = P_{\\text{pupil}} + s_{\\text{bolt}} \\cdot R_{\\text{sight}}\\,\\mathbf{b}_{\\text{clear}}',
              note: 'P_pupil 为瞄具出瞳世界坐标；s_bolt ∈ [0, 1] 为枪栓抬起/后拉包络；b_clear 为局部避让偏移'},
          ],
        },
      ],
      desc: {
        preview: 'noise', groupFor: 'AdsSway', figure: '开镜完全到位时的超低频准周期漂移轨迹',
        text: () => [
          '开镜系统同时兼容两种瞄准形态：**越肩开镜**通过参数层的后置视场覆盖按正切倍率收窄画面，并叠加仅摇晃画面的超低频准周期噪声（武器真实指向不变，准星随真实弹着点反向微漂）；**第一人称倍镜**则将相机平滑推入瞄具出瞳，同时将呼吸噪声转移至枪身与分划板（`bMovesSight`），使镜内十字分划与弹道落点一体浮动。',
          '持续晃动遵循「确定性噪声 × 上下文包络」架构：噪声发生器为相机累积时钟的纯函数，任意帧率在同一时刻采样结果恒同；包络幅度直接映射自两端缓和的开镜进度 $\\alpha$，实现开镜渐起、收镜渐消的无跳变过渡。',
        ],
        tex: () => [
          {tex: '\\theta_{\\text{ads}} = 2\\arctan\\!\\left(\\frac{\\tan(\\theta_0 / 2)}{z}\\right), \\qquad y(t) = e(\\alpha)\\cdot n(t)',
            note: 'z 为武器开镜角放大率；e(α) 为开镜包络，n(t) 为无理数频率比三正弦合成噪声'},
        ],
      },
    },
    {
      id: 'fire', label: '开枪', en: 'FIRE', icon: 'impulse', image: 'camera',
      intro: '接收武器击发与机件操作事件携带的阻尼弹簧冲量：各把武器的多层后坐力波形与随机区间定义在武器数据资产中，相机端保持解耦并仅提供全局缩放与瞄具联动开关。',
      elements: ['WeaponRecoil', 'WeaponHandling'],
      desc: {
        text: () => [
          '为避免相机系统与具体武器逻辑耦合，开火与拉栓震动的物理参数（俯仰 / 偏航 / 侧倾 / 视场峰值区间、冲击阶段与回弹阶段各自的 $\\omega, \\zeta$）均配置在武器数据资产内。每次击发或机件动作触发时，武器端在随机区间内采样生成一组冲量参数并随事件派发，由相机的 `UGinkgoCameraCarriedShake` 统一接收。',
          '每发子弹生成的冲量各自驱动独立的二阶弹簧实例（抵达峰值速度过零时平滑切换为回弹弹簧参数），连射时按线性系统自然叠加，切枪后旧弹簧亦按原阻尼自然衰减完毕，绝不截断跳变。编辑器 PIE 运行期间修改武器数据资产，下一枪立即生效。',
        ],
        tex: () => [
          {tex: '\\Delta R(t) = S \\sum_{i} x_i(t - t_i)',
            note: 'S 为相机端全局缩放倍数；每次事件在亚帧时刻 t_i 注入独立弹簧实例 x_i 并线性叠加'},
        ],
      },
    },
    {
      id: 'general', label: '通用', en: 'GENERAL', icon: 'smoothing', image: 'camera',
      intro: '跨状态共用的管线底层配置：参数层的临界阻尼平滑弹簧将修改器线性淡入淡出的折角磨圆，叠加层的旋转支点决定震动时相机自身原地偏转还是绕跟随点公转。',
      categories: [
        {id: 'springs', title: '参数弹簧', en: 'PARAM SPRINGS', springs: true},
        {id: 'overlay', title: '叠加层', en: 'OVERLAY', fields: ['OverlayPivot']},
      ],
      desc: {
        preview: 'spring', valueFrom: 'springs', figure: '参数层临界阻尼弹簧的单位阶跃响应',
        text: () => [
          '各参数修改器按优先级完成覆盖与增量合成后，目标参数再经一层二阶临界阻尼弹簧平滑输出：修改器权重本身采用分段线性斜坡淡入淡出，弹簧负责消除起止折角处的导数跳变（$C^1$ 连续）。管线在帧内所有权重折点（跨阈值时刻、抵达 0 或 1 饱和时刻）处精确拆分时间段做解析积分，确保权重与弹簧输出均严格帧率无关。',
          '叠加层（冲量弹簧、准周期噪声）位于碰撞检测之后执行，仅输出视角空间旋转偏移与视场增量，各效果分量线性相加。',
        ],
        tex: () => [
          {tex: 'x(t) = 1 - (1 + \\omega t)\\,e^{-\\omega t}',
            note: '临界阻尼弹簧单位阶跃响应：最快无超调收敛，t_50 ≈ 1.68 / ω，t_90 ≈ 3.89 / ω'},
        ],
      },
    },
    {
      id: 'other', label: '其他', en: 'OTHER', icon: 'more', image: 'camera',
      intro: '配置资产中新增但尚未在面板目录中归类的修改器或参数项，默认回退展示 C++ 属性注释。',
      desc: {text: () => ['当前列表中展示的是已在 C++ 配置资产中声明、但尚未在 Source/CaseGinkgoDebug/UI/CameraTuning/catalog.js 中指定归属目录的参数项。']},
    },
  ];

  // ===== 修改器与叠加效果（按配置元素 Name 索引） =====

  const ELEMENTS = {
    SlideFov: {
      dir: 'slide', title: '滑铲的视场', en: 'SLIDE FOV', icon: 'fov', unit: '°', preview: 'slideTimeline', figure: '满速冲刺滑铲：视场增量响应曲线',
      text: c => {
        const s = M.slide(c.move);
        const value = c.get('Value.Value');
        return [
          `获得滑铲起步冲量的瞬间叠加上视场增量 [[${signed(value)}°]]；当滑行水平速度衰减至常态跑速以下时按 [[${sec(c.get('BlendOutTime'))}]] 线性淡出并触发单次锁死（Latch Off）——同一次滑铲途中即便顺坡再次加速也不会反复拉扯视场。满速冲刺起铲时约在 [[${sec(s.tRun)}]] 处开始回落。`,
          '由于起铲冲量瞬时建立，淡入时间设为 0（权重当帧置 1），视场扩张曲线完全由视场参数弹簧平滑拉升。该修改器标记为「速度类」增量（`Camera.Category.Speed`），开镜时会被开镜视场覆盖按权重自动屏蔽。',
        ];
      },
    },
    SlideArm: {
      dir: 'slide', title: '滑铲的臂长', en: 'SLIDE ARM', icon: 'arm', unit: 'cm', preview: 'dolly', itemPreview: 'slideTimeline', figure: '推拉变焦对比：常态构图与滑铲瞬间',
      text: c => {
        const p = c.params();
        const fovDelta = c.modifierValue('SlideFov');
        const arm = c.get('Value.Value');
        const ratio = (p.ArmLength * Math.tan(p.FieldOfView * Math.PI / 360)) / ((p.ArmLength + arm) * Math.tan((p.FieldOfView + fovDelta) * Math.PI / 360));
        const keep = p.ArmLength * Math.tan(p.FieldOfView * Math.PI / 360) / Math.tan((p.FieldOfView + fovDelta) * Math.PI / 360) - p.ArmLength;
        return [
          `与「滑铲的视场」保持完全同步的激活门限、锁死逻辑、淡出时长与弹簧频率，在滑铲加速段施加臂长偏移 [[${signed(arm, 0)} cm]]。当视场扩张 ${fmt(fovDelta, 1)}° 的同时按比例缩短相机距离以维持 $L\\tan(\\theta/2)$ 恒定，角色在屏幕上的投影高度保持不变，而远景透视迅速拉开——即经典的**推拉变焦（Dolly Zoom）**速度感表现。`,
          `按当前设定，滑铲到位瞬间角色的屏幕高度为常态的 [[${fmt(ratio * 100, 1)}%]]（若需严格等高，理论臂长增量应为 [[${fmt(keep, 1)} cm]]）。视场与臂长的参数弹簧采用同一角频率 $\\omega$，确保过渡全程角色占比始终平稳。`,
        ];
      },
      tex: () => [
        {tex: 'L_1\\tan\\frac{\\theta_1}{2} = L_0\\tan\\frac{\\theta_0}{2}',
          note: '推拉变焦不变量：角色屏幕投影高度 h ∝ 1 / (L·tan(θ/2))，等式成立时主体尺寸恒定、背景视场展宽'},
      ],
    },
    SlideRoll: {
      dir: 'slide', title: '滑铲的侧倾', en: 'SLIDE ROLL', icon: 'roll', unit: '°', preview: 'slideTimeline', figure: '满速冲刺滑铲：画面侧倾曲线',
      text: c => [
        `在获得起步冲量的整段滑铲期间将画面向左倾斜 [[${deg(-c.get('Value.Value'))}]]（侧倾参数 ${signed(c.get('Value.Value'))}°，地平线左高右低），仅绕视线前向旋转成像平面，不改变相机空间位置与武器瞄准射线。权重淡入 [[${sec(c.get('BlendInTime'))}]]、淡出 [[${sec(c.get('BlendOutTime'))}]]，再经角频率 $\\omega =$ [[${fmt(c.springOmega('Roll'), 1)}]] 的侧倾参数弹簧平滑。`,
        '与视场和臂长不同，侧倾不检测实时跑速阈值且不启用中途锁死，一直保持至滑铲状态结束才淡出回正；同时它不归属速度类增量，滑铲途中越肩开镜仍保留侧倾姿态感。',
      ],
    },
    SlideShake: {
      dir: 'slide', title: '滑铲开始的震动', en: 'SLIDE KICK', icon: 'impulse', unit: '', preview: 'impulse', figure: '滑铲触发瞬间的俯仰与偏航冲量响应',
      text: c => {
        const r = M.damped(c.get('Impulse.Pitch'), c.get('Omega'), c.get('Zeta'));
        return [
          `滑铲触发瞬间注入一次确定性的固定角速度冲量（无随机抖动，每次触发体感完全一致）：以向下俯仰顿挫为主并附带微量偏航。当前有阻尼振荡频率为 [[${fmt(r.freq, 1)} Hz]]，俯仰首峰 [[${deg(r.peak)}]] 出现在触发后 [[${ms(r.tPeak)}]]，包络在 [[${ms(r.settle)}]] 内衰减至 2% 以下。`,
          `「滑铲触发」事件携带的强度值等于本次滑铲获得的起步加速冲量（冷却疲劳滑铲为 0），通过最小强度门限 [[${fmt(c.get('MinMagnitude'), 0)}]] 自动滤除无冲量的疲劳滑铲。第一人称光学倍镜下输出比例默认归零，避免镜内剧烈晃动干扰瞄准。`,
        ];
      },
    },
    SlideShakeRoll: {
      dir: 'slide', title: '滑铲开始的侧倾抖动', en: 'SLIDE KICK · ROLL', icon: 'axisRoll', unit: '', preview: 'impulse', figure: '滑铲触发瞬间的侧倾冲量响应',
      text: c => {
        const r = M.damped(c.get('Impulse.Roll'), c.get('Omega'), c.get('Zeta'));
        const pitch = c.modifierGroup('SlideShake');
        return [
          `监听同一滑铲触发事件的独立侧倾弹簧通道：振荡频率 [[${fmt(r.freq, 1)} Hz]]、首峰幅度 [[${deg(r.peak)}]]。刻意与俯仰主通道（[[${pitch ? fmt(M.damped(1, pitch.get('Omega'), pitch.get('Zeta')).freq, 1) : '—'} Hz]]）错开固有频率，使多轴合成轨迹呈现自然的立体顿挫而非呆板的单轴钟摆。`,
        ];
      },
    },
    FootstepShake: {
      dir: 'sprint', title: '冲刺落脚的震动', en: 'FOOTSTEP KICK', icon: 'sprint', unit: '', preview: 'footsteps', figure: '冲刺连续脚步的阻尼振荡叠加',
      text: c => {
        const r = M.damped(c.get('Impulse.Pitch'), c.get('Omega'), c.get('Zeta'));
        const roll = M.damped(c.get('Impulse.Roll'), c.get('Omega'), c.get('Zeta'));
        return [
          `每一步足部触地瞬间施加高频微幅冲量：俯仰下沉首峰 [[${deg(Math.abs(r.peak))}]]、侧倾首峰 [[${deg(Math.abs(roll.peak))}]]（开启负强度镜像后左脚向左倾、右脚向右倾），有阻尼频率 [[${fmt(r.freq, 1)} Hz]]，在 [[${ms(r.settle)}]] 内迅速收敛——短于冲刺单步周期（约 210 ms），确保下一步落地前上一步已基本平息。`,
          `激活门限：处于着地状态、非滑铲中，且水平速度不低于 [[${fmt(c.conditionThreshold(0), 0)} cm/s]]（介于常态跑速 ${fmt(c.move.runSpeed || 476.25, 0)} 与冲刺跑速 ${fmt(c.move.sprintSpeed || 762, 0)} 之间），松开冲刺减速后自动停振。最小触发间隔设为 [[${sec(c.get('MinInterval'))}]]，用于合并滑铲起身过渡动画中双脚在 20 ms 内相继擦地的双发标记。`,
        ];
      },
    },
    WeaponRecoil: {
      dir: 'fire', title: '枪的后坐', en: 'WEAPON RECOIL', icon: 'impulse', unit: '',
      text: c => [
        `接收「武器击发」事件携带的多层后坐力冲量弹簧，并对武器资产定义的幅度施加全局缩放倍数 [[${fmt(c.get('Scale'), 2)}]]。`,
        '每次击发生成独立的弹簧组实例（上升至峰值后可无缝切换为独立的回弹 $\\omega, \\zeta$），连射时线性叠加，换枪后残余振荡按原参数自然走完。开启「第一人称时枪跟着转」后，光学倍镜的镜体与分划板将与画面同步上跳，保持镜内弹着关系一致。',
      ],
    },
    WeaponHandling: {
      dir: 'fire', title: '操作枪的震动', en: 'WEAPON HANDLING', icon: 'impulse', unit: '',
      text: c => [
        `接收「枪械操作」事件（如栓动步枪抬柄、后拉退壳、推弹入膛、压柄闭锁等关键动作帧）携带的微幅机械顿挫冲量，整体乘以缩放系数 [[${fmt(c.get('Scale'), 2)}]]。`,
        '各动作节点的具体冲量波形配置于武器数据资产（`CueCameraShakes`）中，仅对画面旋转产生轻微质感反馈，不影响武器瞄准指向；在越肩与第一人称开镜拉栓期间均生效。',
      ],
    },
    AdsFov: {
      dir: 'aim', title: '开镜的视场', en: 'ADS FOV', icon: 'fov', unit: '°', preview: 'blendCurve', figure: '越肩开镜视场覆盖权重的混入与混出曲线',
      text: c => [
        '越肩开镜期间将相机视场覆盖为按武器角放大率 $z$ 换算后的窄视场：$v = 2\\arctan\\bigl(\\tan(P_0 / 2) / z\\bigr)$（例如 $z = 1.73$ 时将 90° 基准视场收窄至 60°）。基于正切倍率而非写死绝对角度，可保证玩家调整基准视场后仍获得一致的光学放大比例。',
        `该覆盖置于参数弹簧之后执行（\`bAfterSprings\`），权重直接沿指定缓动曲线在开镜 [[${sec(c.get('BlendInTime'))}]]、收镜 [[${sec(c.get('BlendOutTime'))}]] 内精确走完，不被视场弹簧二次拖尾；同时按实时覆盖权重等比屏蔽速度类增量（滑铲视场与臂长）。`,
      ],
      tex: () => [
        {tex: '\\theta_{\\text{ads}} = 2\\arctan\\!\\left(\\frac{\\tan(P_0 / 2)}{z}\\right)',
          note: 'P_0 为基准视场角，z 为武器开镜放大倍率（读取自上下文 Weapon.Value.AdsZoom）'},
      ],
    },
    AdsSway: {
      dir: 'aim', title: '开镜的晃动', en: 'ADS SWAY', icon: 'aim', unit: '', preview: 'noise', figure: '开镜完全到位时的超低频漂移波形与平面轨迹',
      text: c => [
        `模拟持枪瞄准时的超低频呼吸与肌群微漂：俯仰幅度 [[${deg(c.get('Pitch.Amplitude'))}]]（主频 [[${fmt(c.get('Pitch.Frequency'), 2)} Hz]]）、偏航幅度 [[${deg(c.get('Yaw.Amplitude'))}]]（主频 [[${fmt(c.get('Yaw.Frequency'), 2)} Hz]]）、侧倾幅度 [[${deg(c.get('Roll.Amplitude'))}]]。`,
        '越肩开镜下仅摇晃相机画面，枪口射线仍锚定控制器视角，屏幕准星实时投影在真实瞄准点上（表现为准星相对画面反向微漂）；过渡至第一人称光学倍镜后，画面层输出归零并改由枪身与瞄具承担同等噪声偏转（`bMovesSight`），子弹落点严格跟随十字分划。',
      ],
    },
  };

  // 参数弹簧按目标参数命名
  const SPRING_NAMES = {FieldOfView: '视场的弹簧', ArmLength: '臂长的弹簧', Roll: '侧倾的弹簧'};

  // ===== 参数项明细 =====
  // 支持字段：icon、range（滑块软范围，不硬截断输入）、step、digits、unit（无 Units 元数据时的回退单位）、preview、
  // label(c)（覆盖 C++ DisplayName）、off(c)（当前未生效时返回原因说明）、showIn(group)、text(c)、tex(c)

  const ITEMS = {
    // ----- 换肩：射线数组是一项，增删、恢复和本地保存都保持整组一致。
    'GinkgoCameraShoulderSettings.Rays': {
      label: () => '检测射线', icon: 'aim',
      text: () => [
        '角度相对相机前向：**负值向左、正值向右、0° 向前**。左右肩从各自的镜头位置探测，使用同一套角度并保留相机俯仰。图中展示每肩共用的布局。',
        '每条线独立设置长度（米）和命中分值（可用小数）。0 分仍检测、仍显示，但不影响换肩。外圈按最长射线自动缩放；金色为选中线，翠绿为左侧，暖橙为右侧，白色为正前方。',
        '空列表暂停环境探测。默认九条（每条 3.5 米）：左右各 15°（1.4 分）/ 30°（1.2 分）/ 45°（1 分）/ 60°（1 分），加正前方 0°（2 分）。',
      ],
    },
    'GinkgoCameraShoulderSettings.ScanInterval': {
      label: () => '检测间隔', icon: 'time', range: [0.01, 1], step: 0.01, unit: 's',
      text: () => ['两次环境采样的最短间隔；进入 ADS 立即采样，低帧率时不补跑。缩短会更敏捷，也会增加物理查询。'],
    },
    'GinkgoCameraShoulderSettings.TraceChannel': {
      label: () => '检测通道', icon: 'collision',
      text: () => ['所有射线共用的碰撞通道，默认 Camera。只计入 Block，忽略 Overlap、Ignore 和角色自身。通道响应由被检测物体的碰撞配置决定。'],
    },
    'GinkgoCameraShoulderSettings.ScoreTolerance': {
      label: () => '分差容差', icon: 'aim', range: [0, 10], step: 0.1,
      text: () => ['当前侧总分减另一侧总分**严格大于**容差才换肩，等于时不换。每条线使用自己的命中分值，容差也支持小数。'],
    },
    'GinkgoCameraShoulderSettings.Cooldown': {
      label: () => '换肩冷却', icon: 'time', range: [0, 30], step: 0.5, unit: 's',
      text: () => ['自动或手动换肩后暂停检测的时间，期间不发射射线、不重新计分。退出 ADS 会立刻清空冷却。'],
    },
    'GinkgoCameraShoulderSettings.BlendOmega': {
      label: () => '换肩角频率', icon: 'spring', range: [0, 30], step: 0.5, unit: 'rad/s',
      text: () => ['横向构图按临界阻尼弹簧过渡。值越高切换越快，0 表示直接到位；只改变相机肩位，不改变准星朝向。'],
    },
    // ----- 构图
    'GinkgoCameraOrbitSettings.bEnabled': {
      icon: 'frame', text: orbitText,
    },
    'GinkgoCameraOrbitSettings.UpperRadiusScale': {
      icon: 'arm', range: [0.05, 1], step: 0.01,
      text: c => [`上环在指定俯视角的**水平半径 / 中环臂长**，当前 [[${pct(c.v)}]]。比例越小，俯视时越靠近角色。不是在球面半径上再乘一次。`, ...orbitText(c)],
    },
    'GinkgoCameraOrbitSettings.UpperPitch': {
      icon: 'pivotUp', range: [5, 80], step: 1,
      text: c => [`低头 [[${deg(c.v)}]] 时通过上环。角度越小越早进入上环距离；与半径一起决定环高，不限制玩家继续低头。`, ...orbitText(c)],
    },
    'GinkgoCameraOrbitSettings.LowerRadiusScale': {
      icon: 'arm', range: [0.05, 1], step: 0.01,
      text: c => [`下环在指定仰视角的**水平半径 / 中环臂长**，当前 [[${pct(c.v)}]]。抬头时独立于上环变化；贴地时仍由碰撞层把相机挡在地面上方。`, ...orbitText(c)],
    },
    'GinkgoCameraOrbitSettings.LowerPitch': {
      icon: 'pivotUp', range: [5, 80], step: 1,
      text: c => [`抬头 [[${deg(c.v)}]] 时通过下环。继续抬头保持该环的视线距离；0° 平视始终经过中环。`, ...orbitText(c)],
    },
    'GinkgoCameraParams.ArmLength': {
      icon: 'arm', range: [40, 400], step: 1, preview: 'framing', figure: '构图实时投影：悬臂长度',
      text: c => {
        const p = c.params();
        const ratio = (c.d * Math.tan(p.FieldOfView * Math.PI / 360)) / (c.v * Math.tan(p.FieldOfView * Math.PI / 360));
        return [
          '平视时从跟随点沿视线反方向退开的距离 $L$，也是三环轨道的**中环半径**，不含肩部偏移。上、下环按此值同比缩放；关闭三环时所有俯仰角都使用该距离。臂长越短，角色占屏比越大，同样的肩部偏移也会形成更大的侧向让位角。',
          `在固定视场下，角色占屏幕高度的比例近似反比于 $L\\tan(\\theta/2)$：臂长由默认 [[${cm(c.d)}]] 调至当前 [[${cm(c.v)}]] 时，角色屏幕高度约为默认构图的 [[${fmt(ratio * 100, 0)}%]]。`,
          `动态叠加关系：滑铲加速段「滑铲的臂长」在此基准上叠加 [[${signed(c.modifierValue('SlideArm'), 0)} cm]]（与视场 ${signed(c.modifierValue('SlideFov'))}° 同步构成推拉变焦），墙体遮挡则在碰撞阶段临时压缩有效臂长。`,
        ];
      },
      tex: () => [{tex: 'h_{\\text{screen}} \\propto \\frac{1}{L\\,\\tan(\\theta / 2)}', note: 'h_screen：角色占屏高度比例；L：臂长；θ：视场角'}],
    },
    'GinkgoCameraParams.ShoulderOffsetRight': {
      icon: 'shoulderRight', range: [-150, 150], step: 1, preview: 'framing', figure: '构图实时投影：水平肩部偏移',
      text: c => {
        const p = c.params(), fr = M.framing(p, c.move);
        return [
          `相机在视角坐标系下沿右方向平移的距离 $s_r$，用于将角色躯干移向画面一侧以让出屏幕中央准星：当前胶囊中线位于画面宽度的 [[${pct(fr.centerX)}]]。设为负值即可切换为左肩构图，屏幕中心准星与弹道判定不受影响。`,
          `角色中线相对准星的水平偏离角约为 $\\alpha = \\arctan(s_r / L)$，当前为 [[${deg(fr.offsetAngle)}]]（基准臂长 ${cm(p.ArmLength)}）。臂长越短，相同横向偏移产生的让位角越大。`,
        ];
      },
      tex: () => [{tex: '\\alpha = \\arctan\\frac{s_r}{L}', note: 's_r 为横向肩部偏移，L 为当前臂长，α 为角色偏离准星的水平张角'}],
    },
    'GinkgoCameraParams.ShoulderOffsetUp': {
      icon: 'shoulderUp', range: [-100, 100], step: 1, preview: 'framing', figure: '构图侧视投影：垂直肩部偏移',
      text: c => [
        `相机在视角坐标系下沿上方向平移的距离 $s_u$（负值表示低于跟随支点）。当前为 [[${cm(c.v)}]]，相机高度略低于肩部支点，平视时呈现自然微仰或平齐肩背的观察视角。`,
        '与「跟随点偏移（上）」的几何区别：肩部上偏移位于旋转后的视角空间内，随抬头 / 低头俯仰一同倾斜摆动；而跟随点上偏移固定在世界竖直方向，决定相机俯仰环绕的球心高度。',
      ],
    },
    'GinkgoCameraParams.PivotOffsetForward': {
      icon: 'pivotForward', range: [-100, 100], step: 1, preview: 'framing', figure: '构图实时投影：跟随点前向偏移',
      text: () => ['跟随支点 $P$ 相对平滑胶囊中心沿水平视线前向的偏移量 $p_f$。由于相机俯仰时绕 $P$ 旋转，前移支点会将俯仰球心置于角色胸前，使低头俯视时相机自然向前探出。默认保持为 0。'],
    },
    'GinkgoCameraParams.PivotOffsetRight': {
      icon: 'pivotRight', range: [-100, 100], step: 1, preview: 'framing', figure: '构图实时投影：跟随点横向偏移',
      text: () => ['跟随支点 $P$ 沿水平视线右方的偏移量 $p_r$。与「肩部偏移（右）」的区别在于：本项直接平移俯仰旋转球心的位置，而肩部偏移仅平移相机镜头本身。默认保持为 0。'],
    },
    'GinkgoCameraParams.PivotOffsetUp': {
      icon: 'pivotUp', range: [-50, 150], step: 1, preview: 'framing', figure: '构图侧视投影：跟随点高度',
      text: c => [
        `跟随支点 $P$ 高出平滑胶囊中心的竖直距离 $p_u$：当前为 [[${cm(c.v)}]]，站立姿态下距地面约 [[${cm((c.move.capsuleHalfHeight || 88) + c.v)}]]（约齐肩颈高度）。数值越高，越接近从头顶上方俯瞰。`,
        '相机以该支点为三环轨道的中心，其高度影响大角度俯仰时角色在画面中的纵向位置。角色下蹲时胶囊半高缩减、中心下沉，整条轨道会随之下移。',
      ],
    },
    'GinkgoCameraParams.FieldOfView': {
      icon: 'fov', range: [50, 130], step: 0.5, preview: 'framing', figure: '构图实时投影：视场角与视锥宽度',
      text: c => {
        const fov = M.fov(c.v);
        return [
          `定义 **4:3 基准画幅下的水平视场角** $\\theta$：引擎输出 \`FMinimalViewInfo\` 时按 \`AspectRatio_MaintainYFOV\` 规则，先由 4:3 基准折算固定竖直视场角 $\\theta_v$，再随实际视口宽高比横向扩展（Hor+ 缩放）。当前设定 [[${deg(c.v)}]] 在 16:9 宽屏下对应水平视场 [[${deg(fov.horizontal)}]]、竖直视场 [[${deg(fov.vertical)}]]。`,
          `视场角越大，周边余光信息量与纵向速度感越强，但广角透视边缘拉伸也越显著。滑铲加速期间「滑铲的视场」在此基础上叠加 [[${signed(c.modifierValue('SlideFov'))}°]]，并配合缩臂保持角色主体大小恒定。`,
        ];
      },
      tex: () => [
        {tex: '\\theta_v = 2\\arctan\\!\\left(\\frac{3}{4}\\tan\\frac{\\theta}{2}\\right), \\qquad \\theta_{16:9} = 2\\arctan\\!\\left(\\frac{16}{9}\\tan\\frac{\\theta_v}{2}\\right)',
          note: 'Maintaining Y-FOV（Hor+）：保持纵向视场角不变，宽屏横向视野随宽高比自然展宽'},
      ],
    },
    'GinkgoCameraParams.Roll': {
      icon: 'roll', range: [-20, 20], step: 0.1, preview: 'framing', figure: '构图实时投影：画面侧倾角',
      text: c => [
        `绕相机视线前向轴的横滚角（向右顺时针倾斜为正），当前基准值为 [[${deg(c.v)}]]。侧倾仅旋转成像画面，不改变相机空间坐标与武器弹道射线（瞄准方向取侧倾前的控制器视角）。`,
        `静态基准通常保持 0°；滑铲期间由修改器动态注入 [[${signed(c.modifierValue('SlideRoll'))}°]]，后续墙跑等动作的外倾表现亦统一走此参数通道。`,
      ],
    },
    'GinkgoCameraParams.PostProcessWeight': {
      icon: 'post', range: [0, 1], step: 0.01, preview: null,
      off: () => '当前管线尚未绑定后处理材质',
      text: () => [
        '相机动态后处理效果（如高速暗角、边缘速度线）的归一化混合权重，与其余相机参数共用同一套修改器覆盖、增量与弹簧平滑管线。',
        '**当前未接入具体后处理资产**：相机管理器末端输出阶段仅提交位置、旋转与视场角，调节此数值暂不产生画面变化。',
      ],
    },

    // ----- 跟随
    'GinkgoFollowSettings.Omega': {
      icon: 'spring', range: [0, 30], step: 0.1, preview: 'followH', figure: '冲刺急起与急停：水平弹簧固有频率',
      text: c => {
        const s = M.critical(c.v);
        return [
          `水平平面临界阻尼跟随弹簧的固有角频率 $\\omega$：单位阶跃响应收敛至 50% 耗时 $1.68/\\omega$（当前 [[${sec(s.t50)}]]），收敛至 90% 耗时 $3.89/\\omega$（当前 [[${sec(s.t90)}]]）。`,
          '$\\omega$ 越大，相机咬合胶囊越紧，急停、撞墙与空中快速变向时的画面缓冲位移越小，但刚性顿挫感增强；$\\omega$ 越小过渡越柔顺，瞬态甩开幅度则由「水平最大滞后」兜底约束。设为 $\\le 0$ 时禁用平滑、直接刚性绑定胶囊中心。',
        ];
      },
      tex: () => [{tex: 'x(t) = 1 - (1 + \\omega t)\\,e^{-\\omega t}', note: '二阶临界阻尼系统（ζ = 1）的解析单位阶跃响应'}],
    },
    'GinkgoFollowSettings.Feedforward': {
      icon: 'feedforward', range: [0, 1], step: 0.01, preview: 'followH', figure: '冲刺急起与急停：速度前馈补偿',
      text: c => {
        const omega = c.abs('Follow.Omega'), v = c.move.sprintSpeed || 762;
        return [
          `水平速度前馈补偿系数 $k \\in [0, 1]$：将弹簧追踪点沿移动速度方向提前 $k\\cdot 2v/\\omega$，从而抵消匀速运动下的固有滞后。$k = 0$ 时以 ${fmt(v, 0)} cm/s 冲刺会产生 [[${fmt(2 * v / Math.max(omega, 1e-6), 0)} cm]] 稳态拖尾；当前 $k =$ [[${fmt(c.v, 2)}]]，稳态滞后压缩至 [[${fmt((1 - c.v) * 2 * v / Math.max(omega, 1e-6), 1)} cm]]。`,
          '前馈信号直接读取移动组件的物理速度向量而非胶囊坐标差分：因此上下台阶步进、蹲姿胶囊缩放或网络位置校正等纯几何瞬移不会污染速度项，避免前馈目标突发跳变。',
        ];
      },
      tex: () => [{tex: '\\Delta_{\\infty} = (1 - k)\\,\\frac{2v}{\\omega}', note: '匀速运动稳态滞后：k = 1 时精确归零'}],
    },
    'GinkgoFollowSettings.MaxLag': {
      icon: 'leash', range: [0, 400], step: 1, preview: 'followH', figure: '冲刺急起与急停：最大滞后半径钳制',
      text: c => [
        `水平平面内平滑跟随中心偏离真实胶囊中心的最大允许半径，当前为 [[${cm(c.v)}]]（$\\le 0$ 表示不设上限）。超出该圆形边界时，跟随点会被径向投影拉回边界圆周（仅修正位置、保留弹簧速度），防止超高速机动时角色脱离画面构图区。`,
        '在满额速度前馈（$k = 1$）下匀速奔跑不会触碰此边界，仅在高速撞墙急停或钩锁猛烈牵引等剧烈速度阶跃瞬间起保护作用。',
      ],
    },
    'GinkgoFollowSettings.VerticalOmega': {
      icon: 'spring', rotate: 90, range: [0, 30], step: 0.1, preview: 'followV', figure: '原地起跳与落地：竖直弹簧固有频率',
      text: c => {
        const s = M.critical(c.v);
        return [
          `竖直轴向临界阻尼跟随弹簧的固有角频率：数值越小，起跳腾空与台阶起伏时相机的纵向跟随越平缓柔和。当前 $\\omega =$ [[${fmt(c.v, 1)} rad/s]]，阶跃响应达 50% 需 [[${sec(s.t50)}]]、达 90% 需 [[${sec(s.t90)}]]。`,
        ];
      },
    },
    'GinkgoFollowSettings.VerticalFeedforward': {
      icon: 'feedforward', rotate: -90, range: [0, 1], step: 0.01, preview: 'followV', figure: '原地起跳与落地：竖直速度前馈',
      text: c => [
        `竖直方向的速度前馈系数，当前为 [[${fmt(c.v, 2)}]]。默认保持为 \`0\`：利用弹簧的自然滞后吸收跳跃抛物线的纵向起伏，使相机比角色略晚抬升、柔和追落，触地瞬间不会向下过冲。`,
        '若调高至 `1`，相机会紧咬角色竖直速度并在高速坠地急停瞬间因前馈动量向下跌穿地面基准高度再回弹（右侧预览图以红字标出落地过冲量）。',
      ],
    },
    'GinkgoFollowSettings.VerticalMaxLag': {
      icon: 'leash', range: [0, 300], step: 1, preview: 'followV', figure: '原地起跳与落地：竖直最大滞后区间',
      text: c => [`竖直方向允许偏离真实胶囊中心的最大距离上限（一维对称区间钳制），防止长距离高空坠落或竖直钩锁拉升时角色跑出屏幕上下边缘。当前为 [[${cm(c.v)}]]；$\\le 0$ 表示不限。`],
    },
    'GinkgoFollowSettings.FeedforwardFilterOmega': {
      icon: 'filter', range: [0, 30], step: 0.1, preview: 'followH', figure: '冲刺急起与急停：前馈速度低通滤波',
      text: c => [
        `对输入前馈通道的移动速度先施加一层二阶临界阻尼低通滤波，本项为其截止角频率（当前 [[${fmt(c.v, 1)} rad/s]]；$\\le 0$ 表示默认直通不滤波）。`,
        '启用低通可柔化速度阶跃瞬间前馈目标的跳变幅度，代价是空中急转变向或撞墙急停时瞬态滞后峰值增大、回中变慢。经过滤波的速度在帧内连续变化，管线按线性斜坡做帧内精确积分。',
      ],
    },

    // ----- 碰撞与叠加
    'GinkgoCameraConfig.ProbeRadius': {
      icon: 'probe', range: [0, 40], step: 0.5, preview: 'collision', figure: '墙体遮挡切入与离开：球体扫掠半径',
      text: c => [`防穿模球体扫掠（Sphere Sweep）的探测半径 $r$，当前为 [[${cm(c.v)}]]。半径越大，相机与墙壁表面保持的安全净空越充裕、贴墙转角时拉近越早；过小可能导致视锥近裁剪面切穿薄墙，过大则会在狭窄走廊中频繁压短臂长。`],
    },
    'GinkgoCameraConfig.PushOutOmega': {
      icon: 'pushOut', range: [0, 20], step: 0.1, preview: 'collision', figure: '墙体遮挡切入与离开：推远恢复弹簧',
      text: c => {
        const s = M.critical(c.v);
        return [
          `遮挡物移开后，压缩量向零回放的临界阻尼弹簧角频率 $\\omega_{\\text{out}}$：当前 [[${fmt(c.v, 1)} rad/s]] 对应释放 50% 耗时 [[${sec(s.t50)}]]、释放 90% 耗时 [[${sec(s.t90)}]]（设为 $\\le 0$ 时推远亦瞬时跳回）。`,
          '障碍物切入时的拉近压缩始终保持零延迟瞬时截断，不经过该弹簧，确保任何转速下镜头绝不穿入几何体内部。',
        ];
      },
    },
    'GinkgoCameraConfig.OverlayPivot': {
      icon: 'orbit', preview: 'pivot', figure: '同一震动角度下两种旋转支点的空间运动对比',
      text: () => [
        '指定叠加层旋转偏移（冲量弹簧、呼吸噪声）围绕的空间支点。**绕相机自身**（默认）：相机位置保持不动、仅原地偏转视线角，整幅画面连同角色背影一起震动，符合手持摄影机受冲击的直觉，且因碰撞后不再平移相机位置，贴墙时绝无穿模风险。',
        '**绕跟随点**：相机沿以跟随支点 $P$ 为球心的球面公转（弧长约为 $L \\times \\Delta\\theta$），画面中角色主体几乎静止而远景剧烈晃动；由于公转位移发生在防穿模检测之后，紧贴墙面时存在穿墙隐患。',
      ],
    },

    // ----- 第一人称倍镜（FGinkgoFirstPersonSettings）
    'GinkgoFirstPersonSettings.NearClipPlane': {
      icon: 'probe', range: [0.2, 10], step: 0.1, preview: null,
      text: c => [
        `相机进入第一人称光学倍镜观察期间临时覆写的视锥近裁剪面距离，当前为 [[${fmt(c.v, 1)} cm]]。`,
        '眼睛抵达瞄具出瞳时距目镜金属外圈与镜片仅有数厘米，引擎默认的 10 cm 近裁剪面会将整个目镜筒纵向剖开穿模；管线仅在第一人称过渡权重激活的帧内压低近裁剪面，退回越肩后立即恢复引擎默认值以保障远景深度缓冲（Z-Buffer）精度。',
      ],
    },
    'GinkgoFirstPersonSettings.HeadHideDistance': {
      icon: 'aim', range: [0, 100], step: 1, preview: null,
      text: c => [
        `第一人称开镜过渡途中，相机距瞄具出瞳小于该阈值时隐藏本地角色头部与颈部骨骼网格体，当前为 [[${cm(c.v)}]]。`,
        '瞄具出瞳位于持枪抵肩时的真实眼位（处于角色头部体积内部），相机从越肩滑入出瞳必然穿过头模。由于骨骼隐藏需等待下一帧蒙皮刷新生效，将该距离设为略大于头颅半径（考虑低帧率单帧最大滑行步长）可彻底避免切入瞬间闪现头部内壁。',
      ],
    },
    'GinkgoFirstPersonSettings.BoltClearance.X': {
      icon: 'pivotForward', range: [-30, 30], step: 0.5, unit: 'cm', label: () => '拉栓避让 · 前后 (X)', preview: null,
      text: c => [
        `第一人称开镜状态下执行拉栓动作时，视点沿瞄具光轴前向（X 轴，负值为向后退开）的峰值避让偏移，当前为 [[${signed(c.v, 1)} cm]]。`,
        '栓动步枪抬柄后拉时，右手手腕与手指会逼近至出瞳右后方仅数厘米处；若视点单纯向后退会直接撞入拉栓手腕内部，因此配合左上方偏移共同让出清晰的操作视野。偏移量随实时机件行程包络 `Weapon.Value.SightClearance` 平滑插值，压柄闭锁后精确归位出瞳。',
      ],
      tex: () => [
        {tex: 'K_{\\text{eye}} = P_{\\text{pupil}} + s_{\\text{bolt}} \\cdot R_{\\text{sight}}\\,(b_x,\\ b_y,\\ b_z)',
          note: 'b_x < 0 向光轴后方微退；s_bolt 取拉机柄抬起比例与枪栓后拉比例之较大值'},
      ],
    },
    'GinkgoFirstPersonSettings.BoltClearance.Y': {
      icon: 'shoulderRight', range: [-30, 30], step: 0.5, unit: 'cm', label: () => '拉栓避让 · 左右 (Y)', preview: null,
      text: c => [
        `拉栓期间视点沿瞄具局部右方（Y 轴，负值为向左偏头）的峰值避让偏移，当前为 [[${signed(c.v, 1)} cm]]。`,
        '右手握持拉机柄位于瞄具右侧，将视点适度向左偏离枪托贴腮位，可使手腕与前臂退出近距遮挡区，把拉栓手指自然留在画面右下方安全视距之外。',
      ],
    },
    'GinkgoFirstPersonSettings.BoltClearance.Z': {
      icon: 'shoulderUp', range: [-30, 30], step: 0.5, unit: 'cm', label: () => '拉栓避让 · 上下 (Z)', preview: null,
      text: c => [
        `拉栓期间视点沿瞄具局部上方（Z 轴，正值为抬头上移）的峰值避让偏移，当前为 [[${signed(c.v, 1)} cm]]。`,
        '配合向左微偏模拟射手拉栓时略微抬腮离开枪托的真实头部姿态：拉栓瞬间瞄具出瞳因偏离光轴自然呈现暗角遮蔽，待闭栓落锁瞬间视点平滑落回光轴中心并恢复清晰镜内视野。',
      ],
    },

    // ----- 参数修改器（通用说明；元素专属覆盖见下方 ELEMENTS / ITEMS 扩展）
    'GinkgoCameraParamModifier.bEnabled': {
      icon: 'power', preview: 'byGroup',
      text: () => ['修改器总开关：关闭时目标权重置 0 并按正常淡出时长平滑退场，便于在运行中实时盲测对比单个修改器对手感的贡献。'],
    },
    'GinkgoCameraParamModifier.bLatchOff': {
      icon: 'lock', preview: 'byGroup',
      text: () => [
        '单次周期失效锁死（Latch Off）：在同一轮状态标签持续成立期间，一旦连续量条件首次跌破阈值，修改器即进入锁定态——后续即便连续量再次回升越过阈值（如滑铲途中顺坡加速）也不再重新激活，直至外层状态标签结束才解锁复位。',
        '典型应用于「滑铲的视场」与「滑铲的臂长」，防止单次滑铲内因地形坡度起伏导致镜头反复推拉闪烁。',
      ],
    },
    'GinkgoCameraParamModifier.Blend': {
      icon: 'layers', preview: 'byGroup',
      text: () => [
        '**增量（Delta）**：在已完成覆盖计算的参数基础上线性累加 $(1 - m)\\cdot w \\cdot \\Delta$，其中 $m \\in [0, 1]$ 为屏蔽该增量类别的所有生效覆盖中的最大权重（例如越肩开镜淡入至 50% 时，速度类视场与臂长增量同步衰减 50%）。',
        '**覆盖（Override）**：从基准参数 $P_0$ 出发，按优先级由低到高逐层执行线性插值 $P \\gets P + w\\cdot(v - P)$，确保多层覆盖同时混入混出时输出依然连续无跳变。管线固定先解算覆盖、后叠加增量。',
      ],
      tex: () => [
        {tex: 'P_{\\text{over}}^{(i)} = P_{\\text{over}}^{(i-1)} + w_i\\bigl(v_i - P_{\\text{over}}^{(i-1)}\\bigr), \\qquad P = P_{\\text{over}} + \\sum_{j} (1 - m_j)\\,w_j\\,\\Delta_j',
          note: '覆盖按优先级升序逐层插值，增量按屏蔽系数 (1 − m_j) 加权求和'},
      ],
    },
    'GinkgoCameraParamModifier.Value.Value': {
      icon: 'value', preview: 'byGroup', step: 0.1,
      rangeFor: c => ({FieldOfView: [-30, 30], ArmLength: [-150, 150], Roll: [-15, 15], ShoulderOffsetRight: [-100, 100]}[c.group.param] || [-100, 100]),
      unitFor: c => UNITS[c.group.paramUnits] || '',
      label: c => c.groupBlend() === 'Override' ? '覆盖成' : '增量',
      text: c => [
        `作用于目标参数「${c.group.paramName || c.group.param}」的幅度：当前${c.groupBlend() === 'Override' ? '目标覆盖值' : '叠加增量'}为 [[${signed(c.v)}${UNITS[c.group.paramUnits] || ''}]]（默认 ${signed(c.d)}）。经修改器实时权重 $w \\in [0, 1]$ 缩放合成后，再送入角频率 $\\omega =$ [[${fmt(c.springOmega(c.group.param), 1)}]] 的参数弹簧平滑。`,
      ],
    },
    'GinkgoCameraParamModifier.Priority': {
      icon: 'priority', range: [-10, 10], step: 1, preview: 'byGroup',
      off: c => c.groupBlend() !== 'Override' ? '增量模式不使用覆盖优先级' : '',
      text: () => ['控制多个「覆盖」修改器作用于同一参数时的叠放次序：管线按优先级从低到高依次插值，高优先级覆盖在满权重时完全主导最终结果。对增量修改器无效。'],
    },
    'GinkgoCameraParamModifier.BlendInTime': {
      icon: 'rampIn', range: [0, 2], step: 0.01, preview: 'byGroup',
      text: c => [
        `激活条件成立后，修改器权重从 0 线性爬升至 1 所需的时长，当前为 [[${sec(c.v)}]]（设为 0 表示当帧瞬时置满，后续平滑完全交由参数弹簧）。管线根据条件跨越阈值的精确亚帧时刻起算斜坡，结果严格帧率无关。`,
      ],
    },
    'GinkgoCameraParamModifier.BlendOutTime': {
      icon: 'rampOut', range: [0, 2], step: 0.01, preview: 'byGroup',
      text: c => [`激活条件失效或被锁死后，修改器权重从当前值线性回落至 0 的满程时长，当前为 [[${sec(c.v)}]]。中途若条件翻转，权重将从当前位置平滑折返而不跳变。`],
    },
    'GinkgoCameraParamSpring.Omega': {
      icon: 'spring', range: [0, 40], step: 0.1, preview: 'spring', figure: '参数弹簧的单位阶跃响应曲线',
      label: c => SPRING_NAMES[c.group.param] || `${c.group.paramName || c.group.param}的弹簧`,
      text: c => {
        const s = M.critical(c.v);
        return [
          `参数「${c.group.paramName || c.group.param}」在完成覆盖与增量合成后通过的二阶临界阻尼弹簧角频率：当前 $\\omega =$ [[${fmt(c.v, 1)} rad/s]]，阶跃收敛至 50% 需 [[${sec(s.t50)}]]、收敛至 90% 需 [[${sec(s.t90)}]]（$\\le 0$ 时直通不平滑）。`,
          '保持视场弹簧与臂长弹簧的 $\\omega$ 数值相等，可确保滑铲推拉变焦在混入与混出的每一帧都同步成比例演进，角色画面占比全程不发生呼吸胀缩。',
        ];
      },
      tex: () => [{tex: 'x(t) = 1 - (1 + \\omega t)\\,e^{-\\omega t}'}],
    },

    // ----- 叠加效果（UGinkgoCameraOverlayEffect 基类与各子类）
    'GinkgoCameraOverlayEffect.bEnabled': {
      icon: 'power', preview: 'byGroup',
      text: () => ['叠加效果总开关：关闭时立即清空内部弹簧状态并输出零偏移，便于在运行中做单通道对比验证。'],
    },
    'GinkgoCameraOverlayEffect.FirstPersonScale': {
      icon: 'scope', range: [0, 1], step: 0.05, preview: 'byGroup',
      text: c => [
        `过渡至第一人称光学倍镜状态时，该叠加层在相机画面上保留的输出比例，当前为 [[${fmt(c.v, 2)}]]（随第一人称进度 $p_{\\text{fp}} \\in [0, 1]$ 从 1 线性插值至该值）。`,
        '例如开镜呼吸晃动在第一人称下设为 `0`（将画面晃动平滑收掉，改由枪身与瞄具实体摆动）；而开火相机后坐保持为 `1`，使抵肩射击时的视野冲击感与越肩一致。',
      ],
      tex: () => [
        {tex: 's_{\\text{fp}} = 1 + \\bigl(k_{\\text{fp}} - 1\\bigr)\\,p_{\\text{fp}}',
          note: 'p_fp ∈ [0, 1] 为第一人称出瞳过渡进度，k_fp 为本项保留比例'},
      ],
    },
    'GinkgoCameraOverlayEffect.bSightFollows': {
      icon: 'aim', preview: 'byGroup',
      text: () => [
        '启用后，在第一人称光学倍镜状态下，持枪骨骼与瞄具光轴将同步叠加本层的旋转偏转量（由武器组件通过 `PredictSightFollow` 按同帧输入提前推演对齐）。',
        '主要用于「枪的后坐」：击发瞬间瞄具镜筒、十字分划与镜外画面作为整体一同上跳，弹道始终对齐分划中心，避免眼睛贴在出瞳上时画面单独自跳导致镜框黑影错位遮挡视线。',
      ],
    },
    'GinkgoCameraImpulseShake.MinMagnitude': {
      icon: 'magnitude', preview: 'byGroup',
      rangeFor: c => (c.group.event === 'Movement.Event.Footstep' ? [0, 2] : [0, 500]),
      text: c => [
        `事件触发的强度绝对值门槛 $|M| \\ge M_{\\min}$：低于该值的事件直接忽略，设为 0 则无条件响应。当前为 [[${fmt(c.v, 2)}]]。`,
        c.group.event === 'Movement.Event.Footstep'
          ? '脚步动画同步事件的标准强度为 $\\pm 1$（左脚 $+1$、右脚 $-1$），因此若将门槛调至 1 以上将完全屏蔽脚步震动。'
          : '滑铲开始事件的强度值等于本次滑铲实际获得的起步加速（cm/s）：冷却疲劳滑铲强度为 0，设为 1 即可干净过滤无加速的疲劳滑铲。',
      ],
    },
    'GinkgoCameraImpulseShake.Impulse.Pitch': {
      icon: 'axisPitch', range: [-200, 200], step: 0.5, unit: '°/s', label: () => '冲量 · 俯仰', preview: 'byGroup',
      text: c => {
        const r = M.damped(c.v, c.get('Omega'), c.get('Zeta'));
        return [
          `事件触发瞬间直接叠加至俯仰角速度的瞬时冲量 $J_{\\text{pitch}}$（正值为抬头，负值为低头下沉）。当前 [[${fmt(c.v, 1)}°/s]] 产生的首峰偏转为 [[${deg(r.peak)}]]（位于触发后 [[${ms(r.tPeak)}]]）。`,
          '固定冲量保证同一动作每次触发的肌肉反馈一致。由于欠阻尼首峰幅度近似正比于 $J / \\omega$，若希望仅改变震动软硬频率而保持画面最大偏转角不变，应将冲量随 $\\omega$ 同比例缩放。',
        ];
      },
      tex: () => [{tex: 'x(t) = \\frac{J}{\\omega_d}\\,e^{-\\zeta\\omega t}\\sin(\\omega_d t), \\qquad t_{\\text{peak}} = \\frac{\\arctan(\\omega_d / (\\zeta\\omega))}{\\omega_d}',
        note: 'J：瞬时角速度冲量（°/s）；ω_d = ω√(1 − ζ²)'}],
    },
    'GinkgoCameraImpulseShake.Impulse.Yaw': {
      icon: 'axisYaw', range: [-200, 200], step: 0.5, unit: '°/s', label: () => '冲量 · 偏航', preview: 'byGroup',
      text: c => {
        const r = M.damped(c.v, c.get('Omega'), c.get('Zeta'));
        return [`事件触发瞬间叠加至水平偏航角速度的冲量 $J_{\\text{yaw}}$（向右偏转为正）。当前 [[${fmt(c.v, 1)}°/s]] 对应首峰幅度 [[${deg(r.peak)}]]；开启「负强度时左右镜像」且事件强度为负时符号自动取反。`];
      },
      tex: () => [{tex: 'x(t) = \\frac{J}{\\omega_d}\\,e^{-\\zeta\\omega t}\\sin(\\omega_d t)'}],
    },
    'GinkgoCameraImpulseShake.Impulse.Roll': {
      icon: 'axisRoll', range: [-200, 200], step: 0.5, unit: '°/s', label: () => '冲量 · 侧倾', preview: 'byGroup',
      text: c => {
        const r = M.damped(c.v, c.get('Omega'), c.get('Zeta'));
        return [`事件触发瞬间叠加至画面横滚角速度的冲量 $J_{\\text{roll}}$（向右顺时针侧倾为正）。当前 [[${fmt(c.v, 1)}°/s]] 对应首峰幅度 [[${deg(r.peak)}]]；开启「负强度时左右镜像」且事件强度为负时左右对称翻转。`];
      },
      tex: () => [{tex: 'x(t) = \\frac{J}{\\omega_d}\\,e^{-\\zeta\\omega t}\\sin(\\omega_d t)'}],
    },
    'GinkgoCameraImpulseShake.bMirrorOnNegative': {
      icon: 'mirror', preview: 'byGroup',
      text: () => ['当输入事件的强度符号为负时，自动将偏航（Yaw）与侧倾（Roll）冲量取反，俯仰（Pitch）方向保持不变——用于让左脚（$+1$）与右脚（$-1$）落地产生左右交替对称的步伐震动。'],
    },
    'GinkgoCameraImpulseShake.MinInterval': {
      icon: 'interval', range: [0, 0.5], step: 0.005, preview: 'byGroup',
      text: c => [
        `相邻两次冲量响应的最小物理时间间隔（基于事件精确亚帧时间戳判定，与帧率无关），间隔小于 [[${sec(c.v)}]] 的后续事件将被去抖丢弃；设为 0 表示全部接收。`,
        '在冲刺脚步震动中，滑铲起身衔接奔跑的过渡帧内左右脚可能在 20 ms 内相继擦地，设置 0.10 s 去抖窗口（小于正常冲刺步频周期 0.21 s）可防止两脚冲量同相叠加成突兀的双倍下沉。',
      ],
    },
    'GinkgoCameraImpulseShake.FieldOfViewImpulse': {
      icon: 'fov', range: [-100, 100], step: 0.5, unit: '°/s', preview: 'byGroup',
      text: c => [`事件触发瞬间注入视场角弹簧的变化率冲量（°/s），与三维旋转共用同一组 $\\omega, \\zeta$。当前为 [[${fmt(c.v, 1)}°/s]]；设为 0 时不产生视场脉动。`],
    },
    'GinkgoCameraImpulseShake.Omega': {
      icon: 'spring', range: [0, 150], step: 0.1, preview: 'byGroup',
      text: c => {
        const r = M.damped(1, c.v, c.get('Zeta'));
        return [
          `欠阻尼弹簧的无阻尼固有角频率 $\\omega$：数值越大，受击震动的往复频率越高、质感越短促硬朗。有阻尼实际振荡频率为 $f = \\omega\\sqrt{1-\\zeta^2}/(2\\pi)$，当前为 [[${fmt(r.freq, 1)} Hz]]。`,
          r.freq > 15
            ? '**奈奎斯特混叠提示**：当前频率已超过 15 Hz（30 fps 采样率的奈奎斯特极限），在低帧率运行环境下高频振荡会发生欠采样混叠并退化为不规则慢速抖动，建议控制在 15 Hz 以内。'
            : '采样抗混叠边界：保持实际振荡频率在 15 Hz 以下，可确保即使在 30 fps 帧率下也不会越过奈奎斯特频率产生视觉混叠。',
        ];
      },
      tex: () => [{tex: 'f = \\frac{\\omega\\sqrt{1 - \\zeta^2}}{2\\pi}'}],
    },
    'GinkgoCameraImpulseShake.Zeta': {
      icon: 'damping', range: [0, 1], step: 0.01, preview: 'byGroup',
      text: c => {
        const r = M.damped(1, c.get('Omega'), c.v);
        return [
          `二阶系统的阻尼比 $\\zeta \\in [0, 1]$：$\\zeta = 0$ 为无衰减简谐振荡，$\\zeta$ 越接近 1 余振收敛越快（$\\zeta = 1$ 为临界阻尼单峰无回摆）。振幅包络 $e^{-\\zeta\\omega t}$ 衰减至 2% 所需整定时间为 $\\ln 50 / (\\zeta\\omega)$，当前为 [[${ms(r.settle)}]]。`,
        ];
      },
      tex: () => [{tex: 't_{2\\%} = \\frac{\\ln 50}{\\zeta\\,\\omega}'}],
    },

    // ----- 事件携带的冲量（UGinkgoCameraCarriedShake）
    'GinkgoCameraCarriedShake.Scale': {
      icon: 'magnitude', range: [0, 3], step: 0.05,
      text: c => [`对事件携带冲量施加的全局幅度缩放系数：\`1.00\` 表示完全按武器资产定义输出，\`0\` 表示静默屏蔽。当前为 [[${fmt(c.v, 2)}]]，仅作用于后续新接收的事件，已在衰减中的弹簧实例不受影响。`],
    },

    // ----- 噪声 × 包络（UGinkgoCameraNoiseSway）
    'GinkgoCameraNoiseSway.ScalarForZero': {
      icon: 'envelope', range: [0, 1], step: 0.01, preview: 'envelope', figure: '包络映射曲线：上下文连续量 → 幅度权重',
      text: c => [`驱动包络的上下文连续量（当前绑定：${tagName(c.group.envelopeScalar) || '未指定，激活时恒为 1'}）对应零包络输出的起点阈值 $s_0$，当前为 [[${fmt(c.v, 2)}]]。`],
      tex: () => [{tex: 'e = \\mathrm{clamp}\\!\\left(\\frac{s - s_0}{s_1 - s_0},\\ 0,\\ e_{\\max}\\right)'}],
    },
    'GinkgoCameraNoiseSway.ScalarForOne': {
      icon: 'envelope', range: [0, 1], step: 0.01, preview: 'envelope', figure: '包络映射曲线：上下文连续量 → 幅度权重',
      text: c => [`上下文连续量映射至单位满包络（$e = 1$）的基准值 $s_1$，当前为 [[${fmt(c.v, 2)}]]；区间 $[s_0, s_1]$ 内线性插值，超出部分线性外推并最终截断至 $[0, e_{\\max}]$。`],
      tex: () => [{tex: 'e = \\mathrm{clamp}\\!\\left(\\frac{s - s_0}{s_1 - s_0},\\ 0,\\ e_{\\max}\\right)'}],
    },
    'GinkgoCameraNoiseSway.MaxEnvelope': {
      icon: 'ceiling', range: [0, 3], step: 0.01, preview: 'envelope', figure: '包络映射曲线：上下文连续量 → 幅度权重',
      text: c => [`包络权重的最大饱和上限 $e_{\\max}$，当前为 [[${fmt(c.v, 2)}]]。当设为大于 1 时，允许连续量超越 $s_1$ 后继续线性放大晃动幅度（例如将速度映射为移动晃动时，冲刺速度可获得超额包络）。`],
    },
    'GinkgoCameraNoiseSway.EnvelopeOmega': {
      icon: 'spring', range: [0, 20], step: 0.1, preview: 'envelope', figure: '开镜过程中的包络响应曲线',
      text: c => [`包络目标值所经临界阻尼平滑弹簧的角频率 $\\omega_e$，用于令持续晃动柔和渐起渐收；设为 $\\le 0$ 时跳过平滑直接采用映射值。当前为 [[${fmt(c.v, 1)}]]（开镜进度本身已经过两端缓动，无需二次弹簧滤波）。`],
    },
    'GinkgoCameraNoiseSway.Pitch.Amplitude': {
      icon: 'amplitude', range: [0, 2], step: 0.01, label: () => '俯仰 · 幅度', preview: 'byGroup',
      text: c => [`单位包络（$e = 1$）下俯仰轴的最大偏转角幅度 $A$（正值为抬头），当前为 [[${deg(c.v)}]]；设为 0 时关闭该轴晃动。由于三谐波权重之和严格等于 $0.6 + 0.3 + 0.1 = 1$，合成波形极值绝不超出 $A$。`],
      tex: () => [{tex: 'n(t) = A\\sum_{k=1}^{3} w_k \\sin\\bigl(2\\pi f r_k t + \\phi_k\\bigr)',
        note: '黄金比例无理数倍频 r = (1, φ, φ²) ≈ (1, 1.618, 2.618)，使波形准周期演化而不显现机械重复；权重 w = (0.6, 0.3, 0.1)'}],
    },
    'GinkgoCameraNoiseSway.Pitch.Frequency': {
      icon: 'wave', range: [0, 3], step: 0.01, label: () => '俯仰 · 主频率', preview: 'byGroup',
      text: c => [`俯仰轴基波的主频率 $f$（Hz）：频率越低越接近缓慢深沉的呼吸起伏，频率越高越接近肌肉细颤。当前为 [[${fmt(c.v, 2)} Hz]]（基波周期 ${c.v > 0 ? sec(1 / c.v) : '—'}）。`],
    },
    'GinkgoCameraNoiseSway.Yaw.Amplitude': {
      icon: 'amplitude', rotate: 90, range: [0, 2], step: 0.01, label: () => '偏航 · 幅度', preview: 'byGroup',
      text: c => [`单位包络下水平偏航轴的最大偏转角幅度（向右为正），当前为 [[${deg(c.v)}]]。`],
    },
    'GinkgoCameraNoiseSway.Yaw.Frequency': {
      icon: 'wave', label: () => '偏航 · 主频率', range: [0, 3], step: 0.01, preview: 'byGroup',
      text: c => [`水平偏航轴基波的主频率，当前为 [[${fmt(c.v, 2)} Hz]]（基波周期 ${c.v > 0 ? sec(1 / c.v) : '—'}）。将偏航与俯仰设为互不成简单整数比的频率，可在平面上走出自然的非闭合利萨如漂移轨迹。`],
    },
    'GinkgoCameraNoiseSway.Roll.Amplitude': {
      icon: 'axisRoll', range: [0, 2], step: 0.01, label: () => '侧倾 · 幅度', preview: 'byGroup',
      text: c => [`单位包络下画面横滚轴的最大偏转角幅度（向右顺时针为正），当前为 [[${deg(c.v)}]]。`],
    },
    'GinkgoCameraNoiseSway.Roll.Frequency': {
      icon: 'wave', label: () => '侧倾 · 主频率', range: [0, 3], step: 0.01, preview: 'byGroup',
      off: c => c.get('Roll.Amplitude') <= 0 ? '当前侧倾幅度为 0' : '',
      text: c => [`横滚侧倾轴基波的主频率，当前为 [[${fmt(c.v, 2)} Hz]]。`],
    },
    'GinkgoCameraNoiseSway.Seed': {
      icon: 'dice', range: [0, 20], step: 1, preview: 'byGroup',
      text: c => [`确定性初相位的随机种子，当前为 [[${fmt(c.v, 0)}]]。管线按（种子、轴序号、谐波分量）索引二维低差异序列（$R_2$ Sequence）生成均匀散布的初相位 $\\phi_k$：切换种子可在保持频谱与幅度分布不变的前提下更换漂移轨迹形态。`],
    },
    'GinkgoCameraNoiseSway.bMovesSight': {
      icon: 'scope', preview: 'byGroup',
      text: () => [
        '控制第一人称光学倍镜下呼吸噪声的作用载体：开启后，系统将同一确定性噪声采样值直接施加至枪械与瞄具光轴方向（`SampleSightSway`），使镜筒与十字分划在画面中轻微浮动，真实弹道落点严格跟随分划中心；相机画面本身的晃动则配合 `FirstPersonScale = 0` 平滑隐退。',
        '在第三人称越肩视角下，本开关不改变原有行为（仍仅摇晃相机画面，准星投影在稳定枪口指向处）。',
      ],
    },

    // ----- 连续量条件（合并比较符与阈值为单行交互，见 page.js）
    Condition: {
      icon: 'gauge', preview: 'condition', figure: '连续量激活门限与满足区间示意',
      text: c => {
        const cond = c.condition();
        if (!cond) return [];
        return [
          `连续量激活判据：要求角色上下文中的「${cond.scalarName}」${cond.atLeast ? '不小于（≥）' : '小于（<）'}${cond.thresholdScalar ? `动态参考量「${cond.thresholdName}」（当前为 [[${fmt(cond.threshold, 1)}]]，自动同步移动配置，修改跑速无需重调相机）` : `常量阈值 [[${fmt(cond.threshold, 2)}]]`}。修改器须同时满足外层状态标签查询与全部连续量条件方可激活。`,
          '亚帧跨阈值插值：管线利用前后两帧的条件余量（Margin）线性反推出帧内跨越零点的精确时刻，权重斜坡从该亚帧时刻起算淡入或淡出，消除帧率离散采样带来的触发抖动。',
        ];
      },
    },
  };

  // 滑铲臂长幅值专属预览：直接展示推拉变焦双画面对比（其余时间与门限项仍展示滑铲时间线）
  ITEMS['SlideArm:GinkgoCameraParamModifier.Value.Value'] = {...ITEMS['GinkgoCameraParamModifier.Value.Value'], preview: 'dolly', figure: '推拉变焦对比：常态构图与滑铲瞬间'};

  // 开镜视场后置覆盖专属说明：位于参数弹簧之后，淡入淡出时长与缓动曲线直接决定最终视觉过渡形态
  const ADS_BLEND = {preview: 'blendCurve', figure: '越肩开镜视场覆盖权重的混入与混出曲线'};
  ITEMS['AdsFov:GinkgoCameraParamModifier.BlendInTime'] = {
    ...ITEMS['GinkgoCameraParamModifier.BlendInTime'], ...ADS_BLEND,
    text: c => [
      `按下开镜后，视场覆盖权重沿淡入曲线从 0 演进至 1 的精确到位时长，当前为 [[${sec(c.v)}]]（0 表示瞬时切换）。由于该覆盖置于参数弹簧之后，视场角在此时间走完时即 100% 到位，不受视场弹簧二次拖尾影响。`,
      '可与武器数据资产中的抬枪时间（如 98k 的 `AdsTime = 0.30 s`）对齐调校，决定镜头缩放与持枪动画的先后节奏。',
    ],
  };
  ITEMS['AdsFov:GinkgoCameraParamModifier.BlendOutTime'] = {
    ...ITEMS['GinkgoCameraParamModifier.BlendOutTime'], ...ADS_BLEND,
    text: c => [`松开或退出开镜后，视场覆盖权重沿淡出曲线从 1 回落至 0 的时长，当前为 [[${sec(c.v)}]]。开收镜中途反转输入时，权重从当前比例无缝折返，不会发生视场跳变。`],
  };
  ITEMS['AdsFov:GinkgoCameraParamModifier.bLatchOff'] = {
    ...ITEMS['GinkgoCameraParamModifier.bLatchOff'],
    off: () => '开镜视场不含连续量门限条件，无需锁死机制',
  };
  const CURVE_TEXT = '**线性**：匀速过渡；**两头缓**：对称 S 形曲线 $p^k / (p^k + (1-p)^k)$；**先快后慢**：缓出曲线 $1 - (1-p)^k$，按下瞬间立即响应并在到位前柔和收住；**先慢后快**：缓入曲线 $p^k$。';
  const AFTER_SPRINGS_ONLY = group => group.element === 'AdsFov';
  ITEMS['GinkgoCameraParamModifier.bAfterSprings'] = {showIn: () => false};
  ITEMS['GinkgoCameraParamModifier.BlendInCurve'] = {
    icon: 'rampIn', ...ADS_BLEND, showIn: AFTER_SPRINGS_ONLY,
    text: () => ['选择进入越肩开镜时视场覆盖权重的归一化缓动曲线 $S(p)$。', CURVE_TEXT],
    tex: () => [{tex: 'S_{\\text{out}}(p) = 1 - (1 - p)^k, \\qquad S_{\\text{smooth}}(p) = \\frac{p^k}{p^k + (1 - p)^k}', note: 'p ∈ [0, 1] 为归一化时间进度，k ≥ 1 为曲线强度'}],
  };
  ITEMS['GinkgoCameraParamModifier.BlendOutCurve'] = {
    icon: 'rampOut', ...ADS_BLEND, showIn: AFTER_SPRINGS_ONLY,
    text: () => ['选择退出越肩开镜时视场覆盖权重的回落缓动曲线（$w = 1 - S(p)$）。', CURVE_TEXT],
  };
  ITEMS['GinkgoCameraParamModifier.BlendCurveStrength'] = {
    icon: 'gauge', range: [1, 6], step: 0.1, ...ADS_BLEND, showIn: AFTER_SPRINGS_ONLY,
    text: c => [`控制缓动曲线的弯曲指数 $k$（$k = 1$ 时退化为匀速线性），当前为 [[${fmt(c.v, 1)}]]：在「先快后慢」模式下，时间推进至 25% 时视场覆盖权重已达 [[${fmt(M.blendCurve(0.25, 'EaseOut', c.v) * 100, 0)}%]]。`],
  };

  // ===== 配置行到目录与小节的路由规则 =====

  const lastField = row => (row.chain.length ? row.chain[row.chain.length - 1].field : '');
  const firstField = row => (row.chain.length ? row.chain[0].field : '');

  /** 生成通用说明查询键：「首段声明结构.字段链」（剥离数组下标），如 GinkgoCameraParamModifier.Activation.Conditions.Threshold */
  const keyOf = row => (row.chain.length ? `${row.chain[0].owner}.${row.chain.map(s => s.field).join('.')}` : row.path);

  const isCondition = row => row.chain.some(s => s.field === 'Conditions');

  /** 返回 {dir, cat}：cat 为固定分类 id（结构体属性）或分组路径（修改器 / 叠加效果数组元素） */
  const place = (row, group) => {
    if (group.path === 'Orbit') return {dir: 'framing', cat: 'orbit'};
    if (group.path === 'Shoulder') return {dir: 'collision', cat: row.kind === 'rays' ? 'rays' : 'behavior'};
    if (group.path === 'BaseParams' || group.path === 'Follow') {
      const dir = DIRECTORIES.find(d => d.id === (group.path === 'BaseParams' ? 'framing' : 'follow'));
      const cat = dir.categories.find(category => (category.fields || []).includes(firstField(row)));
      return {dir: dir.id, cat: cat ? cat.id : dir.categories[0].id};
    }
    if (group.path === 'FirstPerson') {
      return {dir: 'aim', cat: 'firstPerson'};
    }
    if (group.kind === 'config') {
      for (const dir of DIRECTORIES) {
        for (const category of dir.categories || []) {
          if ((category.fields || []).includes(firstField(row))) return {dir: dir.id, cat: category.id};
        }
      }
      return {dir: 'other', cat: 'config'};
    }
    if (group.property === 'ParamSprings') return {dir: 'general', cat: 'springs'};
    const element = ELEMENTS[group.element];
    if (element) return {dir: element.dir, cat: group.path};
    return {dir: 'other', cat: group.path};
  };

  globalThis.CameraCatalog = {
    TAGS, tagName, UNITS, IMAGES, ROOT_IMAGE, DIRECTORIES, ELEMENTS, ITEMS, SPRING_NAMES, keyOf, lastField, firstField, isCondition, place,
  };
})();
