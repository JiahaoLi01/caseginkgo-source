#pragma once

#include "CoreMinimal.h"
#include "Weapon/GinkgoWeaponAttachment.h"
#include "Weapon/GinkgoWeaponMechanism.h"
#include "GinkgoWeaponSaveState.generated.h"

class UGinkgoWeaponDefinition;

/** 一个武器槽的持久状态；只保存已发生的机构事件和装配结果，不保存蒙太奇或临时预览模型。 */
USTRUCT()
struct CASEGINKGO_API FGinkgoWeaponSlotSaveState
{
	GENERATED_BODY()

	/** 共享武器定义；空值代表空槽。存档代理按资产路径保存。 */
	UPROPERTY(SaveGame)
	TObjectPtr<UGinkgoWeaponDefinition> Definition;

	/** 此槽已经提交的配件，包含 Mutable 模块选择。 */
	UPROPERTY(SaveGame)
	FGinkgoInstalledAttachments Attachments;

	/** 供弹顺序、弹膛、枪机及弹匣是否在枪上；恢复时不重新补满。 */
	UPROPERTY(SaveGame)
	FGinkgoMechanismState Mechanism;

	/** 下一次装填选用的弹种；空值沿用武器默认弹种。 */
	UPROPERTY(SaveGame)
	FGameplayTag LoadAmmo;
};

/** 角色武器组件的持久快照；由组件采集和恢复，检查点只负责持有与序列化。 */
USTRUCT()
struct CASEGINKGO_API FGinkgoWeaponSaveState
{
	GENERATED_BODY()

	/** 按组件槽位顺序存储，空槽也保留；读档要求槽数与组件一致。 */
	UPROPERTY(SaveGame)
	TArray<FGinkgoWeaponSlotSaveState> Slots;

	/** 所有弹种的后备数量，恢复时整体替换，不能与开局赠送弹药相加。 */
	UPROPERTY(SaveGame)
	FGinkgoAmmoReserve Reserve;

	/** 手中武器槽；INDEX_NONE 表示收枪。 */
	UPROPERTY(SaveGame)
	int32 HeldSlot = INDEX_NONE;

	/** 收枪后下次优先拿出的槽；INDEX_NONE 表示没有历史选择。 */
	UPROPERTY(SaveGame)
	int32 LastHeldSlot = INDEX_NONE;
};
