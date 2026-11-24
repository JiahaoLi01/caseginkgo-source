#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * @namespace FCameraTags : 相机管线自己的标签
 * 参数层的增量带一个类别，覆盖类的修改器按类别屏蔽增量（Docs/Design/CameraPipeline.md「求值规则」：开镜屏蔽速度类增量）。
 * 激活条件读的状态标签在各自的领域里（FMovementTags、FWeaponTags）。手写 `extern CASEGINKGO_API` 的原因同 FMovementTags。
 */
namespace FCameraTags
{
	extern CASEGINKGO_API FNativeGameplayTag Camera_Category_Speed;
}
