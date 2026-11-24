#include "Combat/GinkgoPracticeTarget.h"

#include "Animation/AnimSequenceBase.h"
#include "Combat/GinkgoAmmoDefinition.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Combat/GinkgoPenetrableComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Crc.h"
#include "TimerManager.h"
#include "Utilities/Debug/DebugCategory.h"

const FName AGinkgoPracticeTarget::VestName(TEXT("Vest"));
const FName AGinkgoPracticeTarget::HelmetName(TEXT("Helmet"));

namespace
{
	const FName VestBone(TEXT("spine_04"));
	const FName HelmetBone(TEXT("head"));
	const FName RagdollProfile(TEXT("Ragdoll"));
	const FName CharacterMeshProfile(TEXT("CharacterMesh"));
	const FName ShapeColor(TEXT("Color"));

	UStaticMeshComponent* CreateShape(AActor& Owner, const FName Name, const TCHAR* MeshPath)
	{
		UStaticMeshComponent* Shape = Owner.CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Shape->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MeshPath));
		// 只是看的：不挡子弹、不挡人（护甲在可打击的组件上算）
		Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Shape->SetGenerateOverlapEvents(false);
		Shape->SetVisibility(false);
		return Shape;
	}
}

AGinkgoPracticeTarget::AGinkgoPracticeTarget()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(34.f, 90.f);
	RootComponent = Capsule;

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Capsule);
	// 人体模型朝它自己的 +Y：转过来朝 actor 的前面，脚放在胶囊底
	Mesh->SetRelativeLocationAndRotation(FVector(0.0, 0.0, -90.0), FRotator(0.0, -90.0, 0.0));
	// 没有画面的进程里也要按姿势摆形体（测量的路线打靶子）
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	Damageable = CreateDefaultSubobject<UGinkgoDamageableComponent>(TEXT("Damageable"));
	// 子弹穿得过人（Docs/Design/Damage.md「穿射」）：厚度用组件的默认值，人 0.3
	Penetrable = CreateDefaultSubobject<UGinkgoPenetrableComponent>(TEXT("Penetrable"));

	VestShape = CreateShape(*this, TEXT("VestShape"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	VestShape->SetupAttachment(Mesh, VestBone);
	HelmetShape = CreateShape(*this, TEXT("HelmetShape"), TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	HelmetShape->SetupAttachment(Mesh, HelmetBone);
	ApplyAliveCollision();
}

void AGinkgoPracticeTarget::ApplyAliveCollision()
{
	Capsule->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	Capsule->SetCollisionResponseToChannel(Ginkgo::Combat::WeaponTraceChannel, ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Mesh->SetCollisionProfileName(CharacterMeshProfile);
	Mesh->SetCollisionResponseToChannel(Ginkgo::Combat::WeaponTraceChannel, ECR_Block);
	Mesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
}

void AGinkgoPracticeTarget::Configure(const FGinkgoTargetSpawn& Spawn)
{
	Setup = Spawn;
	const UGinkgoDeveloperSettings* Settings = UGinkgoDeveloperSettings::Get();
	TArray<FGinkgoArmorPiece> Armor;
	if (Spawn.bVest)
	{
		FGinkgoArmorPiece& Vest = Armor.AddDefaulted_GetRef();
		Vest.Name = VestName;
		Vest.Covers = {EGinkgoBodyPart::UpperChest, EGinkgoBodyPart::Abdomen};
		Vest.MaxDurability = Settings->TargetVestDurability;
	}
	if (Spawn.bHelmet)
	{
		FGinkgoArmorPiece& Helmet = Armor.AddDefaulted_GetRef();
		Helmet.Name = HelmetName;
		Helmet.Covers = {EGinkgoBodyPart::Head};
		Helmet.MaxDurability = Settings->TargetHelmetDurability;
	}
	Damageable->SetArmor(Armor);
	VestShape->SetVisibility(Spawn.bVest);
	HelmetShape->SetVisibility(Spawn.bHelmet);
}

void AGinkgoPracticeTarget::BeginPlay()
{
	Super::BeginPlay();
	const UGinkgoDeveloperSettings* Settings = UGinkgoDeveloperSettings::Get();
	if (USkeletalMesh* TargetMesh = Settings->TargetMesh.LoadSynchronous())
	{
		Mesh->SetSkeletalMeshAsset(TargetMesh);
	}
	PlayIdle();
	Damageable->OnDamaged.AddUObject(this, &AGinkgoPracticeTarget::HandleDamaged);
	Damageable->OnDied.AddUObject(this, &AGinkgoPracticeTarget::HandleDied);

	// 占位的形状：挂在骨骼上跟着动（倒下时一起倒），朝向同靶子一样直立。背心是压扁的圆柱（宽 38、厚 27、高 44 cm，从胸口盖到腰：它也盖腹部），
	// 头盔是压扁的球（宽 24、高 15 cm，扣在头顶偏后，脸露出来；原来 25 cm 的整球把整个头包住，看着像个大球）
	const FQuat Upright = GetActorQuat();
	const FVector Forward = GetActorForwardVector();
	VestShape->SetWorldScale3D(FVector(0.38, 0.27, 0.44));
	VestShape->SetWorldRotation(Upright);
	VestShape->SetWorldLocation(Mesh->GetSocketLocation(VestBone) + FVector(0.0, 0.0, -9.0));
	HelmetShape->SetWorldScale3D(FVector(0.24, 0.24, 0.15));
	HelmetShape->SetWorldRotation(Upright);
	HelmetShape->SetWorldLocation(Mesh->GetSocketLocation(HelmetBone) + FVector(0.0, 0.0, 15.0) - Forward * 1.5);
	for (const auto& [Shape, Color] : {TPair<UStaticMeshComponent*, FLinearColor>(VestShape, FLinearColor(0.16f, 0.18f, 0.12f)),
		TPair<UStaticMeshComponent*, FLinearColor>(HelmetShape, FLinearColor(0.12f, 0.13f, 0.11f))})
	{
		if (UMaterialInstanceDynamic* Material = Shape->CreateDynamicMaterialInstance(0))
		{
			Material->SetVectorParameterValue(ShapeColor, Color);
		}
	}
	TStringBuilder<64> ArmorText;
	for (const FGinkgoArmorPiece& Piece : Damageable->GetArmor())
	{
		ArmorText.Appendf(TEXT("，%s %.1f"), *Piece.Name.ToString(), Piece.MaxDurability);
	}
	UE_LOG(LogGinkgoCombat, Log, TEXT("[Combat] 人形靶 %s：(%.1f, %.1f, %.1f)%s，血 %.1f"), *GetName(), GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z,
		ArmorText.ToString(), Damageable->GetMaxHealth());
}

void AGinkgoPracticeTarget::PlayIdle()
{
	UAnimSequenceBase* Idle = UGinkgoDeveloperSettings::Get()->TargetIdle.LoadSynchronous();
	if (!Idle)
	{
		return;
	}
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->PlayAnimation(Idle, /*bLooping*/ true);
	// 从循环里不同的地方开始：一排靶子不一起呼吸。按名字的字符串定（CRC），同一条路线每次一样；
	// 不能用 GetTypeHash(FName)：那是名字表里的序号，每个进程不同，测量的路线每次跑靶子的姿势都不一样
	Mesh->SetPosition(static_cast<float>(FCrc::StrCrc32(*GetName()) % 1000) / 1000.f * Idle->GetPlayLength(), /*bFireNotifies*/ false);
}

void AGinkgoPracticeTarget::HandleDamaged(UGinkgoDamageableComponent& Component, const FGinkgoDamageEvent& Event)
{
	if (!Event.Outcome.bArmorBroken)
	{
		return;
	}
	if (Event.ArmorPiece == VestName)
	{
		VestShape->SetVisibility(false);
	}
	else if (Event.ArmorPiece == HelmetName)
	{
		HelmetShape->SetVisibility(false);
	}
}

void AGinkgoPracticeTarget::HandleDied(UGinkgoDamageableComponent& Component, const FGinkgoDamageEvent& Event)
{
	// 布娃娃：胶囊不再挡人，网格按物理资产倒下，打到的那一块沿子弹推一把
	Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCollisionProfileName(RagdollProfile);
	Mesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Mesh->SetAllBodiesSimulatePhysics(true);
	Mesh->SetSimulatePhysics(true);
	Mesh->WakeAllRigidBodies();
	if (Event.Ammo && !Event.Bone.IsNone())
	{
		// 穿过东西才打到的推得轻：同伤害一样乘剩的系数（Docs/Design/Damage.md「穿射」）
		Mesh->AddImpulse(Event.Direction * Event.Ammo->KillPush * Event.PenetrationScale, Event.Bone, /*bVelChange*/ true);
	}
	const float Delay = UGinkgoDeveloperSettings::Get()->TargetRespawnDelay;
	GetWorldTimerManager().SetTimer(RespawnTimer, this, &AGinkgoPracticeTarget::Respawn, FMath::Max(Delay, 0.01f), false);
	UE_LOG(LogGinkgoCombat, Log, TEXT("[Combat] 人形靶 %s 倒下（%s），%.1f s 后复位"), *GetName(), Ginkgo::Combat::DescribeBodyPart(Event.Part), Delay);
}

void AGinkgoPracticeTarget::Respawn()
{
	Mesh->SetSimulatePhysics(false);
	Mesh->SetAllBodiesSimulatePhysics(false);
	Mesh->AttachToComponent(Capsule, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	Mesh->SetRelativeLocationAndRotation(FVector(0.0, 0.0, -90.0), FRotator(0.0, -90.0, 0.0));
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ApplyAliveCollision();
	PlayIdle();
	Damageable->Revive();
	VestShape->SetVisibility(Setup.bVest);
	HelmetShape->SetVisibility(Setup.bHelmet);
	UE_LOG(LogGinkgoCombat, Log, TEXT("[Combat] 人形靶 %s 复位"), *GetName());
}

void AGinkgoPracticeTarget::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING && ENABLE_DRAW_DEBUG
	if (!Ginkgo::Combat::IsShowDamageEnabled())
	{
		return;
	}
	TStringBuilder<128> Text;
	Text.Appendf(TEXT("血 %.0f/%.0f"), Damageable->GetHealth(), Damageable->GetMaxHealth());
	for (const FGinkgoArmorPiece& Piece : Damageable->GetArmor())
	{
		Text.Appendf(TEXT("  %s %.0f"), Piece.Name == VestName ? TEXT("背心") : (Piece.Name == HelmetName ? TEXT("头盔") : *Piece.Name.ToString()), Piece.Durability);
	}
	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0.0, 0.0, 115.0), Text.ToString(), nullptr, Damageable->IsDead() ? FColor::Red : FColor::White, 0.f, true, 1.1f);
#endif
}
