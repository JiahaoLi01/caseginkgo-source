#include "Throwables/GinkgoFlashProjectile.h"
#include "AI/GinkgoAIController.h"
#include "AI/GinkgoAICharacter.h"
#include "Character/GinkgoCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"

namespace
{
	/** 飞行按固定频率推进，让不同帧率的服务器和瞄准线以相同顺序处理连续碰撞。 */
	constexpr int32 FlightHz = 60;

	/** 从脱手到倒计时结束的总步数。 */
	constexpr int32 FuseSteps = static_cast<int32>(AGinkgoFlashProjectile::FuseSeconds * FlightHz);

	/** 轨迹和出手点避障共用的球形扫掠半径（cm）。 */
	constexpr float SweepRadius = 4.f;

	/** 碰撞后保留的法向反弹速度比例。 */
	constexpr double Restitution = .45;

	/** 碰撞后保留的切向速度比例。 */
	constexpr double TangentialRetention = .72;

	/** 有支撑面时，碰后速度低于此值（cm/s）即停稳。 */
	constexpr double RestSpeed = 35.;

	/** 触发金属罐体碰撞声的最小法向入射速度（cm/s），过滤贴墙擦碰与停稳前的微弱接触。 */
	constexpr double MinImpactSpeed = 45.;

	/** 碰撞声达到满音量的法向入射速度（cm/s）。 */
	constexpr double FullImpactSpeed = 750.;

	/** 刚越过最小碰撞速度门限时的起始音量比例。 */
	constexpr float QuietImpactVolume = .28f;

	/** 同一枚弹体两次碰撞声的最短飞行模拟间隔（秒），合并墙角连续多面接触。 */
	constexpr double MinImpactInterval = .055;

	/** 单次球形扫掠捕获的墙面或地面碰撞记录，仅权威端实际飞行收集，轨迹预览不收集。 */
	struct FFlashImpact
	{
		/** 接触点世界空间坐标（cm），碰撞声固定在此处独立播放。 */
		FVector Location = FVector::ZeroVector;

		/** 碰撞前沿接触法线向内的速度大小（cm/s，非负），用于计算碰撞声音量。 */
		double NormalSpeed = 0.;

		/** 接触发生时距弹体生成的飞行模拟时间（秒），用于聚类相近接触。 */
		double FlightTime = 0.;
	};

	enum class EFlightResult { Moving, Bounced, Settled };

	/** 推进整步游戏时间。只有预览需要收集折点，权威端直接更新位置、速度并记录真实碰撞。 */
	EFlightResult AdvanceFlight(const UWorld& World, const AActor* Thrower, FVector& Position, FVector& Velocity,
		int32 Steps, TArray<FVector>* Points = nullptr, TArray<FFlashImpact, TInlineAllocator<8>>* Impacts = nullptr, double StartFlightTime = 0.)
	{
		const FCollisionQueryParams Query(SCENE_QUERY_STAT(FlashFlight), false, Thrower);
		const FCollisionShape Sphere = FCollisionShape::MakeSphere(SweepRadius);
		const FVector Gravity(0., 0., World.GetGravityZ());
		EFlightResult Result = EFlightResult::Moving;
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const double StepStart = StartFlightTime + static_cast<double>(Step) / FlightHz;
			double Remaining = 1. / FlightHz;
			// 一步内撞墙后仍可继续撞地。狭缝里限制解算次数，下一步再尝试脱离穿透。
			for (int32 Contact = 0; Contact < 4 && Remaining > UE_SMALL_NUMBER; ++Contact)
			{
				const FVector End = Position + (Velocity + .5 * Remaining * Gravity) * Remaining;
				FHitResult Hit;
				if (!World.SweepSingleByChannel(Hit, Position, End, FQuat::Identity, ECC_Visibility, Sphere, Query))
				{
					Position = End;
					Velocity += Gravity * Remaining;
					break;
				}
				const double TravelTime = Remaining * Hit.Time;
				Velocity += Gravity * TravelTime;
				Remaining -= TravelTime;
				// Hit.Location 是球心。留出很小的间隙，避免下一次扫掠立刻重复命中同一面。
				Position = Hit.Location + Hit.Normal * (.1 + (Hit.bStartPenetrating ? Hit.PenetrationDepth : 0.));
				if (Points) Points->Add(Position);
				const double InwardSpeed = FVector::DotProduct(Velocity, Hit.Normal);
				if (InwardSpeed < 0.)
				{
					if (Impacts)
					{
						Impacts->Add({Hit.ImpactPoint, -InwardSpeed, StepStart + (1. / FlightHz - Remaining)});
					}
					Velocity = (Velocity - InwardSpeed * Hit.Normal) * TangentialRetention - InwardSpeed * Restitution * Hit.Normal;
					if (Hit.Normal.Z >= .7 && Velocity.SizeSquared() <= FMath::Square(RestSpeed))
					{
						Velocity = FVector::ZeroVector;
						return EFlightResult::Settled;
					}
				}
				Result = EFlightResult::Bounced;
			}
			if (Points && ((Step + 1) % 2 == 0 || Step + 1 == Steps) && !Position.Equals(Points->Last(), UE_SMALL_NUMBER))
				Points->Add(Position);
		}
		return Result;
	}
}

AGinkgoFlashProjectile::AGinkgoFlashProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Game/Weapons/Throwables/SM_M84_Thrown"));
	Body->SetStaticMesh(Mesh.Object);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Game/Weapons/Throwables/M_FlashBurst"));
	static ConstructorHelpers::FObjectFinder<USoundBase> HitAudio(TEXT("/Game/Weapons/Throwables/Audio/MS_Flashbang_Impact"));
	static ConstructorHelpers::FObjectFinder<USoundBase> BurstAudio(TEXT("/Game/Weapons/Throwables/Audio/MS_Flashbang_Explode"));
	BurstMesh = Sphere.Object;
	BurstMaterial = Glow.Object;
	ImpactSound = HitAudio.Object;
	ExplodeSound = BurstAudio.Object;
	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("SceneFlash"));
	Light->SetupAttachment(Body);
	Light->SetCastShadows(false);
	Light->SetAttenuationRadius(850.f);
	Light->SetLightColor(FLinearColor(1.f, .91f, .76f));
	Light->SetVisibility(false);
}

void AGinkgoFlashProjectile::GetLaunch(const AGinkgoCharacter& Character, FVector& Start, FVector& Velocity, const FVector& ReleasedAim)
{
	const FVector Eye = Character.GetPawnViewLocation();
	// M_M84_Throw 第 9 帧的弹体原点：距脚底 115.3 cm，换算到站姿眼位后随蹲起一起升降。
	// 预览、左腕 IK 和服务器生成共用这一点，胸前待机时也能提前绘制完整轨迹。
	const FVector Offset = Character.GetActorRotation().RotateVector(FVector(54.8, -14.0, -36.7));
	Start = Eye + Offset;
	FHitResult Wall;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(FlashLaunch), false, &Character);
	if (Character.GetWorld()->SweepSingleByChannel(Wall, Eye, Start, FQuat::Identity, ECC_Visibility,
		FCollisionShape::MakeSphere(SweepRadius), Query))
		Start = Wall.Location;
	const FVector Direction = ReleasedAim.IsNearlyZero() ? Character.GetControlRotation().Vector() : ReleasedAim.GetSafeNormal();
	Velocity = Direction * 1920.f + FVector::UpVector * 384.f;
}

void AGinkgoFlashProjectile::PredictPath(const UWorld& World, const AActor* Thrower, FVector Start, FVector Velocity, TArray<FVector>& OutPoints)
{
	OutPoints.Reset(FuseSteps / 2 + 1);
	OutPoints.Add(Start);
	AdvanceFlight(World, Thrower, Start, Velocity, FuseSteps, &OutPoints);
}

float AGinkgoFlashProjectile::ComputeImpactVolume(const double NormalSpeed)
{
	if (NormalSpeed < MinImpactSpeed) return 0.f;
	const float Alpha = static_cast<float>(FMath::Clamp((NormalSpeed - MinImpactSpeed) / (FullImpactSpeed - MinImpactSpeed), 0., 1.));
	return FMath::Lerp(QuietImpactVolume, 1.f, FMath::Sqrt(Alpha));
}

void AGinkgoFlashProjectile::BeginPlay()
{
	Super::BeginPlay();
	SetActorTickEnabled(HasAuthority() || bBurst);
}

void AGinkgoFlashProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bBurst)
	{
		Elapsed += DeltaSeconds;
		const float Alpha = FMath::Clamp(1. - Elapsed / .16, 0., 1.);
		Light->SetIntensity(120000.f * Alpha * Alpha);
		Body->SetVisibility(Alpha > .5f);
		if (Alpha <= 0.f)
		{
			Light->SetVisibility(false);
			SetActorTickEnabled(false);
		}
		return;
	}
	Elapsed = FMath::Min(Elapsed + DeltaSeconds, FuseSeconds);
	const int32 TargetSteps = FMath::FloorToInt(Elapsed * FlightHz);
	if (!bSettled && TargetSteps > SimulatedSteps)
	{
		FVector Position = GetActorLocation();
		TArray<FFlashImpact, TInlineAllocator<8>> Impacts;
		const EFlightResult Result = AdvanceFlight(*GetWorld(), GetInstigator(), Position, LaunchVelocity,
			TargetSteps - SimulatedSteps, nullptr, &Impacts, static_cast<double>(SimulatedSteps) / FlightHz);
		SetActorLocation(Position);
		SimulatedSteps = TargetSteps;
		bSettled = Result == EFlightResult::Settled;
		int32 Index = 0;
		while (Index < Impacts.Num())
		{
			FFlashImpact Best = Impacts[Index++];
			while (Index < Impacts.Num() && Impacts[Index].FlightTime - Best.FlightTime < MinImpactInterval)
			{
				if (Impacts[Index].NormalSpeed > Best.NormalSpeed) Best = Impacts[Index];
				++Index;
			}
			const float Volume = ComputeImpactVolume(Best.NormalSpeed);
			if (Volume > 0.f && (LastImpactSoundTime < 0. || Best.FlightTime - LastImpactSoundTime >= MinImpactInterval))
			{
				LastImpactSoundTime = Best.FlightTime;
				MulticastPlayImpact(Best.Location, Volume);
			}
		}
		if (Result != EFlightResult::Moving) ForceNetUpdate();
	}
	if (Elapsed >= FuseSeconds) Burst();
}

void AGinkgoFlashProjectile::Burst()
{
	bBurst = true;
	const FVector Location = GetActorLocation();
	const auto* Settings = UGinkgoDeveloperSettings::Get();
	TSet<AActor*> NearActors;
	if (Settings->FlashProximityRadius > 0.f)
	{
		TArray<FOverlapResult> Overlaps;
		GetWorld()->OverlapMultiByObjectType(Overlaps, Location, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
			FCollisionShape::MakeSphere(Settings->FlashProximityRadius), FCollisionQueryParams(SCENE_QUERY_STAT(FlashProximity)));
		for (const FOverlapResult& Overlap : Overlaps)
		{
			NearActors.Add(Overlap.GetActor());
		}
	}
	// 近身命中直接生效；外圈按实际眼位判断，探身时眼位可能已经伸出胶囊。
	for (TActorIterator<AGinkgoAICharacter> It(GetWorld()); It; ++It)
	{
		auto* Controller = Cast<AGinkgoAIController>(It->GetController());
		if (!Controller || !Controller->IsAIActive()) continue;
		if (NearActors.Contains(*It) || Controller->HasUnobstructedViewOf(Location, Settings->FlashRadius, this))
		{
			Controller->ApplyFlash(Settings->FlashDuration);
		}
	}
	OnRep_Burst();
	ForceNetUpdate();
	SetLifeSpan(.7f);
}

void AGinkgoFlashProjectile::MulticastPlayImpact_Implementation(const FVector_NetQuantize Location, const float Volume)
{
	if (GetNetMode() == NM_DedicatedServer || !ImpactSound || Volume <= 0.f) return;
	UGameplayStatics::SpawnSoundAtLocation(this, ImpactSound, Location, FRotator::ZeroRotator, Volume);
}

void AGinkgoFlashProjectile::OnRep_Burst()
{
	Elapsed = 0.f;
	if (GetNetMode() == NM_DedicatedServer) { SetActorTickEnabled(false); return; }
	if (!bBurstSoundPlayed && ExplodeSound)
	{
		bBurstSoundPlayed = true;
		UGameplayStatics::SpawnSoundAtLocation(this, ExplodeSound, GetActorLocation());
	}
	Body->SetStaticMesh(BurstMesh);
	Body->SetMaterial(0, BurstMaterial);
	Body->SetWorldScale3D(FVector(.22));
	Body->SetCastShadow(false);
	Light->SetVisibility(true);
	Light->SetIntensity(120000.f);
	SetActorTickEnabled(true);
}

void AGinkgoFlashProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, bBurst);
}
