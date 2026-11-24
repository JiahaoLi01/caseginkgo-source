#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * @namespace FSaveNodeTags : 存档节点的分类标签（IGinkgoSaveNode::GetClassificationTag）
 * 只给日志、环检测、JSON 的键用；节点的身份是 GetNodeId() 的 GUID。
 * 命名：SaveNode.Core.* 是框架内置的节点，SaveNode.<功能>.* 是各系统的节点。
 */
namespace FSaveNodeTags
{
	extern CASEGINKGO_API FNativeGameplayTag Core_Actors;
	extern CASEGINKGO_API FNativeGameplayTag Core_DestroyedActors;
}
