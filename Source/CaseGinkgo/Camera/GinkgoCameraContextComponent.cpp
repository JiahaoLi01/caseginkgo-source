#include "GinkgoCameraContextComponent.h"
#include "Character/GinkgoCharacter.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Utilities/Debug/DebugCategory.h"

const FName UGinkgoCameraContextComponent::HeadBoneName(TEXT("head"));

UGinkgoCameraContextComponent::UGinkgoCameraContextComponent()
{
	// 只存数据：写的一方与相机各自在自己的时机读写
	PrimaryComponentTick.bCanEverTick = false;
}

UGinkgoCameraContextComponent* UGinkgoCameraContextComponent::Find(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UGinkgoCameraContextComponent>() : nullptr;
}

void UGinkgoCameraContextComponent::SetTags(const EGinkgoCameraContextSource Source, const FGameplayTagContainer& InTags)
{
	FGameplayTagContainer& Own = SourceTags[static_cast<int32>(Source)];
	if (Own == InTags)
	{
		return;
	}
	Own = InTags;
	Tags.Reset();
	for (const FGameplayTagContainer& Container : SourceTags)
	{
		Tags.AppendTags(Container);
	}
}

void UGinkgoCameraContextComponent::SetScalar(const FGameplayTag& Key, const float Value)
{
	Scalars.Add(Key, Value);
}

TOptional<float> UGinkgoCameraContextComponent::FindScalar(const FGameplayTag& Key) const
{
	const float* Value = Scalars.Find(Key);
	return Value ? TOptional<float>(*Value) : TOptional<float>();
}

void UGinkgoCameraContextComponent::PushEvent(const FGinkgoCameraEvent& Event)
{
	if (!bCollectEvents) return;
	if (Events.Num() >= MaxEvents)
	{
		UE_LOG(LogGinkgoCamera, Warning, TEXT("[Camera] %s 的相机事件队列满了（%d 个，相机没取？），丢掉最早的 %s"), *GetNameSafe(GetOwner()), MaxEvents,
			*Events[0].Tag.ToString());
		Events.RemoveAt(0);
	}
	Events.Add(Event);
	++PushedEventCount;
}

void UGinkgoCameraContextComponent::SetCollectEvents(const bool bCollect)
{
	bCollectEvents = bCollect;
	if (!bCollect) Events.Reset();
}

TArray<FGinkgoCameraEvent> UGinkgoCameraContextComponent::TakeEvents()
{
	TArray<FGinkgoCameraEvent> Taken = MoveTemp(Events);
	Events.Reset();
	return Taken;
}

void UGinkgoCameraContextComponent::SetSight(const USceneComponent* Component, const FGinkgoScope* Scope, const FTransform& Offset)
{
	if (!Component || !Scope)
	{
		SightComponent.Reset();
		SightOffset = FTransform::Identity;
		return;
	}
	SightComponent = Component;
	SightScope = *Scope;
	SightOffset = Offset;
}

void UGinkgoCameraContextComponent::SetHeadHidden(const bool bHidden)
{
	if (bHeadHidden == bHidden)
	{
		return;
	}
	bHeadHidden = bHidden;
	const auto* Character = Cast<ACharacter>(GetOwner());
	const auto* Ginkgo = Cast<AGinkgoCharacter>(Character);
	USkeletalMeshComponent* Mesh = Ginkgo ? Ginkgo->GetPresentationMesh() : (Character ? Character->GetMesh() : nullptr);
	if (!Mesh)
	{
		return;
	}
	if (bHidden)
	{
		Mesh->HideBoneByName(HeadBoneName, PBO_None);
	}
	else
	{
		Mesh->UnHideBoneByName(HeadBoneName);
	}
	UE_LOG(LogGinkgoCamera, Verbose, TEXT("[Camera] %s 第一人称：头%s"), *GetNameSafe(GetOwner()), bHidden ? TEXT("藏起来") : TEXT("露出来"));
}
