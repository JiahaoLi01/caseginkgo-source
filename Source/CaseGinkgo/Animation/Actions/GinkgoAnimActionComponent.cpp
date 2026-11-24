#include "Animation/Actions/GinkgoAnimActionComponent.h"

#include "AnimNotifies/AnimNotify_PlayMontageNotify.h"  // 事件的时刻：蒙太奇里这个名字的通知
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

/**
 * @class FGinkgoAnimInstanceMontageHost : 动画实例当蒙太奇宿主
 * 实例 ID 是引擎的 FAnimMontageInstance::GetInstanceID；淡出与结束的委托绑在放的那个实例上，回调带上实例 ID 交给规则。
 */
class FGinkgoAnimInstanceMontageHost final : public IGinkgoMontageHost
{
public:
	TWeakObjectPtr<UAnimInstance> AnimInstance;
	TWeakPtr<FGinkgoAnimActionRunner> Runner;

	/** 淡出与结束的委托的拥有者：组件没了，委托就不再调 */
	TWeakObjectPtr<UObject> DelegateOwner;

	virtual int32 PlayMontage(UAnimMontage* Montage, const float PlayRate, const FName StartSection, const float StartPosition, const float BlendInTime) override
	{
		// 同一组里原来的蒙太奇在 Montage_Play 里以打断的方式停掉，它的淡出回调当场回来（规则认出那一步已经结束）
		UAnimInstance* Anim = AnimInstance.Get();
		if (!Anim || !Montage)
		{
			return INDEX_NONE;
		}
		FAlphaBlendArgs BlendIn(Montage->BlendIn);
		BlendIn.BlendTime = BlendInTime;
		if ((BlendInTime < 0.f ? Anim->Montage_Play(Montage, PlayRate) : Anim->Montage_PlayWithBlendIn(Montage, BlendIn, PlayRate)) <= 0.f)
		{
			return INDEX_NONE;
		}
		FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(Montage);
		if (!Instance)
		{
			return INDEX_NONE;
		}
		if (!StartSection.IsNone())
		{
			Instance->JumpToSectionName(StartSection);
		}
		if (StartPosition > 0.f)
		{
			Instance->SetPosition(StartPosition);
		}

		// Montage_Set*Delegate 绑的是这个蒙太奇当前活动的实例，也就是刚放的这个
		const int32 InstanceID = Instance->GetInstanceID();
		const TWeakPtr<FGinkgoAnimActionRunner> WeakRunner = Runner;
		FOnMontageBlendingOutStarted BlendingOut = FOnMontageBlendingOutStarted::CreateWeakLambda(DelegateOwner.Get(),
			[WeakRunner, InstanceID](UAnimMontage*, const bool bInterrupted)
			{
				if (const TSharedPtr<FGinkgoAnimActionRunner> Pinned = WeakRunner.Pin())
				{
					Pinned->HandleBlendingOut(InstanceID, bInterrupted);
				}
			});
		Anim->Montage_SetBlendingOutDelegate(BlendingOut, Montage);
		FOnMontageEnded Ended = FOnMontageEnded::CreateWeakLambda(DelegateOwner.Get(),
			[WeakRunner, InstanceID](UAnimMontage*, const bool bInterrupted)
			{
				if (const TSharedPtr<FGinkgoAnimActionRunner> Pinned = WeakRunner.Pin())
				{
					Pinned->HandleEnded(InstanceID, bInterrupted);
				}
			});
		Anim->Montage_SetEndDelegate(Ended, Montage);
		return InstanceID;
	}

	virtual void StopMontage(const int32 InstanceID, const float BlendOutTime) override
	{
		FAnimMontageInstance* Instance = FindInstance(InstanceID);
		if (!Instance || !Instance->IsActive() || !Instance->Montage)
		{
			return;
		}
		const FAlphaBlend& BlendOut = Instance->Montage->BlendOut;
		Instance->Stop(FAlphaBlend(BlendOut, BlendOutTime >= 0.f ? BlendOutTime : BlendOut.GetBlendTime()));
	}

	virtual void JumpToSection(const int32 InstanceID, const FName Section) override
	{
		if (FAnimMontageInstance* Instance = FindInstance(InstanceID))
		{
			Instance->JumpToSectionName(Section);
		}
	}

	virtual void SetNextSection(const int32 InstanceID, const FName From, const FName To) override
	{
		if (FAnimMontageInstance* Instance = FindInstance(InstanceID))
		{
			Instance->SetNextSectionName(From, To);
		}
	}

	virtual bool IsInstanceAlive(const int32 InstanceID) const override
	{
		return FindInstance(InstanceID) != nullptr;
	}

	virtual float GetPosition(const int32 InstanceID) const override
	{
		const FAnimMontageInstance* Instance = FindInstance(InstanceID);
		return Instance ? Instance->GetPosition() : 0.f;
	}

	virtual FName GetGroup(const UAnimMontage* Montage) const override
	{
		return Montage ? Montage->GetGroupName() : NAME_None;
	}

	virtual float GetEventTime(const UAnimMontage* Montage, const FName Event) const override
	{
		if (!Montage)
		{
			return -1.f;
		}
		for (const FAnimNotifyEvent& Notify : Montage->Notifies)
		{
			const UAnimNotify_PlayMontageNotify* PlayMontageNotify = Cast<UAnimNotify_PlayMontageNotify>(Notify.Notify);
			if (PlayMontageNotify && FName(*PlayMontageNotify->GetNotifyName()) == Event)
			{
				return Notify.GetTime();
			}
		}
		return -1.f;
	}

private:
	FAnimMontageInstance* FindInstance(const int32 InstanceID) const
	{
		UAnimInstance* Anim = AnimInstance.Get();
		return Anim && InstanceID != INDEX_NONE ? Anim->GetMontageInstanceForID(InstanceID) : nullptr;
	}
};

// ===== 组件 =====

UGinkgoAnimActionComponent::UGinkgoAnimActionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

bool UGinkgoAnimActionComponent::Start(UGinkgoAnimAction* Action, FGinkgoAnimActionRunner::FFinished OnFinished)
{
	return ensureMsgf(Runner, TEXT("动画动作的执行器还没开始（BeginPlay 之前）")) && Runner->Start(Action, MoveTemp(OnFinished));
}

bool UGinkgoAnimActionComponent::CanOccupy(const FGameplayTag& Tag, const int32 Priority) const
{
	return !Runner || Runner->CanOccupy(Tag, Priority);
}

void UGinkgoAnimActionComponent::FlushPending()
{
	BindAnimInstance();
	if (Runner) { Runner->FlushPending(); }
}

void UGinkgoAnimActionComponent::NotifySimulationEvent(const int32 InstanceID, const FName Event)
{
	if (Runner) { Runner->HandleNotify(InstanceID, Event); }
}

int32 UGinkgoAnimActionComponent::Occupy(const FGameplayTag& Tag, const int32 Priority)
{
	return Runner ? Runner->Occupy(Tag, Priority) : INDEX_NONE;
}

void UGinkgoAnimActionComponent::ReleaseOccupancy(const int32 Handle)
{
	if (Runner)
	{
		Runner->ReleaseOccupancy(Handle);
	}
}

void UGinkgoAnimActionComponent::Cancel(UGinkgoAnimAction* Action, const FGameplayTag& Reason)
{
	if (Runner)
	{
		Runner->Cancel(Action, Reason);
	}
}

void UGinkgoAnimActionComponent::RequestStop(UGinkgoAnimAction* Action, const FGameplayTag& Reason)
{
	if (Runner)
	{
		Runner->RequestStop(Action, Reason);
	}
}

#if !UE_BUILD_SHIPPING
TArray<FString> UGinkgoAnimActionComponent::TakeDebugLog()
{
	return Runner ? Runner->TakeDebugLog() : TArray<FString>();
}
#endif

void UGinkgoAnimActionComponent::BeginPlay()
{
	Super::BeginPlay();

	Host = MakeShared<FGinkgoAnimInstanceMontageHost>();
	Host->DelegateOwner = this;
	Runner = MakeShared<FGinkgoAnimActionRunner>(*Host, GetOwner());
	Host->Runner = Runner;

	// 移动组件之后：中止条件看到的是这一帧移动以后的位置；网格体之前：这里放的蒙太奇这一帧就推进
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			PrimaryComponentTick.AddPrerequisite(Movement, Movement->PrimaryComponentTick);
		}
		if (USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			Mesh->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
		}
	}
	BindAnimInstance();
}

void UGinkgoAnimActionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UAnimInstance* Previous = BoundAnimInstance.Get())
	{
		Previous->OnPlayMontageNotifyBegin.RemoveDynamic(this, &ThisClass::HandleMontageNotifyBegin);
		Previous->OnPlayMontageNotifyEnd.RemoveDynamic(this, &ThisClass::HandleMontageNotifyEnd);
	}
	BoundAnimInstance.Reset();
	// 角色没了：在跑的动作不再有结局（拥有者也在消失），规则与宿主一起放掉
	Runner.Reset();
	Host.Reset();
	Super::EndPlay(EndPlayReason);
}

void UGinkgoAnimActionComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	BindAnimInstance();
	if (Runner)
	{
		Runner->Tick(DeltaTime);
	}
}

UAnimInstance* UGinkgoAnimActionComponent::FindAnimInstance() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	return Mesh ? Mesh->GetAnimInstance() : nullptr;
}

void UGinkgoAnimActionComponent::BindAnimInstance()
{
	UAnimInstance* Current = FindAnimInstance();
	if (Current == BoundAnimInstance.Get())
	{
		return;
	}
	if (UAnimInstance* Previous = BoundAnimInstance.Get())
	{
		Previous->OnPlayMontageNotifyBegin.RemoveDynamic(this, &ThisClass::HandleMontageNotifyBegin);
		Previous->OnPlayMontageNotifyEnd.RemoveDynamic(this, &ThisClass::HandleMontageNotifyEnd);
	}
	BoundAnimInstance = Current;
	if (Host)
	{
		Host->AnimInstance = Current;
	}
	if (Current)
	{
		Current->OnPlayMontageNotifyBegin.AddDynamic(this, &ThisClass::HandleMontageNotifyBegin);
		Current->OnPlayMontageNotifyEnd.AddDynamic(this, &ThisClass::HandleMontageNotifyEnd);
	}
}

void UGinkgoAnimActionComponent::HandleMontageNotifyBegin(const FName NotifyName, const FBranchingPointNotifyPayload& Payload)
{
	if (!Runner)
	{
		return;
	}
	// 一个时刻的通知（PlayMontageNotify）与窗口（PlayMontageNotifyWindow）走同一个多播：有通知状态的是窗口
	if (Payload.NotifyEvent && Payload.NotifyEvent->NotifyStateClass)
	{
		Runner->HandleWindowBegin(Payload.MontageInstanceID, NotifyName);
	}
	else
	{
		Runner->HandleNotify(Payload.MontageInstanceID, NotifyName);
	}
}

void UGinkgoAnimActionComponent::HandleMontageNotifyEnd(const FName NotifyName, const FBranchingPointNotifyPayload& Payload)
{
	if (Runner)
	{
		Runner->HandleWindowEnd(Payload.MontageInstanceID, NotifyName);
	}
}
