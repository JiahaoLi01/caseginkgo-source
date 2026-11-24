#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "MuCO/CustomizableObject.h"
#include "PreviewScene.h"
#include "Weapon/GinkgoModularWeaponVisual.h"
#include "Weapon/GinkgoWeaponDefinition.h"

namespace
{
	/** 使用真实 Mutable 异步输出，逐帧检查动作预览交接；测试结束和超时均清理组件及 Root 引用。 */
	class FAttachmentHandoffScenario : public IAutomationLatentCommand
	{
	public:
		explicit FAttachmentHandoffScenario(FAutomationTestBase& InTest) : Test(InTest) {}

		virtual ~FAttachmentHandoffScenario() override
		{
			if (Visual) { Visual->Reset(); Visual->RemoveFromRoot(); }
		}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 60.0)
			{
				Test.AddError(FString::Printf(TEXT("配件交接等待 Mutable 超时，阶段 %d"), Stage));
				return true;
			}
			if (Stage == 0)
			{
				Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M4/DA_M4.DA_M4"));
				if (!Test.TestNotNull(TEXT("M4 定义"), Weapon)) { return true; }
				auto* Object = LoadObject<UCustomizableObject>(nullptr, TEXT("/Game/Weapons/M4/CO_M4.CO_M4"));
				FCompileParams Compile;
				Compile.bAsync = false;
				Object->Compile(Compile);
				Scene = MakeUnique<FPreviewScene>(FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false).SetTransactional(false));
				Gun = NewObject<UPoseableMeshComponent>();
				Gun->SetSkinnedAssetAndUpdate(LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Weapons/M4/SK_M4_Default.SK_M4_Default")));
				Scene->AddComponent(Gun, FTransform::Identity);
				Visual = NewObject<UGinkgoModularWeaponVisual>();
				Visual->AddToRoot();
				Before = Gun->GetSkinnedAsset();
				Installed = Weapon->GetDefaultAttachments();
				Visual->Update(*Weapon, Installed, *Gun);
				Stage = 1;
				return false;
			}
			if (Stage == 1)
			{
				if (Gun->GetSkinnedAsset() == Before) { return false; }
				Part = FindPart(TEXT("Muzzle_Suppressor"));
				Preview = NewPreview(*Part);
				Gun->HideBoneByName(TEXT("mag_new"), EPhysBodyOp::PBO_None);
				Test.TestTrue(TEXT("动作可在装配提交前移交已落位预览"), Visual->HoldPreview(*Part, *Gun, *Preview, TEXT("mount_muzzle"), false));
				Test.TestTrue(TEXT("异步等待期旧件持续隐藏"), Gun->IsBoneHiddenByName(TEXT("mount_muzzle")));
				Before = Gun->GetSkinnedAsset();
				Installed = Weapon->WithAttachment(Installed, Part);
				Visual->Update(*Weapon, Installed, *Gun);
				Stage = 2;
				return false;
			}
			if (Stage == 2)
			{
				if (Gun->GetSkinnedAsset() == Before)
				{
					Test.TestTrue(TEXT("生成期间每帧保留新件并遮住旧件"), IsValid(Preview) && Preview->IsVisible()
						&& Gun->IsBoneHiddenByName(TEXT("mount_muzzle")) && Visual->GetHandoffCount() == 1);
					return false;
				}
				Test.TestEqual(TEXT("新组合显示的同一帧释放交接"), Visual->GetHandoffCount(), 0);
				Test.TestTrue(TEXT("旧临时组件已销毁"), !IsValid(Preview) || !Preview->IsRegistered());
				Test.TestFalse(TEXT("新枪口不残留隐藏标记"), Gun->IsBoneHiddenByName(TEXT("mount_muzzle")));
				Test.TestTrue(TEXT("生成不清除其他动作的隐藏标记"), Gun->IsBoneHiddenByName(TEXT("mag_new")));
				// 网格已经完成时，动作自行清理即可，不能再挂一个永久占位。
				Preview = NewPreview(*Part);
				Test.TestFalse(TEXT("已显示相同配件无需接管"), Visual->HoldPreview(*Part, *Gun, *Preview, TEXT("mount_muzzle"), false));
				Preview->DestroyComponent();
				Part = FindPart(TEXT("Muzzle_Brake"));
				Preview = NewPreview(*Part);
				Visual->HoldPreview(*Part, *Gun, *Preview, TEXT("mount_muzzle"), false);
				Installed = Weapon->WithAttachment(Installed, Part);
				Visual->Update(*Weapon, Installed, *Gun);
				// 不等刹车器结果就改回消音器，迟到的回调不能恢复旧配置或再次隐藏骨骼。
				Installed = Weapon->WithAttachment(Installed, FindPart(TEXT("Muzzle_Suppressor")));
				Visual->Update(*Weapon, Installed, *Gun);
				Test.TestEqual(TEXT("被新配置取代的占位立即释放"), Visual->GetHandoffCount(), 0);
				Test.TestTrue(TEXT("过期占位组件已销毁"), !IsValid(Preview) || !Preview->IsRegistered());
				Part = FindPart(TEXT("Stock_DMR"));
				Preview = NewPreview(*Part);
				Visual->HoldPreview(*Part, *Gun, *Preview, TEXT("mount_stock"), false);
				Visual->Reset();
				Test.TestEqual(TEXT("清槽立即释放全部占位"), Visual->GetHandoffCount(), 0);
				Test.TestFalse(TEXT("清槽恢复骨骼原始显隐"), Gun->IsBoneHiddenByName(TEXT("mount_stock")));
				Test.TestTrue(TEXT("清槽销毁临时组件"), !IsValid(Preview) || !Preview->IsRegistered());
				return true;
			}
			return true;
		}

	private:
		UGinkgoWeaponAttachment* FindPart(const FName Option) const
		{
			for (const auto& Mount : Weapon->Attachments) { if (Mount.Part->ModularOption == Option) { return Mount.Part; } }
			return nullptr;
		}

		UPoseableMeshComponent* NewPreview(const UGinkgoWeaponAttachment& Attachment)
		{
			auto* Mesh = NewObject<UPoseableMeshComponent>();
			Mesh->SetSkinnedAssetAndUpdate(Attachment.ModuleMesh.LoadSynchronous());
			Scene->AddComponent(Mesh, FTransform::Identity);
			return Mesh;
		}

		/** 测试执行器，只在游戏线程潜伏命令中访问。 */
		FAutomationTestBase& Test;

		/** 独立预览世界，负责所有场景组件的生命周期。 */
		TUniquePtr<FPreviewScene> Scene;

		/** Root 持有的外观管理器；析构时先 Reset，再释放 Root。 */
		UGinkgoModularWeaponVisual* Visual = nullptr;

		/** 当前测试武器定义；生成期间由外观状态和已加载资产引用保持。 */
		UGinkgoWeaponDefinition* Weapon = nullptr;

		/** 本阶段要移交的配件，来自武器适配表。 */
		UGinkgoWeaponAttachment* Part = nullptr;

		/** 预览世界持有的枪组件，所有异步结果写入此目标。 */
		UPoseableMeshComponent* Gun = nullptr;

		/** 用于观察动作临时件何时销毁，不持有跨世界资源。 */
		UPoseableMeshComponent* Preview = nullptr;

		/** 请求前的网格身份，用于识别真正应用了新结果，而非仅发出了请求。 */
		USkinnedAsset* Before = nullptr;

		/** 模拟服务器已提交的装配；不改写资产默认值。 */
		FGinkgoInstalledAttachments Installed;

		/** 测试阶段：初始组合、发起交接、等待交接并验证清理。 */
		int32 Stage = 0;

		/** 总计等待起点（真实秒），防止 Mutable 失败时永久阻塞测试。 */
		double Started = FPlatformTime::Seconds();
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAttachmentHandoffTest, "CaseGinkgo.Weapon.M4.AttachmentHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAttachmentHandoffTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FAttachmentHandoffScenario(*this));
	return true;
}

#endif
