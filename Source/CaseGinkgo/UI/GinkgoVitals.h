#pragma once

#include "CoreMinimal.h"

class UGinkgoDamageableComponent;

/**
 * 本地玩家生命 HUD 的只读数据：游戏线程在 Actor 更新后采样，变化时才发送页面消息。
 * 不维护另一份可扣减的血量，不订阅临时 Pawn 的伤害委托；换装、复活与换 Pawn 都能自然刷新。
 */
struct CASEGINKGO_API FGinkgoVitals
{
	/** 当前 Pawn 是否有生命组件及有效生命上限；没有 Pawn 时隐藏整个组件 */
	bool bAvailable = false;

	/** 当前生命点数，范围 [0, MaxHealth]；死亡时保持 0，不把 0 当作不存在 */
	float Health = 0.f;

	/** 生命上限，沿用伤害组件配置；不是百分比，默认角色为 100 */
	float MaxHealth = 0.f;

	/** 玩家全身防护池的剩余耐久，按装备汇总；0 时护甲环隐藏 */
	float Armor = 0.f;

	/** 当前装备的总护甲上限；已耗尽但仍穿着的装备也计入，卸下后才移除 */
	float MaxArmor = 0.f;

	/** 游戏线程读取；空组件或无效上限返回不可见快照 */
	static FGinkgoVitals Read(const UGinkgoDamageableComponent* Source);

	/** 防护池仍有耐久时才显示护甲环 */
	bool HasArmor() const;

	bool operator==(const FGinkgoVitals& Other) const;
};
