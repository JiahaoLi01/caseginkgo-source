#include "TestTags.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FTestTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GinkgoTest_Window_A,			"GinkgoTest.Window.A",
		"测试用：两个阶段的窗口。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GinkgoTest_Window_A_First,	"GinkgoTest.Window.A.First",
		"测试用：窗口 A 的第一个阶段。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GinkgoTest_Window_A_Second,	"GinkgoTest.Window.A.Second",
		"测试用：窗口 A 的第二个阶段。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GinkgoTest_Window_B,			"GinkgoTest.Window.B",
		"测试用：另一个窗口，推入条件与屏蔽的测试里用。")

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GinkgoTest_Effect,			"GinkgoTest.Effect",
		"测试用：倍率类别的父类别。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GinkgoTest_Effect_Speed,		"GinkgoTest.Effect.Speed",
		"测试用：倍率类别。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GinkgoTest_Effect_Jump,		"GinkgoTest.Effect.Jump",
		"测试用：倍率类别，屏蔽的测试里用。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GinkgoTest_Ability,			"GinkgoTest.Ability",
		"测试用：能力的父标签。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GinkgoTest_Ability_AirJump,	"GinkgoTest.Ability.AirJump",
		"测试用：能力，例如土狼时间给出的空中起跳。")

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GinkgoTest_AnimAction_Abort,		"GinkgoTest.AnimAction.Abort",
		"测试用：动作自己的中止原因，例如够不着。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GinkgoTest_AnimAction_Branch,	"GinkgoTest.AnimAction.Branch",
		"测试用：另一个中止原因，按原因转换的测试里用。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GinkgoTest_AnimAction_Stop,		"GinkgoTest.AnimAction.Stop",
		"测试用：拥有者取消、软停时给的原因。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GinkgoTest_AnimAction_Camera,	"GinkgoTest.AnimAction.Camera",
		"测试用：动作持有的相机标签。")

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GinkgoTest_Ammo_Special,	"GinkgoTest.Ammo.Special",
		"测试用：特殊弹，压一发特殊弹、弹种先后的测试里用。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GinkgoTest_Ammo_Rifle,	"GinkgoTest.Ammo.Rifle",
		"测试用：可拆弹匣、自动装填的枪（AK）的弹。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GinkgoTest_Ammo_Shell,	"GinkgoTest.Ammo.Shell",
		"测试用：管状弹仓的枪（泵动霰弹枪）的弹。")
}

#endif // WITH_DEV_AUTOMATION_TESTS
