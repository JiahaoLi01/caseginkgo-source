#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GinkgoCrouchMeshBlend.h"
#include "Weapon/GinkgoWeaponTypes.h"    // FGinkgoBoltPose
#include "GinkgoCharacter.generated.h"

enum class EGinkgoHand : uint8;
class UGinkgoAnimAction;
class UGinkgoAnimActionComponent;
class UGinkgoCameraContextComponent;
class UGinkgoPlayerCameraComponent;
class AGinkgoWorkbench;
class UGinkgoCharacterAudioComponent;
class UGinkgoCharacterMovementComponent;
class UGinkgoTraversalComponent;
class UMotionWarpingComponent;
class UGinkgoInteractorComponent;
class UGinkgoPenetrableComponent;
class UGinkgoDamageableComponent;
class UGinkgoNoiseEmitterComponent;
class UGinkgoFlashbangComponent;
class UGinkgoWeaponComponent;
class UGinkgoWeaponDefinition;
class UPoseableMeshComponent;
class USkeletalMesh;
class UGinkgoCharacterModel;
class AGinkgoPracticeTarget;
struct FGinkgoMechanismState;
struct FGinkgoTargetSpawn;
class UStaticMeshComponent;

/**
 * 武器的蒙太奇里的事件（PlayMontageNotify 的名字），命令 GinkgoAnimMontage 按片段的 Weapon_OnBack 曲线生成：
 * OnBack 是收枪时枪落到背上的那一帧（曲线第一次为 1），InHand 是拔枪时枪离开背之前的最后一帧（曲线最后一次为 1）。
 * 这两帧之间（含）片段里的 ik_hand_gun 就在背上的插槽上，换挂点不跳
 */
namespace Ginkgo::WeaponEvents
{
	inline const FName OnBack(TEXT("OnBack"));
	inline const FName InHand(TEXT("InHand"));
	/** 枪机开、关（Docs/Design/Weapons.md「机构」的事件）：蒙太奇里的时刻就是片段里抛壳、推到底的那一帧 */
	inline const FName ActionOpen(TEXT("ActionOpen"));
	inline const FName ActionClose(TEXT("ActionClose"));
	/** 压进去一发（Docs/Design/Weapons.md「机构」的事件） */
	inline const FName RoundIn(TEXT("RoundIn"));
	/** 卸下、装上弹匣（Docs/Design/Weapons.md「机构」的事件） */
	inline const FName MagOut(TEXT("MagOut"));
	inline const FName MagIn(TEXT("MagIn"));
	/** 从抛壳口放一发直接进膛（霰弹枪的紧急装填，Docs/Design/Weapons.md「机构」的事件）：片段里手松开、这一发落进去的那一帧 */
	inline const FName RoundChamber(TEXT("RoundChamber"));
}

namespace Ginkgo::WeaponSections
{
	/** 空仓换弹的蒙太奇分两段：换弹匣、放枪机回去。换到一半收枪，拔出来只放后一段（M1911） */
	inline const FName Magazine(TEXT("Magazine"));
	inline const FName SlideRelease(TEXT("SlideRelease"));
	/** 拉栓的蒙太奇分两段：拉开（到枪栓在最后面）、推回去。弹仓空了只放前一段 */
	inline const FName BoltOpen(TEXT("BoltOpen"));
	inline const FName BoltClose(TEXT("BoltClose"));
	/** 闭栓的蒙太奇也分两段：手伸到拉柄上、推回去（同上）。手已经在拉柄上时只放后一段 */
	inline const FName Reach(TEXT("Reach"));
	/**
	 * 抛壳口装填（霰弹枪）分三段：拉开（BoltOpen）、取一发放进去再握回护木（PortLoad）、推回去（BoltClose）。
	 * 护木已经在后面、手不在护木上时（压到一半收枪再拔）从中间这一段开始
	 */
	inline const FName PortLoad(TEXT("Load"));

	/** 中折单发（M79）空仓换弹折开后还未退空壳就被打断时，恢复装填从捏取退壳段开始 */
	inline const FName Extract(TEXT("Extract"));
}

/**
 * @struct FGinkgoReplicatedAnimState : 动画要、引擎却不复制给模拟代理的状态
 * 移动模式与蹲引擎本来就复制，意图走引擎的加速度复制（ShouldReplicateAcceleration）；滑铲只在模拟状态里、冲刺只在输入里，这里补上
 */
USTRUCT()
struct FGinkgoReplicatedAnimState
{
	GENERATED_BODY()

	UPROPERTY()
	bool bSliding = false;

	/** 在地面上冲刺（角色面向速度）：冲刺的输入只在客户端与服务器上 */
	UPROPERTY()
	bool bSprinting = false;
};

/**
 * @class AGinkgoCharacter : 共用角色底座（玩家与 AGinkgoAICharacter）
 * 移动组件换成 UGinkgoCharacterMovementComponent。朝向由移动组件定：平时面向视角（射击游戏里角色总面向准星方向，移动是平移），
 * 在地面上冲刺时面向速度；角色不按控制器转。输入由 AGinkgoPlayerController 绑定后转发过来。
 *
 * 网格是 项目设置 → Game → CaseGinkgo 里的角色网格（默认 GASP 的 UEFN 小白人，由导入命令生成），动画实例是 UGinkgoAnimInstance。
 * 网格资产不在（没跑过导入命令）时用占位：胶囊里一个圆柱体，前上方的小方块指示朝向，蹲下时跟着胶囊缩短。
 *
 * 动画动作（蒙太奇加事件，见 Docs/Design/AnimActions.md）由 AnimActions 组件执行。武器（见 Docs/Design/Weapons.md）：
 * 两个武器槽在武器组件里（槽里是哪把枪由服务器定、复制给各端，拾取武器见 Docs/Design/Interaction.md），网格组件与挂点在这里。
 * 玩家开局第一个槽是设置里的枪，读不到时不带枪；设置里开局拿在手上时，本地控制的角色复制到枪的那一帧就拿到手上。
 * AI 子类重写 InitializeStartingEquipment，使用自己的装备定义，独立于玩家开局设置。
 * 背着的枪挂在背上，两把都在右肩后，只差一点（GetWeaponBackOffset）；手上的枪变了武器组件换动画层。
 * 收枪、拔枪的片段是按第一个槽的位置做的：第二把用同一套片段，换挂点时差的那一点收过去（BeginGunAttachBlend）。
 */
UCLASS()
class CASEGINKGO_API AGinkgoCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	explicit AGinkgoCharacter(const FObjectInitializer& ObjectInitializer);

	UGinkgoCharacterMovementComponent* GetGinkgoMovement() const;

	/** 控制源显式请求的上半身探身，范围 [-1,1]（左负右正）；默认无请求，玩家仍使用原反遮挡判定。 */
	virtual float GetTacticalPeek() const { return 0.f; }

	/** AI 等控制源已知的世界瞄准点（cm）；false 时由相机/眼位射线计算，不能从此接口追踪隐藏目标。 */
	virtual bool GetTacticalAimPoint(FVector& OutPoint) const { return false; }

	/**
	 * @brief 正在滑铲，给动画用：本地角色与服务器读移动组件的模拟状态，模拟代理读复制下来的
	 * @note 网络角色的分支只在这里，动画蓝图里不分
	 */
	bool IsSlidingForAnimation() const;

	/**
	 * @brief 在地面上冲刺，给动画选冲刺的片段用：本地角色与服务器读移动组件，模拟代理读复制下来的
	 * @note 模拟代理的朝向随复制的旋转来，已经面向速度；这一位只决定放冲刺还是跑步的片段
	 */
	bool IsSprintingForAnimation() const;

	/**
	 * @brief 移动输入
	 * @param Input - X 向右、Y 向前，按控制器的 Yaw 转到水平面
	 */
	void Move(const FVector2D& Input);

	/** @brief 冲刺输入（按住为真） */
	void SetWantsToSprint(bool bWantsToSprint);

	/** @brief 这一帧按下了几个键盘方向键（lurch），交给移动组件计数 */
	void AddDirectionPresses(uint8 Presses);

	/** @brief 按着的键盘方向键合起来的朝向（lurch 的方向，后按的赢），X 向右、Y 向前 */
	void SetDirectionKeys(FIntPoint Keys);

	/** 动画动作的执行器 */
	UGinkgoAnimActionComponent* GetAnimActions() const { return AnimActions; }

	/** 交互组件：找附近能交互的目标、接交互键（见 Docs/Design/Interaction.md） */
	UGinkgoInteractorComponent* GetInteractor() const { return Interactor; }

	/** 攀爬协调器：检测相机方向的方案，编排全身动作并管理恢复。 */
	UGinkgoTraversalComponent* GetTraversal() const { return Traversal; }

	/** 当前占用的工作台；由桌上的交互状态维护。 */
	AGinkgoWorkbench* GetWorkbench() const { return Workbench; }
	void SetWorkbench(AGinkgoWorkbench* InBench) { Workbench = InBench; }

	/** 随身武器、持枪槽与姿态。 */
	UGinkgoWeaponComponent* GetWeapons() const { return Weapons; }

	/** 管理左腰投掷物与独立的左臂动作。 */
	UGinkgoFlashbangComponent* GetFlashbang() const { return Flashbang; }

	/** 角色生命与护甲的唯一数据源；玩家使用全身护甲，AI 子类保留分部位模式。伤害结算写入，BeginPlay 时回满。 */
	UGinkgoDamageableComponent* GetDamageable() const { return Damageable; }

	/** 角色受击与死亡音效组件；玩家与 AI 共用 3D 空间死亡喊声，本地玩家独占低血量 2D 急促喘息。 */
	UGinkgoCharacterAudioComponent* GetCharacterAudio() const { return CharacterAudio; }

	/** 实际显示的身体，供第一人称隐藏头部等表现使用。玩法挂点与命中始终使用 GetMesh()。 */
	USkeletalMeshComponent* GetPresentationMesh() const;

	/** 玩法噪声入口，落脚与发射由各自系统在游戏线程提交；只由权威端广播给 AI。 */
	UGinkgoNoiseEmitterComponent* GetNoiseEmitter() const { return NoiseEmitter; }

	/**
	 * @brief 在面前放一个人形靶（Docs/Design/Damage.md「靶子」）：前、右按角色水平的朝向，脚踩在地上、面朝角色。开局的一排、调试命令 ginkgo.combat.spawnTarget 用
	 * @param Name - 叫什么（日志与测量的路线按名字认）；重名时引擎另起一个
	 */
	AGinkgoPracticeTarget* SpawnPracticeTarget(FName Name, const FGinkgoTargetSpawn& Spawn);

	/** 第一个槽的网格组件；槽空着时它没有网格 */
	UPoseableMeshComponent* GetWeapon() const { return WeaponMesh; }

	/** 某个槽的网格组件 */
	UPoseableMeshComponent* GetWeaponSlotComponent(int32 Slot) const;

	/** 手上那把枪的网格组件；徒手、槽里的枪没有网格时为空。拔枪开始时枪还在背上，这里已经是它（手上的槽开始时就换） */
	UPoseableMeshComponent* GetHeldWeaponMesh() const;

	/**
	 * @brief 摆枪栓：两个槽都按机构状态，手上那把按片段。动画实例每帧调（读片段的同一刻算手的轨迹，所以手与拉机柄这一帧对得上）；姿势没变的不动
	 * @param ClipWeight - 手上那把枪带枪栓曲线的片段的比重
	 * @param Clip - 片段上枪栓的姿势
	 * @param GunClip - 枪自己放的片段（M1911）：配了 GunRestClosed 的枪，活动部件都听它，不听枪栓的曲线
	 */
	void ApplyWeaponBolts(float ClipWeight, const FGinkgoBoltPose& Clip, const FGinkgoGunClip& GunClip);

	/**
	 * @brief 压弹时右手里有没有那一发：片段的 Round_InHand 曲线（动画实例每帧调，变了才换）
	 */
	void SetRoundInHand(bool bInHand);

	/** 调试：某个槽弹仓最上面那一发现在看不看得见、在哪（世界） */
	bool IsFeedRoundShown(int32 Slot) const;
	FVector GetFeedRoundLocation(int32 Slot) const;

	/** 某个槽的枪栓现在的姿势 */
	FGinkgoBoltPose GetWeaponBolt(const int32 Slot) const { return AppliedBolts.IsValidIndex(Slot) ? AppliedBolts[Slot] : FGinkgoBoltPose(); }

	/** 手上那把枪真的挂在手上：拔枪开始时、收枪落到背上以后不算 */
	bool IsHeldWeaponInHand() const;

	/** 各槽网格换成携带的武器，新增网格先归入收纳挂点；敌人代理在此后应用服务器手持状态。 */
	virtual void ApplyCarriedWeapons();

	/**
	 * @brief 某个槽的枪挂到手上：WeaponHandBone（虚拟骨骼，跟着胸口走），不用偏移（网格的轴心就在握把上）
	 * @note 网格上没有这根骨头（没跑过加虚拟骨骼的导入命令）时挂 ik_hand_gun，同以前：蹲下时枪不跟着身体
	 * @param bBlend - 拔枪的片段里换挂点：接缝收过去（BeginGunAttachBlend）
	 */
	void AttachWeaponToHand(int32 Slot = 0, bool bBlend = false);

	/**
	 * @brief 捡枪抓住了：枪挂到手上，这一帧还在地上的样子，BlendTime 里收到手里（Docs/Design/Interaction.md「丢枪与捡枪」）
	 * @param WorldBefore - 地上那把此刻的世界变换
	 */
	void AttachPickedWeapon(int32 Slot, const FTransform& WorldBefore, float BlendTime);

	/**
	 * @brief 捡枪抓住的那一刻（角色网格的坐标）：地上那把的朝向、右手现在的朝向（上一次求出来的姿势）、东西在右手骨里的挂点
	 * @param GunWorld - 地上那把此刻的世界变换
	 */
	FGinkgoPickupSettleStart CapturePickupSettleStart(const FTransform& GunWorld, const FVector& ItemInHand) const;

	/** @brief 这个槽的枪藏着：服务器已经放进槽了，手还没抓到，身上不该多出一把 */
	void SetWeaponSlotHidden(int32 Slot, bool bHide);

	/**
	 * @brief 某个槽的枪收起来：主武器挂到背上，手枪插进腰右边的枪套（GetWeaponStowBone、GetWeaponStowOffset）
	 * @param bBlend - 收枪的片段里换挂点：接缝收过去（BeginGunAttachBlend）
	 */
	void AttachWeaponToStow(int32 Slot = 0, bool bBlend = false);

	/** 某个槽的枪收着，不在手上（没有这个槽时也算） */
	bool IsWeaponStowed(int32 Slot) const;

	/** 某个槽的枪收起来时挂的骨头：按槽的种类（Docs/Design/Weapons.md「武器槽」） */
	FName GetWeaponStowBone(int32 Slot) const;

	/** 某个槽的枪收起来时相对那根骨头的变换 */
	FTransform GetWeaponStowOffset(int32 Slot) const;

	/**
	 * @brief 肩关节的位置（世界）：网格上 upperarm_l / upperarm_r 这一帧的位置
	 * @note 拾取选手、看手够不够得着（只决定放不放动画）时用，只在本地控制的角色上：服务器与模拟代理上的骨骼不一定在更新
	 */
	FVector GetShoulderLocation(EGinkgoHand Hand) const;

	/**
	 * @brief 手上拿着一件东西的样子：挂在 hand_l / hand_r 上的静态网格，相对手骨的变换给定
	 * @note 3C 的接口，拾取在手碰到东西的那一刻调（Docs/Design/Interaction.md「拾取子弹盒」）。只是样子：不碰撞、不复制
	 */
	void ShowHandItem(EGinkgoHand Hand, UStaticMesh* Mesh, const FTransform& InHand);

	/** @brief 手上的东西拿掉 */
	void HideHandItem(EGinkgoHand Hand);

	/** 手上拿着东西（回放与调试用） */
	bool IsHandItemShown(EGinkgoHand Hand) const;

	/** @brief 一只手伸去拿东西：交给动画实例（UGinkgoAnimInstance::SetHandReach）。3C 的接口，拾取用 */
	void SetHandReach(EGinkgoHand Hand, const struct FGinkgoHandReach& Reach);

	/** @brief 不再伸手 */
	void ClearHandReach(EGinkgoHand Hand);

	/** @brief 伸着的东西冻在这一刻相对身体的位置（手抓住了东西）：交给动画实例（UGinkgoAnimInstance::FreezeHandReach） */
	void FreezeHandReach(EGinkgoHand Hand);

	/**
	 * @brief 收枪的动作：放这个槽的枪的收枪蒙太奇，枪落到背上的那一帧（OnBack）换到背上、手上换回徒手（层自己淡出，淡完撤掉）
	 * @note 没播到（被打断）就跳过：换之前枪挂在手上、跟着片段走，没换就还在手上，挂点与枪看上去的位置一致
	 */
	UGinkgoAnimAction* MakeStowWeaponAction(int32 Slot = 0);

	/**
	 * @brief 拔枪的动作：开始时手上就换成这个槽（链上层，层自己淡入），枪离开背之前的最后一帧（InHand）换到手上
	 * @note 没播到同样跳过，枪留在背上；结局时枪还在背上就换回徒手
	 */
	UGinkgoAnimAction* MakeDrawWeaponAction(int32 Slot = 0);

	/** @return 模型的基准高度在世界里的位置（胶囊中心 + 网格的相对高度，不算空中蹲下的平滑）：移动组件换胶囊大小前后各读一次 */
	double GetMeshBaseHeight() const;

	/**
	 * @brief 换胶囊大小时模型的基准高度在世界里跳了一下（空中蹲下、站起）：记进平滑的偏移，看到的模型不跳（Docs/Design/MovementComponent.md「空中蹲下时的模型」）
	 * @param Jump - 跳了多少（cm，向上为正）
	 */
	void AbsorbMeshBaseJump(double Jump);

	/** @brief 平滑的偏移往 0 收一步：移动组件每帧移动之后调 */
	void StepCrouchMeshBlend(float DeltaTime);

	//~ Begin ACharacter
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;
	/** 把移动组件的输入加速度（意图）复制给模拟代理：动画的起步、急停、反向读它；不复制时模拟代理上它是速度方向的单位向量 */
	virtual bool ShouldReplicateAcceleration() const override { return true; }
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	/** 窗口给出"空中起跳"时（土狼时间），下落中的第一跳仍算第一跳；判断在移动组件里 */
	virtual bool CanJumpWhileFalling() const override;
protected:
	/** 玩家从项目设置选外观，敌人从出生类型选外观。 */
	virtual const UGinkgoCharacterModel* ResolveCharacterModel() const;

	/** 当前角色初始化或收到出生类型复制后，建立可见网格与源姿势的连接。 */
	void ApplyCharacterModel();

	/** 切断移动与手部动作，让角色网格接管身体碰撞。玩家各端与敌人死亡共用；bPlayDeathAudio 为真时同步在头部播 3D 空间死亡音效。 */
	void EnterDeathRagdoll(bool bPlayDeathAudio = true);

	virtual bool CanJumpInternal_Implementation() const override;
	//~ End ACharacter

	/** 服务器开局装备钩子：玩家开局配装由 AGinkgoGameMode::GrantStartingLoadout 统一发放，AI 子类重写为自身装备定义。 */
	virtual void InitializeStartingEquipment();

private:
	/** 目标网格挂在动画源下，独立于命中物理和根运动。专用服务器无需运行该网格。 */
	UPROPERTY(VisibleAnywhere, Category = "Character")
	TObjectPtr<USkeletalMeshComponent> PresentationMesh;

	/** 响应权威或复制过来的玩家生死状态变化，进入死亡时同步触发空间死亡音效。 */
	void HandlePlayerLifeStateChanged();

	/** 根据玩家的权威生死状态切换身体表现；初始复制（不播死亡声）、实时死亡、读档和原地复活共用。 */
	void ApplyPlayerLifeState(bool bPlayDeathAudio);

	/** 进入布娃娃前的网格相对胶囊变换；仅在死亡期间有效，原地复活时恢复。 */
	TOptional<FTransform> MeshBeforeDeath;

	/** 死亡前角色网格的碰撞配置名，退出布娃娃时恢复。 */
	FName MeshProfileBeforeDeath;

	/** 死亡前胶囊的碰撞开关，原地复活时恢复。 */
	ECollisionEnabled::Type CapsuleCollisionBeforeDeath = ECollisionEnabled::QueryAndPhysics;

	/** 换上设置里的角色网格；资产不在时留着占位 */
	void ApplyCharacterMesh();

	/** 按胶囊当前的尺寸摆放占位网格 */
	void FitPlaceholderToCapsule();

	/** 把 CrouchMeshBlend 的偏移写到网格的相对高度与 BaseTranslationOffset 上：动画实例拿网格减基准算网络平滑，这段偏移不算运动 */
	void ApplyCrouchMeshBlend();

	/** 网格相对高度的基准：构造时摆的，蹲下时引擎抬高；不带 CrouchMeshBlend */
	double MeshBaseRelativeZ = 0.0;

	/** 空中蹲下、站起时看到的模型的平滑：只有本地角色有（模拟代理走引擎的网络平滑） */
	FGinkgoCrouchMeshBlend CrouchMeshBlend;

	/**
	 * @brief 某个槽在背上的插槽（相对 WeaponBackBone）
	 * @note 第一个槽是设置里的 WeaponBackOffset（收枪片段里枪落下的地方）；第二个槽在枪自己的坐标里差 SecondWeaponBackShift
	 */
	FTransform GetWeaponBackOffset(int32 Slot) const;

	UPROPERTY(VisibleAnywhere, Category = "Animation")
	TObjectPtr<UGinkgoAnimActionComponent> AnimActions;

	UPROPERTY(VisibleAnywhere, Category = "Interaction")
	TObjectPtr<UGinkgoInteractorComponent> Interactor;

	/** 本角色的攀爬协调组件；持有动作方案及临时状态，检测与素材均来自移动配置。 */
	UPROPERTY(VisibleAnywhere, Category = "Movement")
	TObjectPtr<UGinkgoTraversalComponent> Traversal;

	/** 根运动修正器；由 Traversal 提供分段目标，CharacterMovement 的提取回调执行修正。 */
	UPROPERTY(VisibleAnywhere, Category = "Movement")
	TObjectPtr<UMotionWarpingComponent> MotionWarping;

	/** 相机上下文：移动组件、武器组件写，相机读（Docs/Design/CameraPipeline.md「修改器激活：相机上下文组件」） */
	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UGinkgoCameraContextComponent> CameraContext;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UGinkgoPlayerCameraComponent> PlayerCamera;

	UPROPERTY(Transient)
	TObjectPtr<AGinkgoWorkbench> Workbench;

	UPROPERTY(VisibleAnywhere, Category = "Weapon")
	TObjectPtr<UGinkgoWeaponComponent> Weapons;

	/** 单个腰部槽位，库存与投掷阶段由权威端复制。 */
	UPROPERTY(VisibleAnywhere, Category = "Character")
	TObjectPtr<UGinkgoFlashbangComponent> Flashbang;

	/** 子弹穿得过（Docs/Design/Damage.md「穿射」）：人的厚度，组件的默认值（你定的 0.3） */
	UPROPERTY(VisibleAnywhere, Category = "Combat")
	TObjectPtr<UGinkgoPenetrableComponent> Penetrable;

	/** 默认 100 生命、无护甲；玩家启用全身防护池，敌人仍按部位。角色销毁时一同释放，不由 HUD 持有 */
	UPROPERTY(VisibleAnywhere, Category = "Combat")
	TObjectPtr<UGinkgoDamageableComponent> Damageable;

	/** 角色死亡 3D 空间音效与本地玩家低血量 2D 急促喘息组件；玩家开启喘息，AI 子类构造时关闭喘息并保留死亡声 */
	UPROPERTY(VisibleAnywhere, Category = "Audio")
	TObjectPtr<UGinkgoCharacterAudioComponent> CharacterAudio;

	/** 角色生命周期内共用的噪声源，配置脚步听距；枪声听距由武器和配件定义提供。 */
	UPROPERTY(VisibleAnywhere, Category = "AI")
	TObjectPtr<UGinkgoNoiseEmitterComponent> NoiseEmitter;

	UPROPERTY(VisibleAnywhere, Category = "Weapon")
	TObjectPtr<UPoseableMeshComponent> WeaponMesh;

	/** 第二个槽的网格组件 */
	UPROPERTY(VisibleAnywhere, Category = "Weapon")
	TObjectPtr<UPoseableMeshComponent> SecondWeaponMesh;

	/** 两只手上拿着的东西（拾取时手里的子弹盒）：挂在 hand_l、hand_r 上，平时没有网格 */
	UPROPERTY(VisibleAnywhere, Category = "Animation")
	TObjectPtr<UStaticMeshComponent> LeftHandItem;

	UPROPERTY(VisibleAnywhere, Category = "Animation")
	TObjectPtr<UStaticMeshComponent> RightHandItem;

	/** 两个槽弹仓最上面那一发：挂在枪上，枪机开着、弹仓有弹时看得见 */
	UPROPERTY(VisibleAnywhere, Category = "Weapon")
	TObjectPtr<UStaticMeshComponent> FeedRound;

	UPROPERTY(VisibleAnywhere, Category = "Weapon")
	TObjectPtr<UStaticMeshComponent> SecondFeedRound;

	UPROPERTY(VisibleAnywhere, Category = "Weapon")
	TObjectPtr<UStaticMeshComponent> SidearmFeedRound;

	/** 手枪的槽的网格组件 */
	UPROPERTY(VisibleAnywhere, Category = "Weapon")
	TObjectPtr<UPoseableMeshComponent> SidearmMesh;

	/** 重武器的槽（MG3）的网格组件：只在手上时有网格，收不起来 */
	UPROPERTY(VisibleAnywhere, Category = "Weapon")
	TObjectPtr<UPoseableMeshComponent> HeavyWeaponMesh;

	UPROPERTY(VisibleAnywhere, Category = "Weapon")
	TObjectPtr<UStaticMeshComponent> HeavyFeedRound;

	/** 某个槽弹仓最上面那一发 */
	UStaticMeshComponent* GetFeedRoundComponent(int32 Slot) const;

	/**
	 * @brief 摆枪自己的活动部件（配了 GunRestClosed 的枪）：不放片段时是枪机关着、开着的样子，放片段时跟着片段，淡入淡出时在两者之间。弹匣不在枪上时缩成看不见
	 * @note 片段的骨架与网格一一对应（枪自己导入时建的骨架），骨骼的下标通用
	 */
	void ApplyGunPose(int32 Slot, const UGinkgoWeaponDefinition& Carried, const USkeletalMesh& GunMesh, const FGinkgoMechanismState* State, const FGinkgoGunClip& Clip);

	/**
	 * @brief 枪换了挂点：这一帧枪不动，差的那一点在 GunAttachBlendTime 里收掉（第二把的片段是第一个槽的，手上的枪和背上的差 SecondWeaponBackShift；
	 *        换到另一把时枪在肩后松开，差得更多，它就是枪落到背上、下一把到手里的那一段）
	 * @note 枪套跟着骨盆，手跟着胸口；片段是站着做的，跑着、蹲着时两边差几厘米（跑着 6.8 cm，Docs/Design/ActionStates.md「M1911」）。背上的枪跟着胸口，没有这个差
	 * @param WorldBefore - 换挂点之前枪的世界变换
	 */
	void BeginGunAttachBlend(int32 Slot, const FTransform& WorldBefore);

	/** 换挂点的接缝收多久（s） */
	static constexpr float GunAttachBlendTime = 0.12f;

	/**
	 * 接缝：差多少（枪的坐标）、挂点上的变换、还剩多久、一共多久。bSettleRebase：捡枪收拢时等 Control Rig 接手挂点（ApplyWeaponBolts），
	 * 在那之前不收
	 */
	struct FGunAttachBlend
	{
		FTransform From = FTransform::Identity;
		FTransform Base = FTransform::Identity;
		float Remaining = 0.f;
		float Duration = GunAttachBlendTime;
		bool bSettleRebase = false;
	};

	/** @brief 收拢开始时挂点（虚拟骨骼）的世界变换：Ginkgo::Weapon::BlendPickupSettle 的进度 0，手是上一次求出来的姿势 */
	FTransform MakePickupSettleAttach(const UGinkgoWeaponDefinition& Carried) const;
	FGunAttachBlend GunAttachBlends[Ginkgo::Weapon::SlotCount];

	/** 藏着的槽（SetWeaponSlotHidden）；没有时为 INDEX_NONE */
	int32 HiddenWeaponSlot = INDEX_NONE;

	/** 各个槽上一次摆成什么样：没变就不再摆 */
	struct FAppliedGunPose
	{
		/** 已经采样过静止姿势；换枪时清空 */
		bool bValid = false;

		/** 上次是否在播放机械轨道，结束后的第一帧必须还原静止姿势 */
		bool bClip = false;

		/** 上次采样的枪机开闭状态 */
		bool bActionOpen = false;

		/** 上次采样的弹匣在位状态 */
		bool bMagazineIn = true;

		/** 进膛的那发子弹是否可见 */
		bool bNextRoundShown = true;

		/** 退出来的空壳是否可见 */
		bool bSpentCaseShown = true;

		/** 显示的供弹骨骼数；-1 表示未采样 */
		int32 FeedShown = -1;

		/** 采样对应的网格，Mutable 异步替换网格后必须按新骨架重采样 */
		TWeakObjectPtr<const USkeletalMesh> Mesh;

		/** 上次采用的装配枪口位置（枪空间 cm），前端改变时使静止缓存失效 */
		FVector MuzzleOffset = FVector::ZeroVector;

		/** 上次应用到武器弹药材质槽的弹种标签；切换弹种或装填新弹头时刷新涂装 */
		FGameplayTag RoundAmmo;
	};

	/** 每槽的机械姿势缓存，由 ApplyGunPose 更新，ApplyCarriedWeapons 在换枪时重置 */
	TArray<FAppliedGunPose> AppliedGunPoses;

	/** 压弹时右手里有那一发（SetRoundInHand） */
	bool bRoundInHand = false;

	/** 各个槽的网格组件现在显示的枪：与槽里的不同的才换网格、重挂（第一把拔在手上时不被拉回背上） */
	TArray<TWeakObjectPtr<UGinkgoWeaponDefinition>> AppliedWeapons;

	/** 各个槽的枪栓摆成的姿势：同 AppliedWeapons 一样长 */
	TArray<FGinkgoBoltPose> AppliedBolts;

	UPROPERTY(VisibleAnywhere, Category = "Placeholder")
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	UPROPERTY(VisibleAnywhere, Category = "Placeholder")
	TObjectPtr<UStaticMeshComponent> PlaceholderVisor;

	/** 只复制给模拟代理，服务器在复制之前从移动组件取（PreReplication） */
	UPROPERTY(Replicated, Transient)
	FGinkgoReplicatedAnimState ReplicatedAnimState;
};
