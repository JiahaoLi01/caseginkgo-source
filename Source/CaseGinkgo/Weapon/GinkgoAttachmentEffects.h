#pragma once

#include "CoreMinimal.h"
#include "GinkgoAttachmentEffects.generated.h"

/**
 * 配件提供的操作与精度修正。百分比用小数表示，同类先相加，再作用于武器基础值。
 * 资产只存增量；武器实例在装配改变时汇总，不修改共享的武器或机构资产。
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoAttachmentEffects
{
	GENERATED_BODY()

	/** 开镜时间的相对改变量，-0.1 表示时间缩短 10%；汇总后最低保留基础时间的 65% */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects")
	float AdsTime = 0.f;

	/** 换弹动作时间的相对改变量；角色、武器轨道和通知共用调整后的播放速率 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects")
	float ReloadTime = 0.f;

	/** 开镜移动速度的相对改变量，仅作用于武器 ADS 地速倍率 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects")
	float AdsSpeed = 0.f;

	/** 非开镜持枪地速的相对改变量，含腰射、跑动和冲刺；按 ADS 进度淡出，避免与 AdsSpeed 重复相乘。0 保持原速 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects")
	float HipSpeed = 0.f;

	/** 腰射准星追随视线的弹簧角频率相对改变量，正值使跟随更快；不改变散布、阻尼比或 ADS 瞄准。汇总后最低保留 10% */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects")
	float HipHandling = 0.f;

	/** 腰射基础散布角的相对改变量，不缩放连射累积或其上限 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects")
	float HipSpread = 0.f;

	/** ADS 基础散布角的相对改变量，不缩放连射累积或其上限 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects")
	float AdsSpread = 0.f;

	/** 移动引起的额外散布的相对改变量；静止时不产生惩罚，0.1 表示移动增量增加 10% */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects")
	float MovingSpread = 0.f;

	/** 弹匣容量覆盖（发），0 保持基础容量；同一装配最多一个弹匣提供非零值，不会凭空增加弹药 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects", meta = (ClampMin = "0"))
	int32 MagazineCapacity = 0;

	/** 累加一个配件；容量由非零项覆盖，兼容性检查保证弹匣插槽唯一 */
	void Add(const FGinkgoAttachmentEffects& Other);

	/** 把相对时间改变量转为安全倍率；0 对应 1，避免组合产生零时长或倒放 */
	static float TimeScale(float Offset) { return FMath::Clamp(1.f + Offset, .65f, 1.5f); }
};
