#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Weapon/GinkgoWeaponAttachment.h"
#include "GinkgoModularWeaponVisual.generated.h"

class UCustomizableObjectInstance;
class UCustomizableObject;
class UPoseableMeshComponent;

/** 动作已经结束、Mutable 尚未交付时的配件占位；只由本机外观对象持有，不参与库存或复制。 */
USTRUCT()
struct FGinkgoModuleHandoff
{
	GENERATED_BODY()

	/** 动作装好的配件；对应配置显示后才释放占位，防止客户端状态尚未复制时提前清理。 */
	UPROPERTY(Transient)
	TObjectPtr<const UGinkgoWeaponAttachment> Part;

	/** 从动作接管的临时网格，已挂回整枪；空选项可以没有几何，仍需要遮住旧模块。 */
	UPROPERTY(Transient)
	TObjectPtr<UPoseableMeshComponent> Preview;

	/** 旧组合中需要继续隐藏的模块骨骼，使用名称跨 Mutable 网格映射。 */
	FName Bone;

	/** 动作开始前该骨骼是否已隐藏，交接完成或取消时恢复原来的显示约束。 */
	bool bWasHidden = false;

	/** 已收到包含 Part 的装配请求；之后被另一配置替换时可丢弃，不等待过期结果。 */
	bool bRequested = false;
};

/**
 * 一把模块武器的本机 Mutable 外观，角色槽和拾取物各持有一份。
 * 装配改变才异步生成；期间保留上一份完整外观。完成回调校验请求序号和目标组件，迟到结果不能覆盖新枪。
 * PoseableMesh 继续由原有枪械动作驱动，Mutable 只替换网格与材质；玩法和复制不等待外观生成。
 * 安装动作结束后可将已落位预览移交给本对象；新组合真正显示时才同时销毁占位、恢复模块显隐。
 */
UCLASS()
class CASEGINKGO_API UGinkgoModularWeaponVisual : public UObject
{
	GENERATED_BODY()

public:
	/** 游戏线程同步目标；相同武器与装配不重复生成，Target 失效后回调不再写入 */
	void Update(const UGinkgoWeaponDefinition& Weapon, const FGinkgoInstalledAttachments& Installed, UPoseableMeshComponent& Target);

	/**
	 * @brief 动作结束时接管已落位的模块，直到对应 Mutable 结果真正显示。
	 * @return true 表示临时网格及骨骼显隐已移交；false 时仍由动作销毁预览并恢复显隐。
	 * @note 可以先于服务器装配提交调用；不改变玩法状态。已显示该配件或目标不匹配时无需接管。
	 */
	bool HoldPreview(const UGinkgoWeaponAttachment& Part, UPoseableMeshComponent& Target,
		UPoseableMeshComponent& Preview, FName Bone, bool bWasHidden);

	/** 换枪、清槽或拥有者退出时立即清理交接网格并作废异步结果，不等待垃圾回收。 */
	void Reset();

	/** 本机仍由外观系统持有的交接网格数；不包含动作正在操作的预览。 */
	int32 GetHandoffCount() const { return Handoffs.Num(); }

	/** 查询指定模块是否正被交接占位隐藏，不包含动作自身设置的隐藏状态。 */
	bool HasHandoff(FName Bone) const;

protected:
	virtual void BeginDestroy() override;

private:
	/** 对象已加载且编译完成后提交生成；PendingInstalled 始终是最后一次请求 */
	void Generate();

	/** 在当前目标上恢复该项的原始显隐并销毁临时网格；由调用方移除数组元素。 */
	void ReleaseHandoff(FGinkgoModuleHandoff& Handoff);

	/** 动作结束后仍可见的模块占位，实际生成成功、配置被替换或 Reset 时清理。 */
	UPROPERTY(Transient)
	TArray<FGinkgoModuleHandoff> Handoffs;

	/** 最后一次真正显示的装配，不能用 PendingInstalled 判断交接是否完成。 */
	UPROPERTY(Transient)
	FGinkgoInstalledAttachments DisplayedInstalled;

	/** 首次进入 PIE 时 Mutable 可能仍在加载编译数据；等待期间保留对象及最后一组参数 */
	UPROPERTY(Transient)
	TObjectPtr<UCustomizableObject> Object;

	/** 最后一次请求的模块选择，受 GC 跟踪，等待加载期间不会丢失配件引用 */
	UPROPERTY(Transient)
	FGinkgoInstalledAttachments PendingInstalled;

	/** 仅等待 Mutable 就绪时注册的游戏线程回调，生成后或销毁时移除 */
	FTSTicker::FDelegateHandle LoadingTick;

	/** 最新一次请求的 Mutable 实例，持有生成网格及材质；外观对象释放后随之回收 */
	UPROPERTY(Transient)
	TObjectPtr<UCustomizableObjectInstance> Instance;

	/** 请求对应的武器，切枪后不会复用旧枪的生成参数 */
	TWeakObjectPtr<const UGinkgoWeaponDefinition> Definition;

	/** 生成完成后写入的组件，只借用，外观对象不会延长角色或拾取物的生命周期 */
	TWeakObjectPtr<UPoseableMeshComponent> Mesh;

	/** 最近请求的装配签名，含挂点和选项，避免可见性刷新触发重复生成 */
	FString Configuration;

	/** 每次新请求递增；回调仅接受最新序号，防止连续改装的完成顺序颠倒 */
	uint32 Generation = 0;
};
