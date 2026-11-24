#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GinkgoPenetrableComponent.generated.h"

/**
 * @class UGinkgoPenetrableComponent : 能被子弹击穿的东西（Docs/Design/Damage.md「穿射」）
 * 挂在 actor 上，整个 actor 算一个东西、一个厚度：子弹打到它，剩的掩体穿射系数（UGinkgoAmmoDefinition::CoverPenetration）比厚度大就穿过去、减掉厚度，
 * 不够就停在它里面。没有这个组件的东西子弹穿不过。
 * 角色与墙挂的是同一个（你定的，2026-11-11）：人形靶、玩家角色默认带着，用默认的厚度（人 0.3）；墙在关卡里加上这个组件、按材料填厚度。
 * 只是数据，不 tick
 */
UCLASS(ClassGroup = (Ginkgo), meta = (BlueprintSpawnableComponent))
class CASEGINKGO_API UGinkgoPenetrableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGinkgoPenetrableComponent();

	/**
	 * @brief 子弹打到的这个 actor 多厚
	 * @return 它的厚度；没有这个组件（子弹穿不过）时为负
	 */
	static float FindThickness(const AActor* Actor);

	/** 人的厚度（你定的，2026-11-11）：组件默认是它，人形靶、玩家角色就用默认的 */
	static constexpr float CharacterThickness = 0.3f;

	/**
	 * 厚度：同掩体穿射系数一个单位，子弹穿过去减掉这么多。默认是人的厚度 0.3；墙按材料填（Docs/Design/Damage.md「穿射」的参考：
	 * 石膏板隔墙 0.1、木板墙 0.25、砖墙 0.5、混凝土 1.0 以上什么都穿不过）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Penetration", meta = (ClampMin = 0))
	float Thickness = CharacterThickness;
};
