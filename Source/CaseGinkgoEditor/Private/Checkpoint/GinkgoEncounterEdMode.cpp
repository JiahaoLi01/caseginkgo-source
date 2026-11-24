#include "Checkpoint/GinkgoEncounterEdMode.h"

#include "AI/GinkgoAICharacter.h"
#include "Checkpoint/GinkgoCheckpoint.h"
#include "Checkpoint/GinkgoCheckpointEnemySpawn.h"
#include "Components/CapsuleComponent.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "EngineUtils.h"
#include "Framework/Notifications/NotificationManager.h"
#include "LevelUtils.h"
#include "PrimitiveDrawingUtils.h"
#include "SceneManagement.h"
#include "ScopedTransaction.h"
#include "Selection.h"
#include "Styling/AppStyle.h"
#include "Toolkits/BaseToolkit.h"
#include "Weapon/GinkgoAmmoPickup.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "GinkgoEncounterEdMode"

namespace
{
	class FGinkgoEncounterToolkit : public FModeToolkit
	{
	public:
		virtual FName GetToolkitFName() const override { return TEXT("GinkgoEncounter"); }
		virtual FText GetBaseToolkitName() const override { return LOCTEXT("Mode", "Checkpoint 敌人布置"); }

		virtual TSharedPtr<SWidget> GetInlineContent() const override
		{
			return SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(8.f)
				[
					SNew(STextBlock).AutoWrapText(true)
					.Text(LOCTEXT("Usage", "按住 Shift 单击地面放置。放敌人前先选 Checkpoint，追加巡逻点前先选出生位。「放置子弹盒」可选择弹种，直接放入当前编辑层。"))
				]
				+ SVerticalBox::Slot().AutoHeight()[FModeToolkit::GetInlineContent().ToSharedRef()];
		}
	};

	AActor* GetSelectedActor()
	{
		USelection* Selection = GEditor->GetSelectedActors();
		return Selection->Num() == 1 ? Selection->GetTop<AActor>() : nullptr;
	}
}

UGinkgoEncounterBrushSettings::UGinkgoEncounterBrushSettings()
{
	AmmoBoxClass = AGinkgoAmmoPickup::StaticClass();
}

UGinkgoEncounterEdMode::UGinkgoEncounterEdMode()
{
	Info = FEditorModeInfo(TEXT("EM_GinkgoEncounter"), LOCTEXT("Mode", "Checkpoint 敌人布置"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Character"), true);
	SettingsClass = UGinkgoEncounterBrushSettings::StaticClass();
}

void UGinkgoEncounterEdMode::CreateToolkit()
{
	Toolkit = MakeShared<FGinkgoEncounterToolkit>();
}

void UGinkgoEncounterEdMode::Enter()
{
	Super::Enter();
	ActorSelectionChangeNotify();
}

void UGinkgoEncounterEdMode::ActorSelectionChangeNotify()
{
	Super::ActorSelectionChangeNotify();
	auto* Brush = GetBrushSettings();
	if (!Brush) { return; }
	AActor* Selected = GetSelectedActor();
	if (auto* Point = Cast<AGinkgoCheckpoint>(Selected)) { Brush->Checkpoint = Point; }
	else if (auto* Marker = Cast<AGinkgoCheckpointEnemySpawn>(Selected); Marker && Marker->Checkpoint)
	{
		Brush->Checkpoint = Marker->Checkpoint;
	}
}

bool UGinkgoEncounterEdMode::HandleClick(FEditorViewportClient* Client, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (Click.GetKey() != EKeys::LeftMouseButton || !Click.IsShiftDown() || Click.IsAltDown() || GEditor->PlayWorld)
	{
		return Super::HandleClick(Client, HitProxy, Click);
	}
	UWorld* World = Client->GetWorld();
	if (!World || World->WorldType != EWorldType::Editor) { return false; }
	FHitResult Hit;
	FText Error;
	if (!World->LineTraceSingleByChannel(Hit, Click.GetOrigin(), Click.GetOrigin() + Click.GetDirection() * HALF_WORLD_MAX,
		ECC_Visibility, FCollisionQueryParams(SCENE_QUERY_STAT(GinkgoEncounterPlacement), true)) || Hit.ImpactNormal.Z < .5)
	{
		Error = LOCTEXT("NoGround", "请点击可站立的地面。");
	}
	else { PlaceAtGround(*World, Hit.ImpactPoint, Error); }
	if (!Error.IsEmpty()) { FSlateNotificationManager::Get().AddNotification(FNotificationInfo(Error)); }
	return true;
}

bool UGinkgoEncounterEdMode::PlaceAtGround(UWorld& World, const FVector& Ground, FText& Error)
{
	const auto* Brush = GetBrushSettings();
	if (!Brush || World.WorldType != EWorldType::Editor || GEditor->PlayWorld)
	{
		Error = LOCTEXT("EditorOnly", "请在关卡编辑视口中放置。");
		return false;
	}
	Error = FText::GetEmpty();
	if (Brush->Brush == EGinkgoEncounterBrush::AmmoBox)
	{
		ULevel* Level = World.GetCurrentLevel();
		if (!Level || FLevelUtils::IsLevelLocked(Level) || !FLevelUtils::IsLevelVisible(Level))
		{
			Error = LOCTEXT("AmmoLevel", "请先显示并解锁当前编辑层。");
			return false;
		}
		if (!Brush->AmmoBoxClass)
		{
			Error = LOCTEXT("NoAmmoClass", "请先选择子弹盒类型。");
			return false;
		}
		const FScopedTransaction Transaction(LOCTEXT("AddAmmo", "放置子弹盒"));
		auto* Box = Cast<AGinkgoAmmoPickup>(GEditor->AddActor(Level, Brush->AmmoBoxClass, FTransform(Ground)));
		if (!Box) { Error = LOCTEXT("AmmoFailed", "未能在当前编辑层放置子弹盒。"); return false; }
		if (Brush->AmmoRounds > 0)
		{
			Box->Modify();
			Box->SetRounds(Brush->AmmoRounds);
			Box->PostEditChange();
		}
		GEditor->RedrawLevelEditingViewports();
		return true;
	}
	auto* Point = Brush->Checkpoint.Get();
	if (!Point || Point->GetWorld() != &World)
	{
		Error = LOCTEXT("NoCheckpoint", "请先选择当前关卡中的 Checkpoint。");
		return false;
	}
	if (FLevelUtils::IsLevelLocked(Point) || !FLevelUtils::IsLevelVisible(Point->GetLevel()))
	{
		Error = LOCTEXT("Locked", "请先显示并解锁 Checkpoint 所在子关卡。");
		return false;
	}
	if (Brush->Brush == EGinkgoEncounterBrush::Spawn)
	{
		const FScopedTransaction Transaction(LOCTEXT("AddEnemy", "放置 Checkpoint 敌人"));
		const FTransform Transform(FRotator(0., Point->GetActorRotation().Yaw, 0.), Ground);
		auto* Marker = Cast<AGinkgoCheckpointEnemySpawn>(GEditor->AddActor(Point->GetLevel(),
			AGinkgoCheckpointEnemySpawn::StaticClass(), Transform));
		if (!Marker) { Error = LOCTEXT("AddFailed", "未能在该子关卡放置出生位。"); return false; }
		Marker->Modify();
		Marker->Checkpoint = Point;
		Marker->WeaponOverride = Brush->Weapon;
		Marker->EnemyType = Brush->EnemyType;
		Marker->BehaviorOverrides = Brush->BehaviorOverrides;
		FActorLabelUtilities::SetActorLabelUnique(Marker, Point->GetActorLabel() + TEXT(" - Enemy"));
		Marker->PostEditChange();
	}
	else
	{
		auto* Marker = Cast<AGinkgoCheckpointEnemySpawn>(GetSelectedActor());
		if (!Marker || Marker->Checkpoint != Point || Marker->GetLevel() != Point->GetLevel())
		{
			Error = LOCTEXT("NoSpawn", "请先选中该 Checkpoint 下要编辑路线的敌人出生位。");
			return false;
		}
		const FScopedTransaction Transaction(LOCTEXT("AddPoint", "追加敌人巡逻点"));
		Marker->Modify();
		auto& Waypoint = Marker->Points.AddDefaulted_GetRef();
		Waypoint.Location = Marker->GetActorTransform().InverseTransformPosition(Ground);
		Waypoint.WaitSeconds = Brush->WaitSeconds;
		Marker->PostEditChange();
	}
	GEditor->RedrawLevelEditingViewports();
	return true;
}

void UGinkgoEncounterEdMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	Super::Render(View, Viewport, PDI);
	const auto* Brush = GetBrushSettings();
	auto* Point = Brush ? Brush->Checkpoint.Get() : nullptr;
	if (!Point || Point->GetWorld() != GetWorld()) { return; }
	for (TActorIterator<AGinkgoCheckpointEnemySpawn> It(GetWorld()); It; ++It)
	{
		if (It->Checkpoint != Point || It->IsHiddenEd()) { continue; }
		const FVector Start = It->GetActorLocation() + FVector(0., 0., 6.);
		PDI->DrawLine(Point->GetActorLocation(), Start, FLinearColor(.8f, .38f, .1f), SDPG_World, 1.f);
		if (const auto* Defaults = It->EnemyClass.GetDefaultObject())
		{
			const auto* Capsule = Defaults->GetCapsuleComponent();
			const double Height = Capsule->GetScaledCapsuleHalfHeight();
			const FVector Center = It->GetActorLocation() + FVector::UpVector * Height;
			if (PDI->IsHitTesting()) { PDI->SetHitProxy(new HActor(*It, nullptr)); }
			DrawWireCapsule(PDI, Center, FVector::ForwardVector, FVector::RightVector, FVector::UpVector,
				FLinearColor(.95f, .45f, .13f), Capsule->GetScaledCapsuleRadius(), Height, 12, SDPG_World);
			if (PDI->IsHitTesting()) { PDI->SetHitProxy(nullptr); }
		}
		if (It->Points.IsEmpty()) { continue; }
		const FVector First = It->GetPointLocation(0) + FVector(0., 0., 6.);
		PDI->DrawLine(Start, First, FLinearColor(.12f, .8f, .65f), SDPG_World, 2.f);
		for (int32 Index = 0; Index < It->Points.Num(); ++Index)
		{
			const FVector From = It->GetPointLocation(Index) + FVector(0., 0., 6.);
			const FVector To = It->GetPointLocation((Index + 1) % It->Points.Num()) + FVector(0., 0., 6.);
			PDI->DrawPoint(From, FLinearColor(.12f, .8f, .65f), 8.f, SDPG_Foreground);
			PDI->DrawLine(From, To, FLinearColor(.12f, .8f, .65f), SDPG_World, 2.f);
		}
	}
}

#undef LOCTEXT_NAMESPACE
