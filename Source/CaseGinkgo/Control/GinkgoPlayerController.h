#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GinkgoAimAssist.h"   // FGinkgoAimAssist：成员
#include "GinkgoDirectionKeys.h"
#include "GinkgoInputIntent.h"
#include "GinkgoInputMath.h"  // FGinkgoHoldActions：冲刺、蹲、开镜合起来开着没有
#include "UI/GinkgoToast.h"
#include "Checkpoint/GinkgoCheckpointState.h"
#include "Presentation/GinkgoCredit.h"
#include "Presentation/GinkgoDialogueSet.h"
#include "GinkgoPlayerController.generated.h"

class AGinkgoCharacter;
class FGinkgoInputReplay;
class FGinkgoInteractionPrompt;
class FGinkgoMainMenu;
class FGinkgoPauseMenu;
class FGinkgoSettingsPage;
class FGinkgoToastHud;
class FGinkgoLocationTitleHud;
class FGinkgoCreditsHud;
class FGinkgoSubtitleHud;
class FGinkgoGameplayHudLayer;
class SWidget;
class UGinkgoDamageableComponent;
struct FGinkgoDamageEvent;
class FGinkgoWeaponHud;
class FGinkgoWindowTimeline;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * @class AGinkgoPlayerController : 玩家控制器
 * 输入在这里建立：InputAction 与 MappingContext 由代码在运行时创建。视角与摇杆走玩家的设置（Docs/Design/Control.md）：映射上挂着读设置的修改器
 * （GinkgoInputModifiers），IA_Look 的值就是这一帧要转的度数。
 * 改键用 Enhanced Input 的系统：键盘与手柄各一张表（Ginkgo::Input::GetKeyBindingDefs），每个动作主键、辅助键两个键位，一个键位是一条可改键的映射；
 * 冲刺、蹲、开镜的每个键位还各选按住或切换（FGinkgoHoldActions），所以它们的每个键位各有一个 InputAction：按键事件不告诉是哪个键。
 * 相机管理器换成 AGinkgoPlayerCameraManager。
 *
 * 输入分两步：处理函数只把实时输入记成这一帧的意图（视角例外，直接改 ControlRotation，玩家输入的旋转不经插值），
 * PlayerTick 再把意图交给角色。调试用的回放（FGinkgoInputReplay，发行版不编译）用录制的意图代替实时输入，
 * 走的是同一段代码，所以回放与实时操作的效果一致。
 *
 * 默认键位：WASD 移动，鼠标视角，空格与滚轮下滚跳跃（滚轮便于连跳），Ctrl 按住蹲、C 切换蹲，Shift 切换冲刺，E 交互，鼠标右键开镜，1、2 第一把、第二把，F/4 手枪，3 收枪；手柄：左摇杆移动，右摇杆视角，A 跳，B 蹲，X 交互，
 * 按下左摇杆切换冲刺，左扳机开镜，十字下收枪，Y 切枪。不能改的：P 键切换 0.2 倍慢动作，F6 窗口时间轴，Esc 或 F10 暂停菜单（编辑器的 PIE 里 Esc 是停止运行，用 F10），手柄的菜单键（Xbox 的 ☰）暂停菜单。
 */
UCLASS()
class CASEGINKGO_API AGinkgoPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AGinkgoPlayerController();

#if !UE_BUILD_SHIPPING
	/** 调试用的输入录制与回放，第一次用到时才创建 */
	FGinkgoInputReplay& GetOrCreateInputReplay();
#endif

	/** 窗口时间轴，第一次打开时才创建：ginkgo.showWindowTimeline、F6、命令行 -GinkgoWindowTimeline。发行版里也有 */
	FGinkgoWindowTimeline& GetOrCreateWindowTimeline();

	/** 窗口时间轴；还没打开过时为空（武器的界面看它开着没有，开着时把武器栏挪到它上面） */
	const FGinkgoWindowTimeline* GetWindowTimeline() const { return WindowTimeline.Get(); }

	/** 开屏与主菜单控制器，首次进入游戏或从暂停菜单返回时按需创建。发行版里也有 */
	FGinkgoMainMenu& GetOrCreateMainMenu();

	/** 开屏画面或主菜单当前是否处于打开状态（含特性画廊与设置子页展开期间） */
	bool IsMainMenuOpen() const;

	/** 从局内返回主菜单：释放暂停态并标记下一次点击新游戏时重载洁净关卡，直接展示主菜单选项（跳过开屏） */
	void ReturnToMainMenu();

	/** 暂停菜单，首次打开时按需创建，提供继续、读检查点、返回主菜单、设置与退出入口。发行版里也有 */
	FGinkgoPauseMenu& GetOrCreatePauseMenu();

	/** 暂停菜单当前是否处于打开状态（含子级设置页展开期间） */
	bool IsPauseMenuOpen() const;

	/** 设置界面，第一次打开时才创建。发行版里也有 */
	FGinkgoSettingsPage& GetOrCreateSettingsPage();

	/** 设置界面当前是否处于打开状态 */
	bool IsSettingsPageOpen() const;

	/** 主菜单、暂停菜单或设置界面任一处于打开状态；玩法 HUD 暂停计时、工作台快捷键与输入模式恢复统一查这里 */
	bool IsMenuOpen() const;

	/** 慢动作激活时施加到世界的全局时间膨胀倍率（无量纲，0.2 表示五分之一速） */
	static constexpr float SlowMotionTimeDilation = 0.2f;

	/**
	 * @brief 开关 0.2 倍全局慢动作（由固定键 P 触发，不进入改键设置表）
	 * @param bEnable - 为真时将全局时间膨胀设为 0.2，为假时恢复 1.0 常速
	 * @param bNotify - 为真且状态确实变化时推送一条轻量 Toast 提示；返回主菜单、读检查点或退场重置时传假
	 */
	void SetSlowMotionEnabled(bool bEnable, bool bNotify = false);

	/** 当前控制器是否处于 0.2 倍慢动作激活状态；返回主菜单、重开检查点或 EndPlay 时自动复原为假 */
	bool IsSlowMotionEnabled() const { return bSlowMotionEnabled; }

	/**
	 * @brief 往 toast 的队列里加一条（Docs/Design/Control.md「Toast」）：玩法、教程、已存档之类的信息，一条一条显示
	 * 只有本地控制器显示，别的什么也不做
	 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|UI")
	void PushToast(const FGinkgoToast& Toast);

	/** 服务器完成存档后通知拥有者；地点显示与通知排队只在本地执行。 */
	UFUNCTION(Client, Reliable)
	void ClientAnnounceCheckpoint(const FGinkgoCheckpointAnnouncement& Announcement);

	/** 署名体积只向进入者发送内容；本地依次显示，不占用普通 HUD 的显示权，不触发存档。 */
	UFUNCTION(Client, Reliable)
	void ClientShowCredits(const TArray<FGinkgoCredit>& Credits);

	/** 附近角色的一次环境台词，丢失时不补播过时信息。语音与本地字幕开关独立。 */
	UFUNCTION(Client, Unreliable)
	void ClientHearDialogue(uint32 SpeakerId, FVector_NetQuantize Origin, const FText& Speaker, const FGinkgoDialogueLine& Line);

	/** 普通 HUD 查询队列是否占用显示权，用于暂停数据更新；显隐由共同父容器在队列边界处理。 */
	bool IsGameplayHudSuppressed() const;

	/** 普通 HUD 挂入统一容器；ZOrder 为容器内部顺序，地点标题和设置页不在此层。 */
	void AddGameplayHudWidget(const TSharedRef<SWidget>& Widget, int32 ZOrder);

	/** HUD 自己结束时解除挂载；不会创建不存在的容器，也不修改子控件的交互状态。 */
	void RemoveGameplayHudWidget(const TSharedRef<SWidget>& Widget);

	/** 手动复活/继续游戏入口，蓝图可调用；控制台输入 RestartCheckpoint 也走同一路径。 */
	UFUNCTION(Exec, BlueprintCallable, Category = "Checkpoint")
	void RestartCheckpoint();

	/** 辅助瞄准（Docs/Design/Control.md「辅助瞄准」）：调试画、测量读这一帧的样子 */
	const FGinkgoAimAssist& GetAimAssist() const { return AimAssist; }

	/** 最近用的设备：辅助瞄准按它选配置（键鼠、手柄各一套） */
	EGinkgoInputDirectory GetActiveDevice() const { return ActiveDevice; }

	/** 当前设备的安装快捷键。避开交互的主、副绑定，让离桌和安装各用一个键；菜单键帽也查这里 */
	FKey GetWorkbenchInstallKey() const;

protected:
	//~ Begin AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor

	//~ Begin APlayerController
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
	virtual void ReceivedPlayer() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	/** 辅助瞄准在这里：这一帧的视角输入（RotationInput）加进 ControlRotation 之前 */
	virtual void UpdateRotation(float DeltaTime) override;
	//~ End APlayerController

private:
	/** 集成回归读取实际 HUD 层与按需资源，不向运行时公开测试接口。 */
	friend class FGinkgoHudLifecycleScenario;
	friend class FGinkgoCreditsWorldScenario;

	friend class FGinkgoLocationTitleHud;
	friend class FGinkgoMainMenu;
	friend class FGinkgoPauseMenu;

	/** 仅地点队列、主菜单与暂停菜单边界刷新普通 HUD 父容器，不逐帧覆盖各 HUD 的 Slate 可见性。 */
	void RefreshGameplayHudVisibility();

	/** 设置子页关闭后的回调中转，交由暂停菜单恢复显隐与输入焦点。 */
	void HandleSettingsClosedFromPauseMenu();

	/** 设置子页关闭后的回调中转，交由主菜单恢复显隐与输入焦点（保持主菜单配乐不断流）。 */
	void HandleSettingsClosedFromMainMenu();

	/** 普通 HUD 的共同父容器；首次挂载时创建，各 HUD 注销后在 EndPlay 释放。 */
	TSharedPtr<FGinkgoGameplayHudLayer> GameplayHudLayer;

	/** 玩家首次死亡后安排读取检查点；无检查点时保留现有死亡行为。 */
	void HandlePlayerDied(UGinkgoDamageableComponent& Component, const FGinkgoDamageEvent& Event);

	/** 计时结束时复核同一 Pawn 仍然死亡，避免原地复活后被旧回调拉回检查点。 */
	void RestartIfStillDead();

	/** 当前 Pawn 的死亡订阅源；换 Pawn 时先解绑，避免旧角色影响新角色。 */
	TWeakObjectPtr<UGinkgoDamageableComponent> DeathSource;

	/** 权威端玩家死亡后保留 2 秒布娃娃再开始读档；EndPlay 与换 Pawn 时清理。 */
	FTimerHandle CheckpointRestartTimer;

	/** 首次收到地点请求时创建的轻量队列；空闲时不持有页面、控件或 Tick，EndPlay 释放。 */
	TSharedPtr<FGinkgoLocationTitleHud> LocationTitle;

	/** 首次署名请求时创建的本地队列；空闲时没有页面/控件/Tick，EndPlay 先于 HUD 父容器释放。 */
	TSharedPtr<FGinkgoCreditsHud> CreditsHud;

	/** 首句台词到达时创建；字幕全部到期后卸下页面，EndPlay 清理计时器。 */
	TSharedPtr<FGinkgoSubtitleHud> SubtitleHud;

	/** 创建输入动作与按键映射，并把映射加到本地玩家 */
	void CreateInputMappings();

	/** 把映射注册给 Enhanced Input 的改键系统，再把玩家的键位交过去。这个系统在本地玩家收到控制器（ReceivedPlayer）时才建好，比 SetupInputComponent 晚 */
	void RegisterKeyMappings();

	/** 回放中忽略实时输入 */
	bool IsReplaying() const;

	/**
	 * 把一帧的输入意图交给角色：移动、跳跃按下沿、方向键按下、蹲与冲刺是否按住、交互键按下沿、开镜是否按住（视角已经在 ControlRotation 里）；
	 * 再推进持枪状态（在移动之前：移动组件的 tick 排在控制器之后）
	 */
	void ApplyInput(const FGinkgoInputIntent& Input, float DeltaTime) const;

	/**
	 * @brief 记下键盘方向键（四个方向的主键与辅助键）这一帧的按下与按住，填进实时意图：按下了几个、按着的键合起来的朝向（后按的赢）
	 * @note 输入处理完之后调用。摇杆是模拟量，没有按下沿，lurch 只有键鼠能用就是从这里来的
	 */
	void UpdateDirectionKeys();

	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnJump(const FInputActionValue& Value);
	void OnInteract(const FInputActionValue& Value);
	void OnFlashPressed();
	void OnFlashReleased();
	void OnFlashCanceled();
	void OnSwitchShoulder(const FInputActionValue& Value);

	/** 换枪的键按下：第一把、第二把、收枪、切枪 */
	void OnWeaponKey(const FInputActionValue& Value, EGinkgoWeaponKey Key);

	/** @brief 开火键现在按着没有：连发的枪按着就接着打。问 Enhanced Input，改过键、手柄都对，打开界面时松手也对 */
	bool IsFireKeyHeld() const;

	/** @brief 某个武器的键现在按着没有（同 IsFireKeyHeld）：装填键按着打开弹种轮盘 */
	bool IsWeaponKeyHeld(EGinkgoWeaponKey Key) const;

	/** 弹种轮盘的鼠标（IA_WheelMouse）：这一帧动的量（度）攒进实时意图 */
	void OnWheelMouse(const FInputActionValue& Value);

	/** 蹲、冲刺、开镜的一个键位按下与松开：Binding 是键位的下标（GetKeyBindingDefs） */
	void OnHoldStarted(const FInputActionValue& Value, int32 Binding);
	void OnHoldCompleted(const FInputActionValue& Value, int32 Binding);

	/** 玩家的设置变了：键交给 Enhanced Input；方式可能换了，切换留下的状态不换算，清掉 */
	void OnInputSettingsChanged();

	/** F6：打开或关闭窗口时间轴。不是角色的输入，不进意图，回放中也照常响应 */
	void OnToggleWindowTimeline(const FInputActionValue& Value);

	/** Esc、F10、手柄的菜单键：打开暂停菜单（开着时由菜单或子设置页自己接这些键）。同上，不进意图 */
	void OnOpenSettings(const FInputActionValue& Value);

	/** 固定键 P：切换 0.2 倍慢动作，再按一次恢复常速；不进入改键系统，菜单打开期间忽略 */
	void OnToggleSlowMotion(const FInputActionValue& Value);

	AGinkgoCharacter* GetGinkgoCharacter() const;

	/** 这一帧的实时输入意图；移动、跳跃与交互的按下沿、方向键按下每帧清零，蹲、冲刺与开镜看 HoldActions */
	FGinkgoInputIntent LiveInput;

	/** 蹲、冲刺、开镜合起来开着没有：按住的方式下就是按着没有 */
	FGinkgoHoldActions HoldActions;

	/** 玩家的设置变了时的订阅 */
	FDelegateHandle InputSettingsChangedHandle;

	/** 键盘方向键的按住状态与按下的先后：lurch 的方向在这里按后按的赢合出来 */
	FGinkgoDirectionKeys DirectionKeyState;

	/** @brief 辅助瞄准这一帧的修正（度）：加到 RotationInput 上。手上没枪、不是本地控制时是零 */
	FRotator UpdateAimAssist(float DeltaTime);

	FGinkgoAimAssist AimAssist;

	/** 最近用的设备（InputKey 里看） */
	EGinkgoInputDirectory ActiveDevice = EGinkgoInputDirectory::KeyboardMouse;

	/** 是否处于按 P 开启的 0.2 倍全局慢动作状态；返回主菜单、读检查点及 EndPlay 时重置为假并恢复 1.0 倍速 */
	bool bSlowMotionEnabled = false;

#if !UE_BUILD_SHIPPING
	/** 这里只有前向声明，所以用共享指针：删除器在创建时确定，析构控制器时不需要完整类型 */
	TSharedPtr<FGinkgoInputReplay> InputReplay;
#endif

	/** 同上，用共享指针；EndPlay 时放掉，控件不留在视口上 */
	TSharedPtr<FGinkgoWindowTimeline> WindowTimeline;

	/** 交互的提示：本地控制器开始时创建，同样用共享指针、EndPlay 时放掉 */
	TSharedPtr<FGinkgoInteractionPrompt> InteractionPrompt;

	/** 武器的界面（武器栏与准星）：同交互的提示 */
	TSharedPtr<FGinkgoWeaponHud> WeaponHud;

	/** toast：同交互的提示 */
	TSharedPtr<FGinkgoToastHud> ToastHud;

	/** 开屏与主菜单：首次启动或从暂停菜单返回时按需创建，EndPlay 时卸载视图并停播配乐 */
	TSharedPtr<FGinkgoMainMenu> MainMenu;

	/** 暂停菜单：同窗口时间轴，首次打开时按需创建、EndPlay 时释放并解除暂停 */
	TSharedPtr<FGinkgoPauseMenu> PauseMenu;

	/** 设置界面：同窗口时间轴，第一次打开时才创建、EndPlay 时放掉 */
	TSharedPtr<FGinkgoSettingsPage> SettingsPage;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookAction;

	/** 弹种轮盘（Docs/Design/Weapons.md「切换弹种」）：鼠标动的量、视角那根摇杆，各自一个动作（同 IA_Look 用一样的键，换算不同：不反转、不加速） */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> WheelMouseAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> WheelStickAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> InteractAction;

	/** 左右肩切换动作：绑定可改键的 Started 按下沿，不随长按每帧切换 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SwitchShoulderAction;

	/** 左手投掷的按下与释放共享同一映射，取消输入时物品回腰。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> FlashAction;

	/** 第一把、第二把、收枪、切枪：顺序同 .cpp 里的 WeaponKeyDefs */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> WeaponKeyActions;

	/** 冲刺、蹲、开镜：每个键位一个动作，下标同 GetKeyBindingDefs；别的键位是空的 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> HoldInputActions;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ToggleWindowTimelineAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> OpenSettingsAction;

	/** 固定键 P 的 0.2 倍慢动作开关动作（不注册进改键表，不随改键配置覆盖） */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ToggleSlowMotionAction;
};
