#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

/**
 * @enum EGinkgoResponseCurve : 手柄视角的响应曲线（摇杆偏转 → 转速占满偏转速的比例）
 */
enum class EGinkgoResponseCurve : uint8
{
	/** 偏转多少就是多少 */
	Linear,
	/** 指数 x^强度：小幅度慢、大幅度快，准心附近更细 */
	Power,
	/** S 形 x^强度 / (x^强度 + (1 − x)^强度)：两头慢、中间快，一半的偏转还是一半的转速 */
	SCurve,
};

/**
 * @enum EGinkgoHoldMode : 冲刺、蹲、开镜的操作方式。每个键位各选各的：Ctrl 按住蹲、C 切换蹲
 */
enum class EGinkgoHoldMode : uint8
{
	/** 按住时才有，松开就没了 */
	Hold,
	/** 按一下开，再按一下关 */
	Toggle,
};

/**
 * @enum EGinkgoInputDirectory : 设置界面的目录（控制器、鼠标键盘、辅助功能）：恢复默认只管当前目录，所以每一项属于哪个目录是数据的一部分
 */
enum class EGinkgoInputDirectory : uint8
{
	Gamepad,
	KeyboardMouse,
	/** 跨输入设备共用的辅助行为，恢复默认时与键位和灵敏度分开处理 */
	Accessibility,
};

/**
 * @enum EGinkgoKeyAction : 能改键的动作。前四个的顺序同 FGinkgoDirectionKeys（右、左、前、后）
 */
enum class EGinkgoKeyAction : uint8
{
	MoveRight,
	MoveLeft,
	MoveForward,
	MoveBack,
	Jump,
	Crouch,
	Sprint,
	Interact,
	Aim,
	Fire,
	Reload,
	Slot1,
	Slot2,
	Sidearm,
	Holster,
	SwitchWeapon,
	DropWeapon,
	/** 手动切换左右肩相机；键盘默认 V，手柄默认未绑定 */
	SwitchShoulder,

	/** 左手投掷；按下取出，释放后投出。 */
	Flashbang,
	Count,
};

/**
 * @enum EGinkgoKeySlot : 每个动作的两个键位：主键与辅助键
 */
enum class EGinkgoKeySlot : uint8
{
	Primary,
	Secondary,
	Count,
};

/**
 * @enum EGinkgoKeyCheck : 一个键能不能绑到某个目录的键位上
 */
enum class EGinkgoKeyCheck : uint8
{
	Ok,
	/** 另一种设备的键：键盘的键不能绑到手柄的键位上 */
	WrongDevice,
	/** 留给界面与调试的键：Esc、F1–F12、Delete、Win、`，手柄的菜单键与视图键 */
	Reserved,
	/** 不是按钮：摇杆的方向、鼠标的移动 */
	NotAButton,
};

/**
 * @struct FGinkgoKeyBinding : 一个键位现在是什么
 */
struct FGinkgoKeyBinding
{
	/** 没绑时是 EKeys::Invalid */
	FKey Key;
	/** 只有冲刺、蹲、开镜看它 */
	EGinkgoHoldMode Mode = EGinkgoHoldMode::Hold;
};

/**
 * @struct FGinkgoInputSettings : 玩家的输入设置，只是数
 * 默认值就是这里的初始值，定成做设置界面之前的手感（Docs/Design/Control.md「默认值」）：改了默认值，手感就变了。
 * 名字、单位、说明在页面的 Content/UI/Settings/catalog.js，范围与存盘的名字在 GetSettingDefs：打包的游戏里没有 UPROPERTY 的元数据，所以都不走反射。
 */
struct CASEGINKGO_API FGinkgoInputSettings
{
	/** 键位填成 GetKeyBindingDefs 的默认值 */
	FGinkgoInputSettings();

	// ===== 控制器 =====

	/** 右摇杆水平满偏时的转速（度 / 秒） */
	float GamepadYawSpeed = 150.f;
	/** 右摇杆竖直满偏时的转速（度 / 秒） */
	float GamepadPitchSpeed = 150.f;
	/** 开镜时转速乘的倍率，随开镜进度过渡 */
	float GamepadAdsScale = 1.f;
	/** 开镜时再按视场换算：同样的偏转，画面上走得和不开镜时一样快 */
	bool bGamepadAdsMatchFov = false;
	/**
	 * 倍镜灵敏度（装配独立光学瞄具的第一人称开镜，Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）：
	 * 设为 1 时按瞄具光学倍率折算，镜内目标像在屏幕上的移动速度与腰射一致；与上方越肩开镜灵敏度相互独立
	 */
	float GamepadScopeScale = 1.f;
	/** 手柄视角响应曲线类型；默认 S 形，微调与大幅转身兼顾 */
	EGinkgoResponseCurve GamepadCurve = EGinkgoResponseCurve::SCurve;

	/** 响应曲线的强度：1 即退化为线性，数值越大准星附近微调分辨率越高 */
	float GamepadCurveStrength = 2.f;

	/** 反转上下：开启时向上推杆对应低头俯视，关闭时向上推杆对应抬头仰视 */
	bool bGamepadInvertPitch = true;

	bool bGamepadInvertYaw = false;

	/** 左右摇杆互换：左摇杆转视角，右摇杆移动 */
	bool bGamepadSwapSticks = false;

	/** 视角摇杆的内死区（半径占满偏的比例）：小了摇杆回不到中心时视角会自己漂，大了小幅度瞄准没反应 */
	float GamepadLookInner = 0.2f;

	/** 视角摇杆的外圈：推到这里就算满偏。小了更快到满速，大了摇杆的行程全用上 */
	float GamepadLookOuter = 1.f;

	/** 移动摇杆的内死区（半径占满偏的比例） */
	float GamepadMoveInner = 0.2f;

	/** 移动摇杆的外圈（半径占满偏的比例）：推到 0.8 即达满幅移动输入 */
	float GamepadMoveOuter = 0.8f;
	/** 转身加速：推到满偏并保持时额外加的水平转速（度 / 秒），0 是不加。平时瞄准的转速可以调低，转身时靠它 */
	float GamepadExtraYaw = 0.f;
	/** 转身加速：额外加的竖直转速（度 / 秒） */
	float GamepadExtraPitch = 0.f;
	/** 转身加速：推到满偏多久以后才开始加（秒） */
	float GamepadExtraDelay = 0.1f;
	/** 转身加速：从开始加到加满用多久（秒） */
	float GamepadExtraRamp = 0.3f;
	/** 辅助瞄准（Docs/Design/Control.md「辅助瞄准」，Ginkgo::AimAssist::MakeProfile 换成算法用的）的总开关：关着时不选目标、不修，界面上下面几项收起来 */
	bool bGamepadAimAssist = true;
	/**
	 * 辅助瞄准的不开镜强度：准星远离目标躯干中线时径向逃逸速度被抵消的比例，0 为不阻尼，上限 0.95（保留脱离余量）。
	 * 开镜期间随平滑开镜进度线性过渡至开镜强度
	 */
	float GamepadAimAssistHip = 0.8f;
	/** 辅助瞄准：开镜到位时的满额阻尼强度 */
	float GamepadAimAssistAds = 0.9f;
	/** 辅助瞄准：世界空间满强度半径（cm），在目标距离处按透视折算为视角张角 */
	float GamepadAimAssistInner = 65.f;
	/** 辅助瞄准：世界空间淡出外半径（cm），超出该半径无阻尼；小于满强度半径时按满强度半径截断 */
	float GamepadAimAssistOuter = 180.f;
	/** 辅助瞄准：最大有效距离（m），超出该距离的目标不参与候选评估 */
	float GamepadAimAssistRange = 85.f;
	/** 辅助瞄准：跟随目标系数。目标与自身位移产生的屏幕相对角速度计入阻尼计算的权重（1 为全额跟随，0 为仅阻尼主动外推输入） */
	float GamepadAimAssistFollow = 1.f;

	// ===== 鼠标与键盘 =====

	/** 鼠标基础灵敏度：硬件每上报 1 count 原始位移，视角水平偏转 MouseSensitivity × 0.022 度（默认值 0.7） */
	float MouseSensitivity = 0.7f;

	/** 垂直灵敏度比例：垂直俯仰灵敏度 = 水平灵敏度 × MousePitchRatio */
	float MousePitchRatio = 1.f;

	/** 越肩开镜时灵敏度乘的倍率，随开镜进度线性过渡 */
	float MouseAdsScale = 1.f;

	/** 越肩开镜时按视场正切放大率反向补偿：使开镜前后相同手部移动在屏幕中心产生的像移距离一致，默认开启 */
	bool bMouseAdsMatchFov = true;

	/** 光学倍镜灵敏度：设为 1 时镜内目标像在屏幕上的移动速度与腰射一致 */
	float MouseScopeScale = 1.f;

	bool bMouseInvertPitch = false;

	bool bMouseInvertYaw = false;

	/** 鼠标加速度总开关：启用基于瞬时物理速率（count/ms）的帧率无关动态增益 */
	bool bMouseAcceleration = false;

	/** 加速度斜率强度：超出起效阈值后，速率每增加 1 count/ms 额外叠加的灵敏度倍率 */
	float MouseAccelStrength = 0.03f;

	/** 加速度起效阈值（count/ms）：低于该速率时保持 1× 基础灵敏度 */
	float MouseAccelOffset = 3.f;

	/** 加速度倍率上限：高速甩鼠时灵敏度最多放大到的倍数 */
	float MouseAccelCap = 2.f;

	/** 键鼠辅助瞄准总开关：默认关闭，避免干扰键鼠原生指哪打哪手感；参数组独立于手柄配置 */
	bool bMouseAimAssist = false;
	float MouseAimAssistHip = 0.4f;
	float MouseAimAssistAds = 0.5f;
	float MouseAimAssistInner = 50.f;
	float MouseAimAssistOuter = 120.f;
	float MouseAimAssistRange = 85.f;
	float MouseAimAssistFollow = 0.5f;

	// ===== 辅助功能：反遮挡（两个输入设备共用） =====

	/** 抬枪时根据枪口遮挡自动探头；默认开启，当前实命中已指向可伤害目标时不发起探头，关闭后平滑回正 */
	bool bAutoPeek = true;

	/** ADS 时按相机前方的环境遮挡自动换肩；两个输入设备共用，默认开启，关闭不影响手动换肩 */
	bool bAdsAutoShoulder = true;

	/** 自动换肩开启时，退出 ADS 平滑恢复本次进入 ADS 时的肩位；默认开启，隐藏此选项时保留已选值 */
	bool bRestoreShoulderOnAdsExit = true;

	/** 是否显示附近角色的台词字幕；独立于语音播放。 */
	bool bSubtitles = true;

	/** 在有说话人名称时显示其前缀；关闭只保留台词正文。 */
	bool bSubtitleSpeaker = true;

	/** 字幕字号（逻辑像素，随视口 DPI 缩放），游戏与设置预览共用；默认 20 px。 */
	float SubtitleSize = 20.f;

	/** 字幕后暗色衬底的不透明度倍率 [0,1]；0 只留白字，默认 0.55。 */
	float SubtitleBackdrop = 0.55f;

	// ===== 键位（两个目录都有） =====

	/** 下标同 GetKeyBindingDefs */
	TArray<FGinkgoKeyBinding> Keys;
};

/**
 * @struct FGinkgoInputSettingDef : 一项设置的定义：存盘与页面用的名字、属于哪个目录、种类、范围。
 * 值统一按 double 进出（开关 0 或 1，选择是 Options 的下标），存盘、页面的消息、恢复默认都走这一张表，加一项只加一行
 */
struct FGinkgoInputSettingDef
{
	enum class EKind : uint8
	{
		Number,
		Toggle,
		Choice,
	};

	/** 文件与页面里的名字，如 gamepad.yawSpeed。改了名字，存过的值就读不回来了 */
	FString Id;
	EGinkgoInputDirectory Directory = EGinkgoInputDirectory::Gamepad;
	EKind Kind = EKind::Number;
	double (*Get)(const FGinkgoInputSettings&) = nullptr;
	void (*Set)(FGinkgoInputSettings&, double) = nullptr;
	double Min = 0.0;
	double Max = 1.0;
	/** 页面上滑块的一格；文件里的值不要求落在格上 */
	double Step = 0.01;
	/** 选择的几个值在文件与页面里的名字 */
	TArray<FString> Options;
};

/**
 * @struct FGinkgoKeyBindingDef : 一个键位的定义
 * 一个键位就是 Enhanced Input 的一条可改键的映射（UPlayerMappableKeySettings）：映射名就是 Id。默认的键与方式在这里，存盘只记和它不同的
 */
struct FGinkgoKeyBindingDef
{
	/** 文件、页面与映射名：mouse.crouch.primary。改了名字，存过的键就读不回来了 */
	FString Id;
	/** 页面上的一行（两个键位）：mouse.crouch */
	FString RowId;
	FName MappingName;
	EGinkgoInputDirectory Directory = EGinkgoInputDirectory::KeyboardMouse;
	EGinkgoKeyAction Action = EGinkgoKeyAction::Jump;
	EGinkgoKeySlot Slot = EGinkgoKeySlot::Primary;
	FKey DefaultKey;
	EGinkgoHoldMode DefaultMode = EGinkgoHoldMode::Hold;
	/** 这个动作有没有按住与切换（冲刺、蹲、开镜） */
	bool bHasMode = false;
};

namespace Ginkgo::Input
{
	/** 所有键位：键盘与鼠标的十三个动作、手柄的九个动作（手柄的移动是摇杆），每个动作主键、辅助键两个键位 */
	CASEGINKGO_API const TArray<FGinkgoKeyBindingDef>& GetKeyBindingDefs();

	/** @brief 这个目录、这个动作、这个键位的下标；手柄没有方向键的四个动作，是 INDEX_NONE */
	CASEGINKGO_API int32 KeyBindingIndex(EGinkgoInputDirectory Directory, EGinkgoKeyAction Action, EGinkgoKeySlot Slot);

	/** @brief 按名字找键位的下标；没有时是 INDEX_NONE */
	CASEGINKGO_API int32 FindKeyBinding(const FString& Id);

	/** @brief 这个目录、这个动作现在绑的键：主键，主键空着时辅助键，都空着时无效（游戏画面上的键位提示用，改过键的也跟着） */
	CASEGINKGO_API FKey FindBoundKey(const FGinkgoInputSettings& Settings, EGinkgoInputDirectory Directory, EGinkgoKeyAction Action);

	/** @brief 这个键能不能绑到这个目录的键位上 */
	CASEGINKGO_API EGinkgoKeyCheck CheckKey(EGinkgoInputDirectory Directory, const FKey& Key);

	/** @brief 同一个目录里，除了 Index 自己，谁在用这个键；没有时是 INDEX_NONE。两种设备的键不会撞 */
	CASEGINKGO_API int32 FindKeyConflict(const FGinkgoInputSettings& Settings, int32 Index, const FKey& Key);

	/** @brief 把键给这个键位：别的键位在用它时互换，所以不会有一个键绑两个动作 */
	CASEGINKGO_API void AssignKey(FGinkgoInputSettings& Settings, int32 Index, const FKey& Key);

	/** 所有设置项，顺序就是文件里的顺序 */
	CASEGINKGO_API const TArray<FGinkgoInputSettingDef>& GetSettingDefs();

	/** @brief 按名字找；没有时为空 */
	CASEGINKGO_API const FGinkgoInputSettingDef* FindSettingDef(const FString& Id);

	/** @brief 给一项写值：数夹到范围里，开关取非零，选择的下标超出时不改。外面来的值都走这里（页面、文件、命令） */
	CASEGINKGO_API void WriteSetting(FGinkgoInputSettings& Settings, const FGinkgoInputSettingDef& Def, double Value);

	/** @brief 几项不同（一个键位的键与方式各算一项）；只比某个目录时传 Directory */
	CASEGINKGO_API int32 CountDifferences(const FGinkgoInputSettings& A, const FGinkgoInputSettings& B, const EGinkgoInputDirectory* Directory = nullptr);

	/** @brief 把某个目录的项与键位从 From 抄到 To（恢复默认只管当前目录） */
	CASEGINKGO_API void CopyDirectory(FGinkgoInputSettings& To, const FGinkgoInputSettings& From, EGinkgoInputDirectory Directory);

	/**
	 * @brief 写成文件的文本：版本加上和默认值不同的项（默认值以后改了，没动过的项跟着新的默认值走）
	 */
	CASEGINKGO_API FString SettingsToJson(const FGinkgoInputSettings& Settings);

	/**
	 * @brief 读文件的文本。缺的项是默认值，超出范围的夹回来，类型不对的、不认识的项跳过并记一条：只要是 JSON，读得懂的都留下。
	 * 键位也一样：不认识的键、绑不了的键跳过；两个键位撞了同一个键（手改的文件、以后改了默认键）时后面的那个空出来
	 * @param OutSettings - 结果；读不懂时是默认值
	 * @param OutIssues - 跳过了什么，写日志用
	 * @return 文本是不是这个格式的 JSON
	 */
	CASEGINKGO_API bool SettingsFromJson(const FString& Text, FGinkgoInputSettings& OutSettings, TArray<FString>* OutIssues = nullptr);

	/**
	 * @brief 存盘：先写到旁边的临时文件，再换过去。写到一半断电，原来的文件还是好的
	 * @param OutError - 失败的原因
	 */
	CASEGINKGO_API bool SaveSettingsFile(const FString& Path, const FGinkgoInputSettings& Settings, FString& OutError);

	/**
	 * @brief 读盘。没有文件时是默认值，返回真；文件坏了（不是 JSON）改名成 .corrupt 留着，用默认值，返回假
	 */
	CASEGINKGO_API bool LoadSettingsFile(const FString& Path, FGinkgoInputSettings& OutSettings, TArray<FString>* OutIssues = nullptr);
}
