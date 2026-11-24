#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
#include "AudioDevice.h"
#include "Character/GinkgoCharacter.h"
#include "Components/AudioComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Control/GinkgoPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponAttenuationTest, "CaseGinkgo.Weapon.Audio.AttenuationAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponAttenuationTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Name : { TEXT("Mauser98k"), TEXT("AK47"), TEXT("M1911"), TEXT("Shotgun"), TEXT("MG3"), TEXT("M79") })
	{
		const FString Path = FString::Printf(TEXT("/Game/Weapons/%s/DA_%s.DA_%s"), Name, Name, Name);
		const UGinkgoWeaponDefinition* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, *Path);
		if (!TestNotNull(*Path, Weapon)) return false;
		const USoundAttenuation* Asset = Weapon->FireAttenuation.LoadSynchronous();
		if (!TestNotNull(FString::Printf(TEXT("%s 的世界枪声衰减可加载"), Name), Asset)) return false;
		const FSoundAttenuationSettings& Settings = Asset->Attenuation;
		TestTrue(TEXT("世界枪声启用空间定位与音量衰减"), Settings.bSpatialize && Settings.bAttenuate);
		TestTrue(TEXT("立体声源在同一枪口叠加时归一化"), Settings.bApplyNormalizationToStereoSounds && Settings.StereoSpread == 0.f);
		const float Inner = Settings.AttenuationShapeExtents.X;
		const float Falloff = Settings.FalloffDistance;
		auto Gain = [&](const float Distance) { return Settings.Evaluate(FTransform::Identity, FVector(Distance, 0.0, 0.0), 1.f); };
		TestEqual(TEXT("近距离保持音量"), Gain(Inner * 0.5f), 1.f);
		const float NearGain = Gain(Inner + Falloff * 0.1f);
		const float MidGain = Gain(Inner + Falloff * 0.5f);
		const float FarGain = Gain(Inner + Falloff * 0.9f);
		TestTrue(TEXT("经过引擎实际衰减函数，音量随距离下降"), 1.f > NearGain && NearGain > MidGain && MidGain > FarGain && FarGain > 0.f);
		TestEqual(TEXT("最大范围之外静音"), Gain(Inner + Falloff + 100.f), 0.f);

		// 按交战需求检查下限，允许继续手调；上一版 60 m 约 -29 dB、100 m 约 -50 dB 会在这里失败。
		TestTrue(TEXT("60 m 交战距离的增益不低于 -12 dB"), Gain(6000.f) >= FMath::Pow(10.f, -12.f / 20.f));
		TestTrue(TEXT("100 m 仍保留可辨识的枪声主体"), Gain(10000.f) >= FMath::Pow(10.f, -18.f / 20.f));
		TestTrue(TEXT("150 m 交战边界仍有枪声，不在边界切断"), Gain(15000.f) >= FMath::Pow(10.f, -24.f / 20.f));

		// 在关键帧之间也采样，防止自定义曲线切线过冲；终点内侧应已接近静音，不能越界才硬切。
		bool bMonotonic = true;
		float PreviousGain = 1.f;
		for (float Distance = 0.f; Distance <= Inner + Falloff; Distance += 100.f)
		{
			const float CurrentGain = Gain(Distance);
			bMonotonic &= FMath::IsFinite(CurrentGain) && CurrentGain >= 0.f && CurrentGain <= PreviousGain + UE_KINDA_SMALL_NUMBER;
			PreviousGain = CurrentGain;
		}
		TestTrue(TEXT("全距离采样没有反向增响或无效值"), bMonotonic);
		TestTrue(TEXT("到达范围边缘之前已平滑淡出"), Gain(Inner + Falloff - 1.f) < 0.001f);
		TestTrue(TEXT("用温和的距离低通补足远近感"), Settings.bAttenuateWithLPF
			&& Settings.LPFRadiusMax > Settings.LPFRadiusMin && Settings.LPFFrequencyAtMin >= 18000.f
			&& Settings.LPFFrequencyAtMax >= 6000.f && Settings.LPFFrequencyAtMax < Settings.LPFFrequencyAtMin);
		AddInfo(FString::Printf(TEXT("%s 实际距离增益：30 m %.1f dB，60 m %.1f dB，100 m %.1f dB，150 m %.1f dB"), Name,
			20.f * FMath::LogX(10.f, Gain(3000.f)), 20.f * FMath::LogX(10.f, Gain(6000.f)),
			20.f * FMath::LogX(10.f, Gain(10000.f)), 20.f * FMath::LogX(10.f, Gain(15000.f))));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponAudioRoutingTest, "CaseGinkgo.Weapon.Audio.LocalAndWorldPlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponAudioRoutingTest::RunTest(const FString& Parameters)
{
	if (!GEngine || !GEngine->UseSound())
	{
		AddInfo(TEXT("当前进程禁用了音频设备。播放集成需另跑：去掉 -nosound，添加 -DeterministicAudio -AllowCommandletAudio。"));
		return true;
	}
	const FString Name = TEXT("WeaponAudioTest") + FGuid::NewGuid().ToString();
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->AddToRoot();
	Instance->InitializeStandalone(*Name, CreatePackage(*(TEXT("/Temp/") + Name)));
	UWorld* World = Instance->GetWorld();
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		Instance->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		Instance->RemoveFromRoot();
	};
	if (!TestTrue(TEXT("播放测试确实有音频设备"), World->GetAudioDevice().IsValid())) return false;
	World->bAllowAudioPlayback = true;
	AGinkgoPlayerController* PC = World->SpawnActor<AGinkgoPlayerController>();
	AAIController* AI = World->SpawnActor<AAIController>();
	AGinkgoCharacter* Player = World->SpawnActor<AGinkgoCharacter>();
	AGinkgoCharacter* Enemy = World->SpawnActor<AGinkgoCharacter>(FVector(1000.0, 0.0, 100.0), FRotator::ZeroRotator);
	if (!PC || !AI || !Player || !Enemy) return false;
	PC->SetAsLocalPlayerController();
	PC->Possess(Player);
	AI->Possess(Enemy);
	TestTrue(TEXT("本地控制器接管成功，近身声不依赖 PlayerState 初始化"), Player->GetController() == PC && Player->IsLocallyControlled());
	TestTrue(TEXT("AI 的 locally controlled 标志不能误走玩家近身声"), Enemy->IsLocallyControlled() && !Enemy->IsPlayerControlled());
	UGinkgoWeaponDefinition* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/AK47/DA_AK47.DA_AK47"));
	if (!TestNotNull(TEXT("AK 定义"), Weapon)) return false;
	for (AGinkgoCharacter* Character : { Player, Enemy })
	{
		Character->GetWeapons()->SetCarried(0, Weapon);
		Character->GetWeapons()->SetHeldSlot(0);
		Character->AttachWeaponToHand(0);
	}
	const FGinkgoWeaponCue* Cue = Weapon->Cues.Find(FWeaponTags::Weapon_Cue_Fire);
	if (!TestNotNull(TEXT("AK 开火 cue"), Cue)) return false;
	UAudioComponent* Local = Player->GetWeapons()->PlayCue(FWeaponTags::Weapon_Cue_Fire);
	if (!TestNotNull(TEXT("本地枪声实际创建组件"), Local)) return false;
	TestFalse(TEXT("自己的声音不随相机左右偏置而空间化"), Local->bAllowSpatialization);
	TestFalse(TEXT("近身枪声仍随游戏暂停"), Local->bIsUISound);
	TestEqual(TEXT("保持玩家现有枪声音量"), Local->VolumeMultiplier, Cue->Volume);
	Local->Stop();
	UAudioComponent* Spatial = Enemy->GetWeapons()->PlayCue(FWeaponTags::Weapon_Cue_Fire);
	if (!TestNotNull(TEXT("AI 枪声实际创建组件"), Spatial)) return false;
	TestTrue(TEXT("AI 枪声允许空间化"), Spatial->bAllowSpatialization);
	TestEqual(TEXT("AI 枪声使用武器配置的衰减"), Spatial->AttenuationSettings.Get(), Weapon->FireAttenuation.Get());
	const FVector Muzzle = Enemy->GetHeldWeaponMesh()->GetComponentTransform().TransformPosition(Weapon->MuzzleOffset);
	TestEqual(TEXT("播放位置是实际枪口而非角色中心"), Spatial->GetComponentLocation(), Muzzle);
	TestNull(TEXT("尾音不挂在移动的枪上"), Spatial->GetAttachParent());
	Enemy->SetActorLocation(Enemy->GetActorLocation() + FVector(1000.0, 0.0, 0.0));
	TestEqual(TEXT("开枪后移动不会搬走旧声音"), Spatial->GetComponentLocation(), Muzzle);
	Spatial->Stop();
	USoundBase* Suppressed = LoadObject<USoundBase>(nullptr, TEXT("/Game/Weapons/AK47/Audio/MS_AK47_FireSuppressed.MS_AK47_FireSuppressed"));
	if (!TestNotNull(TEXT("消音枪声"), Suppressed)) return false;
	UAudioComponent* Replaced = Enemy->GetWeapons()->PlayCue(FWeaponTags::Weapon_Cue_Fire, Suppressed);
	if (!TestNotNull(TEXT("替换声实际播放"), Replaced)) return false;
	TestEqual(TEXT("保留替换后的消音音色"), Replaced->Sound.Get(), Suppressed);
	TestTrue(TEXT("替换音色仍走同一空间衰减"), Replaced->bAllowSpatialization && Replaced->AttenuationSettings == Weapon->FireAttenuation.Get());
	Replaced->Stop();
	AddInfo(TEXT("已执行实际 AudioComponent 检查：本地/AI 分流、枪口定位、冻结尾音与消音替换声。"));
	return true;
}

#endif
