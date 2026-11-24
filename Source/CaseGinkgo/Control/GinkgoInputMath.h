#pragma once

#include "CoreMinimal.h"
#include "GinkgoInputSettings.h"

/**
 * 输入的算法（Docs/Design/Control.md）：死区、响应曲线、鼠标加速度、开镜的倍率、转身加速、按住与切换。
 * 都是纯函数或不碰引擎的小结构，单独测试（CaseGinkgo.Control.Input.*）；Enhanced Input 的修改器（GinkgoInputModifiers）只负责读设置、调这里。
 * 页面的预览（Content/UI/Settings/previews.js）有同样的公式，ginkgo.settings.input selfcheck 拿两边比过。
 */
namespace Ginkgo::Input
{
	/** Source 的 m_yaw、m_pitch 的默认值：灵敏度 1 时鼠标每动一个 count 转多少度 */
	constexpr float MouseDegreesPerCount = 0.022f;

	/** 过了死区以后的偏转到这么大算推到满偏了（转身加速）：摇杆的边沿不是正圆，外圈 1 时也不一定每个方向都到 1 */
	constexpr float FullDeflection = 0.98f;

	/**
	 * @brief 摇杆的径向死区：偏转小于 Inner 时是零，大于 Outer 时是单位向量，中间线性映到 0–1，方向不变
	 * @note 同引擎的 UInputModifierDeadZone（Radial）：Inner 0.2、Outer 1 时逐值相同（测试比过），默认手感不变
	 */
	CASEGINKGO_API FVector2D ApplyRadialDeadZone(const FVector2D& Stick, float Inner, float Outer);

	/**
	 * @brief 响应曲线：0 到 1 的偏转换成 0 到 1 的转速比例，单调，两端不动
	 * @param Strength - 强度：1 时三种都是线性
	 */
	CASEGINKGO_API float ApplyResponseCurve(float Deflection, EGinkgoResponseCurve Curve, float Strength);

	/**
	 * @struct FGinkgoTurnRamp : 转身加速的计时
	 * 按住满偏的时间推进；Step 给的是这一帧的权重在这一帧里的平均值，不是帧尾的值：
	 * 这样同样按住一段时间，多转的角度与帧率无关（积分的形式写死，不靠帧足够短）
	 */
	struct CASEGINKGO_API FGinkgoTurnRamp
	{
		/**
		 * @brief 推进一帧
		 * @param bAtFull - 这一帧是不是推到满偏；不是时从头算
		 * @param Delay - 推到满偏多久以后开始（秒）
		 * @param Ramp - 从 0 爬到 1 用多久（秒），0 是到时一下子满
		 * @return 这一帧的权重（0–1）的平均值
		 */
		float Step(bool bAtFull, float DeltaSeconds, float Delay, float Ramp);

		float HeldTime = 0.f;
	};

	/**
	 * @brief 开镜时灵敏度的倍率：不开镜时 1，开镜到位时 AdsScale（按视场换算时再除以缩放倍率），随开镜进度过渡
	 * @param AdsAlpha - 开镜进度（两头缓过的，相机拉近用的那个）
	 * @param AdsZoom - 这把枪的开镜缩放倍率：FOV = 2·atan(tan(常态 FOV / 2) / 倍率)，所以画面中心附近一度占的距离正好放大这么多倍
	 */
	CASEGINKGO_API float AdsLookScale(float AdsAlpha, float AdsScale, bool bMatchFov, float AdsZoom);

	/**
	 * @brief 鼠标加速度的倍率：1 + 强度 × (速度 − 起点)，夹在 1 到上限之间
	 * @param CountsPerMs - 这一帧鼠标的速度（count/ms）。按速度算，不按这一帧动了多少 count 算：
	 *        Source 的 m_customaccel 按一帧的量算，帧率高了一帧的量小，加速度就变小；这里同 Quake 3 的 cl_mouseAccel 除以帧时长，匀速时与帧率无关
	 */
	CASEGINKGO_API float MouseAccelerationGain(float CountsPerMs, float Strength, float Offset, float Cap);

	/**
	 * @brief 鼠标这一帧要转多少度
	 * @param Counts - 这一帧的原始量（X 向右、Y 向上）
	 * @param ScopeMagnification - 手上的枪的倍镜（第一人称开镜）的倍率；没有倍镜是 0，开镜照越肩的两项（开镜灵敏度、按视场换算）算。
	 *        有倍镜时开镜按倍镜灵敏度 ÷ 倍率算（Ginkgo::Scope::ScopeLookScale）
	 * @return X 偏航（向右为正）、Y 俯仰（向上为正）
	 */
	CASEGINKGO_API FVector2D MouseLookDegrees(const FVector2D& Counts, const FGinkgoInputSettings& Settings, float AdsAlpha, float AdsZoom, float DeltaSeconds,
		float ScopeMagnification = 0.f);

	/**
	 * @brief 右摇杆这一帧要转多少度：死区 → 响应曲线 → 转速（转身加速只在不开镜时）→ 开镜的倍率 → 反转 → 乘帧时长
	 * @param Stick - 摇杆的值（X 向右、Y 向上，-1 到 1）
	 * @param ScopeMagnification - 同 MouseLookDegrees
	 * @return X 偏航、Y 俯仰
	 */
	CASEGINKGO_API FVector2D StickLookDegrees(const FVector2D& Stick, const FGinkgoInputSettings& Settings, float AdsAlpha, float AdsZoom, FGinkgoTurnRamp& Ramp, float DeltaSeconds,
		float ScopeMagnification = 0.f);

	/** @brief 移动摇杆：只有死区，大小与方向交给移动组件 */
	CASEGINKGO_API FVector2D StickMoveInput(const FVector2D& Stick, const FGinkgoInputSettings& Settings);

	/**
	 * @brief 弹种轮盘的鼠标（Docs/Design/Weapons.md「切换弹种」）：count 按灵敏度换成度（灵敏度 × 0.022 × 竖直的比例），不反转、不加速、不乘开镜：
	 *        拨轮盘按手往哪推，同视角的尺度，所以玩家调的灵敏度也管它
	 * @return X 向右、Y 向上
	 */
	CASEGINKGO_API FVector2D WheelMouseDegrees(const FVector2D& Counts, const FGinkgoInputSettings& Settings);

	/** @brief 弹种轮盘的摇杆：视角摇杆的死区，不反转、没有响应曲线（指的是方向） */
	CASEGINKGO_API FVector2D WheelStickInput(const FVector2D& Stick, const FGinkgoInputSettings& Settings);
}

/** @enum EGinkgoHoldAction : 有按住与切换两种方式的动作 */
enum class EGinkgoHoldAction : uint8
{
	Crouch,
	Sprint,
	Aim,
	Count,
};

/**
 * @struct FGinkgoHoldActions : 蹲、冲刺、开镜合起来开着没有
 * 每个键位各有自己的方式（Ctrl 按住蹲、C 切换蹲）：按住方式的键位按着算开，切换方式的按一下翻一下，任何一个开着就开。都是按住方式时就是原来的行为：按着才有。
 * 切换方式需要的几条规则也在这里，都是"按了别的动作时要收掉切换留下的状态"，否则切换开着的动作要手动关：
 * - 开始冲刺收掉切换的开镜与蹲（冲刺打断开镜，见 Docs/Design/ActionStates.md；蹲着冲刺站不起来）；
 * - 开始开镜收掉切换的冲刺（冲刺时不让开镜，不收的话切换冲刺时永远开不了镜）；
 * - 起跳收掉切换的蹲；
 * - 开火收掉切换的冲刺（冲刺中按开火先松开冲刺，见 Docs/Design/ActionStates.md「谁打断谁」）；
 * - 停下来（没有移动输入超过 SprintStopGrace）收掉切换的冲刺：换方向时摇杆经过中心、键盘换键时的重叠不算停。
 * 按住方式的按键不受这几条影响：手还按着，就还开着（同原来）。
 */
struct CASEGINKGO_API FGinkgoHoldActions
{
	/** 没有移动输入多久算停下来了（秒） */
	static constexpr float SprintStopGrace = 0.25f;

	void Press(EGinkgoHoldAction Action, EGinkgoInputDirectory Device, EGinkgoKeySlot Slot, EGinkgoHoldMode Mode);
	void Release(EGinkgoHoldAction Action, EGinkgoInputDirectory Device, EGinkgoKeySlot Slot);

	/** @brief 起跳的按下 */
	void OnJump();

	/** @brief 手上有枪时开火的按下 */
	void OnFire();

	/** @brief 每帧：这一帧有没有移动输入 */
	void Tick(bool bHasMoveInput, float DeltaSeconds);

	/** @brief 全部清掉（改了键位或方式时：按住与切换之间不换算） */
	void Reset();

	/** @brief 只清按着的（打开设置界面时：松开的事件不一定到得了） */
	void ReleaseAllHeld();

	bool IsOn(EGinkgoHoldAction Action) const;

private:
	/** 哪些键位按着（只记按住方式的）：动作、设备、键位 */
	bool bHeld[static_cast<int32>(EGinkgoHoldAction::Count)][2][static_cast<int32>(EGinkgoKeySlot::Count)] = {};
	bool bToggled[static_cast<int32>(EGinkgoHoldAction::Count)] = {};
	float NoMoveTime = 0.f;
};
