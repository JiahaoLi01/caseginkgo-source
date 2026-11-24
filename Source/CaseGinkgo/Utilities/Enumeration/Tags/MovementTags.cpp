#include "MovementTags.h"

namespace FMovementTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Sprint,		"Movement.Sprint",
		"在地面上冲刺（按着冲刺、没蹲、在地面上，角色面向速度）。相机：冲刺 FOV 增量。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Slide,		"Movement.Slide",
		"滑铲（Walking 里的子状态）中。相机：滑铲的 FOV 与臂长增量。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_WallRun,	"Movement.WallRun",
		"墙跑（自定义移动模式 WallRun）中。相机：墙跑侧倾与偏移。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Grapple,	"Movement.Grapple",
		"钩锁拉拽中（Falling 模式里的附加加速度，不是独立模式）。相机：钩锁臂长。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Grounded,	"Movement.Grounded",
		"在地面上（Walking，含滑铲）。相机：冲刺落脚的震动只在地面上。")

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Value_HorizontalSpeed,	"Movement.Value.HorizontalSpeed",
		"相机上下文的连续量：水平速度（cm/s）。相机：跑动晃动的包络，滑铲 FOV 的失效条件（低于跑速）。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Value_SlideBoost,	"Movement.Value.SlideBoost",
		"相机上下文的连续量：这次滑铲的起步加速（cm/s）；不在滑铲、没拿到加速（滑铲疲劳）时为 0。相机：滑铲 FOV 只在拿到加速的滑铲里生效。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Value_RunSpeed,		"Movement.Value.RunSpeed",
		"相机上下文的连续量：现在的跑速（cm/s）：移动配置里的跑速乘持枪倍率。相机的条件拿水平速度与它比，改了跑速相机跟着变。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Event_SlideStarted,	"Movement.Event.SlideStarted",
		"相机上下文的事件：开始滑铲，强度是这次的起步加速（cm/s）。只在第一次模拟时发，时刻是这次移动开始的那一刻（落地接滑铲时是着地的那一刻）。相机：滑铲开始的震动。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Event_Landed,		"Movement.Event.Landed",
		"相机上下文的事件：落地，强度是着地前的下落速度（cm/s）。只在第一次模拟时发，时刻是着地的那一刻。相机：以后的落地冲击。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Event_Footstep,		"Movement.Event.Footstep",
		"相机上下文的事件：脚落地，强度左脚 +1、右脚 −1。动画实例发：所放片段的时刻跨过脚的同步标记（L、R，导入时按接触曲线的上升沿加的），时刻按跨过的几成插在帧里。相机：冲刺落脚的震动。")

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Window_Effect_SlideBoost,	"Movement.Window.Effect.SlideBoost",
		"窗口倍率类别：滑铲的起步加速。滑铲疲劳在这个类别上给 0。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Window_Effect_JumpPenalty,	"Movement.Window.Effect.JumpPenalty",
		"窗口倍率类别：跳跃惩罚，乘在跳跃高度上。跳跃疲劳给出，土狼时间屏蔽。")

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Window_Ability_JumpWhileFalling,	"Movement.Window.Ability.JumpWhileFalling",
		"窗口能力：下落中的第一跳仍算第一跳（ACharacter::CanJumpWhileFalling），不被当成第二段跳。土狼时间给出。")

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Window_Coyote,	"Movement.Window.Coyote",
		"土狼时间：没起跳就离开地面（走下平台边缘）后 0.2 秒内还能起跳，算作地面起跳、不占二段跳的次数，屏蔽跳跃惩罚；推入时清除跳跃疲劳；起跳、落地或到时结束。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Window_SlideFatigue,	"Movement.Window.SlideFatigue",
		"滑铲疲劳：开始滑铲后 2 秒内再铲没有起步加速（起步加速类别的倍率为 0）。每次开始滑铲都重新计时，不管这次有没有拿到加速。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Window_JumpFatigue,	"Movement.Window.JumpFatigue",
		"跳跃疲劳：起跳时打开，落地后 0.75 秒内再跳，跳跃高度打折（0.15 秒内 1/4，之后线性回升到 1）。走下平台边缘时被土狼时间清除。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Window_JumpFatigue_Armed,	"Movement.Window.JumpFatigue.Armed",
		"跳跃疲劳的第一个阶段：起跳之后、落地之前，不打折；落地转入 Recovering。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Window_JumpFatigue_Recovering,	"Movement.Window.JumpFatigue.Recovering",
		"跳跃疲劳的第二个阶段：落地后 0.75 秒，跳跃高度的倍率从 1/4 回升到 1，到时结束。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Window_Lurch,	"Movement.Window.Lurch",
		"lurch：不疲劳的起跳后 0.4 秒内，第一次按下键盘方向键时把水平速度转向新的输入方向；0.2 秒内满强度，之后线性降到 0。用掉一次、落地或到时结束。")

#if !UE_BUILD_SHIPPING
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Window_Probe,			"Movement.Window.Probe",
		"调试用的探针窗口（-GinkgoWindowProbe）：起跳或滑铲开始时推入，不影响移动，只用来验证窗口的推进、切换与联网。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Window_Probe_A,			"Movement.Window.Probe.A",
		"探针窗口的第一个阶段：0.3 秒后切到 B。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Movement_Window_Probe_B,			"Movement.Window.Probe.B",
		"探针窗口的第二个阶段：落地或 1 秒后结束。")
#endif
}
