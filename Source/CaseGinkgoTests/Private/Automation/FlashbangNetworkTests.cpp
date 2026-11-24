#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AI/GinkgoAICharacter.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/LineBatchComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationEditorCommon.h"
#include "Throwables/GinkgoFlashbangComponent.h"
#include "Throwables/GinkgoFlashProjectile.h"

namespace
{
	class FStartFlashNetworkPIE final : public IAutomationLatentCommand
	{
	public:
		bool Update() override
		{
			FRequestPlaySessionParams Params;
			Params.EditorPlaySettings = DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage());
			Params.EditorPlaySettings->SetPlayNetMode(PIE_Client);
			Params.EditorPlaySettings->SetPlayNumberOfClients(2);
			Params.EditorPlaySettings->SetRunUnderOneProcess(true);
			GEditor->RequestPlaySession(Params);
			return true;
		}
	};

	/** 拥有者发输入 RPC，分别在服务器和两台客户端检查同一投掷阶段与敌人反应。 */
	class FFlashNetworkScenario final : public IAutomationLatentCommand
	{
	public:
		explicit FFlashNetworkScenario(FAutomationTestBase& InTest) : Test(InTest) {}

		bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 60.)
			{
				Test.AddError(FString::Printf(TEXT("双客户端投掷超时，阶段 %d"), Stage));
				return true;
			}
			UWorld* Server = nullptr;
			TArray<UWorld*, TInlineAllocator<2>> Clients;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (!World || Context.WorldType != EWorldType::PIE || !World->HasBegunPlay()) continue;
				if (World->GetNetMode() == NM_DedicatedServer) Server = World;
				if (World->GetNetMode() == NM_Client) Clients.Add(World);
			}
			auto* PC = Server ? Server->GetFirstPlayerController() : nullptr;
			auto* Pawn = PC ? Cast<AGinkgoCharacter>(PC->GetPawn()) : nullptr;
			if (!Pawn || !Pawn->GetPlayerState() || Clients.Num() != 2) return false;
			TActorIterator<AGinkgoAICharacter> Enemy(Server);
			if (!Enemy || !Enemy->GetController()) return false;
			if (Stage == 0)
			{
				// 测试期间保持同一拥有者，避免引信倒计时内被敌人击杀后换 Pawn。
				Pawn->GetDamageable()->InitializeVitals(100000.f, {});
				Pawn->GetFlashbang()->RestoreInventory(true);
				++Stage;
			}
			AGinkgoCharacter* Owner = nullptr;
			for (UWorld* World : Clients)
			{
				AGinkgoCharacter* Copy = nullptr;
				for (TActorIterator<AGinkgoCharacter> It(World); It; ++It)
					if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId() == Pawn->GetPlayerState()->GetPlayerId()) { Copy = *It; break; }
				if (!Copy) return false;
				if (Copy->IsLocallyControlled()) Owner = Copy;
				const auto Phase = Copy->GetFlashbang()->GetPhase();
				if (Stage == 1 && Phase != EGinkgoFlashPhase::Belt) return false;
				if (Stage == 2)
				{
					if (Phase != EGinkgoFlashPhase::Hold) return false;
					if (Copy->IsLocallyControlled() != (Copy->FindComponentByClass<ULineBatchComponent>() != nullptr)) return false;
				}
				if (Stage == 3 && (Phase != EGinkgoFlashPhase::Empty || !TActorIterator<AGinkgoFlashProjectile>(World))) return false;
				if (Stage >= 4)
				{
					AGinkgoAICharacter* Replica = nullptr;
					for (TActorIterator<AGinkgoAICharacter> It(World); It; ++It)
						if (It->GetFName() == Enemy->GetFName()) { Replica = *It; break; }
					if (!Replica || Replica->bCoverEyes != (Stage == 4)) return false;
				}
			}
			if (!Owner) return false;
			if (Stage == 1) Owner->GetFlashbang()->Press();
			else if (Stage == 2)
			{
				Test.TestTrue(TEXT("权威端与两台客户端均持在胸前"), Pawn->GetFlashbang()->GetPhase() == EGinkgoFlashPhase::Hold);
				Owner->GetController()->SetControlRotation(FRotator(-60., 0., 0.));
				Enemy->SetActorLocation(Pawn->GetActorLocation() + FVector(0., 350., 0.));
				Enemy->GetCharacterMovement()->DisableMovement();
				Owner->GetFlashbang()->Release();
			}
			else if (Stage == 3)
			{
				TActorIterator<AGinkgoFlashProjectile> Projectile(Server);
				if (!Projectile) return false;
				Test.TestFalse(TEXT("远端投出后服务器腰槽已消耗"), Pawn->GetFlashbang()->HasItem());
				Test.TestFalse(TEXT("两台客户端已有飞行物时敌人尚未受闪"), Enemy->bCoverEyes);
				LaunchTime = Server->GetTimeSeconds() - Projectile->GetGameTimeSinceCreation();
			}
			else if (Stage == 4) Test.TestTrue(TEXT("脱手两秒后两台客户端均收到遮眼状态"),
				Server->GetTimeSeconds() - LaunchTime >= AGinkgoFlashProjectile::FuseSeconds - .05);
			else if (Stage == 5) return true;
			++Stage;
			return false;
		}

	private:
		/** 接收断言与超时信息。 */
		FAutomationTestBase& Test;

		/** 不受 PIE 暂停影响的实时时钟起点（秒）。 */
		double Started = FPlatformTime::Seconds();

		/** 当前等待同步的操作。 */
		int32 Stage = 0;

		/** 权威端实际生成飞行物的世界时间（秒），用于和客户端收到受闪状态的时间对照。 */
		double LaunchTime = 0.;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoFlashbangNetTest, "CaseGinkgo.Flashbang.Network.OwnerAndObserver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoFlashbangNetTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/AI/Maps/L_AIPrototype")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartFlashNetworkPIE());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFlashNetworkScenario>(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
