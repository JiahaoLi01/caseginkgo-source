#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/WeakInterfacePtr.h"     // 表里存的是接口的弱引用：目标被销毁时自动失效
#include "Interaction/GinkgoInteractable.h"
#include "GinkgoInteractionSubsystem.generated.h"

/**
 * @class UGinkgoInteractionSubsystem : 交互目标的表
 * 实现了 IGinkgoInteractable 的目标开始时登记、结束时注销（UGinkgoInteractableComponent 自己做；Actor 自己实现接口时也要这样做）。
 * 交互组件每帧查表：不用为交互建碰撞通道、做重叠查询。目标不多，按顺序扫一遍；多了再按空间划分（SmartObjects 的子系统那样）。
 * 只在游戏与 PIE 的世界里有。
 */
UCLASS()
class CASEGINKGO_API UGinkgoInteractionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** @brief 登记一个目标；没实现 IGinkgoInteractable 的不收 */
	void Register(UObject* Interactable);

	void Unregister(UObject* Interactable);

	/** @brief 在表里：服务器判断请求时先看这个，客户端发来的不是登记过的目标就不理 */
	bool IsRegistered(const UObject* Interactable) const;

	/** 登记了的目标；已经失效的（没注销就被回收）在下一次登记或注销时清掉，读的时候跳过 */
	const TArray<TWeakInterfacePtr<IGinkgoInteractable>>& GetInteractables() const { return Interactables; }

protected:
	//~ Begin UWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End UWorldSubsystem

private:
	TArray<TWeakInterfacePtr<IGinkgoInteractable>> Interactables;
};
