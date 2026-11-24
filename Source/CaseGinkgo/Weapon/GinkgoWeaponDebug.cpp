// 武器的调试命令：发行版不编译
#if !UE_BUILD_SHIPPING

#include "Character/GinkgoCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Interaction/GinkgoPickup.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Weapon/GinkgoAmmoPickup.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoWeaponPickup.h"

namespace GinkgoWeaponDebug
{
	/** 一次放下：等第一个玩家的角色出现，再等 Delay 秒（世界时间），把它某个槽的枪放到前方的地上 */
	struct FDrop
	{
		TWeakObjectPtr<UWorld> World;
		/** 从 0 数；INDEX_NONE 是最后一个有枪的槽 */
		int32 Slot = INDEX_NONE;
		double Distance = 150.0;
		double Delay = 0.0;
		/** 放一把同样的枪到地上，槽里的不拿走（测两个槽都满时用） */
		bool bKeep = false;
		/** 第一次见到角色的世界时间；还没见到时为负 */
		double CharacterSeenAt = -1.0;
	};

	/** 第一个玩家的角色：单机与主机是本地玩家；专用服务器上是第一个连上来的客户端（serverexec 发来的就是它） */
	AGinkgoCharacter* FindCharacter(const UWorld* World)
	{
		const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		return Controller ? Cast<AGinkgoCharacter>(Controller->GetPawn()) : nullptr;
	}

	void Drop(UWorld& World, AGinkgoCharacter& Character, int32 Slot, const double Distance, const bool bKeep)
	{
		UGinkgoWeaponComponent& Weapons = *Character.GetWeapons();
		if (Slot == INDEX_NONE)
		{
			// 先放主武器：后面那个优先，手枪最后
			for (const int32 Index : {1, 0, Ginkgo::Weapon::SidearmSlot})
			{
				if (Slot == INDEX_NONE && Weapons.GetCarried(Index))
				{
					Slot = Index;
				}
			}
		}
		UGinkgoWeaponDefinition* Weapon = Weapons.GetCarried(Slot);
		UStaticMesh* WeaponMesh = Weapon ? Weapon->Mesh.Get() : nullptr;
		if (!WeaponMesh)
		{
			UE_LOG(LogGinkgoWeapon, Warning, TEXT("[Weapon] ginkgo.weapon.drop：%s 没有枪可放"), Slot == INDEX_NONE ? TEXT("两个槽都") : *FString::Printf(TEXT("第 %d 个槽"), Slot + 1));
			return;
		}
		// 角色前方（水平）往下找地面；找不到就放在胶囊底的高度
		const FVector Forward = Character.GetActorForwardVector().GetSafeNormal2D();
		const FVector Above = Character.GetActorLocation() + Forward * Distance;
		const double HalfHeight = Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GinkgoWeaponDrop), false, &Character);
		FHitResult Hit;
		const bool bHit = World.LineTraceSingleByChannel(Hit, Above + FVector(0.0, 0.0, HalfHeight), Above - FVector(0.0, 0.0, HalfHeight + 500.0), ECC_Visibility, Params);
		const FVector Ground = bHit ? Hit.ImpactPoint : Above - FVector(0.0, 0.0, HalfHeight);

		// 枪身横在角色面前（朝角色的右方）
		const FTransform Transform = AGinkgoWeaponPickup::MakeLyingTransform(*WeaponMesh, Ground, Character.GetActorRightVector());
		AGinkgoWeaponPickup* Pickup = World.SpawnActorDeferred<AGinkgoWeaponPickup>(AGinkgoWeaponPickup::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Pickup->SetWeapon(Weapon);
		// 拿走的那把状态原样；keep 放的是一把同样的新枪
		const FGinkgoMechanismState* State = bKeep ? nullptr : Weapons.GetMechanismState(Slot);
		if (State)
		{
			Pickup->SetMechanismState(*State);
		}
		Pickup->FinishSpawning(Transform);
		if (!bKeep)
		{
			Weapons.SetCarried(Slot, nullptr);
		}
		UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] 把第 %d 个槽的 %s %s到地上：前方 %.0f cm，地面 (%.1f, %.1f, %.1f)，世界时间 %.4f s"), Slot + 1, *Weapon->GetName(),
			bKeep ? TEXT("复制一把放") : TEXT("放"), Distance, Ground.X, Ground.Y, Ground.Z, World.GetTimeSeconds());
	}

	/** @return 还要不要继续等 */
	bool Tick(FDrop& Request)
	{
		UWorld* World = Request.World.Get();
		AGinkgoCharacter* Character = FindCharacter(World);
		if (!World || !Character)
		{
			return World != nullptr;
		}
		const double Now = World->GetTimeSeconds();
		if (Request.CharacterSeenAt < 0.0)
		{
			Request.CharacterSeenAt = Now;
		}
		if (Now - Request.CharacterSeenAt < Request.Delay)
		{
			return true;
		}
		Drop(*World, *Character, Request.Slot, Request.Distance, Request.bKeep);
		return false;
	}

	/** ginkgo.weapon.drop [slot=<1|2>] [distance=<cm>] [delay=<秒>] [keep] */
	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		if (World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogGinkgoWeapon, Warning, TEXT("[Weapon] ginkgo.weapon.drop 要在服务器上执行：联网时用 serverexec ginkgo.weapon.drop ..."));
			return;
		}
		TSharedRef<FDrop> Request = MakeShared<FDrop>();
		Request->World = World;
		for (const FString& Arg : Args)
		{
			int32 Slot = 0;
			if (FParse::Value(*Arg, TEXT("slot="), Slot))
			{
				Request->Slot = FMath::Clamp(Slot, 1, UGinkgoWeaponComponent::SlotCount) - 1;
			}
			FParse::Value(*Arg, TEXT("distance="), Request->Distance);
			FParse::Value(*Arg, TEXT("delay="), Request->Delay);
			Request->bKeep |= Arg == TEXT("keep");
		}
		// 角色还没出来（命令行里的 -ExecCmds）或要等一会儿时，每帧看一次
		if (Tick(*Request))
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Request](float)
			{
				return Tick(*Request);
			}));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs DropCommand(
		TEXT("ginkgo.weapon.drop"),
		TEXT("把角色背着的枪放到地上，变成一把能拾取的枪：ginkgo.weapon.drop [slot=<1|2>] [distance=<cm>] [delay=<秒>] [keep]。\n")
		TEXT("slot 不写时放最后一个有枪的槽；distance 是放在角色前方多远（默认 150 cm），枪身横在角色面前；delay 是第一个玩家的角色出现之后再等几秒（世界时间）；")
		TEXT("keep 是复制一把放到地上，槽里的不拿走（测两个槽都满）。\n")
		TEXT("在服务器上执行（单机与主机是本地），联网的客户端用 serverexec ginkgo.weapon.drop。调试工具，发行版不编译"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));

	/** ginkgo.weapon.hold <0|1>：第一把直接拿到手上或背回背上，不放拔枪、收枪 */
	void Hold(const TArray<FString>& Args, UWorld* World)
	{
		AGinkgoCharacter* Character = FindCharacter(World);
		if (!Character || Args.Num() != 1)
		{
			UE_LOG(LogGinkgoWeapon, Warning, TEXT("[Weapon] 用法：ginkgo.weapon.hold <0|1>（要有本地玩家的角色）"));
			return;
		}
		const bool bInHand = FCString::Atoi(*Args[0]) != 0;
		Character->GetWeapons()->SetHeldSlot(bInHand ? 0 : INDEX_NONE);
		if (bInHand && Character->GetWeapons()->GetHeldSlot() == 0)
		{
			Character->AttachWeaponToHand();
		}
		else if (Character->GetWeapons()->GetCarried(0))
		{
			Character->AttachWeaponToStow();
		}
	}

	FAutoConsoleCommandWithWorldAndArgs HoldCommand(
		TEXT("ginkgo.weapon.hold"),
		TEXT("第一把枪直接拿到手上（1）或背回背上（0），不放拔枪、收枪：ginkgo.weapon.hold <0|1>。手上的枪变了动画层跟着换。调试工具，发行版不编译"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Hold));

	/** 机构命令作用的槽：手上的，徒手时第一个有枪的 */
	int32 MechanismSlot(const UGinkgoWeaponComponent& Weapons)
	{
		if (Weapons.GetHeldSlot() != INDEX_NONE)
		{
			return Weapons.GetHeldSlot();
		}
		for (int32 Slot = 0; Slot < UGinkgoWeaponComponent::SlotCount; ++Slot)
		{
			if (Weapons.GetMechanismState(Slot))
			{
				return Slot;
			}
		}
		return INDEX_NONE;
	}

	/** 机构命令要做的事：拿到角色的武器组件与作用的槽 */
	using FMechanismCommand = TFunction<void(UGinkgoWeaponComponent& Weapons, int32 Slot)>;

	/** 角色与它的枪还没出来时（命令行的 -ExecCmds）攒下的机构命令：按发出的顺序，等到有了再做 */
	struct FPendingMechanismCommand
	{
		TWeakObjectPtr<UWorld> World;
		FMechanismCommand Run;
	};
	TArray<FPendingMechanismCommand> PendingMechanismCommands;
	FTSTicker::FDelegateHandle PendingMechanismTicker;

	/** @return 这个世界里第一个玩家的角色手上或背上有带机构的枪：它的武器组件与槽 */
	TPair<UGinkgoWeaponComponent*, int32> FindArmed(UWorld* World)
	{
		AGinkgoCharacter* Character = FindCharacter(World);
		UGinkgoWeaponComponent* Weapons = Character ? Character->GetWeapons() : nullptr;
		const int32 Slot = Weapons ? MechanismSlot(*Weapons) : INDEX_NONE;
		return {Slot != INDEX_NONE ? Weapons : nullptr, Slot};
	}

	/** 做攒下的命令，做到第一条还做不了的为止；世界没了的扔掉 @return 还有没有攒着的 */
	bool RunPendingMechanismCommands()
	{
		while (!PendingMechanismCommands.IsEmpty())
		{
			UWorld* World = PendingMechanismCommands[0].World.Get();
			const TPair<UGinkgoWeaponComponent*, int32> Armed = FindArmed(World);
			if (World && !Armed.Key)
			{
				return true;
			}
			const FPendingMechanismCommand Command = PendingMechanismCommands[0];
			PendingMechanismCommands.RemoveAt(0);
			if (Armed.Key)
			{
				Command.Run(*Armed.Key, Armed.Value);
			}
		}
		PendingMechanismTicker.Reset();
		return false;
	}

	/** 有枪就做；没有（角色还没出来、开局的枪还没复制下来）就按顺序攒着，每帧看一次 */
	void RunWhenArmed(UWorld* World, FMechanismCommand Run)
	{
		PendingMechanismCommands.Add({World, MoveTemp(Run)});
		if (RunPendingMechanismCommands() && !PendingMechanismTicker.IsValid())
		{
			PendingMechanismTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
			{
				return RunPendingMechanismCommands();
			}));
		}
	}

	/** 一行日志：某个槽现在的机构状态与后备 */
	void LogMechanism(const TCHAR* Command, const UGinkgoWeaponComponent& Weapons, const int32 Slot)
	{
		const FGinkgoMechanismState& State = *Weapons.GetMechanismState(Slot);
		UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] %s：第 %d 个槽供弹 %d 发，膛 %s，枪机 %s，后备 %d 发"), Command, Slot + 1, State.Feed.Num(),
			*StaticEnum<EGinkgoChamber>()->GetNameStringByValue(static_cast<int64>(State.Chamber)), State.Action == EGinkgoAction::Open ? TEXT("开") : TEXT("关"),
			Weapons.GetAmmoReserve().Count(Weapons.GetCarried(Slot)->AmmoType));
	}

	/** ginkgo.weapon.ammo 做的事：照参数设状态，没写的照旧 */
	void SetAmmo(UGinkgoWeaponComponent& Weapons, const int32 Slot, const TArray<FString>& Args)
	{
		const UGinkgoWeaponDefinition* Weapon = Weapons.GetCarried(Slot);
		FGinkgoMechanismState State = *Weapons.GetMechanismState(Slot);
		State.Feed.Init(Weapon->AmmoType, FMath::Clamp(FCString::Atoi(*Args[0]), 0, Weapon->Mechanism->GetCapacity()));
		State.bMagazineIn = true;
		if (Args.IsValidIndex(1))
		{
			State.Chamber = static_cast<EGinkgoChamber>(FMath::Clamp(FCString::Atoi(*Args[1]), 0, 2));
			State.ChamberAmmo = State.Chamber == EGinkgoChamber::Empty ? FGameplayTag() : Weapon->AmmoType;
		}
		if (Args.IsValidIndex(2))
		{
			State.Action = FCString::Atoi(*Args[2]) != 0 ? EGinkgoAction::Open : EGinkgoAction::Closed;
		}
		Weapons.SetMechanismState(Slot, State);
		if (Args.IsValidIndex(3))
		{
			Weapons.SetReserveRounds(Weapon->AmmoType, FCString::Atoi(*Args[3]));
		}
		LogMechanism(TEXT("ginkgo.weapon.ammo"), Weapons, Slot);
	}

	/** ginkgo.weapon.ammo <供弹> [膛 0|1|2] [枪机 0|1] [后备] */
	void Ammo(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.IsEmpty() || Args.Num() > 4)
		{
			UE_LOG(LogGinkgoWeapon, Warning, TEXT("[Weapon] 用法：ginkgo.weapon.ammo <供弹> [膛 0 空 / 1 活弹 / 2 空壳] [枪机 0 关 / 1 开] [后备]"));
			return;
		}
		RunWhenArmed(World, [Args](UGinkgoWeaponComponent& Weapons, const int32 Slot) { SetAmmo(Weapons, Slot, Args); });
	}

	FAutoConsoleCommandWithWorldAndArgs AmmoCommand(
		TEXT("ginkgo.weapon.ammo"),
		TEXT("直接设手上那把枪（徒手时背上那把）的机构状态：ginkgo.weapon.ammo <供弹> [膛 0 空 / 1 活弹 / 2 空壳] [枪机 0 关 / 1 开] [后备]。")
		TEXT("供弹压的是这把枪的弹种，最多到容量；没写的照旧。角色与枪还没出来时（命令行的 -ExecCmds）等到有了再设。")
		TEXT("机构状态只在本地，联网的客户端在自己那边执行。调试工具，发行版不编译"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Ammo));

	/** ginkgo.weapon.reserve <弹种> <发数>：直接设某种弹的后备（弹种轮盘的路线：独头弹打完了、没有某种弹） */
	void Reserve(const TArray<FString>& Args, UWorld* World)
	{
		const FGameplayTag Ammo = Args.Num() == 2 ? FGameplayTag::RequestGameplayTag(FName(*Args[0]), /*ErrorIfNotFound*/ false) : FGameplayTag();
		if (!World || !Ammo.IsValid())
		{
			UE_LOG(LogGinkgoWeapon, Warning, TEXT("[Weapon] 用法：ginkgo.weapon.reserve <弹种（Ammo.Gauge12.Slug）> <发数>"));
			return;
		}
		const int32 Rounds = FMath::Max(FCString::Atoi(*Args[1]), 0);
		RunWhenArmed(World, [Ammo, Rounds](UGinkgoWeaponComponent& Weapons, const int32 Slot)
		{
			Weapons.SetReserveRounds(Ammo, Rounds);
			UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] ginkgo.weapon.reserve：%s 后备 %d 发"), *Ammo.ToString(), Weapons.GetAmmoReserve().Count(Ammo));
		});
	}

	FAutoConsoleCommandWithWorldAndArgs ReserveCommand(
		TEXT("ginkgo.weapon.reserve"),
		TEXT("直接设某种弹的后备：ginkgo.weapon.reserve Ammo.Gauge12.Slug 0。角色与枪还没出来时（命令行的 -ExecCmds）等到有了再设。调试工具，发行版不编译"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Reserve));

	/** ginkgo.weapon.loadAmmo <弹种>：手上那把枪装填时压哪种（同弹种轮盘确认，但不装填） */
	void LoadAmmo(const TArray<FString>& Args, UWorld* World)
	{
		const FGameplayTag Ammo = Args.Num() == 1 ? FGameplayTag::RequestGameplayTag(FName(*Args[0]), /*ErrorIfNotFound*/ false) : FGameplayTag();
		if (!World || !Ammo.IsValid())
		{
			UE_LOG(LogGinkgoWeapon, Warning, TEXT("[Weapon] 用法：ginkgo.weapon.loadAmmo <弹种（Ammo.Mauser792x57.SmK）>"));
			return;
		}
		RunWhenArmed(World, [Ammo](UGinkgoWeaponComponent& Weapons, const int32 Slot)
		{
			Weapons.SetLoadAmmo(Slot, Ammo);
			UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] ginkgo.weapon.loadAmmo：第 %d 个槽装填压 %s"), Slot + 1, *Weapons.GetLoadAmmo(Slot).ToString());
		});
	}

	FAutoConsoleCommandWithWorldAndArgs LoadAmmoCommand(
		TEXT("ginkgo.weapon.loadAmmo"),
		TEXT("手上那把枪（徒手时背上那把）装填时压哪种弹：ginkgo.weapon.loadAmmo Ammo.Gauge12.Slug。同弹种轮盘确认，但不装填。调试工具，发行版不编译"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LoadAmmo));

	/** ginkgo.weapon.gunBones <骨骼>...：过 0.2 s（枪的姿势摆过几帧）打出手上那把枪这几根骨骼在组件空间的缩放（弹链、弹匣缩没缩） */
	void GunBones(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.IsEmpty())
		{
			UE_LOG(LogGinkgoWeapon, Warning, TEXT("[Weapon] 用法：ginkgo.weapon.gunBones <骨骼> [骨骼]..."));
			return;
		}
		RunWhenArmed(World, [Args](UGinkgoWeaponComponent& Weapons, const int32 Slot)
		{
			UWorld* SlotWorld = Weapons.GetWorld();
			if (!SlotWorld)
			{
				return;
			}
			FTimerHandle Handle;
			SlotWorld->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(&Weapons, [&Weapons, Slot, Args]()
			{
				const AGinkgoCharacter* Character = Cast<AGinkgoCharacter>(Weapons.GetOwner());
				UPoseableMeshComponent* Gun = Character ? Character->GetWeaponSlotComponent(Slot) : nullptr;
				if (!Gun || !Gun->GetSkinnedAsset())
				{
					return;
				}
				TStringBuilder<256> Text;
				for (const FString& Bone : Args)
				{
					const FName Name(*Bone);
					Text.Appendf(TEXT("%s%s "), Text.Len() > 0 ? TEXT("，") : TEXT(""), *Bone);
					if (Gun->GetBoneIndex(Name) == INDEX_NONE)
					{
						Text.Append(TEXT("没有"));
					}
					else
					{
						Text.Appendf(TEXT("缩放 %.3f"), Gun->GetBoneTransformByName(Name, EBoneSpaces::ComponentSpace).GetScale3D().GetMax());
					}
				}
				const FGinkgoMechanismState* State = Weapons.GetMechanismState(Slot);
				UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] ginkgo.weapon.gunBones：第 %d 个槽 %s，供弹 %d 发：%s"), Slot + 1, *GetNameSafe(Gun->GetSkinnedAsset()),
					State ? State->Feed.Num() : -1, Text.ToString());
			}), 0.2f, false);
		});
	}

	FAutoConsoleCommandWithWorldAndArgs GunBonesCommand(
		TEXT("ginkgo.weapon.gunBones"),
		TEXT("过 0.2 s 打出手上那把枪（徒手时背上那把）几根骨骼在组件空间的缩放：ginkgo.weapon.gunBones belt_1 belt_7。看弹链、弹匣缩没缩。调试工具，发行版不编译"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GunBones));

	/** ginkgo.weapon.heat <°C>：手上那把枪（徒手时背上那把）的枪管比环境高出这么多，之后照常凉（Docs/Design/Weapons.md「枪管的热」） */
	void Heat(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.IsEmpty() || !Args[0].IsNumeric())
		{
			UE_LOG(LogGinkgoWeapon, Warning, TEXT("[Weapon] 用法：ginkgo.weapon.heat <比环境高出多少 °C>"));
			return;
		}
		const float Heat = FCString::Atof(*Args[0]);
		RunWhenArmed(World, [Heat](UGinkgoWeaponComponent& Weapons, const int32 Slot)
		{
			Weapons.SetBarrelHeat(Slot, Heat);
			UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] ginkgo.weapon.heat：第 %d 个槽的枪管高出环境 %.1f °C，热浪 %.2f"), Slot + 1, Weapons.GetBarrelHeat(Slot), Weapons.GetHeatHazeAlpha(Slot));
		});
	}

	FAutoConsoleCommandWithWorldAndArgs HeatCommand(
		TEXT("ginkgo.weapon.heat"),
		TEXT("手上那把枪（徒手时背上那把）的枪管比环境高出这么多 °C，之后照常凉：ginkgo.weapon.heat 110 是热浪最强。看热浪、截图用。调试工具，发行版不编译"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Heat));

	/** ginkgo.weapon.event <事件> [次数] */
	void Event(const TArray<FString>& Args, UWorld* World)
	{
		const int64 Value = Args.IsEmpty() ? INDEX_NONE : StaticEnum<EGinkgoMechanismEvent>()->GetValueByNameString(Args[0]);
		if (!World || Value == INDEX_NONE || Args.Num() > 2)
		{
			UE_LOG(LogGinkgoWeapon, Warning, TEXT("[Weapon] 用法：ginkgo.weapon.event <Fire|ActionOpen|ActionClose|RoundIn|MagOut|MagIn> [次数]"));
			return;
		}
		const int32 Times = Args.IsValidIndex(1) ? FMath::Max(FCString::Atoi(*Args[1]), 1) : 1;
		const FString Name = Args[0];
		RunWhenArmed(World, [Value, Times, Name](UGinkgoWeaponComponent& Weapons, const int32 Slot)
		{
			int32 Done = 0;
			for (int32 Index = 0; Index < Times; ++Index)
			{
				Done += Weapons.ApplyMechanismEvent(Slot, static_cast<EGinkgoMechanismEvent>(Value)) ? 1 : 0;
			}
			LogMechanism(*FString::Printf(TEXT("ginkgo.weapon.event %s（%d 次里做了 %d 次）"), *Name, Times, Done), Weapons, Slot);
		});
	}

	FAutoConsoleCommandWithWorldAndArgs EventCommand(
		TEXT("ginkgo.weapon.event"),
		TEXT("对手上那把枪（徒手时背上那把）发一个机构事件：ginkgo.weapon.event <Fire|ActionOpen|ActionClose|RoundIn|MagOut|MagIn> [次数]。")
		TEXT("前提不满足时不改（同蒙太奇的通知，第 4 批由动作发）；RoundIn、MagIn 用这把枪的弹种。角色与枪还没出来时等到有了再发，")
		TEXT("几条命令按发出的顺序。调试工具，发行版不编译"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Event));

	/** ginkgo.weapon.give <槽 1|2>：这个槽放一把和手上（徒手时背上）一样的枪 */
	void Give(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() != 1 || World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogGinkgoWeapon, Warning, TEXT("[Weapon] 用法：ginkgo.weapon.give <槽 1|2>（在服务器上执行：单机与主机是本地）"));
			return;
		}
		const int32 Target = FMath::Clamp(FCString::Atoi(*Args[0]), 1, UGinkgoWeaponComponent::SlotCount) - 1;
		RunWhenArmed(World, [Target](UGinkgoWeaponComponent& Weapons, const int32 Slot) { Weapons.SetCarried(Target, Weapons.GetCarried(Slot)); });
	}

	FAutoConsoleCommandWithWorldAndArgs GiveCommand(
		TEXT("ginkgo.weapon.give"),
		TEXT("这个槽放一把和手上（徒手时背上）一样的枪，不用走去拾取（测两把枪用）：ginkgo.weapon.give <槽 1|2>。角色与枪还没出来时等到有了再放。调试工具，发行版不编译"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Give));

	/** 按模块选项名装配当前枪，供离屏回放检查不同组合；仍使用武器的兼容性检查，不直接修改共享资产。 */
	void Attachment(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.IsEmpty() || World->GetNetMode() == NM_Client) { return; }
		RunWhenArmed(World, [Args](UGinkgoWeaponComponent& Weapons, const int32 Slot)
		{
			const auto* Weapon = Weapons.GetCarried(Slot);
			for (const FString& Option : Args)
			{
				const auto* Mount = Weapon->Attachments.FindByPredicate([&Option](const auto& Item)
				{
					return Item.Part && Item.Part->ModularOption == FName(*Option);
				});
				if (Mount && Weapons.CanInstallAttachment(Slot, Mount->Part))
				{
					Weapons.InstallAttachment(Slot, Mount->Part);
					UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] 调试装配 %s"), *Option);
				}
			}
		});
	}

	FAutoConsoleCommandWithWorldAndArgs AttachmentCommand(
		TEXT("ginkgo.weapon.attachment"),
		TEXT("按选项名装配当前枪：ginkgo.weapon.attachment Handguard_Classic Stock_Light。只用于调试，发行版不编译"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Attachment));

	// ===== 子弹盒 =====
	/** 一次放子弹盒：等第一个玩家的角色出现，再等 Delay 秒（世界时间），放在角色前方 */
	struct FAmmoBoxSpawn
	{
		TWeakObjectPtr<UWorld> World;
		/** 角色前方多远、右边多远（cm，水平） */
		double Distance = 70.0;
		double Side = 0.0;
		/** 盒底离那里的地面多高（cm）：悬着放，不用摆桌子 */
		double Height = 90.0;
		/** 给几发；0 是照常掷 */
		int32 Rounds = 0;
		double Delay = 0.0;
		/** 改这一盒的手臂够得着的长度（够得着的椭球的三个半轴都设成它）、保底时长（测量用；负的是不改） */
		float ArmReach = -1.f;
		float MaxDuration = -1.f;
		double CharacterSeenAt = -1.0;
	};

	/** 改生成的拾取上一个受保护的 float 属性（反射，只给测量用；负的是不改） */
	void SetPickupFloat(AGinkgoPickup& Pickup, const TCHAR* Name, const float Value)
	{
		if (const FFloatProperty* Property = FindFProperty<FFloatProperty>(AGinkgoPickup::StaticClass(), Name); Property && Value >= 0.f)
		{
			Property->SetPropertyValue_InContainer(&Pickup, Value);
		}
		else if (Value >= 0.f)
		{
			UE_LOG(LogGinkgoWeapon, Warning, TEXT("[Weapon] 拾取上没有 float 属性 %s，没改"), Name);
		}
	}

	/** 同上，FVector 属性（三个分量都设成 Value） */
	void SetPickupVector(AGinkgoPickup& Pickup, const TCHAR* Name, const float Value)
	{
		const FStructProperty* Property = FindFProperty<FStructProperty>(AGinkgoPickup::StaticClass(), Name);
		if (Property && Property->Struct == TBaseStructure<FVector>::Get() && Value >= 0.f)
		{
			*Property->ContainerPtrToValuePtr<FVector>(&Pickup) = FVector(Value);
		}
		else if (Value >= 0.f)
		{
			UE_LOG(LogGinkgoWeapon, Warning, TEXT("[Weapon] 拾取上没有 FVector 属性 %s，没改"), Name);
		}
	}

	void SpawnAmmoBox(UWorld& World, const AGinkgoCharacter& Character, const FAmmoBoxSpawn& Request)
	{
		const FVector Forward = Character.GetActorForwardVector().GetSafeNormal2D();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		const FVector Above = Character.GetActorLocation() + Forward * Request.Distance + Right * Request.Side;
		const double HalfHeight = Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GinkgoAmmoBoxSpawn), false, &Character);
		FHitResult Hit;
		const bool bHit = World.LineTraceSingleByChannel(Hit, Above + FVector(0.0, 0.0, HalfHeight), Above - FVector(0.0, 0.0, HalfHeight + 500.0), ECC_Visibility, Params);
		const FVector Ground = bHit ? Hit.ImpactPoint : Above - FVector(0.0, 0.0, HalfHeight);

		// 盒子的正面（网格的 +Y，Blender 里的 -Y）朝着角色：网格的 X 轴顺着角色的右方
		const FTransform Transform(Right.Rotation(), Ground + FVector(0.0, 0.0, Request.Height));
		AGinkgoAmmoPickup* Box = World.SpawnActorDeferred<AGinkgoAmmoPickup>(AGinkgoAmmoPickup::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Request.Rounds > 0)
		{
			Box->SetRounds(Request.Rounds);
		}
		SetPickupVector(*Box, TEXT("ArmReachRadii"), Request.ArmReach);
		SetPickupFloat(*Box, TEXT("MaxDuration"), Request.MaxDuration);
		Box->FinishSpawning(Transform);
		UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] 放了子弹盒 %s：前方 %.0f cm、右 %.0f cm、离地 %.0f cm，(%.1f, %.1f, %.1f)，%d 发，世界时间 %.4f s"), *Box->GetName(), Request.Distance,
			Request.Side, Request.Height, Transform.GetLocation().X, Transform.GetLocation().Y, Transform.GetLocation().Z, Box->GetRounds(), World.GetTimeSeconds());
	}

	/** @return 还要不要继续等 */
	bool TickAmmoBox(FAmmoBoxSpawn& Request)
	{
		UWorld* World = Request.World.Get();
		const AGinkgoCharacter* Character = FindCharacter(World);
		if (!World || !Character)
		{
			return World != nullptr;
		}
		const double Now = World->GetTimeSeconds();
		if (Request.CharacterSeenAt < 0.0)
		{
			Request.CharacterSeenAt = Now;
		}
		if (Now - Request.CharacterSeenAt < Request.Delay)
		{
			return true;
		}
		SpawnAmmoBox(*World, *Character, Request);
		return false;
	}

	/** ginkgo.pickup.ammo [distance=<cm>] [side=<cm>] [height=<cm>] [rounds=<发>] [delay=<秒>] [armreach=<cm>] [maxduration=<秒>] */
	void AmmoBox(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		if (World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogGinkgoWeapon, Warning, TEXT("[Weapon] ginkgo.pickup.ammo 要在服务器上执行：联网时用 serverexec ginkgo.pickup.ammo ..."));
			return;
		}
		TSharedRef<FAmmoBoxSpawn> Request = MakeShared<FAmmoBoxSpawn>();
		Request->World = World;
		for (const FString& Arg : Args)
		{
			FParse::Value(*Arg, TEXT("distance="), Request->Distance);
			FParse::Value(*Arg, TEXT("side="), Request->Side);
			FParse::Value(*Arg, TEXT("height="), Request->Height);
			FParse::Value(*Arg, TEXT("rounds="), Request->Rounds);
			FParse::Value(*Arg, TEXT("delay="), Request->Delay);
			FParse::Value(*Arg, TEXT("armreach="), Request->ArmReach);
			FParse::Value(*Arg, TEXT("maxduration="), Request->MaxDuration);
		}
		if (TickAmmoBox(*Request))
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Request](float)
			{
				return TickAmmoBox(*Request);
			}));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs AmmoBoxCommand(
		TEXT("ginkgo.pickup.ammo"),
		TEXT("在角色前方放一个子弹盒（能拾取，给 4–7 发）：ginkgo.pickup.ammo [distance=<cm>] [side=<cm>] [height=<cm>] [rounds=<发>] [delay=<秒>] [armreach=<cm>] [maxduration=<秒>]。\n")
		TEXT("distance、side 是角色前方、右方多远（默认 70、0 cm），height 是盒底离那里的地面多高（默认 90 cm，悬着放）；rounds 不写时照常掷；")
		TEXT("delay 是第一个玩家的角色出现之后再等几秒（世界时间）；armreach 把这一盒够得着的椭球的三个半轴都改成这个长度，maxduration 改保底时长（测量用）。\n")
		TEXT("在服务器上执行（单机与主机是本地），联网的客户端用 serverexec ginkgo.pickup.ammo。调试工具，发行版不编译"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AmmoBox));
}

#endif // !UE_BUILD_SHIPPING
