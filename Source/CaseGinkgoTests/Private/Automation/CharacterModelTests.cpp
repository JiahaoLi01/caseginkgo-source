#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AI/GinkgoAICharacter.h"
#include "AI/GinkgoAIController.h"
#include "AI/GinkgoEnemyArchetype.h"
#include "Animation/GinkgoAnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/GinkgoAmmoDefinition.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Tests/AutomationEditorCommon.h"
#include "TraversalPIEScene.h"

namespace
{
	/** 在真实角色的命中网格上分别命中六个部位，检查扣的是实例耐久，并且没有跨部位多算护甲。 */
	void CheckArmor(FAutomationTestBase& Test, AGinkgoAICharacter& Enemy, float Capacity)
	{
		auto* Vitals = Enemy.GetDamageable();
		Test.TestEqual(TEXT("出生生命"), Vitals->GetHealth(), 100.f);
		if (!Test.TestEqual(TEXT("防具件数"), Vitals->GetArmor().Num(), Capacity > 0.f ? 1 : 0)) return;
		auto* Ammo = NewObject<UGinkgoAmmoDefinition>();
		Ammo->Damage = Ammo->ArmorDamage = 2.f;
		float Remaining = Capacity;
		for (const FName Bone : { TEXT("spine_05"), TEXT("head"), TEXT("pelvis"), TEXT("hand_l"), TEXT("calf_r"), TEXT("foot_l") })
		{
			FHitResult Hit(&Enemy, Enemy.GetMesh(), Enemy.GetMesh()->GetSocketLocation(Bone), FVector::UpVector);
			Hit.BoneName = Bone;
			const bool bCovered = Capacity == 500.f || (Capacity == 100.f && Bone == TEXT("spine_05"));
			const auto Event = Vitals->ApplyBulletHit(*Ammo, 1.f, Hit, FVector::ForwardVector, 0.f, nullptr);
			Test.TestEqual(*FString::Printf(TEXT("%s 的防护范围"), *Bone.ToString()), Event.Outcome.bArmorHit, bCovered);
			Test.TestEqual(TEXT("未覆盖部位扣生命"), Event.Outcome.HealthDamage > 0.f, !bCovered);
			if (Capacity > 0.f)
			{
				if (bCovered) Test.TestTrue(TEXT("覆盖部位消耗耐久"), Event.Outcome.ArmorDamage > 0.f);
				Remaining -= Event.Outcome.ArmorDamage;
				Test.TestEqual(TEXT("所有覆盖部位扣同一件防具"), Vitals->GetArmor()[0].Durability, Remaining);
			}
		}
		Vitals->Revive();
		if (Capacity > 0.f) Test.TestEqual(TEXT("复位恢复完整护甲池"), Vitals->GetArmor()[0].Durability, Capacity);
	}

	/** 对四种外观播放项目攀爬蒙太奇，以相对骨盆的位移排除根运动带着静态网格移动的假阳性。 */
	class FCheckCharacterModels : public IAutomationLatentCommand
	{
	public:
		explicit FCheckCharacterModels(FAutomationTestBase& InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 80.)
			{
				Test.AddError(TEXT("角色外观 PIE 采样超时"));
				return true;
			}
			UWorld* World = GEditor->PlayWorld;
			auto* PC = World ? World->GetFirstPlayerController() : nullptr;
			auto* Player = PC ? Cast<AGinkgoCharacter>(PC->GetPawn()) : nullptr;
			if (!Player || !Player->HasActorBegunPlay()) return false;
			if (Characters.IsEmpty())
			{
				Characters.Add(Player);
				for (TActorIterator<AGinkgoAIController> It(World); It; ++It) It->StopAI();
				for (const TCHAR* Id : { TEXT("Unarmored"), TEXT("Light"), TEXT("Heavy") })
				{
					const FTransform Transform(FRotator::ZeroRotator, Player->GetActorLocation() + FVector(450., 150. * Characters.Num(), 0.));
					auto* Enemy = World->SpawnActorDeferred<AGinkgoAICharacter>(AGinkgoAICharacter::StaticClass(), Transform, nullptr, nullptr,
						ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
					if (!Test.TestNotNull(TEXT("可以生成测试敌人"), Enemy)) return true;
					if (Characters.Num() > 1) Enemy->EnemyType = LoadObject<UGinkgoEnemyArchetype>(nullptr, *(FString(TEXT("/Game/AI/Enemies/DA_Enemy_")) + Id));
					Enemy->FinishSpawning(Transform);
					if (auto* AI = Cast<AGinkgoAIController>(Enemy->GetController())) AI->StopAI();
					CheckArmor(Test, *Enemy, Characters.Num() == 1 ? 0.f : (Characters.Num() == 2 ? 100.f : 500.f));
					Characters.Add(Enemy);
				}
				const FName MeshNames[] = { TEXT("SK_Survival_Character"), TEXT("SK_Man_Full_03"), TEXT("SKM_QuantumCharacter"), TEXT("SK_Grave_Mercer") };
				for (int32 Index = 0; Index < Characters.Num(); ++Index)
				{
					auto* Actor = Characters[Index].Get();
					auto* Source = Actor->GetMesh();
					auto* Visual = Actor->GetPresentationMesh();
					if (!Test.TestTrue(TEXT("可见模型与项目动画驱动网格独立"), Visual != Source && !Source->IsVisible() && Visual->IsVisible())
						|| !Test.TestNotNull(TEXT("显示网格具有重定向动画实例"), Visual->GetAnimInstance())) return true;
					Test.TestTrue(TEXT("玩家与敌人保留项目的动作蓝图"), Source->GetAnimInstance() && Source->GetAnimInstance()->IsA<UGinkgoAnimInstance>());
					Test.TestEqual(TEXT("使用所选着装"), Visual->GetSkeletalMeshAsset()->GetFName(), MeshNames[Index]);
					if (Actor == Player)
					{
						const int32 Backpack = Visual->GetMaterialIndex(TEXT("Backpack1"));
						if (!Test.TestTrue(TEXT("玩家模型具有独立背包材质槽"), Backpack != INDEX_NONE)) return true;
						for (int32 LOD = 0; LOD < Visual->GetSkeletalMeshAsset()->GetLODNum(); ++LOD)
						{
							for (int32 Slot = 0; Slot < Visual->GetNumMaterials(); ++Slot)
							{
								Test.TestEqual(*FString::Printf(TEXT("玩家 LOD %d 材质槽 %d 仅隐藏背包"), LOD, Slot),
									Visual->IsMaterialSectionShown(Slot, LOD), Slot != Backpack);
							}
						}
					}
					Test.TestTrue(TEXT("显示端只使用项目重定向蓝图"), Visual->GetAnimInstance()->GetClass()->GetPathName().StartsWith(TEXT("/Game/Characters/Ginkgo/Models/Retarget/ABP_Ginkgo_")));
					Test.TestEqual(TEXT("命中仅使用源骨架"), Visual->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
					Visual->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				}
				ReadyAt = World->GetTimeSeconds() + .4;
				return false;
			}
			const double Now = World->GetTimeSeconds();
			if (Now < ReadyAt) return false;
			if (RagdollAt >= 0.)
			{
				if (Now - RagdollAt < 1.5) return false;
				auto* Enemy = Characters.Last().Get();
				auto TorsoAxis = [](const USkeletalMeshComponent* Mesh)
				{
					return (Mesh->GetSocketLocation(TEXT("head")) - Mesh->GetSocketLocation(TEXT("pelvis"))).GetSafeNormal();
				};
				const FVector PhysicsAxis = TorsoAxis(Enemy->GetMesh());
				Test.TestTrue(TEXT("倒地后可见躯干随物理骨架倾倒"), Enemy->GetMesh()->IsSimulatingPhysics(TEXT("pelvis"))
					&& PhysicsAxis.Z < .8 && PhysicsAxis.Dot(TorsoAxis(Enemy->GetPresentationMesh())) > .9);
				return true;
			}
			const FName Bones[] = { TEXT("hand_r"), TEXT("foot_l") };
			auto Relative = [](USkeletalMeshComponent* Mesh, FName Bone)
			{
				return Mesh->GetSocketTransform(Bone, RTS_Component).GetLocation() - Mesh->GetSocketTransform(TEXT("pelvis"), RTS_Component).GetLocation();
			};
			if (PlaybackAt < 0.)
			{
				auto* Montage = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Characters/Traversal/AM_Traversal_Climb"));
				if (!Test.TestNotNull(TEXT("项目攀爬动作存在"), Montage)) return true;
				for (auto Weak : Characters)
				{
					auto* Actor = Weak.Get();
					for (const FName Wrist : { TEXT("hand_l"), TEXT("hand_r") })
					{
						const double Error = FVector::Distance(Actor->GetMesh()->GetSocketLocation(Wrist), Actor->GetPresentationMesh()->GetSocketLocation(Wrist));
						const FString Grip = FString::Printf(TEXT("%s %s 持枪手腕误差 %.2f cm"),
							*GetNameSafe(Actor->GetPresentationMesh()->GetSkeletalMeshAsset()), *Wrist.ToString(), Error);
						Test.AddInfo(Grip);
						Test.TestTrue(*Grip, Error < 3.);
					}
					for (FName Bone : Bones)
					{
						Samples.Add({Relative(Actor->GetMesh(), Bone), Relative(Actor->GetPresentationMesh(), Bone)});
					}
					if (!Test.TestTrue(TEXT("在项目动画实例开始攀爬"), Actor->GetMesh()->GetAnimInstance()->Montage_Play(Montage) > 0.f)) return true;
				}
				PlaybackAt = Now;
				return false;
			}
			int32 Index = 0;
			for (auto Weak : Characters)
			{
				auto* Actor = Weak.Get();
				for (FName Bone : Bones)
				{
					auto& Sample = Samples[Index++];
					const FVector SourceDelta = Relative(Actor->GetMesh(), Bone) - Sample.SourceStart;
					const FVector VisualDelta = Relative(Actor->GetPresentationMesh(), Bone) - Sample.VisualStart;
					if (SourceDelta.Size() > Sample.SourceTravel)
					{
						Sample.SourceTravel = SourceDelta.Size();
						Sample.VisualTravel = VisualDelta.Size();
						Sample.Alignment = SourceDelta.GetSafeNormal().Dot(VisualDelta.GetSafeNormal());
					}
				}
			}
			if (Now - PlaybackAt < 1.1) return false;
			for (int32 I = 0; I < Samples.Num(); ++I)
			{
				const auto& S = Samples[I];
				Test.TestTrue(*FString::Printf(TEXT("%s %s 源/目标位移 %.1f/%.1f cm，方向余弦 %.2f"),
					*GetNameSafe(Characters[I/2]->GetPresentationMesh()->GetSkeletalMeshAsset()), *Bones[I%2].ToString(), S.SourceTravel, S.VisualTravel, S.Alignment),
					S.SourceTravel > 5. && S.VisualTravel > S.SourceTravel * .55 && S.VisualTravel < S.SourceTravel * 1.5 && S.Alignment > .8);
			}
			auto* Heavy = Characters.Last().Get();
			auto* Ammo = NewObject<UGinkgoAmmoDefinition>();
			Ammo->Damage = 10000.f;
			Ammo->ArmorPenetration = 1.f;
			const auto Hit = Heavy->GetDamageable()->ApplyBulletHit(*Ammo, 1.f, FHitResult(), FVector::ForwardVector, 0.f, nullptr);
			if (!Test.TestTrue(TEXT("重甲敌人进入死亡流程"), Hit.bKilled)) return true;
			RagdollAt = Now;
			return false;
		}

	private:
		struct FBoneSample
		{
			/** 项目骨架动作开始时相对骨盆的位置（网格空间，cm）。 */
			FVector SourceStart;

			/** 可见骨架同一时刻相对骨盆的位置（网格空间，cm）。 */
			FVector VisualStart;

			/** 已观测到的源骨骼最大位移（cm）。 */
			double SourceTravel = 0.;

			/** 源位移达到上述峰值时可见骨骼的位移（cm）。 */
			double VisualTravel = 0.;

			/** 同帧两侧位移的方向余弦。 */
			double Alignment = 0.;
		};

		/** 自动化框架拥有，接收各模型和部位的断言。 */
		FAutomationTestBase& Test;

		/** PIE 等待的墙钟起点（秒）。 */
		double Started = FPlatformTime::Seconds();

		/** 等待源动画和目标处理器完成初始化的世界时间（秒）。 */
		double ReadyAt = 0.;

		/** 蒙太奇开始的世界时间（秒），负数表示未播放。 */
		double PlaybackAt = -1.;

		/** 重甲角色在 PIE 中死亡的世界时间（秒），负数表示还在采样动画。 */
		double RagdollAt = -1.;

		/** 当前 PIE 世界里的玩家、普通、轻甲和重甲角色。 */
		TArray<TWeakObjectPtr<AGinkgoCharacter>> Characters;

		/** 依角色顺序记录手、脚两个采样点。 */
		TArray<FBoneSample> Samples;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCharacterModelsTest, "CaseGinkgo.Character.Models.RetargetAndArmor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCharacterModelsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/AI/Maps/L_AIPrototype")));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<GinkgoTraversalTests::FStartStandalonePIE>());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FCheckCharacterModels>(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
