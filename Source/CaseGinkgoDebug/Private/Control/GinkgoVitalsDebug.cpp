#include "Combat/GinkgoAmmoDefinition.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UI/GinkgoVitals.h"
#include "Utilities/Debug/DebugCategory.h"

namespace
{
	/** 开发版 HUD 实测：全部通过实际伤害组件，不向页面注入伪造生命值；定时项随世界销毁自动释放。 */
	void RunVitalsInput(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.IsEmpty()) { return; }
		if (Args[0].StartsWith(TEXT("at=")))
		{
			const float Delay = FMath::Max(0.001f, FCString::Atof(*Args[0].RightChop(3)) - World->GetTimeSeconds());
			TArray<FString> Rest = Args;
			Rest.RemoveAt(0);
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([Weak = TWeakObjectPtr<UWorld>(World), Rest]()
			{
				RunVitalsInput(Rest, Weak.Get());
			}), Delay, false);
			return;
		}
		APlayerController* PC = World->GetFirstPlayerController();
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		auto* Target = Pawn ? Pawn->FindComponentByClass<UGinkgoDamageableComponent>() : nullptr;
		if (!Target) { return; }
		if (Args[0] == TEXT("revive")) { Target->Revive(); }
		else if (Args[0] == TEXT("armor"))
		{
			TArray<FGinkgoArmorPiece> Pieces;
			for (int32 Index = 1; Index < FMath::Min(Args.Num(), 3); ++Index)
			{
				const float Amount = FMath::Max(0.f, FCString::Atof(*Args[Index]));
				if (Amount <= 0.f) { continue; }
				FGinkgoArmorPiece& Piece = Pieces.AddDefaulted_GetRef();
				Piece.Name = Index == 1 ? TEXT("Vest") : TEXT("Helmet");
				Piece.MaxDurability = Amount;
				Piece.Covers = Index == 1 ? TArray<EGinkgoBodyPart>{EGinkgoBodyPart::UpperChest, EGinkgoBodyPart::Abdomen} : TArray<EGinkgoBodyPart>{EGinkgoBodyPart::Head};
			}
			Target->SetArmor(Pieces);
		}
		else if (Args[0] == TEXT("hit") && Args.Num() > 1)
		{
			auto* Ammo = NewObject<UGinkgoAmmoDefinition>();
			Ammo->Damage = FMath::Max(0.f, FCString::Atof(*Args[1]));
			Ammo->ArmorDamage = Ammo->Damage;
			Ammo->ArmorPenetration = 0.f;
			Target->ApplyBulletHit(*Ammo, 1.f, FHitResult(), FVector::ForwardVector, 0.f, nullptr);
		}
		const FGinkgoVitals State = FGinkgoVitals::Read(Target);
		UE_LOG(LogGinkgoUI, Log, TEXT("[VitalsTest] %s: health=%.2f/%.2f armor=%.2f/%.2f armorVisible=%d"),
			*FString::Join(Args, TEXT(" ")), State.Health, State.MaxHealth, State.Armor, State.MaxArmor, State.HasArmor());
	}

	FAutoConsoleCommandWithWorldAndArgs VitalsTestCommand(TEXT("ginkgo.vitals.test"),
		TEXT("生命 HUD 实测：[at=<世界秒>] revive | armor <耐久> [额外装备耐久] | hit <伤害>。玩家全身共用总耐久，armor 0 卸甲；只在开发模块存在。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunVitalsInput));
}
