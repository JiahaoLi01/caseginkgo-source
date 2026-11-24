#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/GinkgoAICharacter.h"
#include "AIController.h"
#include "AudioDevice.h"
#include "Character/GinkgoCharacter.h"
#include "Character/GinkgoCharacterAudioComponent.h"
#include "Combat/GinkgoAmmoDefinition.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Control/GinkgoPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundSource.h"
#include "Misc/ScopeExit.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "UObject/UObjectIterator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoDamageAudioAssetsAndMathTest, "CaseGinkgo.Character.DamageAudio.AssetsAndBreathMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoDamageAudioAssetsAndMathTest::RunTest(const FString& Parameters)
{
	const USoundBase* DeathSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/Damage/MS_Character_Death.MS_Character_Death"));
	const USoundBase* BreathSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/Damage/MS_Character_BreathFast.MS_Character_BreathFast"));
	const USoundAttenuation* DeathAttenuation = LoadObject<USoundAttenuation>(nullptr, TEXT("/Game/Audio/Damage/SA_Character_Death.SA_Character_Death"));
	if (!TestNotNull(TEXT("加载死亡空间音效 MS_Character_Death"), DeathSound)
		|| !TestNotNull(TEXT("加载低血量喘息音效 MS_Character_BreathFast"), BreathSound)
		|| !TestNotNull(TEXT("加载死亡衰减资产 SA_Character_Death"), DeathAttenuation))
	{
		return false;
	}

	// 1. 核验玩家与 AI 默认角色音效配置：共用死亡音效，仅玩家默认开启低血量喘息
	const auto* PlayerDefaults = GetDefault<AGinkgoCharacter>();
	const auto* EnemyDefaults = GetDefault<AGinkgoAICharacter>();
	if (!TestNotNull(TEXT("玩家角色默认挂有 CharacterAudio 组件"), PlayerDefaults->GetCharacterAudio())
		|| !TestNotNull(TEXT("AI 角色默认挂有 CharacterAudio 组件"), EnemyDefaults->GetCharacterAudio()))
	{
		return false;
	}
	TestEqual(TEXT("玩家默认死亡音效指向 MS_Character_Death"), static_cast<const USoundBase*>(PlayerDefaults->GetCharacterAudio()->GetDeathSound()), DeathSound);
	TestEqual(TEXT("AI 默认死亡音效指向 MS_Character_Death"), static_cast<const USoundBase*>(EnemyDefaults->GetCharacterAudio()->GetDeathSound()), DeathSound);
	TestEqual(TEXT("玩家默认低血量喘息指向 MS_Character_BreathFast"), static_cast<const USoundBase*>(PlayerDefaults->GetCharacterAudio()->GetLowHealthBreathSound()), BreathSound);
	TestTrue(TEXT("玩家默认开启低血量急促喘息"), PlayerDefaults->GetCharacterAudio()->IsLowHealthBreathEnabled());
	TestFalse(TEXT("AI 敌人默认关闭低血量急促喘息"), EnemyDefaults->GetCharacterAudio()->IsLowHealthBreathEnabled());

	// 2. 核验 MS_Character_Death 图结构：7 段单声道不连重随机取样 + 随机音高 + 随机增益 + 绑定 SA_Character_Death
	const auto* DeathMeta = Cast<UMetaSoundSource>(DeathSound);
	if (!TestNotNull(TEXT("MS_Character_Death 为 MetaSoundSource"), DeathMeta)) return false;
	{
		const auto& Document = DeathMeta->GetConstDocument();
		const auto& Graph = Document.RootGraph.GetConstDefaultGraph();
		auto CountNodes = [&](const FName Namespace, const FName Name, const FName Variant) -> int32
		{
			const auto* Class = Document.Dependencies.FindByPredicate([&](const auto& Item)
			{
				const auto& Key = Item.Metadata.GetClassName();
				return Key.Namespace == Namespace && Key.Name == Name && Key.Variant == Variant;
			});
			if (!Class) return 0;
			int32 Count = 0;
			for (const auto& Node : Graph.Nodes)
			{
				if (Node.ClassID == Class->ID) ++Count;
			}
			return Count;
		};
		auto FindNode = [&](const FName Namespace, const FName Name, const FName Variant) -> const FMetasoundFrontendNode*
		{
			const auto* Class = Document.Dependencies.FindByPredicate([&](const auto& Item)
			{
				const auto& Key = Item.Metadata.GetClassName();
				return Key.Namespace == Namespace && Key.Name == Name && Key.Variant == Variant;
			});
			return Class ? Graph.Nodes.FindByPredicate([&](const auto& Node) { return Node.ClassID == Class->ID; }) : nullptr;
		};

		const auto* Picker = FindNode(TEXT("Array"), TEXT("Random Get"), TEXT("WaveAsset:Array"));
		const auto* Player = FindNode(TEXT("UE"), TEXT("Wave Player"), TEXT("Mono"));
		if (!TestNotNull(TEXT("死亡音效含随机取样节点"), Picker)
			|| !TestNotNull(TEXT("死亡音效含单声道播放器（适配 3D 空间化）"), Player))
		{
			return false;
		}
		TestEqual(TEXT("死亡音效含 2 个随机浮点节点（音高与增益）"), CountNodes(TEXT("UE"), TEXT("RandomFloat"), NAME_None), 2);
		TestEqual(TEXT("死亡音效含分贝转线性增益节点"), CountNodes(TEXT("UE"), TEXT("Decibels to Linear Gain"), TEXT("Float")), 1);
		TestEqual(TEXT("死亡音效含单声道增益乘法节点"), CountNodes(TEXT("UE"), TEXT("Multiply"), TEXT("Audio by Float")), 1);

		auto Literal = [&](const FName Name) -> const FMetasoundFrontendLiteral*
		{
			const auto* Input = Picker->Interface.Inputs.FindByPredicate([&](const auto& Vertex) { return Vertex.Name == Name; });
			if (!Input) return nullptr;
			const auto* Value = Picker->InputLiterals.FindByPredicate([&](const auto& Item) { return Item.VertexID == Input->VertexID; });
			return Value ? &Value->Value : nullptr;
		};

		TArray<UObject*> Waves;
		const auto* Samples = Literal(TEXT("In Array"));
		if (TestTrue(TEXT("死亡音效 7 段样本数组已落盘"), Samples && Samples->TryGet(Waves)))
		{
			TestEqual(TEXT("死亡音效样本数量为 7 段"), Waves.Num(), 7);
			TSet<UObject*> Unique;
			for (UObject* Item : Waves)
			{
				Unique.Add(Item);
				const auto* Wave = Cast<USoundWave>(Item);
				if (!TestNotNull(TEXT("死亡数组项为 SoundWave"), Wave)) continue;
				TestEqual(TEXT("死亡样本保持单声道"), Wave->NumChannels, 1);
				TestFalse(TEXT("死亡样本单次播放不循环"), Wave->bLooping);
				TestTrue(TEXT("死亡样本时长落在 0.50–0.85 s 区间"), Wave->Duration >= 0.50f && Wave->Duration <= 0.85f);
			}
			TestEqual(TEXT("7 段死亡样本互不重复"), Unique.Num(), 7);
		}

		int32 NoRepeats = 0;
		int32 Sharing = 0;
		bool bShared = false;
		TestTrue(TEXT("死亡音效禁止连续重复同段"), Literal(TEXT("No Repeats")) && Literal(TEXT("No Repeats"))->TryGet(NoRepeats) && NoRepeats == 1);
		TestTrue(TEXT("死亡音效跨实例共享随机历史"), Literal(TEXT("Enable Shared State")) && Literal(TEXT("Enable Shared State"))->TryGet(bShared) && bShared);
		TestTrue(TEXT("死亡音效按样本数组隔离共享状态"), Literal(TEXT("Shared State Behavior")) && Literal(TEXT("Shared State Behavior"))->TryGet(Sharing) && Sharing == 2);
		TestEqual(TEXT("MS_Character_Death 内嵌绑定 SA_Character_Death"),
			static_cast<const USoundAttenuation*>(DeathMeta->AttenuationSettings.Get()), DeathAttenuation);
	}

	// 3. 核验 SA_Character_Death 的 3D 空间化、单调衰减曲线与距离低通
	{
		const FSoundAttenuationSettings& Atten = DeathAttenuation->Attenuation;
		TestTrue(TEXT("死亡衰减启用 3D 空间定位与距离衰减"), Atten.bSpatialize && Atten.bAttenuate);
		TestEqual(TEXT("死亡衰减使用自定义曲线"), Atten.DistanceAlgorithm, EAttenuationDistanceModel::Custom);
		const float Inner = Atten.AttenuationShapeExtents.X;
		const float Falloff = Atten.FalloffDistance;
		TestTrue(TEXT("死亡衰减范围为内圈 2 m、外圈 45 m"),
			FMath::IsNearlyEqual(Inner, 200.f, .5f) && FMath::IsNearlyEqual(Inner + Falloff, 4500.f, .5f));
		auto Gain = [&](const float DistanceCm)
		{
			return Atten.Evaluate(FTransform::Identity, FVector(DistanceCm, 0., 0.), 1.f);
		};
		TestEqual(TEXT("2 m 内保持满音量"), Gain(150.f), 1.f);
		TestEqual(TEXT("45 m 外完全静音"), Gain(4600.f), 0.f);
		bool bMonotonic = true;
		float Prev = 1.f;
		for (float Dist = 0.f; Dist <= Inner + Falloff; Dist += 50.f)
		{
			const float Cur = Gain(Dist);
			bMonotonic &= FMath::IsFinite(Cur) && Cur >= 0.f && Cur <= Prev + UE_KINDA_SMALL_NUMBER;
			Prev = Cur;
		}
		TestTrue(TEXT("死亡距离衰减曲线单调递减无过冲"), bMonotonic);
		TestTrue(TEXT("死亡衰减启用距离低通滤波"), Atten.bAttenuateWithLPF && Atten.LPFFrequencyAtMax < Atten.LPFFrequencyAtMin);
	}

	// 4. 核验 MS_Character_BreathFast 图结构：循环单声道播放器 + 随机起始时刻 + 微幅随机音高 + PlayWhenSilent + 非空间 2D
	const auto* BreathMeta = Cast<UMetaSoundSource>(BreathSound);
	if (!TestNotNull(TEXT("MS_Character_BreathFast 为 MetaSoundSource"), BreathMeta)) return false;
	{
		TestNull(TEXT("喘息音效不绑定空间衰减（本地 2D）"), BreathMeta->AttenuationSettings.Get());
		TestEqual(TEXT("喘息音效启用 PlayWhenSilent 防止缓入低音量被剔除"), BreathMeta->VirtualizationMode, EVirtualizationMode::PlayWhenSilent);

		const auto& Document = BreathMeta->GetConstDocument();
		const auto& Graph = Document.RootGraph.GetConstDefaultGraph();
		auto HasNode = [&](const FName Namespace, const FName Name, const FName Variant) -> bool
		{
			const auto* Class = Document.Dependencies.FindByPredicate([&](const auto& Item)
			{
				const auto& Key = Item.Metadata.GetClassName();
				return Key.Namespace == Namespace && Key.Name == Name && Key.Variant == Variant;
			});
			return Class && Graph.Nodes.ContainsByPredicate([&](const auto& Node) { return Node.ClassID == Class->ID; });
		};
		TestTrue(TEXT("喘息图包含单声道循环 Wave Player"), HasNode(TEXT("UE"), TEXT("Wave Player"), TEXT("Mono")));
		TestTrue(TEXT("喘息图包含随机音高节点"), HasNode(TEXT("UE"), TEXT("RandomFloat"), NAME_None));
		TestTrue(TEXT("喘息图包含随机起播时刻节点"), HasNode(TEXT("UE"), TEXT("RandomTime"), NAME_None));

		const auto* BreathWave = LoadObject<USoundWave>(nullptr, TEXT("/Game/Audio/Damage/SW_Character_BreathFast.SW_Character_BreathFast"));
		if (TestNotNull(TEXT("加载循环喘息样本 SW_Character_BreathFast"), BreathWave))
		{
			TestEqual(TEXT("喘息样本保持单声道"), BreathWave->NumChannels, 1);
			TestTrue(TEXT("喘息样本开启无缝循环"), BreathWave->bLooping);
			TestTrue(TEXT("喘息循环样本时长超过 25 s"), BreathWave->Duration >= 25.f);
		}
	}

	// 5. 核验低血量喘息缓入/缓出纯函数包络数学
	{
		FGinkgoLowHealthBreathSettings Settings;
		float Envelope = 0.f;

		TestEqual(TEXT("满血时喘息保持静音"),
			Ginkgo::Character::StepLowHealthBreath(Envelope, 1.f, true, false, .5f, Settings), 0.f);
		TestEqual(TEXT("略高于起步阈值（65% 血）时仍不触发喘息"),
			Ginkgo::Character::StepLowHealthBreath(Envelope, .65f, true, false, .5f, Settings), 0.f);

		// 掉到 18% 低血量：在 FadeInDuration (0.85 s) 内单调缓入，首帧不直接跳到最大音量
		float PrevVol = 0.f;
		bool bFadeInMonotonic = true;
		for (int32 Step = 0; Step < 5; ++Step)
		{
			const float Vol = Ginkgo::Character::StepLowHealthBreath(Envelope, .18f, true, false, .15f, Settings);
			bFadeInMonotonic &= (Vol > PrevVol);
			PrevVol = Vol;
		}
		TestTrue(TEXT("低血量前 0.75 s 音量严格单调缓入"), bFadeInMonotonic);
		TestTrue(TEXT("0.75 s 时尚未突兀跳满，仍处于柔和爬升后段"), Envelope > .8f && Envelope < 1.f);
		const float PeakVol = Ginkgo::Character::StepLowHealthBreath(Envelope, .18f, true, false, .20f, Settings);
		TestEqual(TEXT("超过 FadeInDuration 后达到完整最大喘息音量"), PeakVol, Settings.MaxVolume, .001f);

		// 回血至 75%（离开低血量区间）：在 FadeOutDuration (1.25 s) 内单调缓出至 0
		float PrevOut = PeakVol;
		bool bFadeOutMonotonic = true;
		for (int32 Step = 0; Step < 5; ++Step)
		{
			const float Vol = Ginkgo::Character::StepLowHealthBreath(Envelope, .75f, true, false, .20f, Settings);
			bFadeOutMonotonic &= (Vol < PrevOut);
			PrevOut = Vol;
		}
		TestTrue(TEXT("脱战回血离开阈值后音量严格单调缓出"), bFadeOutMonotonic);
		TestTrue(TEXT("1.0 s 缓出中仍有柔顺尾音，未生硬切断"), Envelope > .1f && PrevOut > 0.f);
		const float SilencedVol = Ginkgo::Character::StepLowHealthBreath(Envelope, .85f, true, false, .40f, Settings);
		TestEqual(TEXT("超过 FadeOutDuration 后完全静音归零"), SilencedVol, 0.f);
		TestEqual(TEXT("完全缓出后包络归零"), Envelope, 0.f);

		// 死亡时采用更快的 DeathFadeOutDuration (0.35 s) 迅速淡出收尾
		Envelope = 1.f;
		const float MidDeathVol = Ginkgo::Character::StepLowHealthBreath(Envelope, 0.f, false, true, .175f, Settings);
		TestTrue(TEXT("死亡半程（0.175 s）喘息已明显收弱"), MidDeathVol > 0.f && MidDeathVol < Settings.MaxVolume * .6f);
		const float EndDeathVol = Ginkgo::Character::StepLowHealthBreath(Envelope, 0.f, false, true, .25f, Settings);
		TestEqual(TEXT("超过 DeathFadeOutDuration 后尸体彻底停止喘息"), EndDeathVol, 0.f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoDamageAudioPlaybackTest, "CaseGinkgo.Character.DamageAudio.LivePlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoDamageAudioPlaybackTest::RunTest(const FString& Parameters)
{
	if (!GEngine || !GEngine->UseSound())
	{
		AddInfo(TEXT("当前进程禁用了音频设备。角色死亡与低血量喘息播放集成需去掉 -nosound，添加 -DeterministicAudio -AllowCommandletAudio。"));
		return true;
	}

	const FString WorldName = TEXT("DamageAudioTest") + FGuid::NewGuid().ToString();
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->AddToRoot();
	Instance->InitializeStandalone(*WorldName, CreatePackage(*(TEXT("/Temp/") + WorldName)));
	UWorld* World = Instance->GetWorld();
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		if (World && World->HasBegunPlay())
		{
			World->EndPlay(EEndPlayReason::Quit);
		}
		Instance->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		Instance->RemoveFromRoot();
	};
	if (!TestTrue(TEXT("独立测试世界具有有效音频设备"), World->GetAudioDevice().IsValid())) return false;
	World->bAllowAudioPlayback = true;
	if (AWorldSettings* WorldSettings = World->GetWorldSettings())
	{
		WorldSettings->NotifyBeginPlay();
		WorldSettings->NotifyMatchStarted();
	}
	World->BeginPlay();

	auto* PC = World->SpawnActor<AGinkgoPlayerController>();
	auto* AI = World->SpawnActor<AAIController>();
	auto* Player = World->SpawnActor<AGinkgoCharacter>(FVector(0., 0., 100.), FRotator::ZeroRotator);
	auto* Enemy = World->SpawnActor<AGinkgoAICharacter>(FVector(1000., 0., 100.), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("生成本地玩家与 AI 敌人"), PC && AI && Player && Enemy ? Player : nullptr)) return false;
	PC->SetAsLocalPlayerController();
	PC->Possess(Player);
	AI->Possess(Enemy);

	auto FindPlayingVoices = [&](const USoundBase* TargetSound)
	{
		TArray<UAudioComponent*> Matching;
		for (TObjectIterator<UAudioComponent> It; It; ++It)
		{
			if (It->GetWorld() == World && It->IsPlaying() && It->Sound == TargetSound)
			{
				Matching.Add(*It);
			}
		}
		return Matching;
	};

	const USoundBase* DeathSound = Player->GetCharacterAudio()->GetDeathSound();
	const USoundBase* BreathSound = Player->GetCharacterAudio()->GetLowHealthBreathSound();
	if (!TestNotNull(TEXT("玩家持有死亡与喘息音效资产"), DeathSound && BreathSound ? DeathSound : nullptr)) return false;

	auto* LethalAmmo = NewObject<UGinkgoAmmoDefinition>();
	LethalAmmo->Damage = 500.f;
	LethalAmmo->ArmorDamage = 500.f;
	LethalAmmo->ArmorPenetration = 1.f;

	// 1. 敌人受击进入低血量不触发低血量喘息，死亡瞬间在头部位置触发 3D 空间死亡音效
	auto* LightAmmo = NewObject<UGinkgoAmmoDefinition>();
	LightAmmo->Damage = 75.f;
	LightAmmo->ArmorDamage = 75.f;
	LightAmmo->ArmorPenetration = 1.f;

	Enemy->GetDamageable()->ApplyBulletHit(*LightAmmo, 1.f, FHitResult(), FVector::ForwardVector, 1000.f, Player);
	Enemy->GetCharacterAudio()->StepLowHealthBreathAudio(.5f);
	TestEqual(TEXT("AI 敌人残血绝不播放低血量喘息"), FindPlayingVoices(BreathSound).Num(), 0);
	TestNull(TEXT("AI 敌人不分配喘息音频组件"), Enemy->GetCharacterAudio()->GetBreathAudioComponent());

	const FVector EnemyExpectedOrigin = Enemy->GetMesh()->DoesSocketExist(TEXT("head"))
		? Enemy->GetMesh()->GetSocketLocation(TEXT("head")) : Enemy->GetPawnViewLocation();
	Enemy->GetDamageable()->ApplyBulletHit(*LethalAmmo, 1.f, FHitResult(), FVector::ForwardVector, 1000.f, Player);
	const TArray<UAudioComponent*> EnemyDeathVoices = FindPlayingVoices(DeathSound);
	if (!TestEqual(TEXT("AI 敌人死亡触发一声 MS_Character_Death"), EnemyDeathVoices.Num(), 1)) return false;
	TestTrue(TEXT("AI 死亡音效启用 3D 空间化且独立留存不挂尸体"),
		EnemyDeathVoices[0]->bAllowSpatialization && EnemyDeathVoices[0]->GetAttachParent() == nullptr);
	TestTrue(TEXT("AI 死亡音效定位在头部世界坐标"), EnemyDeathVoices[0]->GetComponentLocation().Equals(EnemyExpectedOrigin, 5.f));
	EnemyDeathVoices[0]->Stop();

	// 2. 本地玩家满血静默 -> 受伤低血量平滑缓入 -> 3s 后回血平滑缓出并停播
	Player->GetCharacterAudio()->StepLowHealthBreathAudio(.5f);
	TestEqual(TEXT("满血玩家不播放喘息"), FindPlayingVoices(BreathSound).Num(), 0);

	Player->GetDamageable()->ApplyBulletHit(*LightAmmo, 1.f, FHitResult(), FVector::ForwardVector, 1000.f, Enemy);
	TestTrue(TEXT("玩家受击后处于残血存活状态"), !Player->GetDamageable()->IsDead() && Player->GetDamageable()->GetHealth() < 30.f);

	Player->GetCharacterAudio()->StepLowHealthBreathAudio(.15f);
	UAudioComponent* BreathAudio = Player->GetCharacterAudio()->GetBreathAudioComponent();
	if (!TestNotNull(TEXT("玩家残血创建低血量喘息音频组件"), BreathAudio)) return false;
	TestTrue(TEXT("低血量喘息已开始播放且为非空间化 2D"), BreathAudio->IsPlaying() && !BreathAudio->bAllowSpatialization);
	const float EarlyVol = BreathAudio->VolumeMultiplier;

	Player->GetCharacterAudio()->StepLowHealthBreathAudio(.40f);
	const float MidVol = BreathAudio->VolumeMultiplier;
	Player->GetCharacterAudio()->StepLowHealthBreathAudio(.60f);
	const float FullVol = BreathAudio->VolumeMultiplier;
	TestTrue(TEXT("低血量喘息音量随时间平滑缓入递增"), EarlyVol > 0.f && EarlyVol < MidVol && MidVol < FullVol);

	// 推进 3 s 无受击等待 + 2 s 回血，同时推进喘息音频，确认脱离低血量后平滑缓出并停播
	Player->GetDamageable()->StepHealthRegen(3.f);
	Player->GetDamageable()->StepHealthRegen(2.f);
	TestEqual(TEXT("玩家经过 3 s + 2 s 完整回满血"), Player->GetDamageable()->GetHealth(), Player->GetDamageable()->GetMaxHealth(), .01f);

	Player->GetCharacterAudio()->StepLowHealthBreathAudio(.40f);
	const float FadingOutVol = BreathAudio->VolumeMultiplier;
	TestTrue(TEXT("满血后 0.4 s 仍在平滑缓出而非硬切"), BreathAudio->IsPlaying() && FadingOutVol > 0.f && FadingOutVol < FullVol);

	Player->GetCharacterAudio()->StepLowHealthBreathAudio(1.20f);
	TestFalse(TEXT("缓出完成后自动停止喘息音频组件释放声道"), BreathAudio->IsPlaying());
	TestEqual(TEXT("缓出完成后包络归零"), Player->GetCharacterAudio()->GetBreathEnvelope(), 0.f);

	// 3. 本地玩家残血喘息期间被击杀 -> 头部播 3D 空间死亡声 + 喘息在 0.35 s 内快速淡出停播
	Player->GetDamageable()->ApplyBulletHit(*LightAmmo, 1.f, FHitResult(), FVector::ForwardVector, 1000.f, Enemy);
	Player->GetCharacterAudio()->StepLowHealthBreathAudio(1.0f);
	TestTrue(TEXT("再次残血重新激活喘息"), BreathAudio->IsPlaying());

	const FVector PlayerExpectedOrigin = Player->GetMesh()->DoesSocketExist(TEXT("head"))
		? Player->GetMesh()->GetSocketLocation(TEXT("head")) : Player->GetPawnViewLocation();
	Player->GetDamageable()->ApplyBulletHit(*LethalAmmo, 1.f, FHitResult(), FVector::ForwardVector, 1000.f, Enemy);
	const TArray<UAudioComponent*> PlayerDeathVoices = FindPlayingVoices(DeathSound);
	if (!TestEqual(TEXT("玩家死亡同样触发一声 MS_Character_Death"), PlayerDeathVoices.Num(), 1)) return false;
	TestTrue(TEXT("玩家死亡音效启用 3D 空间化并位于头部坐标"),
		PlayerDeathVoices[0]->bAllowSpatialization
		&& PlayerDeathVoices[0]->GetComponentLocation().Equals(PlayerExpectedOrigin, 5.f));
	PlayerDeathVoices[0]->Stop();

	Player->GetCharacterAudio()->StepLowHealthBreathAudio(.50f);
	TestFalse(TEXT("玩家死亡后 0.5 s 内喘息平滑淡出停止"), BreathAudio->IsPlaying());

	return true;
}

#endif
