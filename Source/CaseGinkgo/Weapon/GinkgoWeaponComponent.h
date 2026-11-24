#pragma once

#include "CoreMinimal.h"
#include "Combat/GinkgoBulletImpact.h"
#include "Combat/GinkgoCombatTypes.h"                // 命中的白 X
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "StateTreeExecutionContext.h"            // FOnCollectStateTreeExternalData 的参数
#include "GameplayTagContainer.h"                  // 现在跑着哪一步
#include "StateTreeInstanceData.h"                // 动作状态的树的实例数据是值类型
#include "Weapon/GinkgoScope.h"                   // 第一人称开镜：瞄具在世界里的位置是值类型
#include "Weapon/GinkgoWeaponAttachment.h"
#include "Weapon/GinkgoWeaponMechanism.h"         // 每个槽的机构状态与后备子弹是值类型
#include "Weapon/GinkgoWeaponTypes.h"
#include "GinkgoWeaponComponent.generated.h"

class AGinkgoCharacter;
class AGinkgoWeaponPickup;
class UAudioComponent;
class UGinkgoAmmoDefinition;
class UGinkgoAnimAction;
class UGinkgoCameraContextComponent;
class UGinkgoWeaponDefinition;
class UNiagaraComponent;
class UStaticMeshComponent;
class UStateTree;
struct FGinkgoWeaponSaveState;
struct FGinkgoWeaponCue;

/**
 * @struct FGinkgoStartingAmmoEntry : 开局后备弹药的一条弹种数量记录
 * 由服务器 AGinkgoGameMode 在发放正式开局配装后写入并随武器组件复制到客户端，
 * 确保单机与联机客户端均按游戏模式规定的后备弹匣数覆写本地 AmmoReserve。
 */
USTRUCT()
struct CASEGINKGO_API FGinkgoStartingAmmoEntry
{
	GENERATED_BODY()

	/** 弹种标签 */
	UPROPERTY()
	FGameplayTag Ammo;

	/** 该弹种的初始后备发数（>= 0） */
	UPROPERTY()
	int32 Rounds = 0;
};

/**
 * @class UGinkgoWeaponComponent : 角色的武器
 * 见 Docs/Design/Weapons.md「部分与边界」：武器槽（服务器定、复制给各端，按种类分，「武器槽」）、手上是哪个槽、持枪状态的快照，
 * 每个槽的机构状态与后备子弹（只在本地，同射击）。
 * 网格组件与挂点还在角色上（AGinkgoCharacter），这里只管数据：槽变了让角色换网格，手上的枪变了换动画层。
 *
 * 玩家控制器与服务器 AI 通过 UpdateHoldState 推进持枪状态。敌人的模拟代理使用服务器复制的手持槽与动画姿势。
 * 姿态按纯函数选（Ginkgo::Weapon::SelectStance），动作状态只给约束；
 * 调试时 ginkgo.weapon.stance 强制姿态（回放与截图用）。
 *
 * 动作状态的树（Docs/Design/ActionStates.md）也由这里推进：树只管手现在在干什么，枪的事实留在这里（每个槽的机构状态，收枪、拔枪不清）。
 * 换枪的键只改目标槽，树按手上的槽与目标槽选状态
 */
UCLASS(ClassGroup = (Ginkgo))
class CASEGINKGO_API UGinkgoWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGinkgoWeaponComponent();

	/** 武器槽的个数 */
	static constexpr int32 SlotCount = Ginkgo::Weapon::SlotCount;

	/** 槽里的枪；空着时为空 */
	UGinkgoWeaponDefinition* GetCarried(int32 Slot) const;

	/** @brief 这一种的第一个空槽；这一种都满时为 INDEX_NONE */
	int32 FindFreeSlot(EGinkgoWeaponSlotKind Kind) const;

	/**
	 * @brief 把枪放进槽，空的就是拿走
	 * @note 只在服务器上（单机与主机是本地）：槽复制给各端，各端在复制下来时换网格、挂到背上
	 */
	void SetCarried(int32 Slot, UGinkgoWeaponDefinition* Weapon);

	/** 槽里这把枪已经装上的配件。空槽返回空集合 */
	const FGinkgoInstalledAttachments& GetAttachments(int32 Slot) const;

	/** 枪接受这个配件且整套装配兼容；模块武器允许替换同插槽，普通武器只接受空挂点 */
	bool CanInstallAttachment(int32 Slot, const UGinkgoWeaponAttachment* Part) const;

	/** 服务器在安装动作完成或捡回枪时写入；替换弹匣时将超出容量的弹药退回后备 */
	void InstallAttachment(int32 Slot, UGinkgoWeaponAttachment* Part);

	/** 工作台换匣的入位事件。服务器同时更换配件并按新容量装填，返回是否成功提交。 */
	bool InstallWorkbenchMagazine(int32 Slot, UGinkgoWeaponAttachment* Part);

	/** 已装配件的汇总效果，装配变化时刷新；空槽返回中性值 */
	const FGinkgoAttachmentEffects& GetAttachmentEffects(int32 Slot) const;

	/** 该实例实际使用的机构规则；模块武器的容量覆盖只写入实例副本，绝不修改共享资产 */
	const UGinkgoWeaponMechanism* GetMechanism(int32 Slot) const;

	/** 装配后的枪口位置（枪空间 cm），没有前端覆盖时返回武器基础值 */
	FVector GetMuzzleOffset(int32 Slot) const;

	/** 装配后的左手接触修正（枪空间 cm），动画快照在游戏线程读取 */
	FVector GetSupportHandOffset(int32 Slot) const;

	/** 将前端的枪空间枪口位移转到父骨骼局部空间后施加；角色和拾取物须先采样基础姿势，调用后再 RefreshBoneTransforms */
	static void ApplyModulePose(const UGinkgoWeaponDefinition& Weapon, const FGinkgoInstalledAttachments& Installed, class UPoseableMeshComponent& Mesh);

	/** 武器、配件或槽的可见性改变时同步各处的网格 */
	UFUNCTION()
	void RefreshAttachmentMeshes();

	/** 安装动作借用本槽的异步外观管理器；槽不存在或普通武器返回空，不转移其所有权。 */
	UGinkgoModularWeaponVisual* GetModularVisual(int32 Slot) const;

	/** 手上是哪个槽；徒手时为 INDEX_NONE */
	int32 GetHeldSlot() const { return HeldSlot; }

	/** 手上的枪；徒手时为空 */
	UGinkgoWeaponDefinition* GetHeldWeapon() const { return GetCarried(HeldSlot); }

	/**
	 * @brief 手上换成哪个槽（INDEX_NONE 是徒手），动画层跟着换：链接这把枪的层，徒手时撤掉，回到主蓝图自己的（原样输出）
	 * @note 拔枪开始时就换，收枪的枪落到背上时换回徒手。层自己淡入、淡出（拔枪的蒙太奇从 0 淡入，链上的那一帧还盖不住）。
	 *       只换数据与层，网格的挂点由调用的一方换。
	 *       树跑着一步时（StartStep 到 EndStep）目标槽不动：那时目标是放完以后去的，按键还在改它。
	 *       别的时候目标槽跟着换，所以开局、丢枪、调试命令直接换手上的槽时，树跟着到对的状态，不会再放一遍
	 */
	void SetHeldSlot(int32 Slot);

	/** 将已装备的出生武器直接挂到手上，同时为首帧动画设置低持状态。 */
	void EquipStartingWeapon(int32 Slot);

	/** 客户端按服务器快照更新挂点、动画层与姿势。携带槽晚到时，调用方须在槽就绪后重用同一快照。 */
	void ApplyReplicatedHoldPose(const FGinkgoReplicatedHoldPose& Pose);

	/**
	 * @brief 这个槽的枪掉到地上，机构状态与配件原样带走（Docs/Design/Interaction.md「丢枪与捡枪」）。空槽无操作。
	 * @param bTossFromHand - 为真时，手上的枪沿用按 G 的向前上方抛出；为假时只松开，继承角色速度并略向外掉落。
	 * @note 主动丢枪在本地控制端调用，将机构快照发给服务器；客户端先清槽，地上的枪由服务器生成。
	 *       AI 死亡由权威端调用，须在停止移动和启用布娃娃之前执行，以保留死亡瞬间的挂点、速度与装填状态。
	 */
	void DropWeapon(int32 Slot, bool bTossFromHand = true);

	/**
	 * @brief 开始捡这把枪：按 Ginkgo::Weapon::PlanWeaponPickup 腾地方，右手空了就伸手（只能右手捡）。这段时间换枪的键不响应
	 * @note 槽已经由服务器放好时枪也藏着，抓住才出现（FinishWeaponPickup）
	 */
	void BeginWeaponPickup(AGinkgoWeaponPickup& Pickup);

	/**
	 * @brief 抓住了（AGinkgoPickup::GrantNow）：状态换成地上那把的，枪收到手里，接拔枪的后半段落到低持
	 * @param WorldBefore - 地上那把此刻的世界变换
	 */
	void FinishWeaponPickup(const AGinkgoWeaponPickup& Pickup, const FTransform& WorldBefore);

	/** @brief 服务器没给（别人先拿了）：枪不藏了、键放开。已经掉到地上的那把不回来 */
	void CancelWeaponPickup(const AGinkgoWeaponPickup& Pickup);

	/** 正在捡枪：从按下到抓住 */
	bool IsPickingUpWeapon() const { return PendingPickupSlot != INDEX_NONE; }

	/** @brief 这一次拔枪是捡枪接上来的：只回答一次 */
	bool ConsumeDrawFromPickup();

	/**
	 * 临时背枪结束后请求取出同一把枪；要求该槽仍为逻辑手持且挂在背上。
	 * 本地/AI 由武器状态树播放，远端玩家直接复用同一右臂 AnimAction；两者均由 InHand 事件切挂点。
	 * 取枪独立拥有后续完成/取消清理，不依赖触发它的攀爬或工作台动作继续存活。
	 */
	bool RequestDrawStowedWeapon(int32 Slot);

	/**
	 * @brief 收枪放到手松开枪的这一刻：接下来要拔的是背上的另一把主武器时记下来，拔枪从接枪的地方接（Docs/Design/ActionStates.md「第二把枪」）
	 * @return 是这种情况，收枪这一步到这里就算完
	 */
	bool BeginDrawFromBack(int32 FromSlot);

	/** @brief 这一次拔这个槽是接在收枪后面的：只回答一次 */
	bool ConsumeDrawFromBack(int32 Slot);

	/**
	 * @brief 捡枪抓住以后的收拢进度（两头缓过，0 到 1；不在收拢时是 1）：枪从右手里带到持枪的位置，持枪的层、拾取的片段按它换过去
	 *        （设置的 PickupSettleTime，Docs/Design/Interaction.md「丢枪与捡枪」）
	 */
	float GetPickupSettleAlpha() const;

	/** 收拢按时间走了多少（0 到 1，没缓过；不在收拢时是 1） */
	float GetPickupSettleProgress() const;

	/** 收拢还剩多久（s）：收拢那一步（AGinkgoCharacter::MakeDrawWeaponAction）按它占着右臂 */
	float GetPickupSettleRemaining() const;

	/** 抓住的那一刻（角色网格的坐标）：收拢从这里出发（Ginkgo::Weapon::BlendPickupSettle） */
	const FGinkgoPickupSettleStart& GetPickupSettleStart() const { return PickupSettleStartPose; }

	/** @brief 收拢做完（或被打断）：枪从右手换回持枪的挂点（虚拟骨骼），进度回到 1 */
	void EndPickupSettle();

	/** 目标槽：手要去哪个槽（INDEX_NONE 是徒手）。换枪的键改它，动作状态的树按它与手上的槽选状态 */
	int32 GetTargetSlot() const { return TargetSlot; }

	/** 上一次手上是哪个槽；还没拿过时为 INDEX_NONE。只有它不记在枪上 */
	int32 GetLastHeldSlot() const { return LastHeldSlot; }

	/** @brief 徒手时按收枪键会拔哪个槽（Ginkgo::Weapon::FindDrawSlot）；一把枪也没有时为 INDEX_NONE */
	int32 FindDrawSlot() const;

	/**
	 * @brief 这一帧按下的武器键（本地控制的角色，控制器每帧给）：换枪的键改目标槽（Ginkgo::Weapon::ApplyWeaponKey），开火、装填记成意图
	 * @note 开火记到开了、咔过或者手上没枪了为止（你定的：低持时按开火记着，抬好就开）。
	 *       装填记到压满、没子弹、换枪、按了开火或开镜（这一发压完，闭栓，再开火、开镜，你定的）。枪没有子树时都不记，没有东西会把它用掉
	 */
	void HandleWeaponKeys(EGinkgoWeaponKey Keys);

	/** @brief 枪的子树现在该做什么（Ginkgo::Weapon::SelectWeaponAction）：子树的条件读它 */
	EGinkgoWeaponAction SelectWeaponAction() const;

	/**
	 * @brief 装填键与弹种轮盘推进一帧（Docs/Design/Weapons.md「切换弹种」）：控制器每帧在 HandleWeaponKeys 之后、UpdateHoldState 之前给（回放也是）
	 * 有轮盘的枪（同一口径有几种弹）：短按松开时装填；按够 AmmoWheelHoldTime 打开轮盘，鼠标拨、视角的摇杆指着选，松开装填键按选的换（你说的），
	 * 开镜键取消（你说的），开火、换枪也取消。没有轮盘的枪按下就装填，同以前
	 * @param bReloadHeld - 装填键这一帧按着
	 * @param WheelMouse - 这一帧鼠标动的量（度，按灵敏度换的，X 右、Y 上，不反转）：轮盘开着时拨指针
	 * @param WheelStick - 视角的摇杆（过了视角的死区，X 右、Y 上，不反转）
	 */
	void UpdateAmmoWheel(bool bReloadHeld, const FVector2D& WheelMouse, const FVector2D& WheelStick, float DeltaSeconds);

	/** 弹种轮盘开着：控制器不把鼠标、视角的摇杆给视角 */
	bool IsAmmoWheelOpen() const { return AmmoWheel.bOpen; }

	/** 弹种轮盘：界面、回放读 */
	const FGinkgoAmmoWheel& GetAmmoWheel() const { return AmmoWheel; }

	/**
	 * @brief 这把枪有没有弹种轮盘：能装填（不是弹链），同一口径有两种以上的弹（UGinkgoAmmoDefinition::GetChoices）。霰弹枪、98k 有（你说的）
	 */
	static bool HasAmmoWheel(const UGinkgoWeaponDefinition* Weapon);

	/** 某个槽装填时压哪种弹（轮盘选的；槽里换了枪时是枪的弹种）。槽空着时为空 */
	FGameplayTag GetLoadAmmo(int32 Slot) const;

	/** @brief 直接设某个槽装填时压哪种弹（调试命令 ginkgo.weapon.loadAmmo） */
	void SetLoadAmmo(int32 Slot, const FGameplayTag& Ammo);

	/** 抬到位了（持枪状态的抬枪进度走满）：低持时按开火要等它 */
	bool IsRaised() const { return HoldState.IsRaised(); }

	/**
	 * @brief 现在不让冲刺：想开火、正在开火（你定的：冲刺中按开火先松开冲刺）。控制器改交给角色的意图，不碰移动的模拟
	 * @note 拉栓不算：进入冲刺不打断拉栓，手还按着冲刺键的话开完这一枪接着冲（Docs/Design/ActionStates.md「谁打断谁」）
	 */
	bool IsSprintBlocked() const;

	/**
	 * @brief 手上这把枪的枪口沿枪管（枪的 X 轴）打一条线，最远 BoreTraceDistance：不开镜时开火打这里，红 X 画在这里
	 * @note 枪挂在虚拟骨骼上，用的是网格组件现在的变换：动画之前调用时就是上一帧的姿势，也就是这一帧屏幕上画着的那个。
	 *       枪的踢动扣掉：那是后坐的样子，不是枪对着哪里，准星不跟着跳
	 * @param OutPoint - 打到的地方；没打到时是最远处
	 * @param OutHit - 打到的那一下（打到时才填）。线是子弹的通道（Ginkgo::Combat::WeaponTraceChannel）：人形靶的形体挡，胶囊不挡
	 * @return 枪在手上（不在背上）才算
	 */
	bool TraceBore(FVector& OutMuzzle, FVector& OutBore, FVector& OutPoint, bool* bOutHit = nullptr, FHitResult* OutHit = nullptr) const;

	/**
	 * @brief 没偏的那条线打到哪里（红 X，散布的圆锥的中心，开枪时再按扩散偏）：开镜时打屏幕正中心（开镜准星永远在那里，你说的，所见即所打：相机这一帧的画面中心打线，再从枪口打过去，挡在前面的算），不开镜时沿枪管。
	 *        带瞄具的枪（第一人称开镜）从瞄具的出瞳沿光轴打：分划的尖就在光轴上（Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）
	 * @param OutHit - 打到的那一下（打到时才填）
	 * @param CoverPenetration - 这一发的掩体穿射系数（Docs/Design/Damage.md「穿射」）：开镜时准星的线先穿过去，后面这一发穿得到的第一个活着的目标就是要打的
	 *        （隔着墙瞄墙后的人，子弹从枪口朝他去）。0（界面画红 X）照原来：准星打到的第一个东西
	 * @return 枪在手上才算
	 */
	bool TraceShot(FVector& OutMuzzle, FVector& OutPoint, bool& bOutHit, FHitResult* OutHit = nullptr, float CoverPenetration = 0.f) const;

	/**
	 * @brief 这一步能不能开枪：拉栓、装填、拔枪、收枪时枪在片段里晃，开不了枪，没有"这一枪打哪里"可说（界面不画红 X）
	 * @note 只看这一步，不看有没有子弹：没子弹时红 X 照画，开不出来是另一回事
	 */
	bool IsShotStepClear() const;

	/**
	 * @brief 本地玩家能否把双手交给改装桌：换枪已到目标槽，没有正在执行的武器步骤、拾取或待处理输入
	 * @note 收起所有武器也能进桌。有没有可放的枪、枪是否已挂在手上，由工作台根据角色另外检查
	 */
	bool CanPlaceOnWorkbench() const;

	/** 投掷抢占左臂时撤回装填意图；保留开火与瞄准输入。 */
	void CancelReloadForThrow();

	/**
	 * @brief 瞄准方向（Docs/Design/Weapons.md「腰射操作性」）：不开镜时是视线后面拖的弹簧，开镜满了就是视线。白点画它，枪口对准它
	 * @param View - 这一帧的视线
	 * @note 只有本地控制的角色有弹簧，别的角色、枪不在手上时就是视线
	 */
	FRotator GetAimRotation(const FRotator& View) const;

	/**
	 * @brief 手上这把枪的瞄具（Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）：按开镜准星的名字在项目设置里找。
	 *        徒手、没有瞄具、调试关掉（ginkgo.weapon.scope 0，越肩拉近对照）时为空
	 */
	const FGinkgoScope* GetHeldScope() const;

	/**
	 * @brief 手上这把枪的瞄具这一刻在世界里在哪：枪的网格现在的变换（动画之前调时是上一帧画出来的那个，也就是屏幕上看到的分划）
	 * @return 有瞄具、枪在手上（不在背上）才算
	 */
	bool GetSightFrame(Ginkgo::Scope::FSightFrame& OutSight) const;

	/** 第一人称开镜的进度：有瞄具时就是开镜进度（两头缓过的），同相机的 */
	float GetFirstPersonAlpha() const;

	/** 第一人称时晃枪的那一份（视角空间，度，已乘第一人称的进度）：瞄准方向加上它（Docs/Design/Weapons.md「第一人称开镜」）。回放记下 */
	const FRotator& GetSightSway() const { return SightSway; }

	/** 第一人称时枪跟着相机后坐转的那一份（视角空间，度，已乘第一人称的进度）：瞄准方向加上它，瞄具同画面一起跳。回放记下 */
	const FRotator& GetSightRecoil() const { return SightRecoil; }

	/** 这一枪恢复到哪了：1 刚开枪，0 能开下一枪（射速，或者 ShotRecoveryTime）。回放记下 */
	float GetFireRecovery() const { return ShotRecoveryDuration > 0.f ? ShotRecoveryRemaining / ShotRecoveryDuration : 0.f; }

	/** 调试：瞄准方向减视线（度，X 偏航、Y 俯仰），开镜进度乘之前的。回放记下 */
	const FVector2D& GetHipAimLag() const { return HipAim.Lag; }

	/** 枪的踢动的变换（枪的坐标，弹簧算出来的，越肩、腰射就是它） */
	const FTransform& GetGunKickTransform() const { return GunKickTransform; }

	/**
	 * 枪口对准以后加在枪上的程序化偏移（枪的坐标）：踢动（第一人称时乘 KickScale）× 第一人称开镜时枪相对眼睛的偏移（转镜头的落后、走路的偏与晃，FGinkgoScopeHandling）。
	 * Control Rig 乘在枪口对准以后的枪上（动画实例转交）；相机去掉它找眼睛的家
	 */
	FTransform GetGunOffsetTransform() const { return AppliedGunKickTransform * SightOffsetTransform; }

	/** 第一人称开镜时枪相对眼睛转多少（度，绕枪托，已乘第一人称的进度）：转镜头的落后加走路的偏与晃。回放记下 */
	const FRotator& GetSightOffset() const { return SightOffset; }

	/** 其中走路的偏与晃那一份（度，已乘第一人称的进度）。回放记下 */
	const FRotator& GetSightMoveSway() const { return SightMoveSway; }

	/** 走路时枪挪多少（cm，枪口对准以后那一把的坐标，已乘第一人称的进度）：走路的挪加走路的颠。回放记下 */
	const FVector& GetSightMoveShift() const { return SightMoveShift; }

	/** 枪口的线最远多长（cm） */
	static constexpr double BoreTraceDistance = 10000.0;

	/** 开镜进度（两头缓过）走到这里，这一枪就算开镜打的：开镜准星已经比点大了 */
	static constexpr float AdsShotAlpha = 0.5f;

	/** @brief 动作状态的约束：这一步里能不能开镜（FGinkgoWeaponStepTask 进出状态时设）。不让时按着开镜也不开，放完才响应 */
	void SetAimAllowed(const bool bInAimAllowed) { bAimAllowed = bInAimAllowed; }

	/** @brief 动作状态的约束：这一步里能不能抬枪（FGinkgoWeaponStepTask 进出状态时设）。拔枪、收枪、压弹不能：腰射留的时间清掉 */
	void SetRaiseAllowed(const bool bInRaiseAllowed) { bRaiseAllowed = bInRaiseAllowed; }

	/** @brief 动作状态的约束：这一步两只手都在枪上（FGinkgoWeaponStepTask 进出状态时设）。冲刺时枪不用单手举着，放低 */
	void SetBothHandsOnGun(const bool bInBothHands) { bBothHandsOnGun = bInBothHands; }

	/**
	 * @brief 跑一步：按 Weapon.Step.* 的标签找这把枪的片段，交给动画动作的执行器（FGinkgoWeaponStepTask 调）
	 * @return 跑起来的动作；这一步做不了时为空
	 */
	UGinkgoAnimAction* StartStep(const FGameplayTag& Step);

	/** @brief 这一步结束（退出状态时）：还没放完的取消，没到通知的事件不发 */
	void EndStep(UGinkgoAnimAction* Action);

	/** @brief 这一步没做成（放不起来、被顶掉）：目标槽作废，回到手上的，树不反复重试 */
	void AbandonTarget() { TargetSlot = HeldSlot; }

	/** 退出当前武器的动作子树。下次推进时按实际持枪槽重选，保留各槽的机构状态。 */
	void StopActionStates();

	/** @brief 重启动作状态的树（回放开始时）：放到一半的取消，目标槽回到手上的，下一帧按事实重选 */
	void RestartActionStates();

	/** @brief 这一帧开火键按没按着（控制器每帧给，回放也是）。连发的枪按着就接着打 */
	void SetTriggerHeld(const bool bHeld) { bTriggerHeld = bHeld; }

	/** 撤销尚未执行的开火请求及持续扣扳机。AI 停火、失去控制时用；已经发出的子弹与正在播放的开火动作不回滚。 */
	void CancelFireIntent() { bTriggerHeld = false; bWantsFire = false; }

	/** 双手被攀爬接管时丢弃开火、换弹和切枪请求；不回滚已经发生的机构事件、不补发尚未发生的装弹。 */
	void InterruptHandRequests();

	/** 死亡时取消枪械动作并退出瞄准；清理后控制器不再推进持枪状态，首人称相机也能立即退回。 */
	void InterruptForDeath();

	bool IsTriggerHeld() const { return bTriggerHeld; }

	/** @brief 瞄准后坐的状态：测量、测试读 */
	const FGinkgoRecoilState& GetRecoilState() const { return AimRecoil; }

	/** @brief 后坐对视线一共动了多少：回放的视角是绝对值，录的时候扣掉、放的时候加上 */
	FRotator GetRecoilOffset() const { return FRotator(AimRecoil.Applied.Y, AimRecoil.Applied.X, 0.0); }

	/** 最后一枪打中了可打击的目标的那一次（命中的白 X，Docs/Design/Damage.md）：界面看 Serial 变了就弹一次 */
	const FGinkgoHitConfirm& GetHitConfirm() const { return HitConfirm; }

	/** 抛射物（M79 榴弹等）在远处碰炸命中可打击目标时回传，触发界面白/红 X 与本地玩家 2D 命中音效 */
	void NotifyProjectileHitConfirm(FGinkgoHitConfirm Shot);

	/** 客户端本地模拟的碰炸闪光弹引爆时通知权威端，在服务器上对活动 AI 执行与手投闪光弹一致的致盲判定 */
	UFUNCTION(Server, Reliable)
	void ServerApplyFlashBurst(FVector_NetQuantize Location);

#if !UE_BUILD_SHIPPING
	/** 调试：树现在的状态路径（Root/Armed/Action/Root/Ready）；树没在跑时为空 */
	FString GetActionStatePath() const;

	/** 调试：这一帧的约束，回放记下 */
	bool IsAimAllowed() const { return bAimAllowed; }

	/** 调试：还记着想开火，回放记下 */
	bool WantsFire() const { return bWantsFire; }

	/** 调试：还记着想装填，回放记下 */
	bool WantsReload() const { return bWantsReload; }

	/** 调试：还记着想从抛壳口压膛（轮盘确认了一种、下一枪还不是它），回放记下 */
	bool WantsChamberSwap() const { return bWantsChamberSwap; }

	/** 调试：装填键的长按（按下了在等、按了多久），回放记下 */
	const FGinkgoReloadKey& GetReloadKey() const { return ReloadKey; }

	/** @struct FDebugShots : 调试：开过几枪、最后一枪的线，回放记下 */
	struct FDebugShots
	{
		int32 Count = 0;
		FVector Muzzle = FVector::ZeroVector;
		FVector Point = FVector::ZeroVector;
		bool bHit = false;
		/** 没偏的那条线打到的地方（圆锥的中心，红 X），这一枪的扩散与实际偏了多少（度） */
		FVector AimedPoint = FVector::ZeroVector;
		float Spread = 0.f;
		float Deviation = 0.f;
		/** 弹道的烟（Docs/Design/Weapons.md「弹道的烟」）：放过几道，最后一道多长（cm）、几个点、长到头的快慢（FGinkgoTrailShape::Reveal） */
		int32 Trails = 0;
		float TrailLength = 0.f;
		int32 TrailPoints = 0;
		float TrailReveal = 0.f;
	};

	const FDebugShots& GetDebugShots() const { return DebugShots; }

	/** 调试：弹道的烟还有几道在画（组件还开着）。回放记下：寿命过了要回到 0，不然组件没回池 */
	int32 GetDebugTrailsAlive() const;

	/** 调试：这一帧算扩散用的。回放记下 */
	const FGinkgoSpreadInput& GetSpreadInput() const { return SpreadInput; }

	/** 调试：这个时刻响过几次（回放记下；没有声音的进程里也算） */
	int32 GetDebugCueCount(const FGameplayTag& Cue) const
	{
		const int32* Count = DebugCues.Find(Cue);
		return Count ? *Count : 0;
	}
#endif

#if !UE_BUILD_SHIPPING
	/** @struct FDebugEject : 调试：抛过几个、最新的那个，回放记下 */
	struct FDebugEject
	{
		int32 Count = 0;
		int32 LiveCount = 0;
		FVector Location = FVector::ZeroVector;
		double Speed = 0.0;
		/** 撞出过几声、其中几声是重的，最后一声撞的速度、音量 */
		int32 HitSounds = 0;
		int32 HardHitSounds = 0;
		double LastHitSpeed = 0.0;
		float LastHitVolume = 0.f;
	};

	/** 调试：抛壳的统计。位置、速度是这一刻最新那个的 */
	FDebugEject GetDebugEject() const;
#endif

	/** 调试：某个槽的细烟还剩多少（1 刚开枪，0 冒完）。回放记下；没有画面的进程里没有粒子，这个数照走 */
	float GetMuzzleWispRemaining(const int32 Slot) const { return WispRemaining[Slot]; }

	/** 某个槽的枪管比环境高出多少（°C，Docs/Design/Weapons.md「枪管的热」）。没有画面的进程里也照算 */
	float GetBarrelHeat(const int32 Slot) const { return BarrelHeat[Slot]; }

	/** 某个槽这一刻的热浪有多强（0–1，Ginkgo::Weapon::HeatHazeAlpha） */
	float GetHeatHazeAlpha(const int32 Slot) const;

	/** 调试（ginkgo.weapon.heat）：直接给一个槽里现在这把枪的枪管设多热 */
	void SetBarrelHeat(int32 Slot, float Heat);

	/**
	 * @brief 播放当前武器 cue。开火区分本地玩家近身声与其他角色的世界枪口声；其他动作继续挂在枪上。
	 * @param Cue - Weapon.Cue.*
	 * @param Override - 有替代声音时沿用 cue 的音量与淡出。开火优先取已装配件的 FireSound，再取这一发子弹的 FireSound
	 * @return 本次播放的组件，由音频系统在结束时自动销毁；无资产、无设备或世界枪声未配衰减时返回空。
	 */
	UAudioComponent* PlayCue(const FGameplayTag& Cue, USoundBase* Override = nullptr);

	/** 工作台的机械接触声。优先取当前枪的指定提示，缺少时用备用提示；音量倍数只影响这次接触。 */
	void PlayAttachmentContact(FGameplayTag Preferred, FGameplayTag Fallback, float VolumeScale);

	/**
	 * @brief 实际接触的通知到了，用对应武器的 ContactCameraShakes 向本地相机上下文发一个 Handling 事件
	 * @param Slot - 做动作的武器槽，收枪、拔枪的挂点切换时也用这一槽
	 * @param Contact - 动画事件名，武器未配置这项时跳过。事件时间取接触时的世界时间，随机取样与开火分开
	 */
	void PlayContactShake(int32 Slot, FName Contact);

	/**
	 * @brief 放一声命中的声音（Docs/Design/Damage.md「命中的声音」，设置的 HitSounds）：2D 的，只有本地玩家听得到。两条队列互不相干：
	 *        命中、打在护甲上、爆头一次只响一声（前一声按 FadePrevious 淡掉）；打穿护甲、打死、爆头打死不随下一次命中取消，最多同时响 PriorityCapacity 声。
	 *        开枪打中了目标时调；调试（ginkgo.combat.hitSoundTest、hitSoundSeq）也直接调
	 * @return 放了没有（这一种的声音空着、没有声音设备时没放）
	 */
	bool PlayHitSound(EGinkgoHitSound Sound);

	/** @brief 把这把枪开枪要用的声音、特效先读进来，细烟先激活一次：第一枪不等磁盘，也不卡一帧（放进槽的时候调） */
	void PreloadFireAssets(AGinkgoCharacter& Character, int32 Slot, const UGinkgoWeaponDefinition& Weapon);

	/** @brief 开镜键按着没有（本地控制的角色，控制器每帧给）。按下的那一下让装填停在这一发 */
	void SetWantsToAim(bool bInWantsToAim);

	/**
	 * @brief 推进一帧：先是动作状态的树，再是持枪状态（选姿态、走两个进度）。本地控制的角色每帧在控制器的 PlayerTick 里调，移动之前
	 * @note 开局要拿在手上（设置里的 bStartWeaponInHand）时，第一个槽复制下来的那一帧在这里拿到手上。
	 *       这一帧的地速倍率也在这里交给移动组件（SetSpeedScales），移动带着它进存档。
	 *       推进完覆盖写相机上下文：姿态是开镜时有 Weapon.ADS，开镜进度（两头缓过的）写成 Weapon.Value.AdsAlpha，
	 *       这把枪的倍率写成 Weapon.Value.AdsZoom
	 */
	void UpdateHoldState(float DeltaSeconds);

	/** 持枪状态的快照：动画、相机读 */
	const FGinkgoHoldState& GetHoldState() const { return HoldState; }

	/** 某个槽的机构状态（本地的）；槽空着、枪没有机构时为空。武器栏读它 */
	const FGinkgoMechanismState* GetMechanismState(int32 Slot) const;

	/** 后备子弹（本地的） */
	const FGinkgoAmmoReserve& GetAmmoReserve() const { return AmmoReserve; }

	/** 采集已提交的枪械事实；装填中也按当前供弹/枪机状态保存，不记临时手部动作。 */
	FGinkgoWeaponSaveState CaptureSaveState() const;

	/** 在新生角色上整体恢复存档；先恢复装配规则，再恢复弹药，最后同步挂点与动画层。只供权威端调用。 */
	bool RestoreSaveState(const FGinkgoWeaponSaveState& State);

	/**
	 * @brief 对某个槽的枪发一个机构事件。第 4 批由蒙太奇的通知发；现在只有调试命令 ginkgo.weapon.event
	 * @param Ammo - RoundIn、MagIn 用哪种弹；空的用这把枪的弹种
	 * @return 前提满足、改了没有
	 */
	bool ApplyMechanismEvent(int32 Slot, EGinkgoMechanismEvent Event, FGameplayTag Ammo = FGameplayTag());

	/** @brief 直接设某个槽的机构状态（调试命令 ginkgo.weapon.ammo）；槽空着、枪没有机构时不设 */
	void SetMechanismState(int32 Slot, const FGinkgoMechanismState& State);

	/** @brief 直接设某种弹的后备发数（调试命令 ginkgo.weapon.ammo） */
	void SetReserveRounds(const FGameplayTag& Ammo, int32 Rounds);

	/**
	 * @brief 权威端写入开局配装的精确后备弹药表，替换武器默认自带的 StartingReserve，并同步到客户端
	 * @param Entries - 开局允许保留的弹种与发数列表；未列入的弹种将被清空
	 */
	void ApplyStartingReserve(TConstArrayView<FGinkgoStartingAmmoEntry> Entries);

	/**
	 * @brief 后备加几发：拾取子弹盒在本地给（Docs/Design/Interaction.md「拾取子弹盒」）
	 * @note 后备只在本地，同射击：在拾取的那台机器上调用。武器栏读后备，变了就亮
	 */
	void AddReserveRounds(const FGameplayTag& Ammo, int32 Rounds);

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

private:
	AGinkgoCharacter* GetCharacter() const;

	/** 每个槽有没有枪：换枪的规则（纯函数）读它 */
	TArray<bool, TInlineAllocator<SlotCount>> GetCarriedFlags() const;

	/** 动作状态的根树：第一次用到时读（设置里的 WeaponStateTree）；读不到时为空 */
	const UStateTree* GetActionTree();

	/** 推进一帧动作状态的树；还没开始（开局、回放重启）时先开始 */
	void TickActionStates(float DeltaSeconds);

	/** 给执行上下文接上外部数据与手上这把枪的子树 @return 上下文能不能用 */
	bool SetupActionContext(FStateTreeExecutionContext& Context);

	/** 开一枪：按下那一帧改机构、算命中（Docs/Design/ActionStates.md「开火不等通知」），返回放片段的动作 */
	UGinkgoAnimAction* MakeFireAction(AGinkgoCharacter& Character);

	/**
	 * @brief 一颗弹丸打到的东西：可打击的目标（活着的）按子弹算伤害（Docs/Design/Damage.md），这一枪的命中记进 Shot
	 * @param Direction - 这颗弹丸的方向
	 * @param PenetrationScale - 穿过东西以后伤害乘多少（Docs/Design/Damage.md「穿射」，FGinkgoPenetrationHit::DamageScale）
	 * @param Penetrated - 打到它之前穿过了几个东西
	 */
	void HitTarget(const FHitResult& Hit, const UGinkgoAmmoDefinition& Ammo, const UGinkgoWeaponDefinition& Weapon, const FVector& Muzzle, const FVector& Direction,
		FGinkgoHitConfirm& Shot, float PenetrationScale, int32 Penetrated);

	/** 想开火，不能响：咔一声，意图用掉。没有片段，开始后马上完成 */
	UGinkgoAnimAction* MakeDryFireAction(AGinkgoCharacter& Character);

	/**
	 * 枪上要两只手的一步（拉栓、闭栓、开栓、起手、压一发、换弹匣）：放定义里这一步的片段，机构的事件来自蒙太奇的通知，没到通知的跳过。
	 * 占两条胳膊（优先级见 Ginkgo::Occupancy）；开栓、起手、压一发做完时手在拉柄上（bHandAtAction）
	 */
	UGinkgoAnimAction* MakeGunStepAction(AGinkgoCharacter& Character, const FGameplayTag& Step);

	/** 要拿的那把枪还能不能压（UGinkgoWeaponMechanism::CanLoadRound，压这个槽的 LoadAmmo） */
	bool CanLoadTargetWeapon() const;

	/** 要拿的那把枪压满了、泵一下还能再压（UGinkgoWeaponMechanism::CanLoadAfterCycle）：装填的意图留着 */
	bool CanLoadTargetWeaponAfterCycle() const;

	/** 要拿的那把枪从抛壳口压膛有没有用（UGinkgoWeaponMechanism::CanLoadChamber） */
	bool CanLoadTargetWeaponChamber() const;

	/**
	 * @brief 装填要压的那种（特殊弹）后备没了：换回枪的弹种（我定的：特殊弹打完了再按装填就装普通弹）。普通弹没了不自动用特殊弹，要在轮盘里选
	 * @return 换了没有
	 */
	bool ResolveLoadAmmo(int32 Slot);

	/** @brief 想装填（短按、轮盘确认、空扣转去装填）：意图记下，要压的那种没了先换 */
	void StartReload();

	/** @brief 现在打不打得开轮盘：手上的就是要拿的那把、在手上（不在背上）、有轮盘、没在开镜、没在捡枪 */
	bool CanOpenAmmoWheel() const;

	void OpenAmmoWheel();

	/** @brief 松开装填键：按选的那一种换。后备里没有它时什么也不改 */
	void ConfirmAmmoWheel();

	/** @brief 轮盘关上（Result 是怎么关的）；按着的装填键也作废，松开时什么也不做 */
	void CloseAmmoWheel(EGinkgoAmmoWheelResult Result);

	/** @brief 开镜、开火、换枪、捡枪时：轮盘开着就取消，按着的装填键作废 */
	void CancelReloadKey();

	/** 下一枪（膛里的活弹，或者拉一下进膛的那一发）的子弹数据：扩散按它改。枪里一发也没有时为空 */
	const UGinkgoAmmoDefinition* GetNextRoundAmmoDefinition(int32 Slot) const;

	/** 树的节点要的外部数据：武器组件 */
	bool CollectActionData(const FStateTreeExecutionContext& Context, const UStateTree* StateTree, TArrayView<const FStateTreeExternalDataDesc> Descs, TArrayView<FStateTreeDataView> OutDataViews);

	/**
	 * @brief 手上的枪的动画层链上去；Weapon 为空时等层淡完再撤掉现在的（UGinkgoWeaponAnimLayers 的 LayerBlendTime）
	 * @note 不用换层的惯性化：它混整个姿势与全部曲线，跑着拔枪时腿与腿部 IK 的曲线跟着乱（见 Docs/Design/Weapons.md「换层」）
	 */
	void LinkAnimLayers(const UGinkgoWeaponDefinition* Weapon);

	/** 撤掉现在链着的层，回到主蓝图自己那份（原样输出） */
	void UnlinkAnimLayers();

	/** 撤层之前比层的淡出多等的（s）：层按动画的帧长走，定时器按世界的时间，差一帧时还没淡完 */
	static constexpr float UnlinkMargin = 0.1f;

	/** 携带武器槽位复制到达时：先同步机构，再按需覆写开局后备并刷新网格 */
	UFUNCTION()
	void OnRep_CarriedWeapons();

	/** 开局后备弹药覆盖复制到达时：若武器槽已完成机构同步则立即覆写后备 */
	UFUNCTION()
	void OnRep_StartingReserve();

	/** 判断当前所有携带槽是否都已同步到本地机构状态，用于避免客户端在 OnRep 顺序差异下漏覆写后备 */
	bool AreCarriedMechanismsSynced() const;

	/** 当存在尚未消费的开局后备序号且武器机构同步完成时，用服务器指定的开局后备整体覆写本地 AmmoReserve */
	void TryApplyStartingReserve();

	/** @brief 右手空了就伸手；地上那把没了就不捡了 */
	void UpdatePendingPickup();

	/** @brief 服务器上：地上放一把，槽清空 */
	void SpawnDroppedWeapon(int32 Slot, const FGinkgoMechanismState& State, const FTransform& Transform, const FVector& Velocity, const FVector& AngularVelocity);

	/** 客户端丢枪：机构状态只在本地，一起发上去 */
	UFUNCTION(Server, Reliable)
	void ServerDropWeapon(int32 Slot, const FGinkgoMechanismState& State, const FTransform& Transform, const FVector& Velocity, const FVector& AngularVelocity);

	/** 服务器确认换匣后，拥有者以本地弹量结算一次。容量随事件传递，避免依赖配件属性复制的先后次序。 */
	UFUNCTION(Client, Reliable)
	void ClientRefillWorkbenchMagazine(int32 Slot, UGinkgoWeaponDefinition* Weapon, int32 Capacity);

	/** 弹匣入位时把旧匣余弹归还到对应弹种，再从选中弹种装入新匣。保留膛内状态。 */
	void RefillWorkbenchMagazine(int32 Slot, const UGinkgoWeaponDefinition* Weapon, int32 Capacity);

	/** 只播放已解析的武器提示音。常规动画另行调度相机震动，工作台借用音色时只走声音。 */
	UAudioComponent* PlayCueAudio(const UGinkgoWeaponDefinition& Weapon, const FGameplayTag& Cue, const FGinkgoWeaponCue& Item, USoundBase* Override, float VolumeScale);

	/**
	 * @brief 槽里换了枪：新的枪装满、上好膛，带上它的后备子弹；拿走了的槽清掉
	 * @note 每台机器各自做（机构状态只在本地）：服务器在 SetCarried 里，客户端在复制下来时
	 */
	void SyncMechanisms();

	/** 各个槽里的枪（SlotCount 个，空的是没有枪）：服务器定，复制给各端 */
	UPROPERTY(ReplicatedUsing = OnRep_CarriedWeapons, Transient)
	TArray<TObjectPtr<UGinkgoWeaponDefinition>> CarriedWeapons;

	/** 下标与 CarriedWeapons 一致，两把同型号的枪也各自保存 */
	UPROPERTY(ReplicatedUsing = RefreshAttachmentMeshes, Transient)
	TArray<FGinkgoInstalledAttachments> InstalledAttachments;

	UPROPERTY(Transient)
	TArray<FGinkgoAttachmentMeshes> AttachmentMeshes;

	/** 每槽的配件效果缓存，RefreshAttachmentMeshes 在装配或武器引用改变时重建 */
	TArray<FGinkgoAttachmentEffects> AttachmentEffects;

	/** 每槽独立的容量规则；无容量覆盖时为空，读取回退到武器定义中的机构 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UGinkgoWeaponMechanism>> EffectiveMechanisms;

	/** 机构副本对应的原始武器，防止同槽换枪后误用上一把枪的规则 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UGinkgoWeaponDefinition>> EffectiveMechanismSources;

	/** 正在捡的那把（BeginWeaponPickup 到 FinishWeaponPickup）：地上的枪、捡到哪个槽、哪把枪、右手伸出去了没有 */
	TWeakObjectPtr<AGinkgoWeaponPickup> PendingPickup;
	int32 PendingPickupSlot = INDEX_NONE;
	TObjectPtr<UGinkgoWeaponDefinition> PendingPickupWeapon;
	bool bPickupReachStarted = false;

	/** 捡的那把带着丢下时的状态：放进槽时照它，不再装满、不再送后备（SyncMechanisms） */
	bool bPendingPickupHasState = false;
	FGinkgoMechanismState PendingPickupState;

	/** 下一次拔枪是捡枪接上来的（ConsumeDrawFromPickup） */
	bool bDrawFromPickup = false;

	/** BeginDrawFromBack 记下的：拔哪个槽、什么时候记的（世界时间）。这期间目标变了、拔枪没放起来时过一会儿就作废 */
	int32 DrawFromBackSlot = INDEX_NONE;
	double DrawFromBackTime = 0.0;

	/** 捡枪抓住以后的收拢：哪个槽、从哪一刻开始（世界时间）、多久；不在收拢时槽是 INDEX_NONE */
	int32 PickupSettleSlot = INDEX_NONE;
	double PickupSettleStart = 0.0;
	float PickupSettleDuration = 0.f;
	FGinkgoPickupSettleStart PickupSettleStartPose;

	/** 丢枪时转多快的随机数：同一条路线每次一样 */
	FRandomStream DropRandom = FRandomStream(19450508);

	/** 每个槽的机构状态（SlotCount 个）：只在本地，只在机构事件上改 */
	UPROPERTY(Transient)
	TArray<FGinkgoMechanismState> Mechanisms;

	/** 每个槽的机构状态是哪把枪的：槽里换了枪时重置 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<const UGinkgoWeaponDefinition>> MechanismWeapons;

	/** 后备子弹：按弹种记数，只在本地 */
	UPROPERTY(Transient)
	FGinkgoAmmoReserve AmmoReserve;

	/** 服务器下发的开局后备弹药表；与 StartingReserveSerial 搭配复制到客户端，仅在开局同步完成时应用一次 */
	UPROPERTY(ReplicatedUsing = OnRep_StartingReserve, Transient)
	TArray<FGinkgoStartingAmmoEntry> StartingReserveEntries;

	/** 开局后备弹药表的单调递增版本号；0 表示未下发覆盖（沿用武器默认 StartingReserve） */
	UPROPERTY(ReplicatedUsing = OnRep_StartingReserve, Transient)
	uint32 StartingReserveSerial = 0;

	/** 本机已消费的开局后备版本号；防止同一份开局后备在后续拾取/换枪时重复覆写 */
	uint32 AppliedStartingReserveSerial = 0;

	/** 当前手持槽。玩家侧随装备动作更新，敌人的模拟代理由服务器姿势快照还原；INDEX_NONE 表示空手。 */
	int32 HeldSlot = INDEX_NONE;

	/** 手要去哪个槽：本地的，见 GetTargetSlot */
	int32 TargetSlot = INDEX_NONE;

	/** 上一次手上是哪个槽 */
	int32 LastHeldSlot = INDEX_NONE;

	/** 动作状态的根树（GetActionTree） */
	UPROPERTY(Transient)
	TObjectPtr<const UStateTree> ActionTree;

	/** 树的实例数据：状态、任务的实例。收枪、丢枪、回放都可以重启，事实不在这里 */
	UPROPERTY(Transient)
	FStateTreeInstanceData ActionStateData;

	/** 读过根树了（读不到时不再读） */
	bool bActionTreeLoaded = false;

	/** 动作状态的约束：这一步里能不能开镜 */
	bool bAimAllowed = true;

	/** 树正跑着哪一步（StartStep 到 EndStep）；没在跑时为空 */
	FGameplayTag CurrentStep;

	/** @brief 想开枪：按过开火键，或者连发的枪按着开火键、这一发还能响（打空以后按着不算，要再按一下才转去装填） */
	bool WantsToShoot() const;

	/** @brief 每帧：瞄准后坐摊到视线上，算玩家压掉了多少，停火以后回复（FGinkgoPatternRecoil） */
	void UpdateRecoil(const AGinkgoCharacter& Character, float DeltaSeconds);

	/** 想开火（按键记下来的意图，不是机构的事实）：见 HandleWeaponKeys */
	bool bWantsFire = false;

	/** 开火键按着（SetTriggerHeld） */
	bool bTriggerHeld = false;

	/** 连发的下一枪最早什么时候响（世界时间）：Ginkgo::Weapon::GetNextShotTime */
	double NextShotTime = -TNumericLimits<float>::Max();

	/** 这一次树的更新里上一枪刚打完（EndStep 记，树更新完清）：这时候再开枪就是接着响的 */
	bool bShotChained = false;

	/** 每个提示音上一次放的声音：FGinkgoWeaponCue::FadePrevious */
	TMap<FGameplayTag, TWeakObjectPtr<UAudioComponent>> LastCueAudio;

	/** 命中、打在护甲上、爆头：上一声（FGinkgoHitSounds::FadePrevious） */
	TWeakObjectPtr<UAudioComponent> LastHitAudio;

	/** 打穿护甲、打死、爆头打死自己的队列：还在响的，老的在前（FGinkgoHitSounds::PriorityCapacity） */
	TArray<TWeakObjectPtr<UAudioComponent>> PriorityHitAudio;

	/** 上次冒硝烟的时刻（世界时间）：MuzzleSmokeInterval */
	double LastMuzzleSmokeTime = -TNumericLimits<float>::Max();

	/** 上一道弹道的烟那一枪排的时刻（世界时间，Ginkgo::Weapon::GetScheduledShotTime）：设置的 BulletTrailMinInterval */
	double LastTrailShotTime = -TNumericLimits<float>::Max();

	/** 瞄准后坐 */
	FGinkgoRecoilState AimRecoil;

	/** 上一帧结束时的视线：同这一帧开头比，差的就是玩家动的 */
	FRotator LastRecoilView = FRotator::ZeroRotator;

	bool bRecoilViewValid = false;

	/** 这一帧后坐把视线动了多少：腰射的手感弹簧要去掉它 */
	FVector2D RecoilMovedThisFrame = FVector2D::ZeroVector;

	/** 想装填（同上） */
	bool bWantsReload = false;

	/**
	 * 想从抛壳口压膛（霰弹枪，Docs/Design/Weapons.md「切换弹种」）：轮盘确认了一种、下一枪还不是它。膛里是它的活弹、压不了、按了开火或开镜、换了枪为止
	 */
	bool bWantsChamberSwap = false;

	/** 这一帧按下了装填键（HandleWeaponKeys 记，UpdateAmmoWheel 用掉） */
	bool bReloadPressed = false;

	/** 装填键的长按 */
	FGinkgoReloadKey ReloadKey;

	/** 弹种轮盘 */
	FGinkgoAmmoWheel AmmoWheel;

	/** 每个槽装填时压哪种弹：轮盘选的，槽里换了枪时是枪的弹种 */
	FGameplayTag LoadAmmo[SlotCount];

	/** 手在拉柄上：开栓、起手、压一发做完的那一帧；下一步不接着时手就回握把了（淡出），所以这一帧树选完就清掉 */
	bool bHandAtAction = false;

	/** 动作状态的约束：这一步两只手都在枪上 */
	bool bBothHandsOnGun = false;

	/** 动作状态的约束：这一步里能不能抬枪 */
	bool bRaiseAllowed = true;

	/** 枪的踢动的弹簧 */
	FGinkgoGunKickState GunKick;

	/** @brief 每帧：低持、站着不动走多久了，够了就放一次放松（枪配了才有）。一动就从头算 */
	void UpdateIdleRelax(const AGinkgoCharacter& Character, const UGinkgoWeaponDefinition* Weapon, float DeltaSeconds);

	/** 站着不动多久了；这一次站够多久放（负的是还没取）；站够了 */
	float IdleStillTime = 0.f;
	float IdleRelaxWait = -1.f;
	bool bIdleRelaxDue = false;

	/** 放松的等待取数：种子固定，回放里每次一样；不动开枪的那一条 */
	FRandomStream IdleRelaxRandom = FRandomStream(19111229);

	/** @brief 每帧：腰射操作性的弹簧追这一帧的视线。枪不在手上时清零，拿到手上从屏幕中心开始追 */
	void UpdateHipAim(const AGinkgoCharacter& Character, const UGinkgoWeaponDefinition* Weapon, float DeltaSeconds);

	FGinkgoHipAimState HipAim;

	/**
	 * @brief 开栓或折开退弹时抛出膛里的东西：空壳，或者活弹。物理的，只碰场景，不挡人，不挡瞄准的线
	 * @param Chamber - 退膛之前膛里是什么。空膛不抛
	 * @param EjectedAmmo - 退出的那发子弹的弹种标签；活弹且该弹种配了专用 LiveRoundMesh 时使用对应弹头模型
	 */
	void EjectRound(int32 Slot, EGinkgoChamber Chamber, FGameplayTag EjectedAmmo = FGameplayTag());

	/** @brief 抛出去的撞到东西：按撞的速度放一声（Docs/Design/ActionStates.md「子弹与弹壳」） */
	UFUNCTION()
	void OnEjectedRoundHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	/** 地上最多留多少个：再抛回收最老的 */
	static constexpr int32 MaxEjectedRounds = 24;

	/** 抛出去的：环形用，第一次用到才建 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> EjectedRounds;

	/** 下一个用 EjectedRounds 的哪个 */
	int32 NextEjectedRound = 0;

	/** 这个壳是哪把枪抛的（壳还在飞就收了枪，落地照样响）、上一声是什么时候（世界时间），同 EjectedRounds 一样长 */
	struct FEjectedRoundSound
	{
		TWeakObjectPtr<const UGinkgoWeaponDefinition> Weapon;
		double LastTime = 0.0;
	};
	TArray<FEjectedRoundSound> EjectedRoundSounds;

	/** 自动装填的枪开了火还没抛出来的壳，每个槽还要等多久（枪上的 EjectDelay：等套筒退到后面）；0 是没有 */
	float PendingEject[SlotCount] = {};

	/** 这一枪还要多久才恢复、一共多久：准星两边的横线跟着它 */
	float ShotRecoveryRemaining = 0.f;
	float ShotRecoveryDuration = 0.f;

	/** 抛壳专用的随机流：种子固定，回放里每次一样；不动开枪那一条，开枪取到的数不变 */
	FRandomStream EjectRandom = FRandomStream(19390901);

#if !UE_BUILD_SHIPPING
	int32 DebugEjectCount = 0;
	int32 DebugEjectLiveCount = 0;
	int32 DebugHitSounds = 0;
	int32 DebugHardHitSounds = 0;
	double DebugLastHitSpeed = 0.0;
	float DebugLastHitVolume = 0.f;
#endif

	/** 上一帧的视线：算这一帧转了多少 */
	FRotator LastHipAimView = FRotator::ZeroRotator;
	bool bHipAimViewValid = false;

	/** 第一人称时晃枪的那一份（GetSightSway）：UpdateHoldState 里按相机这一帧末的时钟取 */
	FRotator SightSway = FRotator::ZeroRotator;

	/** 第一人称时枪跟着相机后坐转的那一份（GetSightRecoil）：UpdateHoldState 里按相机这一帧要用的输入推一份 */
	FRotator SightRecoil = FRotator::ZeroRotator;

	/** 枪的踢动的变换：上一次推进弹簧以后的 */
	FTransform GunKickTransform = FTransform::Identity;

	/** 真加在枪上的踢动（第一人称时乘 KickScale）：上一次求姿势用的，去掉枪上的踢动时用它 */
	FTransform AppliedGunKickTransform = FTransform::Identity;

	/** 第一人称开镜时枪相对眼睛的偏移（GetSightOffset、GetSightMoveSway）与它绕枪托的变换：UpdateHoldState 里按这一帧的开镜进度算 */
	FRotator SightOffset = FRotator::ZeroRotator;
	FRotator SightMoveSway = FRotator::ZeroRotator;
	FVector SightMoveShift = FVector::ZeroVector;
	FTransform SightOffsetTransform = FTransform::Identity;

	/** 走路的偏与晃的弹簧与步的相位：不在第一人称时清掉，下次从那一刻的速度开始追 */
	Ginkgo::Scope::FMoveSwayState MoveSway;

	/** 枪的踢动、相机晃的方向每枪随机，种子固定：回放里每枪都一样 */
	FRandomStream ShotRandom = FRandomStream(19980812);

	/** 操作枪的相机震动（CueCameraShakes）取数用：单独一条，开枪的随机流取数的次数不变 */
	FRandomStream HandlingRandom = FRandomStream(20261112);

	/**
	 * @struct FPendingCueShake : 等着推给相机的操作枪的震动。动画的提示按声音的提前量早发（FGinkgoWeaponCue::Lead，98k 拉到底那一下早 0.2 s），
	 * 震动要等到撞上的那一刻（声音的撞击点）：PlayCue 记下来，每帧开头（UpdateHoldState）把到了的推给相机，事件的时刻就是撞上的那一刻
	 */
	struct FPendingCueShake
	{
		TWeakObjectPtr<const UGinkgoWeaponDefinition> Weapon;
		FGameplayTag Cue;
		double Time = 0.0;
	};
	TArray<FPendingCueShake> PendingCueShakes;

	/** @brief 到了时刻的操作枪的震动推给相机（每层取一次数，带在 Weapon.Event.Handling 上） */
	void PushDueCueShakes();

	/** 这一枪偏到哪，种子固定：回放里每枪都一样。单独一个流，枪踢、相机晃取到的数不变 */
	FRandomStream SpreadRandom = FRandomStream(19110329);

	/** 这一帧算扩散用的。连射累积：开一枪加，按恢复速度收回，换枪清零 */
	FGinkgoSpreadInput SpreadInput;

	/** 在空中、蹲着、滑铲多少（线性）：进出都缓过，不然跑过小坎时准星闪一下 */
	float SpreadAirProgress = 0.f;
	float SpreadCrouchProgress = 0.f;
	float SpreadSlideProgress = 0.f;

	/** 进出空中、蹲姿、滑铲时系数走多久（s） */
	static constexpr float SpreadPostureBlendTime = 0.1f;

	/** 最后一次打中目标的那一枪 */
	FGinkgoHitConfirm HitConfirm;

#if !UE_BUILD_SHIPPING
	FDebugShots DebugShots;
	TMap<FGameplayTag, int32> DebugCues;
	/** 放过的弹道的烟（GetDebugTrailsAlive 数还开着的） */
	TArray<TWeakObjectPtr<UNiagaraComponent>> DebugTrails;
#endif

	/**
	 * @brief 弹道的烟（Docs/Design/Weapons.md「弹道的烟」）：每颗弹丸一道，从枪口到打到的地方。子弹没配（榴弹之类的）、太短的不放；没有画面的进程里不放
	 * @param Ends - 每颗弹丸打到的地方（没打到东西是射线的最远处）
	 */
	void SpawnBulletTrails(const UGinkgoAmmoDefinition& Ammo, const FVector& Start, TConstArrayView<FVector> Ends);

	/** 服务器模拟的每枪合并发送一次。瞬时表现允许丢包，接收端无需重新追踪或结算伤害。 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBulletImpacts(const TArray<FGinkgoBulletImpact>& Impacts);

	/** 预载的声音、空间衰减与特效；由组件强引用保持到自身销毁，避免软引用资产被回收后在下一枪重新加载。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> LoadedFireAssets;

	/** @brief 这个槽的细烟组件，没有就建，挂在枪上、放在枪口。没有画面的进程里是空的 */
	UNiagaraComponent* GetWispComponent(AGinkgoCharacter& Character, int32 Slot, const UGinkgoWeaponDefinition& Weapon);

	/** @brief 开一枪：这个槽的细烟从头冒 */
	void StartMuzzleWisp(AGinkgoCharacter& Character, int32 Slot);

	/**
	 * @brief 这一帧的扩散写进持枪状态：累积按恢复速度收回，离地、落地缓过（Docs/Design/Weapons.md「散布」）
	 * @param DeltaSeconds - 0 时只重算：开枪的这一帧打完以后，准星这一帧就张开
	 */
	void UpdateSpread(const UGinkgoWeaponDefinition* Weapon, float DeltaSeconds);

	/** @brief 每帧：走细烟剩下的时间，把浓度交给粒子，冒完停掉 */
	void UpdateMuzzleWisps(float DeltaSeconds);

	/** @brief 这个槽的热浪组件，没有就建：挂在枪上、枪口往后 MuzzleHeatOffset，X 是枪管。没有画面的进程里是空的 */
	UNiagaraComponent* GetHeatComponent(AGinkgoCharacter& Character, int32 Slot, const UGinkgoWeaponDefinition& Weapon);

	/** @brief 每帧：每个槽的枪管凉一点，开枪那一下的热气退一点，热浪按它们冒；不热了停掉。槽里换了枪从凉的算 */
	void UpdateBarrelHeat(float DeltaSeconds);

	/** 枪口的细烟（SlotCount 个）：按槽算，枪收到背上还接着冒。挂在这个槽的网格上，放在枪口；第一次开枪时才有 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraComponent>> WispComponents;

	/** 每个槽的细烟还剩多少：开枪时 1，冒完 0 */
	float WispRemaining[SlotCount] = {};

	/** 枪口的热浪（SlotCount 个）：按槽算，枪收到背上还接着冒。挂在这个槽的网格上；第一次热起来时才有 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraComponent>> HeatComponents;

	/** 每个槽的枪管比环境高出多少（°C）、开枪那一下的热气还剩多少（0–1）、算的是哪把枪（换了枪从凉的算） */
	float BarrelHeat[SlotCount] = {};
	float HeatPuff[SlotCount] = {};
	TWeakObjectPtr<const UGinkgoWeaponDefinition> HeatWeapons[SlotCount];

	/** 持枪占着右臂的句柄（UGinkgoAnimActionComponent::Occupy），枪不在手上时 INDEX_NONE */
	int32 HoldOccupancy = INDEX_NONE;

	UPROPERTY(Transient)
	FGinkgoHoldState HoldState;

	/** 现在链着的层的类：换层时先撤掉它；徒手以后淡出的这段时间还链着 */
	UPROPERTY(Transient)
	TObjectPtr<UClass> LinkedLayerClass;

	/** 淡完以后撤层 */
	FTimerHandle UnlinkTimer;

	bool bWantsToAim = false;

	/** 开局拿到手上的那一次做过了 */
	bool bStartHoldDone = false;

	/** 角色上的相机上下文：第一次写时找 */
	TWeakObjectPtr<UGinkgoCameraContextComponent> CameraContext;
};
