#include "GinkgoMovementConfig.h"

#include "Movement/Windows/GinkgoLurchWindow.h"
#include "Movement/Windows/GinkgoMovementWindow.h"
#include "Utilities/Enumeration/Tags/MovementTags.h"
#if !UE_BUILD_SHIPPING
#include "Misc/CommandLine.h"
#endif

UGinkgoMovementConfig::UGinkgoMovementConfig()
{
	// 初始键引用同一配置的默认跑速/冲刺速度，曲线保存后由资产独立编辑，不在加载时覆盖用户的键。
	FRichCurve* ReachCurve = TraversalReachBySpeed.GetRichCurve();
	for (const FVector2D& Key : {FVector2D(0.f, Traversal.Reach), FVector2D(RunSpeed, 100.f), FVector2D(SprintSpeed, 125.f)})
	{
		ReachCurve->SetKeyInterpMode(ReachCurve->AddKey(Key.X, Key.Y), RCIM_Linear);
	}

	// ===== 窗口 =====
	// 列表的顺序两端要一样：先是默认带的窗口，再是命令行加的调试窗口

	// 土狼时间：没起跳就离开地面（走下平台边缘）后 0.2 s 内还能起跳。给出"空中起跳"的能力，下落中的这一跳仍算第一跳（地面起跳），
	// 不占二段跳的次数。推入时清除跳跃疲劳：走下边缘就关掉疲劳开关（Apex），之后落地也不再有疲劳；
	// 另外屏蔽跳跃惩罚，别的窗口给出的跳跃惩罚也不算到土狼起跳上。起跳、落地或到时结束
	UGinkgoMovementWindow* Coyote = CreateDefaultSubobject<UGinkgoMovementWindow>(TEXT("CoyoteTime"));
	Coyote->Tag = FMovementTags::Movement_Window_Coyote;
	Coyote->DisplayName = NSLOCTEXT("GinkgoMovement", "CoyoteTime", "土狼时间");
	Coyote->PushEvents = {EGinkgoWindowEvent::WalkedOffLedge};
	Coyote->Repush = EGinkgoWindowRepush::Refresh;
	Coyote->ClearedWindows.AddTag(FMovementTags::Movement_Window_JumpFatigue);
	FGinkgoWindowPhase Grace;
	Grace.Duration = 0.2f;
	Coyote->Phases = {Grace};
	Coyote->EndEvents = {EGinkgoWindowEvent::Jumped, EGinkgoWindowEvent::Landed};
	Coyote->GrantedAbilities.AddTag(FMovementTags::Movement_Window_Ability_JumpWhileFalling);
	Coyote->BlockedCategories.AddTag(FMovementTags::Movement_Window_Effect_JumpPenalty);
	Windows.Add(Coyote);

	// 滑铲疲劳（Apex）：开始滑铲后 2 s 内再铲没有起步加速。每次开始滑铲都推入，已有时刷新，不管这次有没有拿到加速。
	// 移动组件先按这时的倍率算起步加速，再发滑铲开始的事件，所以这一铲读到的是上一铲留下的窗口。翻越加速生效时也要推入，等翻越（M8）
	UGinkgoMovementWindow* SlideFatigue = CreateDefaultSubobject<UGinkgoMovementWindow>(TEXT("SlideFatigue"));
	SlideFatigue->Tag = FMovementTags::Movement_Window_SlideFatigue;
	SlideFatigue->DisplayName = NSLOCTEXT("GinkgoMovement", "SlideFatigue", "滑铲疲劳");
	SlideFatigue->PushEvents = {EGinkgoWindowEvent::SlideStarted};
	SlideFatigue->Repush = EGinkgoWindowRepush::Refresh;
	FGinkgoWindowPhase Fatigue;
	Fatigue.Duration = 2.f;
	SlideFatigue->Phases = {Fatigue};
	// 数值来源也是类默认值的子对象，配置资产各有一份
	UGinkgoWindowValueConstant* NoBoost = CreateDefaultSubobject<UGinkgoWindowValueConstant>(TEXT("SlideFatigueBoostScale"));
	NoBoost->Value = 0.f;
	FGinkgoWindowMultiplier BoostScale;
	BoostScale.Category = FMovementTags::Movement_Window_Effect_SlideBoost;
	BoostScale.Value = NoBoost;
	SlideFatigue->Multipliers = {BoostScale};
	Windows.Add(SlideFatigue);

	// 跳跃疲劳（Apex）：疲劳开关与回升计时是两个量。起跳时打开开关（Armed），落地才开始计时（Recovering）：从高处跳下、在空中飞了几秒，
	// 落地时照样有疲劳。落地后 0.15 s 内再跳只有 1/4 的高度，之后线性回升，0.75 s 时回到 1；起跳速度相应乘 √倍率。
	// 再起跳时从 Armed 重新开始；走下平台边缘时由土狼时间清除，成功开始翻越/攀爬也清除两个阶段的疲劳。
	UGinkgoMovementWindow* JumpFatigue = CreateDefaultSubobject<UGinkgoMovementWindow>(TEXT("JumpFatigue"));
	JumpFatigue->Tag = FMovementTags::Movement_Window_JumpFatigue;
	JumpFatigue->DisplayName = NSLOCTEXT("GinkgoMovement", "JumpFatigue", "跳跃疲劳");
	JumpFatigue->PushEvents = {EGinkgoWindowEvent::Jumped};
	JumpFatigue->EndEvents = {EGinkgoWindowEvent::TraversalStarted};
	JumpFatigue->Repush = EGinkgoWindowRepush::Refresh;
	FGinkgoWindowPhase Armed;
	Armed.Tag = FMovementTags::Movement_Window_JumpFatigue_Armed;
	Armed.Transitions.Add({EGinkgoWindowEvent::Landed, 1});
	FGinkgoWindowPhase Recovering;
	Recovering.Tag = FMovementTags::Movement_Window_JumpFatigue_Recovering;
	Recovering.Duration = 0.75f;
	JumpFatigue->Phases = {Armed, Recovering};
	UGinkgoWindowValueCurve* Recovery = CreateDefaultSubobject<UGinkgoWindowValueCurve>(TEXT("JumpFatigueRecovery"));
	FRichCurve* RecoveryCurve = Recovery->Curve.GetRichCurve();
	RecoveryCurve->AddKey(0.f, 0.25f);
	RecoveryCurve->AddKey(0.15f, 0.25f);
	RecoveryCurve->AddKey(0.75f, 1.f);
	FGinkgoWindowMultiplier HeightScale;
	HeightScale.Category = FMovementTags::Movement_Window_Effect_JumpPenalty;
	HeightScale.Phase = 1;
	HeightScale.Value = Recovery;
	JumpFatigue->Multipliers = {HeightScale};
	Windows.Add(JumpFatigue);

	// lurch（泰坦陨落 2、Apex）：不疲劳的起跳推入，0.4 s 内第一次按下键盘方向键时，把水平速度转向新的输入方向，用掉窗口。
	// 强度按窗口已过的时间：0.2 s 内满强度，之后线性降到 0.4 s 时为 0（Apex 的社区实测）。再起跳时从头开始，落地或到时结束
	UGinkgoLurchWindow* Lurch = CreateDefaultSubobject<UGinkgoLurchWindow>(TEXT("Lurch"));
	Lurch->Tag = FMovementTags::Movement_Window_Lurch;
	Lurch->DisplayName = NSLOCTEXT("GinkgoMovement", "Lurch", "lurch");
	Lurch->PushEvents = {EGinkgoWindowEvent::Jumped};
	Lurch->PushFlag = EGinkgoWindowEventFlagFilter::Clear;
	Lurch->Repush = EGinkgoWindowRepush::Refresh;
	FGinkgoWindowPhase LurchGrace;
	LurchGrace.Duration = 0.4f;
	Lurch->Phases = {LurchGrace};
	Lurch->EndEvents = {EGinkgoWindowEvent::Landed};
	UGinkgoWindowValueCurve* LurchDecay = CreateDefaultSubobject<UGinkgoWindowValueCurve>(TEXT("LurchStrengthScale"));
	FRichCurve* LurchDecayCurve = LurchDecay->Curve.GetRichCurve();
	LurchDecayCurve->AddKey(0.f, 0.7f);
	LurchDecayCurve->AddKey(0.2f, 0.7f);
	LurchDecayCurve->AddKey(0.4f, 0.f);
	Lurch->StrengthScale = LurchDecay;
	Windows.Add(Lurch);

#if !UE_BUILD_SHIPPING
	// 调试用的探针窗口：起跳或滑铲开始时推入（再推入时从头开始），A 阶段 0.3 s 后切到 B，B 落地或 1 s 后结束；没有贡献，不影响移动。
	// 只在命令行带 -GinkgoWindowProbe 时加进来。服务器与客户端要一起带上，两端的定义列表才一样
	if (FParse::Param(FCommandLine::Get(), TEXT("GinkgoWindowProbe")))
	{
		UGinkgoMovementWindow* Probe = CreateDefaultSubobject<UGinkgoMovementWindow>(TEXT("WindowProbe"));
		Probe->Tag = FMovementTags::Movement_Window_Probe;
		Probe->DisplayName = INVTEXT("探针");
		Probe->PushEvents = {EGinkgoWindowEvent::Jumped, EGinkgoWindowEvent::SlideStarted};
		Probe->Repush = EGinkgoWindowRepush::Refresh;
		FGinkgoWindowPhase PhaseA;
		PhaseA.Tag = FMovementTags::Movement_Window_Probe_A;
		PhaseA.Duration = 0.3f;
		PhaseA.NextPhase = 1;
		FGinkgoWindowPhase PhaseB;
		PhaseB.Tag = FMovementTags::Movement_Window_Probe_B;
		PhaseB.Duration = 1.f;
		PhaseB.Transitions.Add({EGinkgoWindowEvent::Landed, INDEX_NONE});
		Probe->Phases = {PhaseA, PhaseB};
		Windows.Add(Probe);
	}
#endif
}

float UGinkgoMovementConfig::GetTraversalReach(const float HorizontalSpeed) const
{
	const float Speed = FMath::IsFinite(HorizontalSpeed) ? FMath::Max(0.f, HorizontalSpeed) : 0.f;
	const float BaseReach = FMath::IsFinite(Traversal.Reach) ? FMath::Clamp(Traversal.Reach, 1.f, 125.f) : 50.f;
	const FRichCurve* Curve = TraversalReachBySpeed.GetRichCurveConst();
	const float Reach = Curve && Curve->GetNumKeys() > 0 ? Curve->Eval(Speed, BaseReach) : BaseReach;
	return FMath::IsFinite(Reach) ? FMath::Clamp(Reach, 1.f, 125.f) : BaseReach;
}
