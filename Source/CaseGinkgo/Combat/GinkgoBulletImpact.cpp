#include "Combat/GinkgoBulletImpact.h"

#include "Combat/GinkgoDamageableComponent.h"
#include "Components/DecalComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

bool FGinkgoBulletImpact::FromHit(const FHitResult& Hit, FGinkgoBulletImpact& Out)
{
	UPrimitiveComponent* Component = Hit.GetComponent();
	if (!Hit.IsValidBlockingHit() || !IsValid(Component) || Hit.ImpactNormal.IsNearlyZero())
	{
		return false;
	}
	const AActor* Actor = Hit.GetActor();
	Out = {};
	// 玩家、敌人与人形练习靶都用带受伤组件的骨骼身体接弹，附近的独立墙体仍按自身的命中结果处理。
	Out.bBlood = Component->IsA<USkeletalMeshComponent>() && IsValid(Actor) && Actor->FindComponentByClass<UGinkgoDamageableComponent>();
	Out.bAttached = Component->Mobility == EComponentMobility::Movable;
	if (Out.bAttached)
	{
		Out.Receiver = Component;
		Out.Bone = Hit.BoneName;
		const FTransform Frame = Component->GetSocketTransform(Out.Bone);
		Out.Point = Frame.InverseTransformPosition(Hit.ImpactPoint);
		Out.Normal = Frame.InverseTransformVectorNoScale(Hit.ImpactNormal).GetSafeNormal();
	}
	else
	{
		Out.Point = Hit.ImpactPoint;
		Out.Normal = Hit.ImpactNormal;
	}
	return true;
}

void UGinkgoBulletImpactSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.IsNetMode(NM_DedicatedServer) || !FApp::CanEverRender()) return;

	Dust = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Weapons/FX/NS_BulletImpactDust.NS_BulletImpactDust"));
	BloodMist = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Weapons/FX/NS_BulletImpactBlood.NS_BulletImpactBlood"));
	Chip = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Weapons/FX/M_BulletImpactChip.M_BulletImpactChip"));
	Stain = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Weapons/FX/M_BulletImpactStain.M_BulletImpactStain"));
}

void UGinkgoBulletImpactSubsystem::Play(const FGinkgoBulletImpact& Impact)
{
	if (Impact.bAttached && !IsValid(Impact.Receiver)) return;
	const FTransform Frame = Impact.bAttached ? Impact.Receiver->GetSocketTransform(Impact.Bone) : FTransform::Identity;
	const FVector Point = Frame.TransformPosition(Impact.Point);
	const FVector Normal = Frame.TransformVectorNoScale(Impact.Normal);
	if (UNiagaraSystem* Burst = Impact.bBlood ? BloodMist.Get() : Dust.Get())
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Burst, Point + Normal, Normal.Rotation(), FVector::OneVector,
			true, true, ENCPoolMethod::AutoRelease);
	}

	UMaterialInterface* Material = Impact.bBlood ? Stain.Get() : Chip.Get();
	if (!Material) return;
	const float Radius = Impact.bBlood ? FMath::FRandRange(7.f, 10.f) : FMath::FRandRange(3.5f, 5.f);
	const FVector Size(Impact.bBlood ? 6.f : 2.f, Radius, Radius);
	FRotator Rotation = (-Normal).Rotation();
	Rotation.Roll = FMath::FRandRange(-180.f, 180.f);
	UDecalComponent* Mark = Impact.bAttached
		? UGameplayStatics::SpawnDecalAttached(Material, Size, Impact.Receiver, Impact.Bone, Point, Rotation, EAttachLocation::KeepWorldPosition)
		: UGameplayStatics::SpawnDecalAtLocation(this, Material, Size, Point, Rotation);
	if (!Mark) return;
	Mark->SetFadeScreenSize(0.001f);
	Mark->SetFadeOut(Impact.bBlood ? 8.f : 12.f, 1.f, false);
	constexpr int32 MaxMarks = 96;
	if (Marks.Num() < MaxMarks)
	{
		Marks.Add(Mark);
	}
	else
	{
		if (UDecalComponent* Oldest = Marks[NextMark].Get()) Oldest->DestroyComponent();
		Marks[NextMark] = Mark;
		NextMark = (NextMark + 1) % MaxMarks;
	}
}
