#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GinkgoRecoilPattern.generated.h"

class UAssetImportData;

/**
 * @class UGinkgoRecoilPattern : 后坐模板
 * 连发的枪的瞄准后坐（Docs/Design/Weapons.md「后坐模板」）：逐发的绝对落点，度。单发、栓动的枪不用它，填武器上的数。
 * 用网页工具 Tools/RecoilEditor 编辑，导出 .recoil（JSON 文本，进版本控制）再导入成这个资产，不用插件（UGinkgoRecoilPatternFactory）。
 * 运行时每打一发取到下一发的增量写进视线（ControlRotation），所以准星跟着子弹走，压枪 1:1 抵消。
 * 这里只有弹道的形状；状态倍率、分摊、停火回复在武器上（FGinkgoPatternRecoil），同一个模板可以给不同的枪用
 */
UCLASS(BlueprintType)
class CASEGINKGO_API UGinkgoRecoilPattern : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * @brief 第 ShotIndex 发（从 0 数）打完以后，到下一发视线要走多少
	 * @param ShotIndex - 小于 0 按 0 算；打到头以后从 LoopFrom 接着循环
	 * @return X 是偏航，向右为正；Y 是俯仰，向上为正（度）。少于两发时是 0
	 */
	FVector2f GetStepAfterShot(int32 ShotIndex) const;

#if WITH_EDITOR
	/**
	 * @brief 读 .recoil 的文本
	 * @param Text - JSON：format 是 ginkgo.recoil，version 是 1，shots 是 [偏航, 俯仰] 的数组，loopFrom、jitter 可以没有
	 * @param OutError - 读不成时写原因（第几发出的错）
	 * @return 读成了没有；读不成时模板不变
	 */
	bool ImportFromJson(const FString& Text, FString& OutError);
#endif

#if WITH_EDITORONLY_DATA
	//~ Begin UObject
	virtual void PostInitProperties() override;
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;
	//~ End UObject

	/** 从哪个 .recoil 导入的：右键 Reimport 读它 */
	UPROPERTY(VisibleAnywhere, Instanced, Category = "Import")
	TObjectPtr<UAssetImportData> AssetImportData;
#endif

	/** 逐发的落点（度，相对第一发按下时的瞄准方向）：X 偏航，向右为正；Y 俯仰，向上为正。Shots[i] 是第 i 发（从 0 数）打的地方 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil")
	TArray<FVector2f> Shots;

	/** 打到头以后，从第几发（从 0 数）接着循环。大了只循环最后几发的走向，0 是整条重走一遍。弹匣比模板短时用不上 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 0))
	int32 LoopFrom = 0;

	/** 每发在模板之外再随机偏多少（度，±）：X 偏航，Y 俯仰。0 是每次弹道一样（CS2 的 AK 是 0），大了压枪压不干净 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 0, Units = "Degrees"))
	FVector2f Jitter = FVector2f::ZeroVector;
};
