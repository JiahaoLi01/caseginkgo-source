#include "SaveGameTags.h"

namespace FSaveNodeTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Core_Actors,				"SaveNode.Core.Actors",
		"存档节点：实现了 IGinkgoSaveGameObject 的 Actor。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Core_DestroyedActors,	"SaveNode.Core.DestroyedActors",
		"存档节点：运行时被销毁的关卡 Actor，读档后重新销毁。")
}
