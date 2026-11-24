#pragma once

#include "CoreMinimal.h"
#include "Combat/GinkgoCombatTypes.h"
#include "Framework/GinkgoDifficulty.h"
#include "Serialization/StructuredArchive.h"
#include "Weapon/GinkgoWeaponSaveState.h"
#include "GinkgoCheckpointState.generated.h"

class USoundBase;

/** 本地玩家的地点标注请求；存档结果已确定，界面只延后反馈，不负责写档。 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoCheckpointAnnouncement
{
	GENERATED_BODY()

	/** 屏幕中央的地点名，支持本地化；空值由触发器拒绝。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Location")
	FText LocationName;

	/** 地点标注开始时的二维音效；默认 MetaSound 从六段 take 随机挑选，跨播放实例避免连续重复。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Location")
	TSoftObjectPtr<USoundBase> Sound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Checkpoint/MS_Checkpoint_Location.MS_Checkpoint_Location")));

	/** 本次文件是否成功写入；失败也展示地点，随后显示失败提示而非“已存档”。 */
	UPROPERTY()
	bool bSaved = false;
};

/** 检查点节点的持久数据。坐标属于 MapPackage 的世界；敌人/触发器标识包含关卡路径，不保存 PIE 前缀。 */
USTRUCT()
struct CASEGINKGO_API FGinkgoCheckpointState
{
	GENERATED_BODY()

	/** 是否已经成功抵达过检查点；旧存档缺少此节点时为 false。 */
	UPROPERTY(SaveGame)
	bool bHasCheckpoint = false;

	/** 最新检查点标识；显式 ID 或关卡 Actor 的稳定软路径。 */
	UPROPERTY(SaveGame)
	FString CheckpointId;

	/** 最新检查点所属持久地图的包名，不含 PIE 前缀。 */
	UPROPERTY(SaveGame)
	FString MapPackage;

	/** 最近一次成功激活检查点的地点名，供暂停菜单顶部展示；旧存档缺省时为空文本。 */
	UPROPERTY(SaveGame)
	FText LocationName;

	/** 复活胶囊中心与水平朝向的世界变换；不是触发时玩家的当前位置，缩放固定为 1。 */
	UPROPERTY(SaveGame)
	FTransform RespawnTransform;

	/** 已成功存档的检查点，避免进出体积或读档出生时重复标注。 */
	UPROPERTY(SaveGame)
	TArray<FString> ReachedCheckpoints;

	/** 已击杀敌人的稳定标识；死亡时立即加入工作状态，下一次存档才持久化。 */
	UPROPERTY(SaveGame)
	TArray<FString> DefeatedEnemies;

	/** 抵达时玩家生命上限，单位生命点数。 */
	UPROPERTY(SaveGame)
	float MaxHealth = 100.f;

	/** 抵达时的剩余生命；只允许活着的玩家激活检查点。 */
	UPROPERTY(SaveGame)
	float Health = 100.f;

	/** 护甲装备与各件实际耐久，恢复时不自动补满。 */
	UPROPERTY(SaveGame)
	TArray<FGinkgoArmorPiece> Armor;

	/** 玩家当时的武器、弹药、配件与持枪槽。 */
	UPROPERTY(SaveGame)
	FGinkgoWeaponSaveState Weapons;

	/** 存档时尚未脱手的闪光弹，读档统一放回腰侧。旧档默认空位。 */
	UPROPERTY(SaveGame)
	bool bHasFlashbang = false;

	/** 该次游玩在主菜单选定的战斗难度；由 bHasSavedDifficulty 区分旧版无难度字段的存档。 */
	UPROPERTY(SaveGame)
	EGinkgoDifficulty Difficulty = EGinkgoDifficulty::Balanced;

	/** 存档是否记录了有效难度档位；为 true 时读取检查点同步恢复到 UGinkgoTitleSubsystem。 */
	UPROPERTY(SaveGame)
	bool bHasSavedDifficulty = false;
};

/** 交给项目存档代理序列化 SaveGame 属性；嵌套状态也显式标记 SaveGame，保持二进制与 JSON 一致。 */
inline void operator<<(FStructuredArchive::FSlot Slot, FGinkgoCheckpointState& State)
{
	FGinkgoCheckpointState::StaticStruct()->SerializeItem(Slot, &State, nullptr);
}
