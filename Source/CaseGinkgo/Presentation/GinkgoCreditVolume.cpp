#include "Presentation/GinkgoCreditVolume.h"

#include "Character/GinkgoCharacter.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Control/GinkgoPlayerController.h"

AGinkgoCreditVolume::AGinkgoCreditVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	SetRootComponent(Trigger);
	Trigger->SetBoxExtent(FVector(150., 250., 150.));
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::HandleBegin);
	Trigger->OnComponentEndOverlap.AddDynamic(this, &ThisClass::HandleEnd);
}

void AGinkgoCreditVolume::HandleBegin(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent* OtherComponent, int32, bool, const FHitResult&)
{
	if (auto* Player = Cast<AGinkgoCharacter>(Other); Player && OtherComponent == Player->GetCapsuleComponent()) { TryActivate(Player); }
}

void AGinkgoCreditVolume::HandleEnd(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent* OtherComponent, int32)
{
	if (const auto* Player = Cast<AGinkgoCharacter>(Other); Player && OtherComponent == Player->GetCapsuleComponent())
	{
		Inside.Remove(Cast<AGinkgoPlayerController>(Player->GetController()));
	}
}

bool AGinkgoCreditVolume::TryActivate(AGinkgoCharacter* Player)
{
	auto* PC = Player ? Cast<AGinkgoPlayerController>(Player->GetController()) : nullptr;
	if (!HasAuthority() || !Player || !Player->HasAuthority() || !PC || Player->GetDamageable()->IsDead()
		|| Inside.Contains(PC) || (!bRepeatable && Triggered.Contains(PC))) { return false; }
	TArray<FGinkgoCredit> Valid;
	for (const auto& Credit : Credits)
	{
		if (Credit.HasContent()) { Valid.Add(Credit); }
	}
	if (Valid.IsEmpty()) { return false; }
	Inside.Add(PC);
	Triggered.Add(PC);
	PC->ClientShowCredits(Valid);
	return true;
}
