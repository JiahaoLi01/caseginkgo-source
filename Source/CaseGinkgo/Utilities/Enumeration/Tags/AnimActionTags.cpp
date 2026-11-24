#include "AnimActionTags.h"

namespace FAnimActionTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(AnimAction_Abort_Interrupted,	"AnimAction.Abort.Interrupted",
		"中止：动作的蒙太奇被别人停了、顶掉了（引擎的淡出或结束事件带着打断）。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(AnimAction_Abort_PlayFailed,		"AnimAction.Abort.PlayFailed",
		"中止：蒙太奇放不起来（蒙太奇是空的、骨架不对），或者链的步骤没造出动作。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(AnimAction_Abort_InstanceLost,	"AnimAction.Abort.InstanceLost",
		"中止：蒙太奇的实例不在了，却没收到结束事件（例如动画实例被重新初始化）。事件丢了，动作也一定会结束。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(AnimAction_Abort_Occupied,		"AnimAction.Abort.Occupied",
		"中止：要占的部位被占着，自己的优先级不够，没有开始（FGinkgoAnimActionRunner::Start 返回假）。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(AnimAction_Cancel_Replaced,		"AnimAction.Cancel.Replaced",
		"取消：同一个槽组里放了新动作的蒙太奇。引擎一个组只留一个蒙太奇，执行器照此一个组一个动作。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(AnimAction_Cancel_OwnerExit,		"AnimAction.Cancel.OwnerExit",
		"取消：拥有者不要了，例如动作状态的 StateTree 退出了跑这个动作的状态，或者调试命令叫停。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(AnimAction_Cancel_Preempted,		"AnimAction.Cancel.Preempted",
		"取消：优先级更高的占了它占着的部位。")

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Occupancy_LeftArm,				"Occupancy.LeftArm",
		"占用：左臂。标签描述 gameplay 上的占用结构，不描述骨骼（Docs/Design/AnimActions.md「占用」）：拾取最后是手碰东西，占的却是整条胳膊。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Occupancy_LeftArm_Hand,			"Occupancy.LeftArm.Hand",
		"占用：只占左手，胳膊别的动作还能用（放松手指这种）。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Occupancy_RightArm,				"Occupancy.RightArm",
		"占用：右臂。持枪（优先级 0）、拔枪与收枪（优先级 5）、右手的拾取都是它。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Occupancy_RightArm_Hand,			"Occupancy.RightArm.Hand",
		"占用：只占右手，胳膊别的动作还能用（放松手指这种）。")
}
