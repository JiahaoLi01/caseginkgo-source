#include "Combat/GinkgoDamageableComponent.h"

#include "Combat/GinkgoAmmoDefinition.h"
#include "Combat/GinkgoTargetRegistry.h"
#include "Components/SkinnedMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "UI/GinkgoTitleSubsystem.h"
#include "Utilities/Debug/DebugCategory.h"

#if !UE_BUILD_SHIPPING
namespace
{
	int32 GShowDamage = 0;
	FAutoConsoleVariableRef CVarShowDamage(TEXT("ginkgo.combat.showDamage"), GShowDamage,
		TEXT("命中时在命中点写部位、扣了多少血与护甲（调试，Docs/Design/Damage.md「调试」）：0 不写（默认），1 写；靶子头上写剩下的血、护甲。发行版不编译"));
}
#endif

UGinkgoDamageableComponent::UGinkgoDamageableComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	BoneParts = Ginkgo::Combat::MakeMannequinBodyParts();
}

void UGinkgoDamageableComponent::BeginPlay()
{
	Super::BeginPlay();
	Revive();
	// 登记成世界里的目标：辅助瞄准从这里挑
	if (UGinkgoTargetRegistry* Registry = UGinkgoTargetRegistry::Get(this))
	{
		Registry->Add(this);
	}
}

void UGinkgoDamageableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGinkgoTargetRegistry* Registry = UGinkgoTargetRegistry::Get(this))
	{
		Registry->Remove(this);
	}
	Super::EndPlay(EndPlayReason);
}

void UGinkgoDamageableComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	StepHealthRegen(DeltaTime);
}

void UGinkgoDamageableComponent::SetHealthRegenEnabled(const bool bEnabled)
{
	bHealthRegenEnabled = bEnabled;
	if (!bHealthRegenEnabled)
	{
		TimeSinceLastDamaged = 0.f;
	}
	RefreshRegenTick();
}

void UGinkgoDamageableComponent::RefreshRegenTick()
{
	const bool bShouldTick = bHealthRegenEnabled && CanWriteVitals() && !bDead && Health > 0.f && Health < MaxHealth;
	if (IsComponentTickEnabled() != bShouldTick)
	{
		SetComponentTickEnabled(bShouldTick);
	}
}

void UGinkgoDamageableComponent::StepHealthRegen(const float DeltaSeconds)
{
	if (DeltaSeconds <= 0.f || !CanWriteVitals() || !bHealthRegenEnabled || bDead || Health <= 0.f || Health >= MaxHealth)
	{
		RefreshRegenTick();
		return;
	}
	// 挂在 Pawn 上时仅对玩家控制的角色生效（避免被 AI 附身的 AGinkgoCharacter 触发回血；同时兼容无 PlayerState 的独立测试世界）
	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner());
		OwnerPawn && !OwnerPawn->IsPlayerControlled() && !Cast<APlayerController>(OwnerPawn->GetController()))
	{
		return;
	}
	const float Delay = FMath::Max(0.f, HealthRegenDelay);
	const float PreviousElapsed = TimeSinceLastDamaged;
	TimeSinceLastDamaged += DeltaSeconds;
	if (TimeSinceLastDamaged <= Delay)
	{
		return;
	}
	const float RegenSeconds = PreviousElapsed >= Delay ? DeltaSeconds : (TimeSinceLastDamaged - Delay);
	const float RegenRate = MaxHealth / FMath::Max(0.01f, HealthRegenFullDuration);
	const float NextHealth = FMath::Min(MaxHealth, Health + RegenRate * RegenSeconds);
	if (NextHealth != Health)
	{
		Health = NextHealth;
		if (GetIsReplicated() && GetOwner())
		{
			GetOwner()->ForceNetUpdate();
		}
	}
	if (Health >= MaxHealth)
	{
		TimeSinceLastDamaged = 0.f;
		RefreshRegenTick();
	}
}

void UGinkgoDamageableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, MaxHealth);
	DOREPLIFETIME(ThisClass, Health);
	DOREPLIFETIME(ThisClass, Armor);
	DOREPLIFETIME(ThisClass, bFullBodyArmor);
	DOREPLIFETIME(ThisClass, bDead);
}

bool UGinkgoDamageableComponent::CanWriteVitals() const
{
	return !GetIsReplicated() || !GetOwner() || GetOwner()->HasAuthority();
}

void UGinkgoDamageableComponent::OnRep_Dead()
{
	OnLifeStateChanged.Broadcast();
}

bool UGinkgoDamageableComponent::RestoreSaveState(const float SavedHealth, const float SavedMaxHealth, const TArray<FGinkgoArmorPiece>& SavedArmor)
{
	if (!CanWriteVitals() || !FMath::IsFinite(SavedHealth) || !FMath::IsFinite(SavedMaxHealth) || SavedHealth <= 0.f || SavedMaxHealth <= 0.f)
	{
		return false;
	}
	MaxHealth = SavedMaxHealth;
	Health = FMath::Clamp(SavedHealth, 0.f, MaxHealth);
	Armor = SavedArmor;
	for (FGinkgoArmorPiece& Piece : Armor)
	{
		Piece.MaxDurability = FMath::Max(0.f, Piece.MaxDurability);
		Piece.Durability = FMath::Clamp(Piece.Durability, 0.f, Piece.MaxDurability);
	}
	if (bDead) { bDead = false; OnLifeStateChanged.Broadcast(); }
	TimeSinceLastDamaged = 0.f;
	RefreshRegenTick();
	if (GetIsReplicated() && GetOwner()) { GetOwner()->ForceNetUpdate(); }
	return true;
}

void UGinkgoDamageableComponent::InitializeVitals(const float InMaxHealth, const TArray<FGinkgoArmorPiece>& InArmor)
{
	if (!CanWriteVitals()) { return; }
	MaxHealth = FMath::Max(1.f, InMaxHealth);
	Armor = InArmor;
	Revive();
}

void UGinkgoDamageableComponent::Revive()
{
	if (!CanWriteVitals()) { return; }
	const bool bWasDead = bDead;
	Health = MaxHealth;
	bDead = false;
	TimeSinceLastDamaged = 0.f;
	for (FGinkgoArmorPiece& Piece : Armor)
	{
		Piece.Durability = Piece.MaxDurability;
	}
	RefreshRegenTick();
	if (bWasDead) { OnLifeStateChanged.Broadcast(); }
}

void UGinkgoDamageableComponent::SetArmor(const TArray<FGinkgoArmorPiece>& InArmor)
{
	if (!CanWriteVitals()) { return; }
	Armor = InArmor;
	for (FGinkgoArmorPiece& Piece : Armor)
	{
		Piece.Durability = Piece.MaxDurability;
	}
}

EGinkgoBodyPart UGinkgoDamageableComponent::FindBodyPart(const FHitResult& Hit) const
{
	const USkinnedMeshComponent* Mesh = Cast<USkinnedMeshComponent>(Hit.GetComponent());
	if (!Mesh || Hit.BoneName.IsNone())
	{
		return DefaultPart;
	}
	return Ginkgo::Combat::FindBodyPart(Hit.BoneName, BoneParts, DefaultPart, [Mesh](const FName Bone) { return Mesh->GetParentBone(Bone); });
}

FGinkgoDamageEvent UGinkgoDamageableComponent::ApplyBulletHit(const UGinkgoAmmoDefinition& Ammo, const float WeaponMultiplier, const FHitResult& Hit, const FVector& Direction,
	const float Distance, AActor* Instigator, const float PenetrationScale, const int32 Penetrated)
{
	FGinkgoDamageEvent Event;
	Event.Ammo = &Ammo;
	Event.Instigator = Instigator;
	Event.Point = Hit.ImpactPoint;
	Event.Direction = Direction;
	Event.Bone = Hit.BoneName;
	Event.Part = FindBodyPart(Hit);
	Event.Distance = Distance;
	Event.Penetrated = Penetrated;
	Event.PenetrationScale = PenetrationScale;
	Event.HealthLeft = Health;
	if (bDead || !CanWriteVitals())
	{
		return Event;
	}

	// 子弹的伤害、护甲伤害乘枪、穿射、部位、距离的倍率（Docs/Design/Damage.md「伤害怎么算」「穿射」）
	float Multiplier = WeaponMultiplier * PenetrationScale * Ammo.BodyParts.Get(Event.Part) * Ginkgo::Combat::FalloffMultiplier(Ammo.Falloff, Distance);
	// 仅对玩家控制角色应用战斗难度承伤倍率（娱乐 -90% / 平衡 -50% / 地狱 0%）；敌方与练习靶不受影响
	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner());
		OwnerPawn && (OwnerPawn->IsPlayerControlled() || Cast<APlayerController>(OwnerPawn->GetController())))
	{
		if (const UGinkgoTitleSubsystem* Title = UGinkgoTitleSubsystem::Get(this))
		{
			Multiplier *= Title->GetPlayerIncomingDamageScale();
		}
	}
	FGinkgoHitDamage HitDamage;
	HitDamage.Health = Ammo.Damage * Multiplier;
	HitDamage.Armor = Ammo.ArmorDamage * Multiplier;
	HitDamage.Penetration = Ammo.ArmorPenetration;
	Event.BaseDamage = HitDamage.Health;

	if (bFullBodyArmor)
	{
		// 玩家全身共用一池护甲：只结算一次穿甲与溢出，不能在每件交界处提前漏血。
		float Remaining = 0.f;
		for (const FGinkgoArmorPiece& Item : Armor)
		{
			Remaining += FMath::Clamp(Item.Durability, 0.f, FMath::Max(0.f, Item.MaxDurability));
		}
		Event.Outcome = Ginkgo::Combat::ApplyHitDamage(HitDamage, Remaining);
		float ToSpend = Event.Outcome.ArmorDamage;
		for (FGinkgoArmorPiece& Item : Armor)
		{
			const float Spent = FMath::Min(ToSpend, FMath::Clamp(Item.Durability, 0.f, FMath::Max(0.f, Item.MaxDurability)));
			Item.Durability -= Spent;
			ToSpend -= Spent;
		}
		if (Event.Outcome.bArmorHit)
		{
			Event.ArmorPiece = TEXT("FullBody");
			Event.ArmorLeft = Remaining;
		}
	}
	else
	{
		// 敌人继续选盖着命中部位、还有耐久的第一件护甲，不借用其他部位的防护。
		FGinkgoArmorPiece* Piece = Armor.FindByPredicate([&Event](const FGinkgoArmorPiece& Item) { return Item.Durability > 0.f && Item.Covers.Contains(Event.Part); });
		float NoArmor = 0.f;
		Event.Outcome = Ginkgo::Combat::ApplyHitDamage(HitDamage, Piece ? Piece->Durability : NoArmor);
		if (Piece)
		{
			Event.ArmorPiece = Piece->Name;
			Event.ArmorLeft = Piece->Durability;
		}
	}
	Health = FMath::Max(Health - Event.Outcome.HealthDamage, 0.f);
	Event.HealthLeft = Health;
	Event.bKilled = Health <= 0.f;
	bDead = Event.bKilled;
	if (Event.BaseDamage > 0.f)
	{
		TimeSinceLastDamaged = 0.f;
	}
	RefreshRegenTick();

	// 枪的倍率不是 1 的、穿过东西打到的写在最后（测量的路线按它们算该扣多少；原来的几段不动，旧的读法照样读得到）
	UE_LOG(LogGinkgoCombat, Log, TEXT("[Combat] %s 打到 %s 的%s（%s）：%s，伤害 %.1f，扣血 %.1f、护甲 %.1f%s，血 %.1f/%.0f，距离 %.1f m%s%s%s"),
		*GetNameSafe(Instigator), *GetNameSafe(GetOwner()), Ginkgo::Combat::DescribeBodyPart(Event.Part), *Event.Bone.ToString(), *Ammo.Ammo.ToString(), Event.BaseDamage,
		Event.Outcome.HealthDamage, Event.Outcome.ArmorDamage,
		Event.Outcome.bArmorHit ? *FString::Printf(TEXT("（%s 剩 %.1f%s）"), *Event.ArmorPiece.ToString(), Event.ArmorLeft, Event.Outcome.bArmorBroken ? TEXT("，打穿") : TEXT("")) : TEXT(""),
		Health, MaxHealth, Distance / 100.f, Event.bKilled ? TEXT("，打死") : TEXT(""),
		WeaponMultiplier != 1.f ? *FString::Printf(TEXT("，枪的倍率 ×%.4f"), WeaponMultiplier) : TEXT(""),
		Penetrated > 0 ? *FString::Printf(TEXT("，穿过 %d 个、伤害 ×%.4f"), Penetrated, PenetrationScale) : TEXT(""));
#if !UE_BUILD_SHIPPING && ENABLE_DRAW_DEBUG
	if (GShowDamage != 0)
	{
		FString Text = Event.Outcome.ArmorDamage > 0.f
			? FString::Printf(TEXT("%s -%.0f  甲 -%.0f"), Ginkgo::Combat::DescribeBodyPart(Event.Part), Event.Outcome.HealthDamage, Event.Outcome.ArmorDamage)
			: FString::Printf(TEXT("%s -%.0f"), Ginkgo::Combat::DescribeBodyPart(Event.Part), Event.Outcome.HealthDamage);
		// 穿过东西才打到的：写乘了多少（Docs/Design/Damage.md「穿射」）。一列人从开枪的人看过去叠在一点上，穿过几个往下挪几行，字不压在一起
		if (Penetrated > 0)
		{
			Text.Appendf(TEXT("  穿 ×%.2f"), PenetrationScale);
		}
		const FVector TextAt = Event.Point - FVector(0.0, 0.0, 15.0 * Penetrated);
		DrawDebugString(GetWorld(), TextAt, Text, nullptr, Event.bKilled ? FColor::Red : (Event.Outcome.bArmorHit ? FColor::Cyan : FColor::White), 1.5f, true, 1.2f);
	}
#endif
	OnDamaged.Broadcast(*this, Event);
	if (Event.bKilled)
	{
		OnLifeStateChanged.Broadcast();
		OnDied.Broadcast(*this, Event);
	}
	if (GetIsReplicated() && GetOwner()) { GetOwner()->ForceNetUpdate(); }
	return Event;
}

bool Ginkgo::Combat::IsShowDamageEnabled()
{
#if !UE_BUILD_SHIPPING
	return GShowDamage != 0;
#else
	return false;
#endif
}
