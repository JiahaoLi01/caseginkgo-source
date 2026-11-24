#pragma once

#include "CoreMinimal.h"
#include "Framework/GinkgoDifficulty.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GinkgoTitleSubsystem.generated.h"

class UWorld;

/**
 * @class UGinkgoTitleSubsystem : 会话级开屏与主菜单流转状态
 * 挂在 UGameInstance 上，跨关卡重载保持有效（例如「继续游戏」读取检查点、死亡复活重载或「回到主菜单」后再次点「新游戏」）。
 * 职责仅限于记录本会话是否已经历主菜单以及下一次地图载入的开屏策略；实际界面呈现与音频播放由 FGinkgoMainMenu 负责。
 */
UCLASS()
class CASEGINKGO_API UGinkgoTitleSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** 按上下文获取当前 GameInstance 对应的标题状态子系统；世界或游戏实例无效时返回 nullptr */
	static UGinkgoTitleSubsystem* Get(const UObject* Context);

	/**
	 * @brief 决定本地玩家控制器在 BeginPlay 时是否应自动拉起开屏/主菜单，以及是否从开屏镜头开始
	 * @param World - 当前正进入的游戏世界，用于过滤专用服务器等环境
	 * @param OutStartWithSplash - 输出是否先播放两镜开屏动画；为 false 时直接停留在主菜单选项页
	 * @return 应自动显示标题界面返回 true；无人值守自动化测试、-GinkgoSkipTitle 或已跳过重载返回 false
	 * @note 消费一次性的 SkipTitleNextLoad 与 StartAtMainMenuNoSplash 标记，仅在本地控制器 BeginPlay 调用一次
	 */
	bool ConsumeAutoShowDecision(const UWorld* World, bool& OutStartWithSplash);

	/** 标记本会话已正式进入可操作游戏状态；后续切关或读检查点默认不再重放开屏动画 */
	void MarkEnteredGameplay();

	/** 标记下一次地图重载跳过标题界面（用于「继续游戏」读检查点、死亡检查点复活或「新游戏」清场重载） */
	void RequestSkipTitleOnNextLoad();

	/** 记录从暂停菜单返回主菜单：下次若点击「新游戏」需重载关卡还原初始状态，且再次打开主菜单跳过开屏 */
	void MarkReturnedFromGameplay();

	/** 清除「新游戏需要重载地图」的脏状态（完成洁净地图加载后重置） */
	void ClearFreshReloadRequirement();

	/** 当前世界是否因先前实际游玩而变脏；为 true 时点击「新游戏」应重载当前地图而非原地解除遮罩 */
	bool NeedsFreshReloadForNewGame() const { return bNeedsFreshReloadForNewGame; }

	/** 本会话是否已经至少进入过一次实际游玩 */
	bool HasEnteredGameplay() const { return bHasEnteredGameplay; }

	/** 设置当前局内战斗难度并标记为已激活，由主菜单「新游戏」选择、检查点读档恢复或测试调用 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Difficulty")
	void SetDifficulty(EGinkgoDifficulty NewDifficulty);

	/** 返回当前选定或恢复的战斗难度档位（默认 Balanced 平衡） */
	UFUNCTION(BlueprintPure, Category = "Ginkgo|Difficulty")
	EGinkgoDifficulty GetDifficulty() const { return CurrentDifficulty; }

	/**
	 * @brief 返回当前局内玩家承受伤害的线性倍率（0.10 娱乐 / 0.50 平衡 / 1.00 地狱）
	 * @note 无人值守自动化测试未显式调用 SetDifficulty 时返回 1.00，避免改写既有战斗与检查点单元测试的基准数值
	 */
	UFUNCTION(BlueprintPure, Category = "Ginkgo|Difficulty")
	float GetPlayerIncomingDamageScale() const;

	/** 重置所有会话状态与难度标记，供自动化测试在独立用例前后恢复初态 */
	void ResetSessionState();

private:
	/** 本会话是否已进入过实际游玩；首次启动为 false，点击新游戏或继续游戏后置 true */
	bool bHasEnteredGameplay = false;

	/** 一次性跳过标记：置 true 时下一次 BeginPlay 直接进入玩法并自动清零 */
	bool bSkipTitleNextLoad = false;

	/** 一次性直达主菜单标记：置 true 时下一次自动拉起直接跳过开屏进入主菜单选项并自动清零 */
	bool bStartAtMainMenuNoSplash = false;

	/** 当前加载的世界是否已经经历过玩家实际游玩；为 true 时点击「新游戏」执行重载地图以彻底重置关卡 */
	bool bNeedsFreshReloadForNewGame = false;

	/** 当前生效的游戏战斗难度；默认平衡（-50% 承伤），由新游戏面板选择或检查点读档覆盖 */
	EGinkgoDifficulty CurrentDifficulty = EGinkgoDifficulty::Balanced;

	/** 本会话是否已显式选定或从存档恢复难度；无人值守自动化测试未置位时保持 1.0 倍原始伤害 */
	bool bDifficultyExplicitlySet = false;
};
