#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AudioDevice.h"
#include "Character/GinkgoCharacter.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
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
#include "Throwables/GinkgoFlashbangComponent.h"
#include "Throwables/GinkgoFlashProjectile.h"
#include "UObject/UObjectIterator.h"

namespace
{
	/** 单组闪光弹空间音效的图结构与风格化衰减预期约束。 */
	struct FFlashAudioExpectation
	{
		/** 断言前缀标签。 */
		const TCHAR* Label = TEXT("");

		/** 待核验的声音资产指针。 */
		USoundBase* Sound = nullptr;

		/** 随机取样数组中应包含的独立裁剪样本数。 */
		int32 WaveCount = 0;

		/** 裁剪后单条样本的最短允许时长（秒）。 */
		float MinDuration = 0.f;

		/** 裁剪后单条样本的最长允许时长（秒）。 */
		float MaxDuration = 0.f;

		/** 球形内半径（cm），范围内保持 0 dB 原始能量。 */
		float ExpectedInnerCm = 0.f;

		/** 球形总可听半径（cm），外侧完全静音。 */
		float ExpectedOuterCm = 0.f;

		/** 近距离缓降检查点距离（cm），验证近距衰减速度降低。 */
		float NearCheckCm = 0.f;

		/** 近距离检查点允许的最低增益（dB）。 */
		float NearMinDb = 0.f;

		/** 中距离战术可读检查点距离（cm）。 */
		float MidCheckCm = 0.f;

		/** 中距离检查点允许的最低增益（dB）。 */
		float MidMinDb = 0.f;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoFlashbangAudioAssetTest, "CaseGinkgo.Flashbang.Audio.AssetsAndAttenuation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoFlashbangAudioAssetTest::RunTest(const FString& Parameters)
{
	const auto* CompDefaults = GetDefault<UGinkgoFlashbangComponent>();
	const auto* ProjDefaults = GetDefault<AGinkgoFlashProjectile>();

	const FFlashAudioExpectation Specs[] = {
		{TEXT("投掷"), CompDefaults->GetThrowSound(), 4, .50f, .70f, 350.f, 2500.f, 800.f, -2.5f, 1300.f, -6.0f},
		{TEXT("撞墙弹跳"), ProjDefaults->GetImpactSound(), 6, .08f, .15f, 400.f, 6500.f, 1200.f, -2.5f, 2200.f, -5.5f},
		{TEXT("爆闪爆炸"), ProjDefaults->GetExplodeSound(), 4, 1.38f, 1.52f, 400.f, 20000.f, 1500.f, -2.0f, 3000.f, -5.0f},
	};

	TSet<const USoundAttenuation*> DistinctAttenuations;
	for (const FFlashAudioExpectation& Spec : Specs)
	{
		const auto* MetaSound = Cast<UMetaSoundSource>(Spec.Sound);
		if (!TestNotNull(*FString::Printf(TEXT("%s 默认绑定真实 MetaSoundSource"), Spec.Label), MetaSound)) return false;

		const auto& Document = MetaSound->GetConstDocument();
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
		const auto* Player = FindNode(TEXT("UE"), TEXT("Wave Player"), TEXT("Stereo"));
		if (!TestNotNull(*FString::Printf(TEXT("%s 含随机取样节点"), Spec.Label), Picker)
			|| !TestNotNull(*FString::Printf(TEXT("%s 含双声道播放器"), Spec.Label), Player))
		{
			return false;
		}
		TestEqual(*FString::Printf(TEXT("%s 立体声双通道独立乘随机增益"), Spec.Label),
			CountNodes(TEXT("UE"), TEXT("Multiply"), TEXT("Audio by Float")), 2);

		auto Literal = [&](const FName Name) -> const FMetasoundFrontendLiteral*
		{
			const auto* Input = Picker->Interface.Inputs.FindByPredicate([&](const auto& Vertex) { return Vertex.Name == Name; });
			if (!Input) return nullptr;
			const auto* Value = Picker->InputLiterals.FindByPredicate([&](const auto& Item) { return Item.VertexID == Input->VertexID; });
			return Value ? &Value->Value : nullptr;
		};

		TArray<UObject*> Waves;
		const auto* Samples = Literal(TEXT("In Array"));
		if (TestTrue(*FString::Printf(TEXT("%s 样本数组已落盘"), Spec.Label), Samples && Samples->TryGet(Waves)))
		{
			TestEqual(*FString::Printf(TEXT("%s 裁剪样本数量"), Spec.Label), Waves.Num(), Spec.WaveCount);
			TSet<UObject*> Unique;
			for (UObject* Item : Waves)
			{
				Unique.Add(Item);
				const auto* Wave = Cast<USoundWave>(Item);
				if (!TestNotNull(*FString::Printf(TEXT("%s 数组项是 SoundWave"), Spec.Label), Wave)) continue;
				TestEqual(*FString::Printf(TEXT("%s 保持双声道输出电平"), Spec.Label), Wave->NumChannels, 2);
				TestFalse(*FString::Printf(TEXT("%s 单次播放不循环"), Spec.Label), Wave->bLooping);
				TestTrue(*FString::Printf(TEXT("%s 时长 %.3f s 落在裁剪区间"), Spec.Label, Wave->Duration),
					Wave->Duration >= Spec.MinDuration && Wave->Duration <= Spec.MaxDuration);
			}
			TestEqual(*FString::Printf(TEXT("%s 各变体互不重复引用"), Spec.Label), Unique.Num(), Spec.WaveCount);
		}

		int32 NoRepeats = 0;
		int32 Sharing = 0;
		bool bShared = false;
		TestTrue(*FString::Printf(TEXT("%s 禁止连续重复"), Spec.Label),
			Literal(TEXT("No Repeats")) && Literal(TEXT("No Repeats"))->TryGet(NoRepeats) && NoRepeats == 1);
		TestTrue(*FString::Printf(TEXT("%s 跨播放实例共享历史"), Spec.Label),
			Literal(TEXT("Enable Shared State")) && Literal(TEXT("Enable Shared State"))->TryGet(bShared) && bShared);
		TestTrue(*FString::Printf(TEXT("%s 按样本数组隔离共享状态"), Spec.Label),
			Literal(TEXT("Shared State Behavior")) && Literal(TEXT("Shared State Behavior"))->TryGet(Sharing) && Sharing == 2);

		const USoundAttenuation* Attenuation = MetaSound->AttenuationSettings.Get();
		if (!TestNotNull(*FString::Printf(TEXT("%s 绑定独立空间衰减资产"), Spec.Label), Attenuation)) return false;
		DistinctAttenuations.Add(Attenuation);

		const FSoundAttenuationSettings& Settings = Attenuation->Attenuation;
		TestTrue(*FString::Printf(TEXT("%s 启用三维空间定位与距离衰减"), Spec.Label), Settings.bSpatialize && Settings.bAttenuate);
		TestTrue(*FString::Printf(TEXT("%s 启用同点立体声归一化"), Spec.Label),
			Settings.bApplyNormalizationToStereoSounds && FMath::IsNearlyZero(Settings.StereoSpread));
		TestEqual(*FString::Printf(TEXT("%s 采用自定义风格化曲线"), Spec.Label),
			Settings.DistanceAlgorithm, EAttenuationDistanceModel::Custom);

		const float Inner = Settings.AttenuationShapeExtents.X;
		const float Falloff = Settings.FalloffDistance;
		TestTrue(*FString::Printf(TEXT("%s 内外半径符合模板"), Spec.Label),
			FMath::IsNearlyEqual(Inner, Spec.ExpectedInnerCm, .5f) && FMath::IsNearlyEqual(Inner + Falloff, Spec.ExpectedOuterCm, .5f));

		auto Gain = [&](const float DistanceCm)
		{
			return Settings.Evaluate(FTransform::Identity, FVector(DistanceCm, 0., 0.), 1.f);
		};
		TestEqual(*FString::Printf(TEXT("%s 内半径保持满音量"), Spec.Label), Gain(Inner * .5f), 1.f);
		TestTrue(*FString::Printf(TEXT("%s 近距离缓降检查点保持高能量"), Spec.Label),
			Gain(Spec.NearCheckCm) >= FMath::Pow(10.f, Spec.NearMinDb / 20.f));
		TestTrue(*FString::Printf(TEXT("%s 中距离检查点保持清晰可读"), Spec.Label),
			Gain(Spec.MidCheckCm) >= FMath::Pow(10.f, Spec.MidMinDb / 20.f));

		bool bMonotonic = true;
		float Previous = 1.f;
		for (float Distance = 0.f; Distance <= Inner + Falloff; Distance += 50.f)
		{
			const float Current = Gain(Distance);
			bMonotonic &= FMath::IsFinite(Current) && Current >= 0.f && Current <= Previous + UE_KINDA_SMALL_NUMBER;
			Previous = Current;
		}
		TestTrue(*FString::Printf(TEXT("%s 全程单调递减且无切线过冲"), Spec.Label), bMonotonic);
		TestTrue(*FString::Printf(TEXT("%s 边界内侧已平滑淡出"), Spec.Label), Gain(Inner + Falloff - 1.f) < .001f);
		TestEqual(*FString::Printf(TEXT("%s 边界以外静音"), Spec.Label), Gain(Inner + Falloff + 50.f), 0.f);
		TestTrue(*FString::Printf(TEXT("%s 启用温和空气吸收低通"), Spec.Label),
			Settings.bAttenuateWithLPF && Settings.LPFRadiusMax > Settings.LPFRadiusMin
			&& Settings.LPFFrequencyAtMin >= 18000.f && Settings.LPFFrequencyAtMax >= 7000.f
			&& Settings.LPFFrequencyAtMax < Settings.LPFFrequencyAtMin);
	}
	TestEqual(TEXT("投掷、撞墙、爆炸各自使用不同尺度的风格化衰减模板"), DistinctAttenuations.Num(), 3);

	const USoundAttenuation* ExplodeAttenuation = ProjDefaults->GetExplodeSound()->AttenuationSettings.Get();
	const float Gain100m = ExplodeAttenuation->Attenuation.Evaluate(FTransform::Identity, FVector(10000., 0., 0.), 1.f);
	const float Gain150m = ExplodeAttenuation->Attenuation.Evaluate(FTransform::Identity, FVector(15000., 0., 0.), 1.f);
	TestTrue(TEXT("爆炸声在 100 m 闪光半径边界仍具冲击可读性"), Gain100m >= FMath::Pow(10.f, -16.f / 20.f));
	TestTrue(TEXT("爆炸声在 150 m 交战边界不硬切"), Gain150m >= FMath::Pow(10.f, -23.f / 20.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoFlashbangAudioPlaybackTest, "CaseGinkgo.Flashbang.Audio.SpatialPlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoFlashbangAudioPlaybackTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("低于门限的擦碰或静止微接触不发声"), AGinkgoFlashProjectile::ComputeImpactVolume(25.), 0.f);
	const float SoftBounce = AGinkgoFlashProjectile::ComputeImpactVolume(85.);
	const float MidBounce = AGinkgoFlashProjectile::ComputeImpactVolume(300.);
	const float HardSlam = AGinkgoFlashProjectile::ComputeImpactVolume(750.);
	TestTrue(TEXT("碰撞音量随法向撞击速度平滑递增并封顶为 1"),
		SoftBounce > .25f && MidBounce > SoftBounce && HardSlam > MidBounce && FMath::IsNearlyEqual(HardSlam, 1.f));

	if (!GEngine || !GEngine->UseSound())
	{
		AddInfo(TEXT("当前进程禁用了音频设备。空间播放测试需去掉 -nosound，添加 -DeterministicAudio -AllowCommandletAudio。"));
		return true;
	}

	const FString WorldName = TEXT("FlashbangAudioTest") + FGuid::NewGuid().ToString();
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->AddToRoot();
	Instance->InitializeStandalone(*WorldName, CreatePackage(*(TEXT("/Temp/") + WorldName)));
	UWorld* World = Instance->GetWorld();
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
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
	auto* Player = World->SpawnActor<AGinkgoCharacter>(FVector(0., 0., 100.), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("生成本地控制器"), PC) || !TestNotNull(TEXT("生成测试角色"), Player)) return false;
	PC->SetAsLocalPlayerController();
	PC->Possess(Player);

	auto* Flash = Player->GetFlashbang();
	Flash->RestoreInventory(true);
	Flash->Press();
	World->Tick(LEVELTICK_All, .6f);
	if (!TestTrue(TEXT("进入胸前持握阶段"), Flash->GetPhase() == EGinkgoFlashPhase::Hold)) return false;
	TestNull(TEXT("仅持握瞄准时不提前播放投掷声"), Flash->GetThrowAudio());

	// 挥臂中途取消：验证已触发的三维挥臂声在脱手前取消时立即清除，不残留假脱手甩动声。
	Flash->Release();
	UAudioComponent* CancelledVoice = Flash->GetThrowAudio();
	if (!TestNotNull(TEXT("进入投掷挥臂阶段创建三维音频组件"), CancelledVoice)) return false;
	TestTrue(TEXT("投掷挥臂声启用空间定位"), CancelledVoice->bAllowSpatialization);
	TestEqual(TEXT("投掷挥臂声附着在角色网格左手"), CancelledVoice->GetAttachParent(), static_cast<USceneComponent*>(Player->GetMesh()));
	TestEqual(TEXT("投掷挥臂声挂点为 hand_l"), CancelledVoice->GetAttachSocketName(), FName(TEXT("hand_l")));
	Flash->CancelThrow();
	TestNull(TEXT("脱手前取消后清空投掷音频引用"), Flash->GetThrowAudio());

	// 完整投出：验证越过脱手帧进入随动段时，投掷尾音保持播放不被切断。
	Flash->Press();
	World->Tick(LEVELTICK_All, .6f);
	Flash->Release();
	UAudioComponent* FullThrowVoice = Flash->GetThrowAudio();
	if (!TestNotNull(TEXT("再次投掷创建三维音频组件"), FullThrowVoice)) return false;
	World->Tick(LEVELTICK_All, .30f);
	TestTrue(TEXT("脱手进入随动段后投掷尾音继续自然播放"),
		Flash->GetPhase() == EGinkgoFlashPhase::FollowThrough && FullThrowVoice->IsPlaying());
	FullThrowVoice->Stop();

	// 搭建独立墙体与地板，验证弹体碰墙反弹与到期爆闪的 3D 空间声定位在接触点/爆点而非跟随弹体飞走。
	const FVector Origin(0., 5000., 2000.);
	auto SpawnSurface = [&](const FVector Offset, const FVector Extent)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		auto* Box = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Box->SetCanEverAffectNavigation(false);
		Box->RegisterComponent();
		Box->SetWorldLocation(Origin + Offset);
	};
	SpawnSurface(FVector(100., 0., 0.), FVector(100., 400., 300.));
	SpawnSurface(FVector(0., 0., -120.), FVector(1200., 400., 20.));

	auto FindWorldVoices = [&](USoundBase* TargetSound)
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

	const FVector Start = Origin - FVector(20., 0., 0.);
	auto* Projectile = World->SpawnActor<AGinkgoFlashProjectile>(Start, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("生成飞行弹体"), Projectile)) return false;
	Projectile->LaunchVelocity = FVector(700., 0., 0.);

	Projectile->Tick(.125f);
	TestTrue(TEXT("弹体撞墙后已向反方向弹回"), Projectile->GetActorLocation().X < Start.X);
	const TArray<UAudioComponent*> WallVoices = FindWorldVoices(Projectile->GetImpactSound());
	if (!TestEqual(TEXT("单次撞墙触发一声空间碰撞声"), WallVoices.Num(), 1)) return false;
	UAudioComponent* WallVoice = WallVoices[0];
	TestTrue(TEXT("撞墙碰撞声启用空间定位"), WallVoice->bAllowSpatialization);
	TestNull(TEXT("撞墙碰撞声固定在墙面接触点，不挂在反弹飞走的弹体上"), WallVoice->GetAttachParent());
	TestTrue(TEXT("撞墙碰撞声坐标位于墙面（X ≈ 0），而非反弹后的弹体位置"),
		FMath::IsNearlyEqual(WallVoice->GetComponentLocation().X, Origin.X, 1.f));
	WallVoice->Stop();

	Projectile->Tick(static_cast<float>(AGinkgoFlashProjectile::FuseSeconds));
	const FVector BurstPoint = Projectile->GetActorLocation();
	const TArray<UAudioComponent*> BurstVoices = FindWorldVoices(Projectile->GetExplodeSound());
	if (!TestEqual(TEXT("引信到期触发一声空间爆炸声"), BurstVoices.Num(), 1)) return false;
	UAudioComponent* BurstVoice = BurstVoices[0];
	TestTrue(TEXT("爆炸声启用空间定位"), BurstVoice->bAllowSpatialization);
	TestNull(TEXT("爆炸声独立于 0.7 s 后销毁的弹体，保留完整混响长尾"), BurstVoice->GetAttachParent());
	TestTrue(TEXT("爆炸声坐标与实际爆点一致"), BurstVoice->GetComponentLocation().Equals(BurstPoint, .5f));
	BurstVoice->Stop();

	return true;
}

#endif
