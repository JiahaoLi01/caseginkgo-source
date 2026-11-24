#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AI/GinkgoAICharacter.h"
#include "AI/GinkgoAIController.h"
#include "AI/GinkgoAIConfig.h"
#include "AI/GinkgoAICharacterMovementComponent.h"
#include "Animation/GinkgoAnimInstance.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Combat/GinkgoAmmoDefinition.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interaction/GinkgoInteractionSubsystem.h"
#include "Interaction/GinkgoInteractorComponent.h"
#include "Tests/AutomationEditorCommon.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoWeaponMechanism.h"
#include "Weapon/GinkgoWeaponPickup.h"

namespace
{
	/** 在真实 PIE 世界推进验收；不直接写 AI 记忆，发现与丢失均通过感知、场景遮挡与玩家移动产生。 */
	class FGinkgoAIWorldScenario : public IAutomationLatentCommand
	{
	public:
		explicit FGinkgoAIWorldScenario(FAutomationTestBase& InTest) : Test(InTest) {}
		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 100.0)
			{
				Test.AddError(FString::Printf(TEXT("AI PIE 场景超时，阶段 %d"), Stage));
				return true;
			}
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay()) return false;
			if (!Enemy.IsValid())
			{
				for (TActorIterator<AGinkgoAICharacter> It(World); It; ++It) { Enemy = *It; break; }
			}
			AGinkgoAICharacter* NPC = Enemy.Get();
			AGinkgoAIController* AI = NPC ? Cast<AGinkgoAIController>(NPC->GetController()) : nullptr;
			AGinkgoCharacter* Player = World->GetFirstPlayerController() ? Cast<AGinkgoCharacter>(World->GetFirstPlayerController()->GetPawn()) : nullptr;
			if (!NPC || !AI || !Player) return false;
			// 穿甲弹会绕过部分护甲，测试靶即使有大量护甲也可能先死亡。保留命中证据后在低生命时复位，继续测空仓换弹。
			bPlayerHit |= Player->GetDamageable()->GetHealth() < Player->GetDamageable()->GetMaxHealth();
			if (Player->GetDamageable()->GetHealth() < 50.f) Player->GetDamageable()->Revive();
			UGinkgoWeaponComponent* Weapons = NPC->GetWeapons();
			const FGinkgoMechanismState* Mechanism = Weapons->GetMechanismState(Weapons->GetHeldSlot());
			const double Now = World->GetTimeSeconds();
			const double Elapsed = Now - StageStarted;
			const auto* Animation = Cast<UGinkgoAnimInstance>(NPC->GetMesh()->GetAnimInstance());
			const auto* Movement = Cast<UGinkgoAICharacterMovementComponent>(NPC->GetCharacterMovement());
			auto Advance = [&]()
			{
				Test.AddInfo(FString::Printf(TEXT("阶段 %d 完成 @ %.2f s: 枪内 %d，后备 %d，初始后备 %d"), Stage, Now,
					Mechanism ? Mechanism->CountLiveRounds() : -1, Weapons->GetAmmoReserve().Count(Weapons->GetHeldWeapon()->AmmoType), InitialReserve));
				++Stage;
				StageStarted = Now;
			};
			switch (Stage)
			{
			case 0:
				if (Now < 0.5) break;
				Test.TestTrue(TEXT("真实接管启动决策树"), AI->IsAIActive());
				Test.TestTrue(TEXT("高墙挡住开局玩家，保持巡逻"), !AI->GetKnowledge().bHasMemory);
				Test.TestNotNull(TEXT("AI 具有实际武器机构"), Mechanism);
				if (!Mechanism) return true;
				InitialLocation = NPC->GetActorLocation();
				InitialRounds = Mechanism->CountLiveRounds();
				InitialReserve = Weapons->GetAmmoReserve().Count(Weapons->GetHeldWeapon()->AmmoType);
				{
					FGinkgoArmorPiece Armor;
					Armor.Name = TEXT("AutomationArmor");
					Armor.MaxDurability = 1000000.f;
					Armor.Covers = { EGinkgoBodyPart::UpperChest };
					Player->GetDamageable()->SetArmor({ Armor });
				}
				Advance();
				break;
			case 1:
				if (Elapsed < 1.0) break;
				Test.TestTrue(TEXT("巡逻点的停留时间内不移动"), FVector::Dist2D(InitialLocation, NPC->GetActorLocation()) < 15.0);
				Advance();
				break;
			case 2:
				if (Elapsed < 3.0) break;
				Test.TestTrue(TEXT("停留结束沿 NavMesh 实际移动"), FVector::Dist2D(InitialLocation, NPC->GetActorLocation()) > 80.0);
				Test.TestTrue(TEXT("巡逻慢走使用模拟输入，不能用降低 GetMaxSpeed 代替"), Movement
					&& Movement->GetMaxSpeed() > AI->GetDefinition()->PatrolSpeed
					&& Movement->GetAnalogInputModifier() > 0.f && Movement->GetAnalogInputModifier() < 0.6f);
				Test.TestTrue(TEXT("真实动画管线为巡逻选择 Walk"), Animation && Animation->GetGait() == EGinkgoAnimGait::Walk);
				NPC->PatrolRoute = nullptr;
				AI->StopMovement();
				NPC->SetActorLocation(FVector(0.0, 700.0, 96.0));
				AI->SetControlRotation(FRotator(0.0, -45.0, 0.0));
				Player->SetActorLocation(FVector(700.0, 0.0, 96.0));
				Player->GetCharacterMovement()->DisableMovement();
				Advance();
				break;
			case 3:
				if (Elapsed < 2.0) break;
				Test.TestTrue(TEXT("真实感知获得玩家"), AI->GetKnowledge().bVisible);
				Test.TestEqual(TEXT("树切换到 Engage"), AI->GetActiveBehavior(), EGinkgoAIBehavior::Engage);
				Test.TestTrue(TEXT("射击通过现有机构消耗枪内子弹"), Mechanism && Mechanism->CountLiveRounds() < InitialRounds);
				Advance();
				break;
			case 4:
				if (Elapsed < 25.0 && (Weapons->GetAmmoReserve().Count(Weapons->GetHeldWeapon()->AmmoType) >= InitialReserve
					|| !Mechanism || Mechanism->CountLiveRounds() >= 28)) break;
				Test.TestTrue(TEXT("打空后真实换弹消耗后备"), Weapons->GetAmmoReserve().Count(Weapons->GetHeldWeapon()->AmmoType) < InitialReserve);
				Test.TestTrue(TEXT("换弹动作完成后能够继续射击"), Mechanism && Mechanism->CountLiveRounds() < 28);
				Test.TestTrue(TEXT("交战期间 EQS 找到掩体并实际移动"), FVector::Dist2D(NPC->GetActorLocation(), FVector(0.0, 700.0, 96.0)) > 150.0);
				Test.TestTrue(TEXT("枪口弹道实际命中玩家并结算伤害"), bPlayerHit);
				Player->SetActorLocation(FVector(5000.0, -5000.0, 96.0));
				Advance();
				break;
			case 5:
				if ((AI->GetKnowledge().bVisible || AI->GetActiveBehavior() == EGinkgoAIBehavior::Engage) && Elapsed < 2.0) break;
				Test.TestFalse(TEXT("移出视野后停止实时目击"), AI->GetKnowledge().bVisible);
				Test.TestEqual(TEXT("丢失目标进入压制推进"), AI->GetActiveBehavior(), EGinkgoAIBehavior::Pursue);
				FrozenLocation = AI->GetKnowledge().LastLocation;
				Test.TestTrue(TEXT("不会拿隐藏玩家的新位置作目标"), FVector::Dist2D(FrozenLocation, Player->GetActorLocation()) > 1000.0);
				Advance();
				break;
			case 6:
				if (AI->GetActiveBehavior() != EGinkgoAIBehavior::Search && Elapsed < AI->GetDefinition()->MemoryLifetime) break;
				Test.TestEqual(TEXT("抵达最后目击位置并确认无人后搜索"), AI->GetActiveBehavior(), EGinkgoAIBehavior::Search);
				Test.TestTrue(TEXT("搜索从已抵达的目击点开始"), AI->HasReached(FrozenLocation));
				Test.TestEqual(TEXT("搜索不更新隐藏目标位置"), AI->GetKnowledge().LastLocation, FrozenLocation);
				Test.TestFalse(TEXT("搜索清除排队开火"), Weapons->WantsFire());
				Advance();
				break;
			case 7:
				if (AI->GetActiveBehavior() == EGinkgoAIBehavior::Search && Elapsed > 1.0 && Movement && Animation
					&& Movement->Velocity.Size2D() > 30.0 && !Movement->GetCurrentAcceleration().IsNearlyZero())
				{
					++SearchMoveSamples;
					bSearchOnlyWalk &= Animation->GetGait() == EGinkgoAnimGait::Walk;
					bSearchAnalogInput &= Movement->GetAnalogInputModifier() > 0.f && Movement->GetAnalogInputModifier() < 0.6f;
					PeakSearchSpeed = FMath::Max(PeakSearchSpeed, Movement->Velocity.Size2D());
				}
				if (AI->GetActiveBehavior() != EGinkgoAIBehavior::Patrol && Elapsed < 18.0) break;
				Test.TestTrue(TEXT("搜索阶段实际采样到持续移动"), SearchMoveSamples >= 20);
				Test.TestTrue(TEXT("搜索持续移动全程选 Walk，不再低速跑步滑行"), bSearchOnlyWalk);
				Test.TestTrue(TEXT("搜索沿玩家轻推手柄的模拟输入路径"), bSearchAnalogInput);
				Test.TestTrue(TEXT("慢走实际速度仍符合搜索定义"), PeakSearchSpeed <= AI->GetDefinition()->SearchSpeed + 5.0);
				Test.AddInfo(FString::Printf(TEXT("搜索 locomotion：%d 个移动样本，峰值 %.2f cm/s，Walk=%d，模拟输入=%d"),
					SearchMoveSamples, PeakSearchSpeed, bSearchOnlyWalk, bSearchAnalogInput));
				Test.TestEqual(TEXT("搜索超时回到巡逻"), AI->GetActiveBehavior(), EGinkgoAIBehavior::Patrol);
				Test.TestFalse(TEXT("搜索结束清除记忆"), AI->GetKnowledge().bHasMemory);
				AI->StopMovement();
				NPC->SetActorLocation(FVector(0.0, 700.0, 96.0));
				NPC->SetActorRotation(FRotator(0.0, -45.0, 0.0));
				AI->SetControlRotation(FRotator(0.0, -45.0, 0.0));
				Player->SetActorLocation(FVector(700.0, 0.0, 96.0));
				Advance();
				break;
			case 8:
			{
				if (!AI->GetKnowledge().bVisible && Elapsed < 3.0) break;
				Test.TestTrue(TEXT("放弃后仍可重新发现玩家"), AI->GetKnowledge().bVisible);
				DroppedWeapon = Weapons->GetHeldWeapon();
				if (!Test.TestTrue(TEXT("死亡前仍持有真实交战的枪及机构状态"), DroppedWeapon.IsValid() && Mechanism)) return true;
				DroppedState = *Mechanism;
				DroppedPart = LoadObject<UGinkgoWeaponAttachment>(nullptr, TEXT("/Game/Weapons/AK47/DA_AK47Suppressor.DA_AK47Suppressor"));
				if (!Test.TestTrue(TEXT("测试枪支持消音器"), Weapons->CanInstallAttachment(Weapons->GetHeldSlot(), DroppedPart.Get()))) return true;
				Weapons->InstallAttachment(Weapons->GetHeldSlot(), DroppedPart.Get());

				// 枪套中的另一把处于卸下弹匣的状态，检验全槽掉落和未完成装填不会自动补满。
				auto* Sidearm = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M1911/DA_M1911.DA_M1911"));
				if (!Test.TestNotNull(TEXT("死亡掉落测试的副武器"), Sidearm)) return true;
				const int32 SidearmSlot = Weapons->FindFreeSlot(Sidearm->SlotKind);
				if (!Test.TestTrue(TEXT("敌人有空的副武器槽"), SidearmSlot != INDEX_NONE)) return true;
				Weapons->SetCarried(SidearmSlot, Sidearm);
				Test.TestTrue(TEXT("副武器实际卸下弹匣"), Weapons->ApplyMechanismEvent(SidearmSlot, EGinkgoMechanismEvent::MagOut));
				const FGinkgoMechanismState SidearmState = *Weapons->GetMechanismState(SidearmSlot);
				TSet<AGinkgoWeaponPickup*> ExistingPickups;
				for (TActorIterator<AGinkgoWeaponPickup> It(World); It; ++It) ExistingPickups.Add(*It);
				const FVector GunLocation = NPC->GetHeldWeaponMesh()->GetComponentLocation();
				const FVector DeathVelocity(120.0, 40.0, 0.0);
				NPC->GetCharacterMovement()->Velocity = DeathVelocity;
				{
					UGinkgoAmmoDefinition* Ammo = NewObject<UGinkgoAmmoDefinition>();
					Ammo->Damage = 10000.f;
					NPC->GetDamageable()->ApplyBulletHit(*Ammo, 1.f, FHitResult(), FVector::ForwardVector, 0.f, Player);
					// 尸体再次中弹不能额外生成一份武器。
					NPC->GetDamageable()->ApplyBulletHit(*Ammo, 1.f, FHitResult(), FVector::ForwardVector, 0.f, Player);
				}
				Test.TestFalse(TEXT("死亡立即停树"), AI->IsAIActive());
				Test.TestFalse(TEXT("死亡立即清除开火请求"), Weapons->WantsFire());
				Test.TestFalse(TEXT("死亡清除目击记忆"), AI->GetKnowledge().bHasMemory);
				Test.TestEqual(TEXT("死亡后手上没有枪"), Weapons->GetHeldSlot(), INDEX_NONE);
				for (int32 Slot = 0; Slot < Weapons->SlotCount; ++Slot)
				{
					Test.TestNull(TEXT("死亡后携带槽清空"), Weapons->GetCarried(Slot));
					Test.TestTrue(TEXT("死亡后原槽的配件清空"), Weapons->GetAttachments(Slot).Parts.IsEmpty());
				}
				TArray<AGinkgoWeaponPickup*> NewPickups;
				for (TActorIterator<AGinkgoWeaponPickup> It(World); It; ++It)
				{
					if (ExistingPickups.Contains(*It)) continue;
					NewPickups.Add(*It);
					// 拾取物通过组件实现交互接口，登记对象是组件；工作台才直接登记 Actor。
					const UGinkgoPickupComponent* Interaction = It->FindComponentByClass<UGinkgoPickupComponent>();
					Test.TestTrue(TEXT("掉落物的交互组件已登记"), Interaction && World->GetSubsystem<UGinkgoInteractionSubsystem>()->IsRegistered(Interaction));
					if (It->GetWeapon() == DroppedWeapon.Get()) DroppedPickup = *It;
					if (It->GetWeapon() == Sidearm)
					{
						Test.TestTrue(TEXT("副武器掉落保留卸匣状态"), It->GetMechanismState() && *It->GetMechanismState() == SidearmState);
					}
				}
				Test.TestEqual(TEXT("两把携带武器各掉落一次，空槽与重复受击不生成额外武器"), NewPickups.Num(), 2);
				if (!Test.TestTrue(TEXT("找到死亡时手持的枪"), DroppedPickup.IsValid())) return true;
				Test.TestTrue(TEXT("掉落的枪保留交战后的弹药与机构状态"), DroppedPickup->GetMechanismState() && *DroppedPickup->GetMechanismState() == DroppedState);
				Test.TestTrue(TEXT("掉落从死亡瞬间的枪挂点开始"), DroppedPickup->GetActorLocation().Equals(GunLocation, 1.0));
				const auto* DroppedMesh = Cast<UStaticMeshComponent>(DroppedPickup->GetRootComponent());
				if (!Test.TestTrue(TEXT("掉落枪进入物理模拟"), DroppedMesh && DroppedMesh->IsSimulatingPhysics())) return true;
				const FVector ExtraVelocity = DroppedMesh->GetPhysicsLinearVelocity() - DeathVelocity;
				Test.TestTrue(TEXT("死亡掉落继承移动速度，不额外向前上方抛出"), FMath::Abs(ExtraVelocity.Z) < 1.0
					&& FMath::Abs(FVector::DotProduct(ExtraVelocity, NPC->GetActorForwardVector())) < 1.0);

				// 通过真实的本地伸手、服务器占领与接受回执拾取，等待动画抓住，不直接往玩家槽里写掉落状态。
				UGinkgoWeaponComponent* PlayerWeapons = Player->GetWeapons();
				PlayerWeapons->CancelFireIntent();
				PlayerWeapons->StopActionStates();
				for (int32 Slot = 0; Slot < PlayerWeapons->SlotCount; ++Slot) PlayerWeapons->SetCarried(Slot, nullptr);
				Player->SetActorLocation(NPC->GetActorLocation() + FVector(-100.0, -100.0, 0.0));
				Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
				PickupSlot = PlayerWeapons->FindFreeSlot(DroppedWeapon->SlotKind);
				ReserveBeforePickup = PlayerWeapons->GetAmmoReserve().Count(DroppedWeapon->AmmoType);
				const FGinkgoInteractionContext PickupContext{Player, Player->GetInteractor()};
				FText Reason;
				Test.TestEqual(TEXT("死亡掉落的枪可由玩家正常拾取"), DroppedPickup->GetAvailability(PickupContext, Reason), EGinkgoInteractionAvailability::Available);
				DroppedPickup->BeginLocalInteract(PickupContext);
				DroppedPickup->Interact(PickupContext);
				DroppedPickup->OnInteractResult(PickupContext, EGinkgoInteractionResult::Accepted);
				++Stage;
				StageStarted = Now;
				break;
			}
			case 9:
			{
				UGinkgoWeaponComponent* PlayerWeapons = Player->GetWeapons();
				if (Elapsed < 4.0 && (PlayerWeapons->GetHeldSlot() != PickupSlot || !Player->IsHeldWeaponInHand()
					|| PlayerWeapons->GetPickupSettleAlpha() < 1.f)) break;
				Test.TestTrue(TEXT("拾取动画完成，敌人的枪已到玩家手上"), PlayerWeapons->GetHeldWeapon() == DroppedWeapon.Get() && Player->IsHeldWeaponInHand());
				const FGinkgoMechanismState* PickedState = PlayerWeapons->GetMechanismState(PickupSlot);
				Test.TestTrue(TEXT("捡回后机构与死亡时一致，没有补满弹药"), PickedState && *PickedState == DroppedState);
				Test.TestTrue(TEXT("消音器随敌人的枪转移到玩家"), PlayerWeapons->GetAttachments(PickupSlot).Parts.Contains(DroppedPart.Get()));
				Test.TestEqual(TEXT("死亡掉落不赠送开局后备子弹"), PlayerWeapons->GetAmmoReserve().Count(DroppedWeapon->AmmoType), ReserveBeforePickup);
				return true;
			}
			}
			return false;
		}

	private:
		/** 测试报告接收者，由 Automation 框架拥有，在本命令结束前有效。 */
		FAutomationTestBase& Test;

		/** 超时基准（真实秒），即使 PIE 未成功启动也能终止测试。 */
		double Started = FPlatformTime::Seconds();

		/** 当前场景阶段；每阶段通过真实世界时间等待感知、导航与动画。 */
		int32 Stage = 0;

		/** 进入当前阶段的世界时间（秒）。 */
		double StageStarted = 0.0;

		/** 本次 PIE 的敌人引用，世界销毁后自动失效。 */
		TWeakObjectPtr<AGinkgoAICharacter> Enemy;

		/** 开始巡逻的位置（世界 cm），验证停留和导航产生的位移。 */
		FVector InitialLocation = FVector::ZeroVector;

		/** 丢失目标后的记忆位置（世界 cm），用于验证没有穿墙追踪。 */
		FVector FrozenLocation = FVector::ZeroVector;

		/** 初始枪内子弹数，验证真实射击路径。 */
		int32 InitialRounds = 0;

		/** 初始后备数量，验证真实装填事件。 */
		int32 InitialReserve = 0;

		/** 是否曾经观察到玩家真实掉血；复位测试靶前保存，避免护甲穿透使后续场景提前结束。 */
		bool bPlayerHit = false;

		/** 搜索开始一秒后、有移动输入且实际速度超过 30 cm/s 的帧数，确保不是在静止时误报步态通过。 */
		int32 SearchMoveSamples = 0;

		/** 搜索移动样本中是否一直由真实 AnimInstance 选择 Walk；任一 Run 样本即失败。 */
		bool bSearchOnlyWalk = true;

		/** 搜索移动样本是否始终保留轻推手柄对应的非满幅模拟输入。 */
		bool bSearchAnalogInput = true;

		/** 搜索稳态移动期间观测到的最大水平速度（cm/s），排除进入状态时的上一行为减速过渡。 */
		double PeakSearchSpeed = 0.0;

		/** 死亡时手持的武器定义，由资产与掉落物持有；用于下一阶段检查拾取结果。 */
		TWeakObjectPtr<UGinkgoWeaponDefinition> DroppedWeapon;

		/** 死亡前的机构快照，跨阶段比较枪内弹药、弹种、枪机与弹匣状态。 */
		FGinkgoMechanismState DroppedState;

		/** 死亡前安装的消音器，用来验证配件随拾取物转移而非留在尸体槽里。 */
		TWeakObjectPtr<UGinkgoWeaponAttachment> DroppedPart;

		/** 本次死亡新生成的手持武器拾取物；被拾取清理或 PIE 结束时自动失效。 */
		TWeakObjectPtr<AGinkgoWeaponPickup> DroppedPickup;

		/** 玩家预定接收掉落枪的槽；死亡阶段开始拾取时记录，未选择时为 INDEX_NONE。 */
		int32 PickupSlot = INDEX_NONE;

		/** 拾取前玩家拥有的该弹种后备数量，确保掉落武器不会按新枪赠送弹药。 */
		int32 ReserveBeforePickup = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAIWorldTest, "CaseGinkgo.AI.World.PatrolCombatSearchAndDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAIWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/AI/Maps/L_AIPrototype")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FGinkgoAIWorldScenario>(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
