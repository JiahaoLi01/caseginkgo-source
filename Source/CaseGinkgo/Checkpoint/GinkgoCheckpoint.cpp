#include "Checkpoint/GinkgoCheckpoint.h"

#include "Checkpoint/GinkgoCheckpointSubsystem.h"
#include "Character/GinkgoCharacter.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"

AGinkgoCheckpoint::AGinkgoCheckpoint()
{
	PrimaryActorTick.bCanEverTick = false;
	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	SetRootComponent(Trigger);
	Trigger->SetBoxExtent(FVector(150., 250., 150.));
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Trigger->SetGenerateOverlapEvents(true);
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ThisClass::HandleOverlap);
	RespawnArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Respawn"));
	RespawnArrow->SetupAttachment(Trigger);
	RespawnArrow->SetRelativeTransform(RespawnPoint);
}

void AGinkgoCheckpoint::PostLoad()
{
	Super::PostLoad();
	// 已放进关卡的旧检查点可能序列化了单段 SoundWave；只迁移该旧默认值，保留手动指定的其它声音和空值。
	if (Announcement.Sound.ToSoftObjectPath() == FSoftObjectPath(TEXT("/Game/Audio/Checkpoint/SW_Checkpoint_Location.SW_Checkpoint_Location")))
	{
		Announcement.Sound = GetDefault<AGinkgoCheckpoint>()->Announcement.Sound;
	}
}

void AGinkgoCheckpoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RespawnArrow->SetRelativeTransform(RespawnPoint);
}

FTransform AGinkgoCheckpoint::GetRespawnTransform() const
{
	const FTransform WorldPoint = RespawnPoint * GetActorTransform();
	return FTransform(FRotator(0., WorldPoint.Rotator().Yaw, 0.), WorldPoint.GetLocation());
}

void AGinkgoCheckpoint::HandleOverlap(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent* OtherComponent, int32, bool, const FHitResult&)
{
	AGinkgoCharacter* Player = Cast<AGinkgoCharacter>(Other);
	if (Player && OtherComponent == Player->GetCapsuleComponent())
	{
		TryActivate(Player);
	}
}

bool AGinkgoCheckpoint::TryActivate(AGinkgoCharacter* Player)
{
	UGinkgoCheckpointSubsystem* Checkpoints = UGinkgoCheckpointSubsystem::Get(this);
	return Checkpoints && Checkpoints->Activate(*this, Player);
}
