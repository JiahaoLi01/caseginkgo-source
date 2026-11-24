#pragma once

#include "AI/GinkgoAIBehaviorOverrides.h"
#include "Tools/LegacyEdModeWidgetHelpers.h"
#include "GinkgoEncounterEdMode.generated.h"

class UGinkgoEnemyArchetype;

class AGinkgoCheckpoint;
class AGinkgoAmmoPickup;
class AGinkgoCheckpointEnemySpawn;
class UGinkgoWeaponDefinition;

UENUM()
enum class EGinkgoEncounterBrush : uint8
{
	Spawn UMETA(DisplayName = "放置敌人"),
	Patrol UMETA(DisplayName = "追加巡逻点"),
	AmmoBox UMETA(DisplayName = "放置子弹盒")
};

/** 当前编辑会话的放置参数。切换关卡或退出模式后不持有关卡 Actor。 */
UCLASS(Transient)
class UGinkgoEncounterBrushSettings : public UObject
{
	GENERATED_BODY()

public:
	UGinkgoEncounterBrushSettings();

	/** Shift 加单击的操作。普通选择、变换控件与撤销仍由关卡编辑器处理。 */
	UPROPERTY(EditAnywhere, Category = "布置", meta = (DisplayName = "操作"))
	EGinkgoEncounterBrush Brush = EGinkgoEncounterBrush::Spawn;

	/** 新出生位的归属，选择已有出生位时也会显示其检查点。 */
	UPROPERTY(EditAnywhere, Category = "布置", meta = (DisplayName = "Checkpoint"))
	TWeakObjectPtr<AGinkgoCheckpoint> Checkpoint;

	/** 新放置敌人的起始武器，留空使用 AI 默认。已有敌人可在出生位详情中单独修改。 */
	UPROPERTY(EditAnywhere, Category = "布置", meta = (DisplayName = "新敌人武器", EditCondition = "Brush == EGinkgoEncounterBrush::Spawn", EditConditionHides))
	TSoftObjectPtr<UGinkgoWeaponDefinition> Weapon;

	/** 新出生位的外观与防具类型。空值表示沿用敌人类默认的类型。 */
	UPROPERTY(EditAnywhere, Category = "布置", meta = (DisplayName = "新敌人类型", EditCondition = "Brush == EGinkgoEncounterBrush::Spawn", EditConditionHides))
	TSoftObjectPtr<UGinkgoEnemyArchetype> EnemyType;

	/** 新出生位的行为覆盖，每次放置时复制到标记，可在标记详情中继续修改。 */
	UPROPERTY(EditAnywhere, Category = "布置", meta = (DisplayName = "新敌人行为", EditCondition = "Brush == EGinkgoEncounterBrush::Spawn", EditConditionHides))
	FGinkgoAIBehaviorOverrides BehaviorOverrides;

	/** 弹种、模型与拾取提示由子弹盒预制体提供。默认类使用项目的 7.92 mm 子弹盒。 */
	UPROPERTY(EditAnywhere, Category = "布置", meta = (DisplayName = "子弹盒类型", AllowAbstract = "false", EditCondition = "Brush == EGinkgoEncounterBrush::AmmoBox", EditConditionHides))
	TSubclassOf<AGinkgoAmmoPickup> AmmoBoxClass;

	/** 每盒固定发数；0 保留预制体自己的固定发数或开局随机范围。 */
	UPROPERTY(EditAnywhere, Category = "布置", meta = (DisplayName = "固定发数", ClampMin = "0", EditCondition = "Brush == EGinkgoEncounterBrush::AmmoBox", EditConditionHides))
	int32 AmmoRounds = 0;

	/** 新巡逻点的停留时间（秒）。零表示直接走向下一点。 */
	UPROPERTY(EditAnywhere, Category = "布置", meta = (DisplayName = "新巡逻点停留", Units = "s", ClampMin = "0", EditCondition = "Brush == EGinkgoEncounterBrush::Patrol", EditConditionHides))
	float WaitSeconds = 0.f;
};

/** 关卡编辑模式。出生位绑定检查点，子弹盒直接摆入当前编辑层，三种放置操作均支持撤销。 */
UCLASS()
class UGinkgoEncounterEdMode : public UBaseLegacyWidgetEdMode
{
	GENERATED_BODY()

public:
	UGinkgoEncounterEdMode();
	virtual void Enter() override;
	virtual void ActorSelectionChangeNotify() override;
	virtual bool UsesPropertyWidgets() const override { return true; }
	virtual bool HandleClick(FEditorViewportClient* Client, HHitProxy* HitProxy, const FViewportClick& Click) override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;

	/** 将视口拾取到的世界地面位置（cm）写入当前操作。失败时返回可直接显示的原因。 */
	bool PlaceAtGround(UWorld& World, const FVector& Ground, FText& Error);

	/** 当前工具面板绑定的临时参数，仅在模式已进入时有效。 */
	UGinkgoEncounterBrushSettings* GetBrushSettings() const { return Cast<UGinkgoEncounterBrushSettings>(SettingsObject); }

protected:
	virtual void CreateToolkit() override;
};
