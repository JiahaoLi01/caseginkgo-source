#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "GinkgoDialogueSet.generated.h"

class USoundBase;

/** 一次播报的本地化内容。服务器选定变体，所有听众收到同一句。 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoDialogueLine
{
	GENERATED_BODY()

	/** 字幕正文；空文本且无语音的条目不参与选取。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText Text;

	/** 可选人声，随本句在说话位置播放；空引用用于纯字幕。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<USoundBase> Voice = nullptr;

	/** 字幕停留的游戏时间（秒），可按每句录音或阅读时长调整。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.2", Units="s"))
	float Duration = 2.6f;
};

USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoDialogueCue
{
	GENERATED_BODY()

	/** 同一角色重复响应该事件的最短间隔（游戏秒）；0 允许每次事件触发。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", Units="s"))
	float Cooldown = 6.f;

	/** 可选台词；多句时从有效条目中随机选择，避免连续重复同一句。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FGinkgoDialogueLine> Lines;
};

/** 对话组件使用的共享台词表。事件键也是组件实际订阅的标签，设计师可为不同角色指定独立资产。 */
UCLASS(BlueprintType)
class CASEGINKGO_API UGinkgoDialogueSet : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 字幕的说话人名称；留空只显示正文。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText Speaker;

	/** 从说话角色到听众的最大世界距离（cm），0 表示只在重合位置可听。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", Units="cm"))
	float AudibleRadius = 3000.f;

	/** Actor 事件标签对应的候选句与冷却，两名角色使用同一资产时各自记录播放进度。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TMap<FGameplayTag, FGinkgoDialogueCue> Cues;
};
