#include "Character/GinkgoCharacterAudioComponent.h"

#include "AudioDevice.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** 角色死亡空间音效的默认 MetaSound 路径 */
	constexpr TCHAR DefaultDeathSoundPath[] = TEXT("/Game/Audio/Damage/MS_Character_Death.MS_Character_Death");

	/** 低血量急促喘息循环的默认 MetaSound 路径 */
	constexpr TCHAR DefaultBreathFastSoundPath[] = TEXT("/Game/Audio/Damage/MS_Character_BreathFast.MS_Character_BreathFast");

	/** 头部骨骼插槽名，用于精确锚定角色死亡发声的世界坐标 */
	const FName HeadSocketName(TEXT("head"));
}

float Ginkgo::Character::StepLowHealthBreath(float& InOutEnvelope, const float HealthRatio, const bool bAliveAndLocal,
	const bool bDead, const float DeltaSeconds, const FGinkgoLowHealthBreathSettings& Settings)
{
	const float ClampedRatio = FMath::Clamp(HealthRatio, 0.f, 1.f);
	float Target = 0.f;
	if (bAliveAndLocal && !bDead && ClampedRatio < Settings.StartHealthRatio)
	{
		const float Span = FMath::Max(0.01f, Settings.StartHealthRatio - Settings.FullHealthRatio);
		const float Normalized = FMath::Clamp((Settings.StartHealthRatio - ClampedRatio) / Span, 0.f, 1.f);
		// 略偏柔和的幂曲线：刚进入低血量即可听见轻微急促气息，血量越低紧迫感越强
		Target = FMath::Pow(Normalized, 0.85f);
	}

	if (DeltaSeconds > 0.f)
	{
		if (Target > InOutEnvelope)
		{
			const float FadeIn = FMath::Max(0.01f, Settings.FadeInDuration);
			InOutEnvelope = FMath::Min(Target, InOutEnvelope + DeltaSeconds / FadeIn);
		}
		else if (Target < InOutEnvelope)
		{
			const float FadeOut = FMath::Max(0.01f, bDead ? Settings.DeathFadeOutDuration : Settings.FadeOutDuration);
			InOutEnvelope = FMath::Max(Target, InOutEnvelope - DeltaSeconds / FadeOut);
		}
	}
	InOutEnvelope = FMath::Clamp(InOutEnvelope, 0.f, 1.f);

	// SmoothStep（3t^2 - 2t^3）在 0 与 1 两端导数为零，保证缓入起步与缓出归零丝滑无突兀跳变
	const float Smooth = InOutEnvelope * InOutEnvelope * (3.f - 2.f * InOutEnvelope);
	return Smooth * FMath::Max(0.f, Settings.MaxVolume);
}

UGinkgoCharacterAudioComponent::UGinkgoCharacterAudioComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	static ConstructorHelpers::FObjectFinderOptional<USoundBase> DeathFinder(DefaultDeathSoundPath);
	DeathSound = DeathFinder.Get();

	static ConstructorHelpers::FObjectFinderOptional<USoundBase> BreathFinder(DefaultBreathFastSoundPath);
	LowHealthBreathSound = BreathFinder.Get();
}

void UGinkgoCharacterAudioComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshBreathTick();
}

void UGinkgoCharacterAudioComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetBreathState();
	if (BreathAudio)
	{
		BreathAudio->DestroyComponent();
		BreathAudio = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void UGinkgoCharacterAudioComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	StepLowHealthBreathAudio(DeltaTime);
}

void UGinkgoCharacterAudioComponent::SetLowHealthBreathEnabled(const bool bEnabled)
{
	bLowHealthBreathEnabled = bEnabled;
	if (!bLowHealthBreathEnabled)
	{
		ResetBreathState();
	}
	RefreshBreathTick();
}

void UGinkgoCharacterAudioComponent::RefreshBreathTick()
{
	const bool bShouldTick = bLowHealthBreathEnabled && GetNetMode() != NM_DedicatedServer;
	if (IsComponentTickEnabled() != bShouldTick)
	{
		SetComponentTickEnabled(bShouldTick);
	}
}

void UGinkgoCharacterAudioComponent::ResetBreathState()
{
	BreathEnvelope = 0.f;
	if (BreathAudio && BreathAudio->IsPlaying())
	{
		BreathAudio->Stop();
	}
}

FVector UGinkgoCharacterAudioComponent::ResolveDeathSoundOrigin() const
{
	if (const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner()))
	{
		if (const USkeletalMeshComponent* Mesh = OwnerCharacter->GetMesh(); Mesh && Mesh->DoesSocketExist(HeadSocketName))
		{
			return Mesh->GetSocketLocation(HeadSocketName);
		}
		return OwnerCharacter->GetPawnViewLocation();
	}
	if (const AActor* Owner = GetOwner())
	{
		return Owner->GetActorLocation();
	}
	return FVector::ZeroVector;
}

UAudioComponent* UGinkgoCharacterAudioComponent::PlayDeathSound()
{
	if (GetNetMode() == NM_DedicatedServer || !DeathSound || !GetWorld())
	{
		return nullptr;
	}
	const FVector Origin = ResolveDeathSoundOrigin();
	// 独立于角色本体生成 3D 空间声源：即使角色死亡后很快销毁或切检查点，约 0.65 s 的死亡声仍能完整播完
	return UGameplayStatics::SpawnSoundAtLocation(this, DeathSound, Origin);
}

void UGinkgoCharacterAudioComponent::StepLowHealthBreathAudio(const float DeltaSeconds)
{
	if (!bLowHealthBreathEnabled || GetNetMode() == NM_DedicatedServer)
	{
		ResetBreathState();
		return;
	}

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const APlayerController* OwnerPC = OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
	const bool bLocalPlayer = OwnerPC && OwnerPC->IsLocalController();
	if (!bLocalPlayer)
	{
		// 失去本地玩家控制（切视角/反附身/被 AI 接管）时立即静音，防止误播喘息
		ResetBreathState();
		return;
	}

	const UGinkgoDamageableComponent* Damageable = OwnerPawn->FindComponentByClass<UGinkgoDamageableComponent>();
	const bool bDead = !Damageable || Damageable->IsDead();
	const float MaxHealth = Damageable ? Damageable->GetMaxHealth() : 0.f;
	const float HealthRatio = MaxHealth > 0.f ? (Damageable->GetHealth() / MaxHealth) : 1.f;

	const float Volume = Ginkgo::Character::StepLowHealthBreath(
		BreathEnvelope, HealthRatio, /*bAliveAndLocal*/ !bDead, bDead, DeltaSeconds, LowHealthBreath);

	if (BreathEnvelope <= KINDA_SMALL_NUMBER || Volume <= KINDA_SMALL_NUMBER)
	{
		if (BreathAudio && BreathAudio->IsPlaying())
		{
			BreathAudio->Stop();
		}
		return;
	}

	if (!LowHealthBreathSound || !GetWorld() || !GetWorld()->GetAudioDevice().IsValid())
	{
		return;
	}

	if (!BreathAudio)
	{
		// 本地玩家私密 2D 喘息：不空间化，bIsUISound=false 保证单机暂停菜单时随游戏世界一并暂停
		BreathAudio = NewObject<UAudioComponent>(GetOwner());
		if (!BreathAudio)
		{
			return;
		}
		BreathAudio->bAutoActivate = false;
		BreathAudio->bAutoDestroy = false;
		BreathAudio->bAllowSpatialization = false;
		BreathAudio->bIsUISound = false;
		BreathAudio->SetSound(LowHealthBreathSound);
		BreathAudio->RegisterComponent();
	}

	BreathAudio->SetVolumeMultiplier(Volume);
	if (!BreathAudio->IsPlaying())
	{
		BreathAudio->Play();
	}
}
