#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interaction/GinkgoInteractable.h"
#include "UObject/WeakInterfacePtr.h"     // 候选里存目标的弱引用：目标被拿走、销毁时自动失效
#include "GinkgoInteractorComponent.generated.h"

class AGinkgoCharacter;
class AGinkgoWorkbench;
class UGinkgoWeaponAttachment;

/**
 * @class UGinkgoInteractorComponent : 交互组件
 * 角色上的组件，见 Docs/Design/Interaction.md「交互组件（角色）」「联网」。
 *
 * 调用时机：
 * - 本地控制的角色每帧在 TG_PostUpdateWork 更新：引擎在这之前更新相机，焦点用的是这一帧最后的相机。服务器与模拟代理上不更新。
 *   从 UGinkgoInteractionSubsystem 的表里取提示距离以内的目标，判断看不看得见、条件，选出焦点，给每个目标一个状态（提示的界面读）。
 * - 控制器在 PlayerTick 里把交互键的按下交给 RequestInteract：交互的是上一帧算出、画在屏幕上的焦点。
 *
 * 联网：拾取改的是大家都要看到的东西，由服务器做。客户端发 ServerInteract（可靠），服务器再判断一遍（目标在表里、条件、距离加容差）、
 * 执行，用 ClientInteractResult 回结果；结果本身靠复制回来。单机与主机上本地就是服务器，直接执行。
 *
 * 实现注意：组件开着复制（没有要复制的属性），RPC 走角色的通道。
 */
UCLASS(ClassGroup = (Ginkgo))
class CASEGINKGO_API UGinkgoInteractorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 这一帧提示距离以内的一个目标 */
	struct FCandidate
	{
		TWeakInterfacePtr<IGinkgoInteractable> Target;

		/** 提示上的样子 */
		EGinkgoInteractionState State = EGinkgoInteractionState::Hidden;

		/** 到角色胶囊中轴线段的距离（cm） */
		double Distance = 0.0;

		/** 离准星的角度（度） */
		double ViewAngle = 0.0;

		/** 提示圈显示得出来：相机看得见，或者目标隔墙也显示 */
		bool bPromptVisible = false;

		/** 在交互距离以内（带滞回） */
		bool bInInteractRange = false;

		/** 在交互距离以内，而且角色的眼睛看得见：能当焦点 */
		bool bReachable = false;

		/** 目标自己的条件 */
		EGinkgoInteractionAvailability Availability = EGinkgoInteractionAvailability::Available;

		/** 提示上的字，比如"拾取 98k" */
		FText Text;

		/** 不能交互的原因 */
		FText Reason;
	};

	UGinkgoInteractorComponent();

	/**
	 * @brief 按下了交互键
	 * @note 交互上一帧算出的焦点（看到哪个就是哪个），它要是"可以交互"。在等服务器回复时不再发
	 */
	void RequestInteract();

	/** 桌前换到指定的武器槽 */
	void SelectWorkbenchSlot(int32 Slot);

	/** 请求安装所选配件，服务器复查武器槽和配件 */
	void InstallWorkbenchAttachment(UGinkgoWeaponAttachment* Part);

	/** 这一帧提示距离以内的目标；状态为 Hidden 的不显示 */
	const TArray<FCandidate>& GetCandidates() const { return Candidates; }

	/** 焦点；没有时为空 */
	const FCandidate* GetFocus() const;

	/** 在等服务器回复的请求的目标；不在等时为空 */
	UObject* GetPendingTarget() const;

	/** 最近一次请求的结果与它的时刻（世界时间）；还没有过时时刻为负 */
	EGinkgoInteractionResult GetLastResult() const { return LastResult; }
	double GetLastResultTime() const { return LastResultTime; }

	//~ Begin UActorComponent
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

private:
	UFUNCTION(Server, Reliable)
	void ServerLeaveWorkbench(AGinkgoWorkbench* Bench);

	UFUNCTION(Server, Reliable)
	void ServerSelectWorkbenchSlot(AGinkgoWorkbench* Bench, int32 Slot);
	UFUNCTION(Server, Reliable)
	void ServerInstallWorkbenchAttachment(AGinkgoWorkbench* Bench, int32 Slot, UGinkgoWeaponAttachment* Part);

	/** 客户端请求交互：服务器判断、执行，再回结果 */
	UFUNCTION(Server, Reliable)
	void ServerInteract(UObject* Target, uint8 RequestId, int32 WeaponSlot);

	UFUNCTION(Client, Reliable)
	void ClientInteractResult(uint8 RequestId, EGinkgoInteractionResult Result);

	/**
	 * @brief 在服务器上（单机与主机是本地）判断并执行一次交互
	 * @param Target - 客户端说的目标：要在表里
	 * @param OutDistance - 服务器上的距离（cm），没量到时为负；联网实测拿它和客户端按下时的距离比
	 */
	EGinkgoInteractionResult ExecuteInteraction(UObject* Target, double& OutDistance, int32 WeaponSlot);

	/** 请求有了结果（服务器回的、本地执行的、等不到的）：记下来，再交给目标收尾它本地开始的东西（OnInteractResult） */
	void FinishRequest(UObject* Target, EGinkgoInteractionResult Result);

	/** 本地控制的角色每帧：候选、状态、焦点 */
	void UpdateCandidates();

	AGinkgoCharacter* GetGinkgoCharacter() const;

	bool IsLocallyControlled() const;

	FGinkgoInteractionContext MakeContext();

	/** 角色胶囊的中轴线段：距离量到它 */
	void GetCapsuleSegment(FVector& OutBottom, FVector& OutTop) const;

#if !UE_BUILD_SHIPPING
	/** ginkgo.showInteraction：两个距离的圈、量距离的线段、焦点与每个目标的状态（实现在 _Debug.cpp） */
	void DrawDebug() const;
#endif

	TArray<FCandidate> Candidates;

	/** 焦点在 Candidates 里的下标 */
	int32 FocusIndex = INDEX_NONE;

	/** 上一个发出的请求编号，0 不用（表示没有请求） */
	uint8 LastRequestId = 0;

	/** 在等服务器回复的请求编号，0 是没有在等 */
	uint8 PendingRequestId = 0;

	TWeakObjectPtr<UObject> PendingTarget;

	/** 发出请求的世界时间（等不到结果的上限按它算）与挂钟时间（日志里的等了多久） */
	double PendingSince = 0.0;
	double PendingSincePlatform = 0.0;

	EGinkgoInteractionResult LastResult = EGinkgoInteractionResult::Accepted;
	double LastResultTime = -1.0;
};
