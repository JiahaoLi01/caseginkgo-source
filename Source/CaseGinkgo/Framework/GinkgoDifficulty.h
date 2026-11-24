#pragma once

#include "CoreMinimal.h"
#include "GinkgoDifficulty.generated.h"

/**
 * @enum EGinkgoDifficulty : 游戏战斗难度档位
 * 由玩家在主菜单「新游戏」流程中选择，持有于 UGinkgoTitleSubsystem 并随检查点存档（FGinkgoCheckpointState）持久化。
 * 仅缩放玩家控制角色承受的伤害，不影响敌方单位与练习靶的受击结算。
 */
UENUM(BlueprintType)
enum class EGinkgoDifficulty : uint8
{
	/** 娱乐：适用于仅想探索游戏的玩家，几乎不提供任何挑战；玩家承受伤害降低 90%（倍率 0.10） */
	Casual UMETA(DisplayName = "娱乐"),

	/** 平衡：推荐难度，提供娱乐性与挑战性的平衡；玩家承受伤害降低 50%（倍率 0.50） */
	Balanced UMETA(DisplayName = "平衡"),

	/** 地狱：极具挑战性的难度，适用于拥有丰富战斗技巧与理念的大师玩家；不提供任何减伤（倍率 1.00） */
	Hell UMETA(DisplayName = "地狱"),
};

namespace Ginkgo::Difficulty
{
	/**
	 * @brief 返回指定难度下玩家承受伤害的线性倍率（范围 [0.1, 1.0]）
	 * @param Difficulty - 当前生效的游戏难度
	 * @return 娱乐 0.10（-90%），平衡 0.50（-50%），地狱 1.00（无减伤）
	 */
	inline float GetPlayerIncomingDamageScale(const EGinkgoDifficulty Difficulty)
	{
		switch (Difficulty)
		{
		case EGinkgoDifficulty::Casual:
			return 0.10f;
		case EGinkgoDifficulty::Balanced:
			return 0.50f;
		case EGinkgoDifficulty::Hell:
			return 1.0f;
		default:
			return 0.50f;
		}
	}

	/** 返回供 Weft 页面与日志通信使用的稳定小写标识（casual / balanced / hell） */
	inline FString ToId(const EGinkgoDifficulty Difficulty)
	{
		switch (Difficulty)
		{
		case EGinkgoDifficulty::Casual:
			return TEXT("casual");
		case EGinkgoDifficulty::Balanced:
			return TEXT("balanced");
		case EGinkgoDifficulty::Hell:
			return TEXT("hell");
		default:
			return TEXT("balanced");
		}
	}

	/** 解析 Weft 页面或命令传来的难度标识；无法识别时返回 false 且不改写 OutDifficulty */
	inline bool TryParseId(const FString& Id, EGinkgoDifficulty& OutDifficulty)
	{
		if (Id.Equals(TEXT("casual"), ESearchCase::IgnoreCase))
		{
			OutDifficulty = EGinkgoDifficulty::Casual;
			return true;
		}
		if (Id.Equals(TEXT("balanced"), ESearchCase::IgnoreCase))
		{
			OutDifficulty = EGinkgoDifficulty::Balanced;
			return true;
		}
		if (Id.Equals(TEXT("hell"), ESearchCase::IgnoreCase))
		{
			OutDifficulty = EGinkgoDifficulty::Hell;
			return true;
		}
		return false;
	}

	/** 返回用于主菜单、暂停菜单与日志展示的中文难度名称 */
	inline FText GetDisplayName(const EGinkgoDifficulty Difficulty)
	{
		switch (Difficulty)
		{
		case EGinkgoDifficulty::Casual:
			return NSLOCTEXT("GinkgoDifficulty", "Casual", "娱乐");
		case EGinkgoDifficulty::Balanced:
			return NSLOCTEXT("GinkgoDifficulty", "Balanced", "平衡");
		case EGinkgoDifficulty::Hell:
			return NSLOCTEXT("GinkgoDifficulty", "Hell", "地狱");
		default:
			return NSLOCTEXT("GinkgoDifficulty", "Balanced", "平衡");
		}
	}
}
