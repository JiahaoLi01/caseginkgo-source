#include "Combat/GinkgoGrenadeProjectile.h"

#include "AI/GinkgoAICharacter.h"
#include "AI/GinkgoAIController.h"
#include "AI/GinkgoNoiseEmitterComponent.h"
#include "Character/GinkgoCharacter.h"
#include "Combat/GinkgoAmmoDefinition.h"
#include "Combat/GinkgoBulletImpact.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Combat/GinkgoTargetRegistry.h"
#include "Components/PointLightComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Weapon/GinkgoWeaponComponent.h"

namespace
{
	/** 抛射物飞行积分频率（Hz），与投掷物一致以保证不同帧率下扫掠轨迹一致 */
	constexpr int32 FlightHz = 60;

	/** 40mm 榴弹头球形碰撞扫掠半径（cm，对应 40mm 口径半径 2.0 cm） */
	constexpr float SweepRadius = 2.f;

	/** M79 1:48 英寸右旋膛线出膛自旋角速度（度/秒，约 62 转/秒） */
	constexpr float RifleSpinDegPerSec = 22400.f;

	/** 高爆碰炸火球与闪光灯持续消散时间（秒） */
	constexpr float HighExplosiveBurstDuration = 0.22f;

	/** 闪光弹强光衰减时长（秒），与 AGinkgoFlashProjectile::Tick 的 0.16 s 保持完全一致 */
	constexpr float FlashBurstDuration = 0.16f;

	/** 闪光弹引爆后保留 Actor 供音效收尾的寿命（秒），与 AGinkgoFlashProjectile::Burst 的 0.7 s 一致 */
	constexpr float FlashPostBurstLife = 0.7f;

	/** 爆炸听觉传播半径（cm，80 m 内 AI 可感知高爆落点） */
	constexpr float ExplosionNoiseRange = 8000.f;

	/** 高爆引爆瞬间的火光基准衰减半径（cm，乘 BlastVfxRadiusScale 后生效） */
	constexpr float HighExplosiveBaseLightRadius = 900.f;

	/** 高爆引爆瞬间的火光基准峰值强度（cd，乘 BlastVfxRadiusScale 后生效） */
	constexpr float HighExplosiveBaseLightIntensity = 95000.f;

	/**
	 * @brief 按 Scale 倍率统一放大 NS_Sub_EXP_Mid_002_02 四个发射器的空间 User 参数（半径、尺寸、初速）
	 * @note 该资产仅主火球发射器开启 LocalSpace，其余三个发射器为 WorldSpace 且不读组件缩放，
	 *       因此保持组件缩放为 1，在 Activate 前直接写入 User 参数确保所有发射器一致按 Scale 放大。
	 */
	void ApplyBlastVfxRadiusScale(UNiagaraComponent& Component, const UNiagaraSystem& System, const float Scale)
	{
		const FNiagaraUserRedirectionParameterStore& Defaults = System.GetExposedParameters();
		const auto ScaleFloatParam = [&](const TCHAR* RawName, const float FallbackDefault)
		{
			const FNiagaraVariable Var(FNiagaraTypeDefinition::GetFloatDef(), FName(RawName));
			float BaseValue = FallbackDefault;
			if (const uint8* Data = Defaults.GetParameterData(Var))
			{
				FMemory::Memcpy(&BaseValue, Data, sizeof(float));
			}
			Component.SetVariableFloat(FName(RawName), BaseValue * Scale);
		};
		const auto ScaleVec2Param = [&](const TCHAR* RawName, const FVector2D& FallbackDefault)
		{
			const FNiagaraVariable Var(FNiagaraTypeDefinition::GetVec2Def(), FName(RawName));
			FVector2f BaseValue(static_cast<float>(FallbackDefault.X), static_cast<float>(FallbackDefault.Y));
			if (const uint8* Data = Defaults.GetParameterData(Var))
			{
				FMemory::Memcpy(&BaseValue, Data, sizeof(FVector2f));
			}
			Component.SetVariableVec2(FName(RawName), FVector2D(BaseValue.X * Scale, BaseValue.Y * Scale));
		};

		ScaleFloatParam(TEXT("User.Common_Radius"), 15.f);
		ScaleFloatParam(TEXT("User.EXP_Size"), 280.f);
		ScaleFloatParam(TEXT("User.Glow_Size"), 3.f);
		ScaleFloatParam(TEXT("User.Debris_SizeMin"), 5.f);
		ScaleFloatParam(TEXT("User.Debris_SizeMax"), 15.f);
		ScaleFloatParam(TEXT("User.Debris_VelMin"), 400.f);
		ScaleFloatParam(TEXT("User.Debris_VelMax"), 1200.f);
		ScaleVec2Param(TEXT("User.Spark_SizeMin"), FVector2D(1.0, 2.0));
		ScaleVec2Param(TEXT("User.Spark_SizeMax"), FVector2D(2.0, 4.0));
		ScaleFloatParam(TEXT("User.Spark_VelMin"), 400.f);
		ScaleFloatParam(TEXT("User.Spark_VelMax"), 1000.f);
	}

	/** 范围破片伤害默认打向胸口骨骼，使人形靶倒下时胸腔获得自然外推冲量 */
	const FName BlastSpineBone(TEXT("spine_04"));

	/** 默认出膛弹头静态网格软路径（当弹种未单独指定 ProjectileMesh 时使用） */
	const TCHAR* const DefaultProjectileMeshPath = TEXT("/Game/Weapons/M79/Rounds/SM_M79_Projectile.SM_M79_Projectile");

	/** 计算当前世界本地听音点到爆心的最近距离平方（cm^2）；无本地玩家控制器时返回无穷大以回退远炸声 */
	double ComputeClosestListenerDistSq(const UWorld& World, const FVector& ExplosionPos)
	{
		double MinDistSq = TNumericLimits<double>::Max();
		for (FConstPlayerControllerIterator It = World.GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			if (!PC || !PC->IsLocalController())
			{
				continue;
			}
			FVector ListenerLoc = FVector::ZeroVector;
			if (PC->PlayerCameraManager && PC->PlayerCameraManager->GetCameraCacheTime() > 0.f)
			{
				FVector FrontDir = FVector::ForwardVector;
				FVector RightDir = FVector::RightVector;
				PC->GetAudioListenerPosition(ListenerLoc, FrontDir, RightDir);
			}
			else if (const APawn* Pawn = PC->GetPawn())
			{
				ListenerLoc = Pawn->GetPawnViewLocation();
			}
			else
			{
				FVector FrontDir = FVector::ForwardVector;
				FVector RightDir = FVector::RightVector;
				PC->GetAudioListenerPosition(ListenerLoc, FrontDir, RightDir);
			}
			MinDistSq = FMath::Min(MinDistSq, FVector::DistSquared(ListenerLoc, ExplosionPos));
		}
		return MinDistSq;
	}
}

float Ginkgo::Combat::ComputeExplosionScale(const float Distance, const float InnerRadius, const float OuterRadius, const float MinScale)
{
	if (OuterRadius <= 0.f || Distance > OuterRadius)
	{
		return 0.f;
	}
	const float ClampedMin = FMath::Clamp(MinScale, 0.f, 1.f);
	if (Distance <= InnerRadius || OuterRadius <= InnerRadius)
	{
		return 1.f;
	}
	const float Alpha = (Distance - InnerRadius) / (OuterRadius - InnerRadius);
	return FMath::Lerp(1.f, ClampedMin, FMath::Clamp(Alpha, 0.f, 1.f));
}

int32 AGinkgoGrenadeProjectile::ApplyFlashBurstToAI(UWorld& World, const FVector& Location, const AActor* IgnoredActor)
{
	const UGinkgoDeveloperSettings* Settings = UGinkgoDeveloperSettings::Get();
	TSet<AActor*> NearActors;
	if (Settings->FlashProximityRadius > 0.f)
	{
		TArray<FOverlapResult> Overlaps;
		World.OverlapMultiByObjectType(Overlaps, Location, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
			FCollisionShape::MakeSphere(Settings->FlashProximityRadius), FCollisionQueryParams(SCENE_QUERY_STAT(FlashProximity)));
		for (const FOverlapResult& Overlap : Overlaps)
		{
			NearActors.Add(Overlap.GetActor());
		}
	}

	int32 FlashedCount = 0;
	for (TActorIterator<AGinkgoAICharacter> It(&World); It; ++It)
	{
		AGinkgoAIController* Controller = Cast<AGinkgoAIController>(It->GetController());
		if (!Controller || !Controller->IsAIActive())
		{
			continue;
		}
		if (NearActors.Contains(*It) || Controller->HasUnobstructedViewOf(Location, Settings->FlashRadius, IgnoredActor))
		{
			Controller->ApplyFlash(Settings->FlashDuration);
			++FlashedCount;
		}
	}
	return FlashedCount;
}

AGinkgoGrenadeProjectile::AGinkgoGrenadeProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);
	Body->SetCanEverAffectNavigation(false);
	Body->SetCastShadow(false);

	BurstSphere = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BurstSphere"));
	BurstSphere->SetupAttachment(Body);
	BurstSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BurstSphere->SetGenerateOverlapEvents(false);
	BurstSphere->SetCanEverAffectNavigation(false);
	BurstSphere->SetCastShadow(false);
	BurstSphere->SetVisibility(false);

	BurstLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("BurstLight"));
	BurstLight->SetupAttachment(Body);
	BurstLight->SetCastShadows(false);
	BurstLight->SetAttenuationRadius(HighExplosiveBaseLightRadius * BlastVfxRadiusScale);
	BurstLight->SetLightColor(FLinearColor(1.f, 0.58f, 0.22f));
	BurstLight->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> BlastFinder(TEXT("/Game/Fire_EXP_Vol01_Free/Niagara/EXP/NS_Sub_EXP_Mid_002_02.NS_Sub_EXP_Mid_002_02"));
	static ConstructorHelpers::FObjectFinder<USoundBase> CloseExplosiveAudio(TEXT("/Game/Weapons/M79/Audio/MS_M79_ExplodeClose.MS_M79_ExplodeClose"));
	static ConstructorHelpers::FObjectFinder<USoundBase> HighExplosiveAudio(TEXT("/Game/Weapons/M79/Audio/MS_M79_Explode.MS_M79_Explode"));
	static ConstructorHelpers::FObjectFinder<USoundBase> FlashExplodeAudio(TEXT("/Game/Weapons/Throwables/Audio/MS_Flashbang_Explode.MS_Flashbang_Explode"));
	BlastSystem = BlastFinder.Object;
	HighExplosiveCloseSound = CloseExplosiveAudio.Object;
	HighExplosiveSound = HighExplosiveAudio.Object;
	FlashExplodeSound = FlashExplodeAudio.Object;

	if (!IsRunningCommandlet())
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowFinder(TEXT("/Game/Weapons/Throwables/M_FlashBurst.M_FlashBurst"));
		BurstSphere->SetStaticMesh(SphereFinder.Object);
		BurstMaterial = GlowFinder.Object;
		if (BurstMaterial)
		{
			BurstSphere->SetMaterial(0, BurstMaterial);
		}
	}
}

void AGinkgoGrenadeProjectile::InitializeProjectile(const FVector& InVelocity, const UGinkgoAmmoDefinition* InAmmo, const float InWeaponMultiplier,
	UGinkgoWeaponComponent* InWeapons)
{
	MuzzleOrigin = GetActorLocation();
	SimulatedPosition = MuzzleOrigin;
	SimulatedVelocity = InVelocity;
	AmmoDefinition = InAmmo;
	Payload = InAmmo ? InAmmo->ProjectilePayload : EGinkgoProjectilePayload::HighExplosive;
	WeaponDamageMultiplier = InWeaponMultiplier;
	FiringWeapons = InWeapons;

	UStaticMesh* ProjectileMesh = InAmmo && !InAmmo->ProjectileMesh.IsNull() ? InAmmo->ProjectileMesh.LoadSynchronous() : nullptr;
	if (!ProjectileMesh)
	{
		ProjectileMesh = LoadObject<UStaticMesh>(nullptr, DefaultProjectileMeshPath);
	}
	if (ProjectileMesh)
	{
		Body->SetStaticMesh(ProjectileMesh);
	}
	if (!SimulatedVelocity.IsNearlyZero())
	{
		SetActorRotation(SimulatedVelocity.ToOrientationQuat());
	}
}

void AGinkgoGrenadeProjectile::BeginPlay()
{
	Super::BeginPlay();
	if (SimulatedPosition.IsZero() && !GetActorLocation().IsZero())
	{
		MuzzleOrigin = GetActorLocation();
		SimulatedPosition = MuzzleOrigin;
	}
}

void AGinkgoGrenadeProjectile::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bDetonated)
	{
		ElapsedSeconds += DeltaSeconds;
		if (Payload == EGinkgoProjectilePayload::Flashbang)
		{
			// 与 AGinkgoFlashProjectile::Tick 逐项一致：0.16 s 强光平方衰减、前半段显示 0.22 倍发光球，0.7 s 后销毁
			const float Alpha = FMath::Clamp(1.f - ElapsedSeconds / FlashBurstDuration, 0.f, 1.f);
			BurstLight->SetIntensity(120000.f * Alpha * Alpha);
			BurstSphere->SetVisibility(Alpha > 0.5f && BurstSphere->GetStaticMesh() != nullptr);
			if (Alpha <= 0.f)
			{
				BurstLight->SetVisibility(false);
			}
			if (ElapsedSeconds >= FlashPostBurstLife)
			{
				Destroy();
			}
			return;
		}

		const float Alpha = FMath::Clamp(1.f - ElapsedSeconds / HighExplosiveBurstDuration, 0.f, 1.f);
		BurstLight->SetIntensity(HighExplosiveBaseLightIntensity * BlastVfxRadiusScale * Alpha * Alpha);
		if (Alpha <= 0.f)
		{
			BurstLight->SetVisibility(false);
			Destroy();
		}
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	ElapsedSeconds += DeltaSeconds;
	if (ElapsedSeconds >= MaxFlightSeconds)
	{
		Destroy();
		return;
	}

	const int32 TargetSteps = FMath::FloorToInt(ElapsedSeconds * FlightHz);
	const FVector StepStartPos = SimulatedPosition;
	if (TargetSteps > SimulatedSteps)
	{
		const FCollisionQueryParams Query(SCENE_QUERY_STAT(GinkgoGrenadeFlight), /*bTraceComplex*/ false, GetInstigator());
		const FCollisionShape Sphere = FCollisionShape::MakeSphere(SweepRadius);
		const FVector Gravity(0.0, 0.0, World->GetGravityZ());
		constexpr double Dt = 1.0 / FlightHz;
		const int32 StepsToRun = TargetSteps - SimulatedSteps;
		for (int32 Step = 0; Step < StepsToRun; ++Step)
		{
			const FVector NextPos = SimulatedPosition + (SimulatedVelocity + 0.5 * Dt * Gravity) * Dt;
			FHitResult Hit;
			if (World->SweepSingleByChannel(Hit, SimulatedPosition, NextPos, FQuat::Identity, Ginkgo::Combat::WeaponTraceChannel, Sphere, Query))
			{
				SimulatedVelocity += Gravity * (Dt * Hit.Time);
				SimulatedPosition = Hit.Location;
				SimulatedSteps += Step + 1;
				Detonate(Hit);
				return;
			}
			SimulatedPosition = NextPos;
			SimulatedVelocity += Gravity * Dt;
		}
		SimulatedSteps = TargetSteps;
	}

	// 在最新离散步之后按剩余亚帧时间外推视觉位置，保证高刷帧率下飞行丝滑不卡顿
	const double SubStep = ElapsedSeconds - static_cast<double>(SimulatedSteps) / FlightHz;
	const FVector Gravity(0.0, 0.0, World->GetGravityZ());
	const FVector VisualPos = SimulatedPosition + (SimulatedVelocity + 0.5 * SubStep * Gravity) * SubStep;
	const FVector VisualVel = SimulatedVelocity + Gravity * SubStep;
	SpinDegrees = FMath::Fmod(SpinDegrees + RifleSpinDegPerSec * DeltaSeconds, 360.f);
	const FQuat Attitude = VisualVel.IsNearlyZero() ? GetActorQuat() : VisualVel.ToOrientationQuat();
	const FQuat Spin(FVector::ForwardVector, FMath::DegreesToRadians(SpinDegrees));
	SetActorLocationAndRotation(VisualPos, Attitude * Spin);

	if (Ginkgo::Combat::IsShowDamageEnabled() && !StepStartPos.Equals(SimulatedPosition, 0.1))
	{
#if !UE_BUILD_SHIPPING && ENABLE_DRAW_DEBUG
		const FColor LineColor = Payload == EGinkgoProjectilePayload::Flashbang ? FColor::Cyan : FColor::Orange;
		DrawDebugLine(World, StepStartPos, SimulatedPosition, LineColor, false, 2.5f, 0, 0.8f);
#endif
	}
}

void AGinkgoGrenadeProjectile::Detonate(const FHitResult& ImpactHit)
{
	if (bDetonated)
	{
		return;
	}
	bDetonated = true;
	ElapsedSeconds = 0.f;

	UWorld* World = GetWorld();
	const FVector ImpactNormal = ImpactHit.ImpactNormal.IsNearlyZero() ? FVector::UpVector : FVector(ImpactHit.ImpactNormal).GetSafeNormal();
	ExplosionLocation = FVector(ImpactHit.ImpactPoint) + ImpactNormal * 4.0;
	SetActorLocationAndRotation(ExplosionLocation, ImpactNormal.ToOrientationQuat());
	Body->SetVisibility(false);

	if (Payload == EGinkgoProjectilePayload::Flashbang)
	{
		// 与 AGinkgoFlashProjectile::OnRep_Burst 及 Burst 保持完全一致的光源、爆闪球、音效与 AI 致盲结算
		if (BurstSphere->GetStaticMesh())
		{
			BurstSphere->SetWorldScale3D(FVector(0.22));
			BurstSphere->SetVisibility(true);
		}
		BurstLight->SetAttenuationRadius(850.f);
		BurstLight->SetLightColor(FLinearColor(1.f, 0.91f, 0.76f));
		BurstLight->SetIntensity(120000.f);
		BurstLight->SetVisibility(true);

		if (World && !World->IsNetMode(NM_DedicatedServer) && FlashExplodeSound)
		{
			UGameplayStatics::SpawnSoundAtLocation(this, FlashExplodeSound, ExplosionLocation);
		}

		int32 FlashedCount = 0;
		if (World)
		{
			if (World->GetNetMode() != NM_Client)
			{
				FlashedCount = ApplyFlashBurstToAI(*World, ExplosionLocation, this);
			}
			else if (UGinkgoWeaponComponent* Weapons = FiringWeapons.Get())
			{
				Weapons->ServerApplyFlashBurst(ExplosionLocation);
			}
		}

		UE_LOG(LogGinkgoCombat, Log, TEXT("[Combat] %s 的 40mm 闪光弹触碰即爆于 %s（飞行 %.2f m）：致盲 %d 名敌人"),
			*GetNameSafe(GetInstigator()), *ExplosionLocation.ToCompactString(),
			FVector::Dist(MuzzleOrigin, ExplosionLocation) / 100.f, FlashedCount);
		return;
	}

	BurstLight->SetAttenuationRadius(HighExplosiveBaseLightRadius * BlastVfxRadiusScale);
	BurstLight->SetLightColor(FLinearColor(1.f, 0.58f, 0.22f));
	BurstLight->SetIntensity(HighExplosiveBaseLightIntensity * BlastVfxRadiusScale);
	BurstLight->SetVisibility(true);

	if (World && !World->IsNetMode(NM_DedicatedServer))
	{
		if (BlastSystem)
		{
			// NS_Sub_EXP_Mid_002_02 以世界 +Z 为上升烟柱与碎屑重力基准，保持直立播放单次爆发；
			// 先以 bAutoActivate=false 生成并在首帧发射前写入 2 倍空间 User 参数，再激活确保 4 个发射器同步放大。
			if (UNiagaraComponent* BlastComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, BlastSystem, ExplosionLocation, FRotator::ZeroRotator,
				FVector::OneVector, /*bAutoDestroy*/ true, /*bAutoActivate*/ false, ENCPoolMethod::AutoRelease))
			{
				ApplyBlastVfxRadiusScale(*BlastComponent, *BlastSystem, BlastVfxRadiusScale);
				BlastComponent->Activate(true);
			}
		}
		FGinkgoBulletImpact DecalImpact;
		if (FGinkgoBulletImpact::FromHit(ImpactHit, DecalImpact))
		{
			if (UGinkgoBulletImpactSubsystem* Effects = World->GetSubsystem<UGinkgoBulletImpactSubsystem>())
			{
				Effects->Play(DecalImpact);
			}
		}
		const bool bCloseBlast = HighExplosiveCloseSound
			&& ComputeClosestListenerDistSq(*World, ExplosionLocation) <= FMath::Square(static_cast<double>(CloseExplosionDistance));
		if (USoundBase* ChosenExplodeSound = bCloseBlast ? HighExplosiveCloseSound.Get() : HighExplosiveSound.Get())
		{
			UGameplayStatics::SpawnSoundAtLocation(this, ChosenExplodeSound, ExplosionLocation);
		}
	}

	AActor* Shooter = GetInstigator();
	if (AGinkgoCharacter* Character = Cast<AGinkgoCharacter>(Shooter))
	{
		Character->GetNoiseEmitter()->ReportShot(ExplosionLocation, ExplosionNoiseRange);
	}

	if (!World || !AmmoDefinition)
	{
		return;
	}

	// 收集直击目标与世界目标表内的所有可打击组件，避免同一目标因多组件重复结算
	TArray<UGinkgoDamageableComponent*, TInlineAllocator<16>> Candidates;
	AActor* DirectActor = ImpactHit.GetActor();
	if (UGinkgoDamageableComponent* DirectDamageable = DirectActor ? DirectActor->FindComponentByClass<UGinkgoDamageableComponent>() : nullptr)
	{
		Candidates.AddUnique(DirectDamageable);
	}
	if (const UGinkgoTargetRegistry* Registry = UGinkgoTargetRegistry::Get(World))
	{
		for (const TWeakObjectPtr<UGinkgoDamageableComponent>& Entry : Registry->GetTargets())
		{
			if (UGinkgoDamageableComponent* Target = Entry.Get())
			{
				Candidates.AddUnique(Target);
			}
		}
	}

	FGinkgoHitConfirm Shot;
	const FVector FlightDir = SimulatedVelocity.IsNearlyZero() ? GetActorForwardVector() : SimulatedVelocity.GetSafeNormal();
	for (UGinkgoDamageableComponent* Damageable : Candidates)
	{
		AActor* TargetActor = Damageable ? Damageable->GetOwner() : nullptr;
		if (!Damageable || !TargetActor || TargetActor == Shooter || Damageable->IsDead())
		{
			continue;
		}

		FGinkgoDamageEvent Event;
		if (TargetActor == DirectActor)
		{
			const float FlightDist = FVector::Dist(MuzzleOrigin, FVector(ImpactHit.ImpactPoint));
			Event = Damageable->ApplyBulletHit(*AmmoDefinition, WeaponDamageMultiplier, ImpactHit, FlightDir, FlightDist, Shooter, 1.f, 0);
		}
		else
		{
			USkinnedMeshComponent* TargetMesh = TargetActor->FindComponentByClass<USkinnedMeshComponent>();
			const FVector TargetPoint = TargetMesh && TargetMesh->GetBoneIndex(BlastSpineBone) != INDEX_NONE
				? TargetMesh->GetBoneLocation(BlastSpineBone)
				: TargetActor->GetActorLocation();
			const float BlastDist = FVector::Dist(ExplosionLocation, TargetPoint);
			const float Scale = Ginkgo::Combat::ComputeExplosionScale(BlastDist, DefaultInnerRadius, DefaultBlastRadius);
			if (Scale <= 0.f)
			{
				continue;
			}
			// 掩体阻挡检查：爆心到目标胸口之间若有静态墙体阻隔则挡住破片
			FHitResult Occlusion;
			FCollisionQueryParams OcclusionParams(SCENE_QUERY_STAT(GinkgoGrenadeBlastLOS), /*bTraceComplex*/ false, this);
			OcclusionParams.AddIgnoredActor(TargetActor);
			if (DirectActor)
			{
				OcclusionParams.AddIgnoredActor(DirectActor);
			}
			if (Shooter)
			{
				OcclusionParams.AddIgnoredActor(Shooter);
			}
			if (World->LineTraceSingleByChannel(Occlusion, ExplosionLocation, TargetPoint, ECC_Visibility, OcclusionParams))
			{
				continue;
			}

			FHitResult BlastHit(TargetActor, TargetMesh, TargetPoint, (ExplosionLocation - TargetPoint).GetSafeNormal());
			BlastHit.ImpactPoint = TargetPoint;
			BlastHit.Location = TargetPoint;
			if (TargetMesh && TargetMesh->GetBoneIndex(BlastSpineBone) != INDEX_NONE)
			{
				BlastHit.BoneName = BlastSpineBone;
			}
			const FVector BlastDir = ((TargetPoint - ExplosionLocation).GetSafeNormal() + FVector(0.0, 0.0, 0.35)).GetSafeNormal();
			Event = Damageable->ApplyBulletHit(*AmmoDefinition, WeaponDamageMultiplier, BlastHit, BlastDir, BlastDist, Shooter, Scale, 0);
		}

		if (Event.BaseDamage <= 0.f)
		{
			continue;
		}
		++Shot.Hits;
		Shot.bKill |= Event.bKilled;
		Shot.bHead |= Event.Part == EGinkgoBodyPart::Head;
		Shot.bKillHead |= Event.bKilled && Event.Part == EGinkgoBodyPart::Head;
		Shot.bArmor |= Event.Outcome.bArmorHit;
		Shot.bArmorBroken |= Event.Outcome.bArmorBroken;
		Shot.HealthDamage += Event.Outcome.HealthDamage;
		Shot.ArmorDamage += Event.Outcome.ArmorDamage;
		Shot.Part = Event.Part;
	}

	UE_LOG(LogGinkgoCombat, Log, TEXT("[Combat] %s 的 40mm 榴弹碰炸于 %s（飞行 %.2f m）：命中 %d 个目标，伤害 %.1f%s"),
		*GetNameSafe(Shooter), *ExplosionLocation.ToCompactString(), FVector::Dist(MuzzleOrigin, ExplosionLocation) / 100.f,
		Shot.Hits, Shot.HealthDamage, Shot.bKill ? TEXT("，击杀") : TEXT(""));

	if (Shot.Hits > 0)
	{
		if (UGinkgoWeaponComponent* Weapons = FiringWeapons.Get())
		{
			Weapons->NotifyProjectileHitConfirm(Shot);
		}
	}

#if !UE_BUILD_SHIPPING && ENABLE_DRAW_DEBUG
	if (Ginkgo::Combat::IsShowDamageEnabled())
	{
		DrawDebugSphere(World, ExplosionLocation, DefaultInnerRadius, 16, FColor::Red, false, 2.5f);
		DrawDebugSphere(World, ExplosionLocation, DefaultBlastRadius, 24, FColor::Orange, false, 2.5f);
	}
#endif
}
