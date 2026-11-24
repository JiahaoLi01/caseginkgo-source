#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GinkgoGameMode.generated.h"

class AGinkgoCharacter;
class UGinkgoWeaponDefinition;

/**
 * @struct FGinkgoStartingWeaponEntry : 正式游戏模式的玩家开局武器配置项
 * 描述开局发放的一把武器及其随枪配发的后备弹匣数量（不含枪内已装满的首匣）；
 * 实际后备发数在装备默认配件后按有效弹匣容量换算，避免硬编码单把武器的容量。
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoStartingWeaponEntry
{
	GENERATED_BODY()

	/** 开局发放的武器数据资产；为空或无法加载时跳过该条目 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loadout")
	TSoftObjectPtr<UGinkgoWeaponDefinition> Weapon;

	/** 随枪配发的后备弹匣数（不含枪内已装填的首匣）；0 表示仅带枪内弹药 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loadout", meta = (ClampMin = 0))
	int32 ReserveMagazines = 2;
};

/**
 * @class AGinkgoGameMode : CaseGinkgo 正式游戏模式
 * 负责玩家默认 Pawn/Controller 指定、检查点复活衔接与正式开局配装发放。
 * 不会生成任何调试靶子、调试敌人、地面展示武器或预览改装桌；
 * 默认仅向玩家发放 M4 与 M1911，各配 2 个弹匣的后备弹药（由 StartingLoadout 统一配置）。
 */
UCLASS()
class CASEGINKGO_API AGinkgoGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AGinkgoGameMode();

	/** 返回当前游戏模式配置的玩家开局配装列表 */
	const TArray<FGinkgoStartingWeaponEntry>& GetStartingLoadout() const { return StartingLoadout; }

	/**
	 * @brief 权威端向玩家角色发放正式开局武器与精确后备弹药
	 * @param Character - 刚完成生成与附身的玩家角色，必须处于权威端且已绑定有效骨骼网格
	 * @note 只发放 StartingLoadout 中的武器并将后备弹药精确设为「有效弹匣容量 × ReserveMagazines」，
	 *       不生成任何调试敌人、靶子、地面武器或预览工作台；检查点读档时下一帧的存档恢复会完整覆盖该配装。
	 */
	virtual void GrantStartingLoadout(AGinkgoCharacter& Character) const;

	//~ Begin AGameModeBase
	/** 已有检查点时在配置的复活点生成，之后由检查点系统应用待恢复的玩家快照。 */
	virtual void RestartPlayer(AController* NewPlayer) override;

	/** 在玩家控制器完成附身后发放正式开局配装，确保武器与后备弹药同步到单机与联机客户端。 */
	virtual void FinishRestartPlayer(AController* NewPlayer, const FRotator& StartRotation) override;
	//~ End AGameModeBase

protected:
	/** 玩家开局配装列表（默认仅包含 M4 与 M1911，各配 2 个弹匣的后备弹药），可在蓝图子类覆盖 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loadout")
	TArray<FGinkgoStartingWeaponEntry> StartingLoadout;
};
