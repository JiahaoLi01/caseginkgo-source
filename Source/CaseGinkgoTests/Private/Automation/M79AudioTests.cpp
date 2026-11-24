#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
#include "AudioDevice.h"
#include "Character/GinkgoCharacter.h"
#include "Combat/GinkgoAmmoDefinition.h"
#include "Combat/GinkgoCombatTypes.h"
#include "Combat/GinkgoGrenadeProjectile.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Control/GinkgoPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundSource.h"
#include "Misc/App.h"
#include "Misc/ScopeExit.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "UObject/UObjectIterator.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"

namespace
{
	/** M79 单组立体声随机采样 MetaSound 的图结构与样本时长预期约束。 */
	struct FM79MetaSoundExpectation
	{
		/** 断言输出使用的中文标签。 */
		const TCHAR* Label = TEXT("");

		/** 待核验的声音资产指针。 */
		const USoundBase* Sound = nullptr;

		/** 随机采样数组中应包含的独立裁剪样本条数。 */
		int32 WaveCount = 0;

		/** 裁剪后单条样本的最短允许时长（秒）。 */
		float MinDuration = 0.f;

		/** 裁剪后单条样本的最长允许时长（秒）。 */
		float MaxDuration = 0.f;

		/** 是否应当在 MetaSound 自身绑定独立空间衰减（发射声不绑，高爆声绑定 SA_M79_Explode）。 */
		bool bExpectEmbeddedAttenuation = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoM79AudioAssetTest, "CaseGinkgo.Weapon.M79.Audio.AssetsAndRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoM79AudioAssetTest::RunTest(const FString& Parameters)
{
	const UGinkgoWeaponDefinition* M79 = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M79/DA_M79.DA_M79"));
	const UGinkgoAmmoDefinition* AmmoHE = LoadObject<UGinkgoAmmoDefinition>(nullptr, TEXT("/Game/Weapons/Ammo/DA_Ammo_Grenade40x46.DA_Ammo_Grenade40x46"));
	const UGinkgoAmmoDefinition* AmmoFlash = LoadObject<UGinkgoAmmoDefinition>(nullptr, TEXT("/Game/Weapons/Ammo/DA_Ammo_Grenade40x46Flash.DA_Ammo_Grenade40x46Flash"));
	const UGinkgoAmmoDefinition* AmmoFlechette = LoadObject<UGinkgoAmmoDefinition>(nullptr, TEXT("/Game/Weapons/Ammo/DA_Ammo_Grenade40x46Flechette.DA_Ammo_Grenade40x46Flechette"));
	const USoundBase* M79Fire = LoadObject<USoundBase>(nullptr, TEXT("/Game/Weapons/M79/Audio/MS_M79_Fire.MS_M79_Fire"));
	const USoundBase* M79ExplodeClose = LoadObject<USoundBase>(nullptr, TEXT("/Game/Weapons/M79/Audio/MS_M79_ExplodeClose.MS_M79_ExplodeClose"));
	const USoundBase* M79Explode = LoadObject<USoundBase>(nullptr, TEXT("/Game/Weapons/M79/Audio/MS_M79_Explode.MS_M79_Explode"));
	const USoundBase* FlashExplode = LoadObject<USoundBase>(nullptr, TEXT("/Game/Weapons/Throwables/Audio/MS_Flashbang_Explode.MS_Flashbang_Explode"));
	const USoundBase* ShotgunFire = LoadObject<USoundBase>(nullptr, TEXT("/Game/Weapons/Shotgun/Audio/MS_Shotgun_Fire.MS_Shotgun_Fire"));
	const USoundAttenuation* ExplodeAttenuation = LoadObject<USoundAttenuation>(nullptr, TEXT("/Game/Weapons/M79/Audio/SA_M79_Explode.SA_M79_Explode"));

	if (!TestNotNull(TEXT("加载 DA_M79"), M79)
		|| !TestNotNull(TEXT("加载 40mm 高爆弹数据"), AmmoHE)
		|| !TestNotNull(TEXT("加载 40mm 闪光弹数据"), AmmoFlash)
		|| !TestNotNull(TEXT("加载 40mm 镖弹数据"), AmmoFlechette)
		|| !TestNotNull(TEXT("加载 MS_M79_Fire"), M79Fire)
		|| !TestNotNull(TEXT("加载 MS_M79_ExplodeClose"), M79ExplodeClose)
		|| !TestNotNull(TEXT("加载 MS_M79_Explode"), M79Explode)
		|| !TestNotNull(TEXT("加载 MS_Flashbang_Explode"), FlashExplode)
		|| !TestNotNull(TEXT("加载 MS_Shotgun_Fire"), ShotgunFire)
		|| !TestNotNull(TEXT("加载 SA_M79_Explode"), ExplodeAttenuation))
	{
		return false;
	}

	// 1. 核验三种弹头发射音分流：闪光弹与高爆弹共用 DA_M79 默认的 MS_M79_Fire，镖弹覆盖为 MS_Shotgun_Fire；且 -M79Cues 未冲掉状态树与动作片段
	TestNotNull(TEXT("DA_M79 仍保持挂载 ST_ActionBreak 子树"), M79->ActionTree.Get());
	TestEqual(TEXT("DA_M79 仍完整保留 7 步动作蒙太奇"), M79->Steps.Num(), 7);
	const FGinkgoWeaponCue* FireCue = M79->Cues.Find(FWeaponTags::Weapon_Cue_Fire);
	if (!TestNotNull(TEXT("DA_M79 配有 Weapon.Cue.Fire"), FireCue)) return false;
	TestEqual(TEXT("DA_M79 默认开火绑定轻膛压 MS_M79_Fire"), FireCue->Sound.LoadSynchronous(), const_cast<USoundBase*>(M79Fire));
	TestTrue(TEXT("DA_M79 开火零提前量且保持 Cue Volume = 1.0 不削弱镖弹"), FMath::IsNearlyZero(FireCue->Lead) && FMath::IsNearlyEqual(FireCue->Volume, 1.f));
	TestTrue(TEXT("高爆弹不覆盖枪声，继承 MS_M79_Fire"), AmmoHE->FireSound.IsNull());
	TestTrue(TEXT("碰炸闪光弹不覆盖枪声，继承 MS_M79_Fire"), AmmoFlash->FireSound.IsNull());
	TestEqual(TEXT("16 钢镖霰弹覆盖为重膛压 MS_Shotgun_Fire"), AmmoFlechette->FireSound.LoadSynchronous(), const_cast<USoundBase*>(ShotgunFire));

	// 2. 核验抛射物引爆音与高爆特效分流：高爆弹绑定 NS_Sub_EXP_Mid_002_02 + 25 m 近/远爆炸声，碰炸闪光弹保持与手投一致的 MS_Flashbang_Explode
	const UNiagaraSystem* ExpectedBlastVfx = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Fire_EXP_Vol01_Free/Niagara/EXP/NS_Sub_EXP_Mid_002_02.NS_Sub_EXP_Mid_002_02"));
	if (!TestNotNull(TEXT("加载高爆 Niagara 特效 NS_Sub_EXP_Mid_002_02"), ExpectedBlastVfx)) return false;
	const auto* ProjDefaults = GetDefault<AGinkgoGrenadeProjectile>();
	TestEqual(TEXT("高爆近炸声分界半径为 25 m (2500 cm)"), AGinkgoGrenadeProjectile::CloseExplosionDistance, 2500.f);
	TestEqual(TEXT("抛射物高爆战斗部绑定 NS_Sub_EXP_Mid_002_02"), static_cast<const UNiagaraSystem*>(ProjDefaults->GetBlastSystem()), ExpectedBlastVfx);
	TestEqual(TEXT("NS_Sub_EXP_Mid_002_02 含火球、火星、碎屑、光晕 4 个发射器"), ExpectedBlastVfx->GetEmitterHandles().Num(), 4);
	TestFalse(TEXT("NS_Sub_EXP_Mid_002_02 为单次爆发而非展厅无限循环"), ExpectedBlastVfx->IsLooping());
	TestEqual(TEXT("抛射物 25 m 内近炸绑定 MS_M79_ExplodeClose"), static_cast<const USoundBase*>(ProjDefaults->GetHighExplosiveCloseSound()), M79ExplodeClose);
	TestEqual(TEXT("抛射物 25 m 外远炸绑定 MS_M79_Explode"), static_cast<const USoundBase*>(ProjDefaults->GetHighExplosiveSound()), M79Explode);
	TestEqual(TEXT("抛射物闪光战斗部保持绑定 MS_Flashbang_Explode"), static_cast<const USoundBase*>(ProjDefaults->GetFlashExplodeSound()), FlashExplode);
	TestTrue(TEXT("近炸、远炸与闪光各自使用独立 MetaSound"),
		M79ExplodeClose != M79Explode && M79ExplodeClose != FlashExplode && M79Explode != FlashExplode);

	// 3. 核验 MS_M79_Fire（8 段）、MS_M79_ExplodeClose（6 段）与 MS_M79_Explode（6 段）的真实 MetaSound 图结构与样本池
	const FM79MetaSoundExpectation Specs[] = {
		{TEXT("M79 轻膛压发射声"), M79Fire, 8, .90f, 1.15f, false},
		{TEXT("M79 25 m 内近距高爆声"), M79ExplodeClose, 6, 5.30f, 6.20f, true},
		{TEXT("M79 25 m 外远距高爆声"), M79Explode, 6, 3.40f, 4.05f, true},
	};
	for (const FM79MetaSoundExpectation& Spec : Specs)
	{
		const auto* MetaSound = Cast<UMetaSoundSource>(Spec.Sound);
		if (!TestNotNull(*FString::Printf(TEXT("%s 为真实 MetaSoundSource"), Spec.Label), MetaSound)) return false;

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
		TestEqual(*FString::Printf(TEXT("%s 双声道独立乘随机增益"), Spec.Label),
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
			TestEqual(*FString::Printf(TEXT("%s 独立裁剪样本数量"), Spec.Label), Waves.Num(), Spec.WaveCount);
			TSet<UObject*> Unique;
			for (UObject* Item : Waves)
			{
				Unique.Add(Item);
				const auto* Wave = Cast<USoundWave>(Item);
				if (!TestNotNull(*FString::Printf(TEXT("%s 数组项是 SoundWave"), Spec.Label), Wave)) continue;
				TestEqual(*FString::Printf(TEXT("%s 保持双声道输出"), Spec.Label), Wave->NumChannels, 2);
				TestFalse(*FString::Printf(TEXT("%s 单次播放不循环"), Spec.Label), Wave->bLooping);
				TestTrue(*FString::Printf(TEXT("%s 时长 %.3f s 落在预期区间"), Spec.Label, Wave->Duration),
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

		if (Spec.bExpectEmbeddedAttenuation)
		{
			TestEqual(TEXT("MS_M79_Explode 内嵌绑定 SA_M79_Explode"), static_cast<const USoundAttenuation*>(MetaSound->AttenuationSettings.Get()), ExplodeAttenuation);
		}
		else
		{
			TestNull(TEXT("MS_M79_Fire 不内嵌衰减，由武器组件区分本地 2D 与世界 SA_WeaponFire"), MetaSound->AttenuationSettings.Get());
		}
	}

	// 4. 核验 SA_M79_Explode 的三维定位、同点归一化、单调交战衰减曲线与空气吸收低通
	const FSoundAttenuationSettings& Settings = ExplodeAttenuation->Attenuation;
	TestTrue(TEXT("高爆衰减启用三维空间定位与距离衰减"), Settings.bSpatialize && Settings.bAttenuate);
	TestTrue(TEXT("高爆衰减启用同点立体声归一化"), Settings.bApplyNormalizationToStereoSounds && FMath::IsNearlyZero(Settings.StereoSpread));
	TestEqual(TEXT("高爆衰减使用自定义风格化曲线"), Settings.DistanceAlgorithm, EAttenuationDistanceModel::Custom);

	const float Inner = Settings.AttenuationShapeExtents.X;
	const float Falloff = Settings.FalloffDistance;
	TestTrue(TEXT("高爆衰减内外半径为 4.5 m 与 240 m"),
		FMath::IsNearlyEqual(Inner, 450.f, .5f) && FMath::IsNearlyEqual(Inner + Falloff, 24000.f, .5f));

	auto Gain = [&](const float DistanceCm)
	{
		return Settings.Evaluate(FTransform::Identity, FVector(DistanceCm, 0., 0.), 1.f);
	};
	TestEqual(TEXT("4.5 m 爆心内保持满音量"), Gain(Inner * .5f), 1.f);
	TestTrue(TEXT("16 m 近距保持充沛冲击能量"), Gain(1600.f) >= FMath::Pow(10.f, -1.5f / 20.f));
	TestTrue(TEXT("35 m 中近交战距离清晰高能"), Gain(3500.f) >= FMath::Pow(10.f, -4.0f / 20.f));
	TestTrue(TEXT("110 m 远距仍有鲜明高爆可辨识度"), Gain(11000.f) >= FMath::Pow(10.f, -13.5f / 20.f));
	TestTrue(TEXT("165 m 交战外缘不断崖硬切"), Gain(16500.f) >= FMath::Pow(10.f, -20.5f / 20.f));

	bool bMonotonic = true;
	float Previous = 1.f;
	for (float Distance = 0.f; Distance <= Inner + Falloff; Distance += 50.f)
	{
		const float Current = Gain(Distance);
		bMonotonic &= FMath::IsFinite(Current) && Current >= 0.f && Current <= Previous + UE_KINDA_SMALL_NUMBER;
		Previous = Current;
	}
	TestTrue(TEXT("高爆衰减全程单调递减且无切线过冲"), bMonotonic);
	TestTrue(TEXT("240 m 边界内侧已平滑淡出"), Gain(Inner + Falloff - 1.f) < .001f);
	TestEqual(TEXT("240 m 以外静音"), Gain(Inner + Falloff + 50.f), 0.f);
	TestTrue(TEXT("高爆衰减启用 20 kHz -> 7 kHz 空气吸收低通"),
		Settings.bAttenuateWithLPF && Settings.LPFRadiusMax > Settings.LPFRadiusMin
		&& Settings.LPFFrequencyAtMin >= 18000.f && Settings.LPFFrequencyAtMax >= 6500.f
		&& Settings.LPFFrequencyAtMax < Settings.LPFFrequencyAtMin);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoM79AudioPlaybackTest, "CaseGinkgo.Weapon.M79.Audio.WarheadPlayback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoM79AudioPlaybackTest::RunTest(const FString& Parameters)
{
	if (!GEngine || !GEngine->UseSound())
	{
		AddInfo(TEXT("当前进程禁用了音频设备。M79 播放集成需去掉 -nosound，添加 -DeterministicAudio -AllowCommandletAudio。"));
		return true;
	}

	UGinkgoWeaponDefinition* M79 = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M79/DA_M79.DA_M79"));
	const UGinkgoAmmoDefinition* AmmoHE = LoadObject<UGinkgoAmmoDefinition>(nullptr, TEXT("/Game/Weapons/Ammo/DA_Ammo_Grenade40x46.DA_Ammo_Grenade40x46"));
	const UGinkgoAmmoDefinition* AmmoFlash = LoadObject<UGinkgoAmmoDefinition>(nullptr, TEXT("/Game/Weapons/Ammo/DA_Ammo_Grenade40x46Flash.DA_Ammo_Grenade40x46Flash"));
	const UGinkgoAmmoDefinition* AmmoFlechette = LoadObject<UGinkgoAmmoDefinition>(nullptr, TEXT("/Game/Weapons/Ammo/DA_Ammo_Grenade40x46Flechette.DA_Ammo_Grenade40x46Flechette"));
	USoundBase* M79Fire = LoadObject<USoundBase>(nullptr, TEXT("/Game/Weapons/M79/Audio/MS_M79_Fire.MS_M79_Fire"));
	USoundBase* ShotgunFire = LoadObject<USoundBase>(nullptr, TEXT("/Game/Weapons/Shotgun/Audio/MS_Shotgun_Fire.MS_Shotgun_Fire"));
	if (!TestNotNull(TEXT("加载 DA_M79 与三种弹药及枪声"), M79 && AmmoHE && AmmoFlash && AmmoFlechette && M79Fire && ShotgunFire ? M79 : nullptr))
	{
		return false;
	}

	const FString WorldName = TEXT("M79AudioTest") + FGuid::NewGuid().ToString();
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
	auto* Enemy = World->SpawnActor<AGinkgoCharacter>(FVector(1200., 0., 100.), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("生成本地玩家与 AI 测试角色"), PC && AI && Player && Enemy ? Player : nullptr)) return false;
	PC->SetAsLocalPlayerController();
	PC->Possess(Player);
	AI->Possess(Enemy);

	for (AGinkgoCharacter* Character : {Player, Enemy})
	{
		Character->GetWeapons()->SetCarried(0, M79);
		Character->GetWeapons()->SetHeldSlot(0);
		Character->AttachWeaponToHand(0);
	}

	// 1. 验证玩家与 AI 发射三种弹头的实际枪声组件：HE / Flash 播 MS_M79_Fire，Flechette 覆盖播 MS_Shotgun_Fire
	for (const UGinkgoAmmoDefinition* GrenadeAmmo : {AmmoHE, AmmoFlash})
	{
		UAudioComponent* LocalVoice = Player->GetWeapons()->PlayCue(FWeaponTags::Weapon_Cue_Fire, GrenadeAmmo->FireSound.LoadSynchronous());
		if (!TestNotNull(TEXT("本地发射榴弹/闪光弹创建音频组件"), LocalVoice)) return false;
		TestEqual(TEXT("本地榴弹/闪光弹使用轻膛压 MS_M79_Fire"), LocalVoice->Sound.Get(), M79Fire);
		TestFalse(TEXT("本地玩家自身发射声为非空间化 2D"), LocalVoice->bAllowSpatialization);
		LocalVoice->Stop();
	}
	{
		UAudioComponent* FlechetteVoice = Player->GetWeapons()->PlayCue(FWeaponTags::Weapon_Cue_Fire, AmmoFlechette->FireSound.LoadSynchronous());
		if (!TestNotNull(TEXT("本地发射 16 钢镖霰弹创建音频组件"), FlechetteVoice)) return false;
		TestEqual(TEXT("本地镖弹覆盖使用重膛压 MS_Shotgun_Fire"), FlechetteVoice->Sound.Get(), ShotgunFire);
		FlechetteVoice->Stop();

		UAudioComponent* EnemyVoice = Enemy->GetWeapons()->PlayCue(FWeaponTags::Weapon_Cue_Fire, AmmoHE->FireSound.LoadSynchronous());
		if (!TestNotNull(TEXT("AI 发射 M79 创建三维音频组件"), EnemyVoice)) return false;
		TestEqual(TEXT("AI 发射 M79 播放 MS_M79_Fire"), EnemyVoice->Sound.Get(), M79Fire);
		TestTrue(TEXT("AI 发射声启用三维空间化并绑定武器衰减"),
			EnemyVoice->bAllowSpatialization && EnemyVoice->AttenuationSettings == M79->FireAttenuation.Get());
		const FVector Muzzle = Enemy->GetHeldWeaponMesh()->GetComponentTransform().TransformPosition(M79->MuzzleOffset);
		TestEqual(TEXT("AI 发射声定位在 M79 实际炮口"), EnemyVoice->GetComponentLocation(), Muzzle);
		EnemyVoice->Stop();
	}

	// 2. 搭建阻挡 Weapon 通道的靶墙，分别验证高爆榴弹与碰炸闪光弹触碰引爆时的空间爆炸声与长尾独立留存
	auto SpawnWall = [&](const FVector& Origin)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		auto* Box = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(60., 400., 400.));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(Ginkgo::Combat::WeaponTraceChannel, ECR_Block);
		Box->SetCanEverAffectNavigation(false);
		Box->RegisterComponent();
		Box->SetWorldLocation(Origin + FVector(120., 0., 0.));
	};
	const FVector NearWallOrigin(0., 2350., 150.); // 约 23.5 m（<= 25 m 近炸区间）
	const FVector WallOrigin(0., 6000., 1500.);    // 约 61.8 m（> 25 m 远炸区间）
	SpawnWall(NearWallOrigin);
	SpawnWall(WallOrigin);

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

	// 2a. 25 m 内近距高爆碰炸（约 23.5 m）-> 播 MS_M79_ExplodeClose，不播远炸或闪光声
	auto* NearHEProj = World->SpawnActor<AGinkgoGrenadeProjectile>(NearWallOrigin, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("生成 25 m 内近距 40mm 高爆抛射物"), NearHEProj)) return false;
	UNiagaraSystem* ExpectedBlastVfx = NearHEProj->GetBlastSystem();
	USoundBase* ExpectedHEClose = NearHEProj->GetHighExplosiveCloseSound();
	USoundBase* ExpectedHEExplode = NearHEProj->GetHighExplosiveSound();
	USoundBase* ExpectedFlashExplode = NearHEProj->GetFlashExplodeSound();
	NearHEProj->InitializeProjectile(FVector(7600., 0., 0.), AmmoHE, 1.f, Player->GetWeapons());
	NearHEProj->Tick(.05f);
	if (!TestTrue(TEXT("25 m 内高爆抛射物撞墙立即引爆"), NearHEProj->HasDetonated())) return false;
	const TArray<UAudioComponent*> NearVoices = FindWorldVoices(ExpectedHEClose);
	if (!TestEqual(TEXT("25 m 内高爆碰炸触发一声近炸 MS_M79_ExplodeClose"), NearVoices.Num(), 1)) return false;
	TestEqual(TEXT("25 m 内高爆碰炸不播远炸 MS_M79_Explode"), FindWorldVoices(ExpectedHEExplode).Num(), 0);
	TestEqual(TEXT("25 m 内高爆碰炸不播闪光弹声"), FindWorldVoices(ExpectedFlashExplode).Num(), 0);
	TestTrue(TEXT("近炸声启用三维空间定位并独立于弹体留存"),
		NearVoices[0]->bAllowSpatialization && NearVoices[0]->GetAttachParent() == nullptr);
	NearVoices[0]->Stop();
	NearHEProj->Destroy();

	// 2b. 25 m 外远距高爆碰炸（约 61.8 m）-> 播 MS_M79_Explode，且 0.22 s 弹体销毁后爆炸长尾继续独立播放
	auto* HighExplosiveProj = World->SpawnActor<AGinkgoGrenadeProjectile>(WallOrigin, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("生成 25 m 外远距 40mm 高爆抛射物"), HighExplosiveProj)) return false;
	HighExplosiveProj->InitializeProjectile(FVector(7600., 0., 0.), AmmoHE, 1.f, Player->GetWeapons());
	HighExplosiveProj->Tick(.05f);
	if (!TestTrue(TEXT("25 m 外高爆抛射物撞墙立即引爆"), HighExplosiveProj->HasDetonated())) return false;
	const FVector HEBlastLocation = HighExplosiveProj->GetExplosionLocation();

	UNiagaraComponent* ActiveBlastVfx = nullptr;
	if (FApp::CanEverRender())
	{
		for (TObjectIterator<UNiagaraComponent> It; It; ++It)
		{
			if (It->GetWorld() == World && It->GetAsset() == ExpectedBlastVfx && It->IsActive()
				&& It->GetComponentLocation().Equals(HEBlastLocation, 1.f))
			{
				ActiveBlastVfx = *It;
				break;
			}
		}
		if (!TestNotNull(TEXT("高爆碰炸在爆点生成并激活 NS_Sub_EXP_Mid_002_02"), ActiveBlastVfx)) return false;
		TestTrue(TEXT("高爆特效生成于实际爆点且朝向直立 +Z"),
			ActiveBlastVfx->GetComponentLocation().Equals(HEBlastLocation, .5f)
			&& ActiveBlastVfx->GetComponentRotation().IsNearlyZero(.1f));
	}

	const TArray<UAudioComponent*> HEVoices = FindWorldVoices(ExpectedHEExplode);
	if (!TestEqual(TEXT("25 m 外高爆碰炸触发一声远炸 MS_M79_Explode"), HEVoices.Num(), 1)) return false;
	TestEqual(TEXT("25 m 外高爆碰炸不误播近炸声"), FindWorldVoices(ExpectedHEClose).Num(), 0);
	TestEqual(TEXT("25 m 外高爆碰炸不误播闪光弹爆炸声"), FindWorldVoices(ExpectedFlashExplode).Num(), 0);
	UAudioComponent* HEVoice = HEVoices[0];
	TestTrue(TEXT("高爆爆炸声启用三维空间定位"), HEVoice->bAllowSpatialization);
	TestNull(TEXT("高爆爆炸声不挂在即将销毁的弹体上"), HEVoice->GetAttachParent());
	TestTrue(TEXT("高爆爆炸声坐标位于实际爆点"), HEVoice->GetComponentLocation().Equals(HEBlastLocation, .5f));

	// 推进超过 HighExplosiveBurstDuration (0.22 s)，确认弹体 Actor 已销毁而室外爆炸尾音仍在继续播放
	HighExplosiveProj->Tick(.30f);
	TestTrue(TEXT("高爆火球结束后弹体销毁，但 3.7 s 爆炸长尾继续播放不被切断"),
		!IsValid(HighExplosiveProj) && HEVoice->IsPlaying());
	if (ActiveBlastVfx)
	{
		// EXP_Debris 碎屑最长寿命为 4.0 s；独立模拟推进 5.0 s 后单次爆发应完整结束而非无限循环
		ActiveBlastVfx->SetForceSolo(true);
		ActiveBlastVfx->AdvanceSimulation(50, .10f);
		TestTrue(TEXT("高爆特效在 4.0 s 碎屑寿命结束后自动完成单次爆发，不无限循环"), ActiveBlastVfx->IsComplete());
	}
	HEVoice->Stop();

	// 2b. 碰炸闪光弹触碰引爆 -> 播 MS_Flashbang_Explode，不误播高爆爆炸声
	auto* FlashProj = World->SpawnActor<AGinkgoGrenadeProjectile>(WallOrigin, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("生成 40mm 碰炸闪光抛射物"), FlashProj)) return false;
	FlashProj->InitializeProjectile(FVector(8200., 0., 0.), AmmoFlash, 1.f, Player->GetWeapons());
	FlashProj->Tick(.05f);
	if (!TestTrue(TEXT("碰炸闪光抛射物撞墙立即引爆"), FlashProj->HasDetonated())) return false;
	const FVector FlashBlastLocation = FlashProj->GetExplosionLocation();

	const TArray<UAudioComponent*> FlashVoices = FindWorldVoices(ExpectedFlashExplode);
	if (!TestEqual(TEXT("碰炸闪光触发一声 MS_Flashbang_Explode"), FlashVoices.Num(), 1)) return false;
	TestEqual(TEXT("碰炸闪光不误播高爆爆炸声"), FindWorldVoices(ExpectedHEExplode).Num(), 0);
	UAudioComponent* FlashVoice = FlashVoices[0];
	TestTrue(TEXT("碰炸闪光声启用三维空间定位并固定在爆点"),
		FlashVoice->bAllowSpatialization && FlashVoice->GetAttachParent() == nullptr
		&& FlashVoice->GetComponentLocation().Equals(FlashBlastLocation, .5f));
	FlashVoice->Stop();

	return true;
}

#endif
