#include "PickupTags.h"

namespace FPickupTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(AnimAction_Abort_OutOfReach,	"AnimAction.Abort.OutOfReach",
		"中止：手还没抓住东西时，肩关节到东西超出了手臂够得着的长度（人走开了）。拾取马上完成。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(AnimAction_Abort_Timeout,	"AnimAction.Abort.Timeout",
		"中止：保底，开始以后过了上限（拾取是 3 s）还没结局。拾取马上完成。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(AnimAction_Abort_Posture,	"AnimAction.Abort.Posture",
		"中止：动作进行中离了地（在空中）或开始滑铲。拾取马上完成。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(AnimAction_Cancel_Rejected,	"AnimAction.Cancel.Rejected",
		"取消：服务器拒绝了这次交互（东西已经被别人拿走、条件对不上），或者客户端等不到回复。拾取不给效果。")
}
