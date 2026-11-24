#pragma once

#include "CoreMinimal.h"
#include "Control/GinkgoInputIntent.h"    // EGinkgoWeaponKey
#include "Curves/CurveFloat.h"            // FRuntimeFloatCurve：速度-扩散修正
#include "GameplayTagContainer.h"         // 弹种轮盘列的弹种
#include "GinkgoWeaponTypes.generated.h"

class UAnimSequence;
class UGinkgoRecoilPattern;
class UGinkgoWeaponDefinition;
class UPhysicalMaterial;
class USoundBase;
class UStaticMesh;
struct FGinkgoCameraImpulse;

/**
 * @enum EGinkgoWeaponSlotKind : 武器槽的种类（你定的，Docs/Design/Weapons.md「武器槽」）
 * 枪的定义写它是哪一种，拿到时进这一种的空槽；种类也定了枪收在哪里。投掷物以后加一种，收在腰的左边
 */
UENUM(BlueprintType)
enum class EGinkgoWeaponSlotKind : uint8
{
	/** 主武器：背上 */
	Primary,
	/** 手枪：腰的右边 */
	Sidearm,
	/**
	 * 重武器（MG3）：只能拿在手上，收不起来（你说的）。没有键，捡起来才有；换枪、收枪都直接把它扔在地上，
	 * 捡别的枪时也是丢它，不收（Docs/Design/Weapons.md「武器槽」）
	 */
	Heavy,
};

/**
 * @enum EGinkgoHoldStance : 持枪的姿态
 * 见 Docs/Design/Weapons.md「持枪状态」。由动作状态定（第一批还没有树，先用 Ginkgo::Weapon::SelectStance 顶上），
 * 动画按它摆上半身，相机按它开镜，移动按它限速（第 3 批）
 */
UENUM(BlueprintType)
enum class EGinkgoHoldStance : uint8
{
	/** 手上没枪：枪在背上，或者没有枪 */
	Unarmed,
	/** 低持：拿着枪的常态，食指在护圈外 */
	Lowered,
	/** 腰射：枪抵肩、平举，食指在扳机上 */
	Hip,
	/** 开镜 */
	Ads,
	/** 在地面上冲刺：枪放低，不瞄准 */
	Sprint,
};

/** 模拟代理绘制持枪姿势所需的服务器快照。武器资产由携带槽复制，动画混合进度量化为字节。 */
USTRUCT()
struct FGinkgoReplicatedHoldPose
{
	GENERATED_BODY()

	/** 当前挂在手上的武器槽；INDEX_NONE 表示空手或已收起。 */
	UPROPERTY()
	int8 Slot = INDEX_NONE;

	/** 当前上半身姿态，出生后已拿枪的敌人从低持开始。 */
	UPROPERTY()
	EGinkgoHoldStance Stance = EGinkgoHoldStance::Unarmed;

	/** 从低持到抵肩的线性进度，0–255 对应动画的 0–1。 */
	UPROPERTY()
	uint8 Raise = 0;

	/** 从抵肩到开镜的线性进度，0–255 对应动画的 0–1。 */
	UPROPERTY()
	uint8 Ads = 0;

	/** 从低持到冲刺姿势的线性进度，0–255 对应动画的 0–1。 */
	UPROPERTY()
	uint8 Sprint = 0;

	/** 当前动作是否要求双手握枪，由权威端武器动作决定。 */
	UPROPERTY()
	bool bBothHandsOnGun = false;
};

/**
 * @enum EGinkgoWeaponRootState : 动作状态根树的四个状态（Docs/Design/ActionStates.md「树」）
 * 按手上的槽与目标槽（手要去的槽）选，按键只改目标槽，放完一段回到根重选
 */
UENUM(BlueprintType)
enum class EGinkgoWeaponRootState : uint8
{
	/** 手上没枪，也不要拿 */
	Unarmed,
	/** 手上没枪，要拔目标槽那把 */
	Draw,
	/** 手上就是目标槽那把 */
	Armed,
	/** 手上的不是目标槽那把（要徒手或要换枪）：先收起来 */
	Stow,
};

/**
 * @enum EGinkgoWeaponAction : 枪的子树现在该做什么（Docs/Design/ActionStates.md「每种枪机一棵子树」）
 * 按枪的事实与按键记下来的意图选（Ginkgo::Weapon::SelectWeaponAction），子树里每个状态的进入条件就是它。
 * 每做完一步回到子树的根重选，所以收枪再拔出来接着做不用另算
 */
UENUM(BlueprintType)
enum class EGinkgoWeaponAction : uint8
{
	/** 都不是：等按键 */
	Ready,
	/** 枪机开着、弹仓有弹：闭栓上膛 */
	CloseBolt,
	/** 膛里空壳、枪机关着，不在装填：拉栓，抛壳、上膛 */
	CycleBolt,
	/** 想装填，枪机关着：开栓，手留在拉柄上 */
	OpenBolt,
	/** 想装填，枪机开着，手不在拉柄上（拔出来接着压）：手伸过去 */
	ReloadStart,
	/** 想装填，枪机开着，手在拉柄上：压一发，手回到拉柄上。做完回根，两发之间能打断。管状弹仓是手在装填口下面，压一发或两发（Ginkgo::Weapon::ShouldLoadTwo） */
	LoadRound,
	/** 管状弹仓压完了（满了、没子弹、按了开火或开镜），手还在装填口下面：手回到护木上，枪放回低持 */
	ReloadEnd,
	/** 想开火，能响：抬到位就开 */
	Fire,
	/** 想开火，不能响：咔一声 */
	DryFire,
	/** 想装填，可拆弹匣，枪机关着（膛里还有）：换弹匣，枪机不动 */
	ReloadTactical,
	/** 想装填，可拆弹匣，枪机开着（打空了挂在后面）：换弹匣，放枪机回去 */
	ReloadEmpty,
	/** 站了一阵，放一次放松：一动就打断（Docs/Design/ActionStates.md「M1911」） */
	IdleRelax,
	/**
	 * 从抛壳口直接压一发进膛（霰弹枪，Docs/Design/Weapons.md「切换弹种」）：换了弹种要下一枪就是它，或者枪里一发也没有时装填先压膛（紧急装填）。
	 * 护木拉到底（膛里的出来）、放一发进去、推回去；护木已经在后面时从放一发开始
	 */
	LoadChamber,
};

/**
 * @struct FGinkgoWeaponActionFacts : 选子树的状态看的东西：枪的事实（机构状态）、按键记下来的意图与手空不空
 */
struct FGinkgoWeaponActionFacts
{
	/** 枪机开着 */
	bool bActionOpen = false;
	/** 膛里是空壳 */
	bool bChamberSpent = false;
	/** 能响：枪机关着，膛里是活弹 */
	bool bCanFire = false;
	/** 供弹里有弹（弹匣在枪上） */
	bool bFeedHasRounds = false;
	/** 想开火 */
	bool bWantsFire = false;
	/** 想装填 */
	bool bWantsReload = false;
	/** 还能压（可拆弹匣是还能换）：压得下，后备有（UGinkgoWeaponMechanism::CanLoadRound） */
	bool bCanLoad = false;
	/** 供弹是可拆弹匣：装填是换整个弹匣，不是逐发压 */
	bool bDetachableFeed = false;
	/** 枪机关着就能逐发压（管状弹仓，泵动霰弹枪）：不开栓，抬到装填位压，压完收回来，膛空就泵一下 */
	bool bLoadsWithActionClosed = false;
	/** 膛是空的（没有活弹也没有空壳）：没有空仓挂机的枪打空了就是这样，枪机关着，换了弹匣还要拉一次机柄 */
	bool bChamberEmpty = false;
	/** 手在拉柄上：刚开栓、刚压完一发，这一帧接着下一步 */
	bool bHandAtAction = false;
	/** 两条胳膊都不被比枪上的动作优先级高的占着 */
	bool bHandsFree = true;

	/** 左臂正在投掷且右手可用，允许当前已就绪的枪单手开火。 */
	bool bOneHandFire = false;
	/** 站够了：放一次放松（枪配了才有）。一动就没了 */
	bool bIdleRelaxDue = false;
	/** 开膛待击（MG3）：枪机在后面就是准备好了，不拉机柄、不装填，只有开火、空扣、放松 */
	bool bOpenBolt = false;
	/** 想从抛壳口压膛：换了弹种、下一枪还不是它（轮盘确认），或者想装填而枪里一发也没有（紧急装填）。武器组件按机构与意图算好 */
	bool bWantsLoadChamber = false;
	/** 压膛有用（UGinkgoWeaponMechanism::CanLoadChamber）：机构允许、后备有这种弹、膛里不是这种弹的活弹 */
	bool bCanLoadChamber = false;

	/** 单发无供弹仓（M79）：只有炮膛内这一发，区分空仓换弹与有弹重装填（高举重力甩弹），装好未合膛时走闭锁 */
	bool bSingleShot = false;
};

/**
 * @enum EGinkgoReloadKeyAction : 装填键这一帧要做什么（Ginkgo::Weapon::StepReloadKey）
 */
enum class EGinkgoReloadKeyAction : uint8
{
	None,
	/** 装填（短按；没有轮盘的枪按下就装） */
	Reload,
	/** 按够了 AmmoWheelHoldTime：打开弹种轮盘 */
	OpenWheel,
	/** 轮盘开着时松开：按选的确认 */
	ConfirmWheel,
};

/**
 * @struct FGinkgoReloadKey : 装填键的长按（Docs/Design/Weapons.md「切换弹种」）
 * 有轮盘的枪（同一口径有几种弹）按下先记着：松开时没到长按就是装填，按够了打开轮盘、松开时确认。没有轮盘的枪按下就装填，同以前
 */
struct FGinkgoReloadKey
{
	/** 按下了，在等松开或者按够 */
	bool bPending = false;
	/** 按了多久（s） */
	float HeldTime = 0.f;
	/** 这一次按着把轮盘打开了 */
	bool bWheelOpen = false;
};

/**
 * @enum EGinkgoAmmoWheelResult : 弹种轮盘上一次怎么关的
 */
enum class EGinkgoAmmoWheelResult : uint8
{
	None,
	/** 松开装填键：换成选的那一种（换了才装填） */
	Confirmed,
	/** 开镜键（你说的）、开火、换枪、枪没了：什么也不改 */
	Cancelled,
	/** 选的那一种后备里没有：什么也不改 */
	Unavailable,
};

/**
 * @struct FGinkgoAmmoWheel : 弹种轮盘（Docs/Design/Weapons.md「切换弹种」）：武器组件上，只在本地，界面与回放读它
 * 一圈平分成几项，第 0 项在正上方、往右排（Ginkgo::Weapon::PickWheelSector）。开着时鼠标、视角的摇杆不转视角，拨的是这里的指针
 */
struct FGinkgoAmmoWheel
{
	bool bOpen = false;
	/** 打开时手上的槽：手上换了枪就关 */
	int32 Slot = INDEX_NONE;
	/** 列的弹种：同一口径的几种，枪的弹种第一（UGinkgoAmmoDefinition::GetChoices） */
	TArray<FGameplayTag, TInlineAllocator<8>> Choices;
	/** 打开时正在装的那一种是第几项 */
	int32 Current = INDEX_NONE;
	/** 选着第几项：打开时是 Current，鼠标动的方向、摇杆指过去才换 */
	int32 Selected = INDEX_NONE;
	/** 鼠标最近往哪动（度，X 右、Y 上）：很短的向量，夹在 AmmoWheelMotionRadius 以内，不动时自己收回去（Ginkgo::Weapon::StepWheelMotion） */
	FVector2D Motion = FVector2D::ZeroVector;
	/** 视角的摇杆这一帧（过了视角的死区以后的，X 右、Y 上，不反转） */
	FVector2D Stick = FVector2D::ZeroVector;
	/** 开、关、换了选的几次：界面看它变了才发 */
	int32 Serial = 0;
	/** 上一次怎么关的 */
	EGinkgoAmmoWheelResult LastResult = EGinkgoAmmoWheelResult::None;
};

/**
 * @struct FGinkgoGunKick : 枪的踢动（程序化的后坐，你说的：片段只管肩膀，Docs/Design/ActionStates.md「后坐」）
 * 每开一枪给弹簧一个冲量：枪往后顶，枪口绕枪托上扬，左右偏、滚转每枪随机。都是枪的坐标，加在枪口对准之后，手跟着枪走（全身 IK）。
 * 写的都是峰值：冲量由它们反算（Ginkgo::Spring::DampedImpulseForPeak）
 */
USTRUCT(BlueprintType)
struct FGinkgoGunKick
{
	GENERATED_BODY()

	/** 往后顶多少（峰值）：片段里枪托顶肩还有 3 cm */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kick", meta = (ClampMin = 0, Units = "cm"))
	float Back = 2.5f;

	/** 枪口上扬多少（峰值） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kick", meta = (Units = "deg"))
	float Climb = 5.f;

	/** 左右偏多少（峰值，每枪在正负这么多之间随机） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kick", meta = (ClampMin = 0, Units = "deg"))
	float Yaw = 1.2f;

	/** 滚转多少（峰值，每枪在正负这么多之间随机） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kick", meta = (ClampMin = 0, Units = "deg"))
	float Roll = 3.f;

	/** 枪口绕哪里转（枪的坐标）：枪托顶肩的地方 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kick")
	FVector Pivot = FVector(-22.0, 0.0, 0.0);

	/** 转动的弹簧：角频率。大了踢得快、回得快 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kick", meta = (ClampMin = 0, ForceUnits = "rad/s"))
	float RotationOmega = 24.f;

	/** 转动的弹簧：阻尼比。小了回来时过冲一点 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kick", meta = (ClampMin = 0, ClampMax = 1))
	float RotationZeta = 0.55f;

	/** 往后顶的弹簧：角频率。比转动快：枪先撞肩，枪口才抬到最高 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kick", meta = (ClampMin = 0, ForceUnits = "rad/s"))
	float BackOmega = 45.f;

	/** 往后顶的弹簧：阻尼比 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Kick", meta = (ClampMin = 0, ClampMax = 1))
	float BackZeta = 0.6f;
};

/**
 * @struct FGinkgoBoltRig : 枪栓怎么动（枪的骨骼网格上的骨骼，Docs/Design/ActionStates.md 第 4 步）。枪管沿枪的 +X，枪栓往后是 -X
 */
USTRUCT(BlueprintType)
struct FGinkgoBoltRig
{
	GENERATED_BODY()

	/** 沿枪管滑动的骨骼 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bolt")
	FName CarrierBone = TEXT("bolt_carrier");

	/** 绕枪管转的骨骼（拉机柄）：滑动骨骼的子骨骼，原点在枪管的轴上 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bolt")
	FName HandleBone = TEXT("bolt_handle");

	/** 拉到底滑多远。片段里的手是按这个距离走的，换网格时要同片段一致 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bolt", meta = (ClampMin = 0, Units = "cm"))
	float Stroke = 9.45f;

	/** 抬到头转多少，绕枪的 +X */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bolt", meta = (Units = "Degrees"))
	float LiftAngle = 90.f;
};

/**
 * @struct FGinkgoGunClip : 枪自己这一帧放的片段（Docs/Design/ActionStates.md「M1911」）：动画实例从在放的蒙太奇里找，角色摆到枪的骨骼上
 */
struct FGinkgoGunClip
{
	/** 枪的骨架上的片段（FGinkgoWeaponStep::GunTrack）。开始淡出以后留着淡出开始那一刻的 */
	const UAnimSequence* Track = nullptr;
	float Time = 0.f;
	/** 比重，0 是不放 */
	float Weight = 0.f;
	/** 这一步的片段自己带着进膛的那一发（FGinkgoWeaponStep::bNextRoundInClip，抛壳口装填）：不按供弹、膛缩它 */
	bool bNextRoundInClip = false;
};

/**
 * @struct FGinkgoRoundVisuals : 看得见的子弹与弹壳（Docs/Design/ActionStates.md「子弹与弹壳」）。只是样子，枪里有几发看机构状态
 */
USTRUCT(BlueprintType)
struct FGinkgoRoundVisuals
{
	GENERATED_BODY()

	/** 活弹：压弹时手里的、枪机开着时弹仓最上面的、膛里是活弹时开栓抛出去的。原点在中间，弹头朝 +X */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rounds")
	TSoftObjectPtr<UStaticMesh> LiveRound;

	/** 空壳：开过火以后开栓抛出去的。原点在中间，口朝 +X */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rounds")
	TSoftObjectPtr<UStaticMesh> SpentCase;

	/** 压弹时手里那一发（右手骨的坐标）：片段的 Round_InHand 曲线为 1 时显示 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rounds")
	FTransform InHand;

	/** 枪机开着时弹仓最上面那一发（枪的坐标）：躺在托弹板上。枪栓推回来时被枪栓面推着往前走。零是不画（弹匣自己有那一发的骨骼） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rounds")
	FVector FeedTop = FVector::ZeroVector;

	/** 推进膛的路上抬多少（cm）：从托弹板到枪管的轴 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rounds", meta = (ClampMin = 0, Units = "cm"))
	float ChamberRise = 0.f;

	/** 从哪里抛出来（枪的坐标）：枪栓拉到底时弹壳的中心 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rounds")
	FVector EjectOffset = FVector::ZeroVector;

	/** 自动装填的枪开火以后多久抛出来：等套筒退到后面。手动的枪是开栓那一刻，不看它 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rounds", meta = (ClampMin = 0, Units = "s"))
	float EjectDelay = 0.f;

	/** 往哪里抛（枪的坐标） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rounds")
	FVector EjectDirection = FVector::RightVector;

	/** 抛出来的速度，相对枪（cm/s） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rounds", meta = (Units = "cm/s"))
	FFloatInterval EjectSpeed = FFloatInterval(250.f, 350.f);

	/** 方向上散多少（度） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rounds", meta = (ClampMin = 0, Units = "Degrees"))
	float EjectSpread = 10.f;

	/** 翻得多快（度/秒）：抛壳挺打在弹壳尾部的左边，壳口往右甩 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rounds", meta = (ForceUnits = "deg/s"))
	FFloatInterval EjectSpin = FFloatInterval(1500.f, 2600.f);

	/** 落地弹几下（弹性、摩擦） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rounds")
	TSoftObjectPtr<UPhysicalMaterial> EjectMaterial;

	/** 撞得重时的声音。一次撞一声，每次随机挑一段、随机音高和音量（在 MetaSound 的图输入上调） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rounds")
	TSoftObjectPtr<USoundBase> HitSoundHard;

	/** 撞得轻时的声音 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rounds")
	TSoftObjectPtr<USoundBase> HitSoundSoft;

	/** 撞的速度（cm/s）：Min 以下没声音（贴着地滚），Max 以上音量满 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rounds", meta = (Units = "cm/s"))
	FFloatInterval HitSpeed = FFloatInterval(45.f, 700.f);

	/** 撞的速度到这里用重的那个声音 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rounds", meta = (ClampMin = 0, Units = "cm/s"))
	float HardHitSpeed = 150.f;

	/** 刚够出声时的音量 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rounds", meta = (ClampMin = 0, ClampMax = 1))
	float HitQuietVolume = 0.2f;

	/** 同一个壳两声之间至少隔多久：物理一帧里可以报好几次接触 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rounds", meta = (ClampMin = 0, Units = "s"))
	float HitSoundInterval = 0.05f;
};

/**
 * @struct FGinkgoWeaponSpread : 散布（精度）的配置，都是枪械属性（2026-11-05 你定的，Docs/Design/Weapons.md「散布」）
 * 扩散是圆锥的半角（度）：子弹离瞄准线最多偏这么多。这一刻的扩散 = (默认 + 连射累积，到上限为止) × 速度系数 × 姿态系数（空中、蹲着、滑铲，Ginkgo::Weapon::ComputeSpread）。
 * 默认与上限按开镜进度从腰射混到开镜。准星反馈的就是这个数：两边的横线的内端画在这个圆锥上
 */
USTRUCT(BlueprintType)
struct FGinkgoWeaponSpread
{
	GENERATED_BODY()

	/** 腰射默认扩散 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = 0, Units = "deg"))
	float HipDefault = 0.f;

	/** 开镜默认扩散 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = 0, Units = "deg"))
	float AdsDefault = 0.f;

	/** 每开一枪扩散增大多少 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = 0, Units = "deg"))
	float GrowthPerShot = 0.f;

	/** 扩散恢复的速度：连射累积的部分每秒收回多少 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = 0, Units = "deg/s"))
	float RecoveryRate = 0.f;

	/** 最大腰射扩散：连射时的上限，两个系数乘在它后面 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = 0, Units = "deg"))
	float HipMax = 0.f;

	/** 最大开镜扩散：同上 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = 0, Units = "deg"))
	float AdsMax = 0.f;

	/** 速度-扩散修正：横轴是角色当前的水平速度（cm/s），纵轴是系数，乘到结果上。没有键时是 1 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread")
	FRuntimeFloatCurve SpeedFactor;

	/** 空中扩散修正：不在地面上时乘到结果上 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = 0))
	float AirFactor = 1.f;

	/** 蹲姿扩散修正：蹲在地面上时乘到结果上 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = 0))
	float CrouchFactor = 0.7f;

	/** 滑铲扩散修正：滑铲时乘到结果上，不再算蹲姿 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread", meta = (ClampMin = 0))
	float SlideFactor = 0.8f;
};

/**
 * @struct FGinkgoSpreadInput : 算这一刻的扩散要的（Ginkgo::Weapon::ComputeSpread）
 */
struct FGinkgoSpreadInput
{
	/** 连射累积（度）：开一枪加 GrowthPerShot，按 RecoveryRate 收回 */
	float Bloom = 0.f;

	/** 开镜进度（两头缓过）：默认与上限按它从腰射混到开镜 */
	float AdsAlpha = 0.f;

	/** 角色的水平速度（cm/s）：纵向的快慢由空中系数管 */
	float Speed = 0.f;

	/** 在空中多少，0 到 1（两头缓过） */
	float AirAlpha = 0.f;

	/** 蹲在地面上多少，同上。滑铲时不算蹲着 */
	float CrouchAlpha = 0.f;

	/** 滑铲多少，同上 */
	float SlideAlpha = 0.f;

	/** 下一枪子弹对腰射基础散布与上限的倍率，由武器组件逐帧采集 */
	float HipScale = 1.f;

	/** 下一枪子弹对 ADS 基础散布与上限的倍率，独头弹为 0.12；普通弹为 1 */
	float AdsScale = 1.f;

	/** 配件对腰射基础散布的倍率，不改变上限；装配效果汇总后逐帧采集 */
	float HipBaseScale = 1.f;

	/** 配件对 ADS 基础散布的倍率，不改变上限；无修正时为 1 */
	float AdsBaseScale = 1.f;

	/** 配件对移动额外散布的倍率，只修正速度曲线超过 1 的部分；静止时不惩罚 */
	float MovingScale = 1.f;
};

/**
 * @struct FGinkgoWeaponCue : 枪的一个时刻放什么声音（Docs/Design/ActionStates.md「声音」）
 */
USTRUCT(BlueprintType)
struct FGinkgoWeaponCue
{
	GENERATED_BODY()

	/** 放什么：MetaSound（在几个样本里随机、不连着重复）或者一个样本。空的不放 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cue")
	TSoftObjectPtr<USoundBase> Sound;

	/** 音量倍数 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cue", meta = (ClampMin = 0))
	float Volume = 1.f;

	/** 声音开头到它的撞击点有多久：提前这么多放，撞击点正好落在片段走到这个时刻的那一帧。开枪不看它（按下就响） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cue", meta = (ClampMin = 0, Units = "s"))
	float Lead = 0.f;

	/** 再放一次时，上一次还在响的这么久淡掉：连发的枪声尾巴长，一秒十发叠十几个声音，抢别的声音的声道。停火时最后一枪的尾巴照常响完。0 不管 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cue", meta = (ClampMin = 0, Units = "s"))
	float FadePrevious = 0.f;
};

/**
 * @struct FGinkgoBoltPose : 枪栓的姿势，都是 0–1：滑动 0 关着、1 拉到底，抬柄 0 压下、1 抬到头。片段上的两条曲线（Bolt_Travel、Bolt_Lift）就是它
 */
struct FGinkgoBoltPose
{
	float Travel = 0.f;
	float Lift = 0.f;

	bool operator==(const FGinkgoBoltPose& Other) const { return Travel == Other.Travel && Lift == Other.Lift; }
};

/**
 * @enum EGinkgoFireMode : 射击模式
 */
UENUM(BlueprintType)
enum class EGinkgoFireMode : uint8
{
	/** 按一下打一枪 */
	Single UMETA(DisplayName = "单发"),
	/** 按着就按射速接着打，到松手或者打空 */
	Automatic UMETA(DisplayName = "连发"),
};

/**
 * @struct FGinkgoPatternRecoil : 瞄准后坐怎么用模板（Docs/Design/Weapons.md「后坐模板」）
 * 模板（UGinkgoRecoilPattern）只有弹道的形状，这里是这把枪怎么用它：状态倍率、多快走到、停火以后怎么回来。
 * 没配模板的枪（单发、栓动）没有瞄准后坐，视线不动
 */
USTRUCT(BlueprintType)
struct FGinkgoPatternRecoil
{
	GENERATED_BODY()

	/** 先混合姿态，再把已装配件的改变量加到两个轴上 */
	CASEGINKGO_API FVector2D GetScale(float AdsAlpha, float CrouchAlpha, float AttachmentOffset) const;

	/** 后坐模板：网页工具 Tools/RecoilEditor 导出的 .recoil 导入的资产。空的没有瞄准后坐 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PatternRecoil")
	TObjectPtr<UGinkgoRecoilPattern> Pattern;

	/** 偏航乘多少：模板不动，整把枪左右摆得多或少 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PatternRecoil", meta = (ClampMin = 0))
	float YawScale = 1.f;

	/** 俯仰乘多少：模板不动，整把枪上抬得多或少 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PatternRecoil", meta = (ClampMin = 0))
	float PitchScale = 1.f;

	/** 开镜时再乘多少（按开镜进度） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PatternRecoil", meta = (ClampMin = 0))
	float AdsScale = 1.f;

	/** 蹲着时再乘多少（按蹲姿的进度，同散布的蹲姿系数） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PatternRecoil", meta = (ClampMin = 0))
	float CrouchScale = 1.f;

	/** 一发的增量多久走完：先快后慢。大了枪口是慢慢抬的，比发射间隔大时下一发打在还没到的地方 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PatternRecoil", meta = (ClampMin = 0, Units = "s"))
	float ApplyTime = 0.06f;

	/** 停火多久开始回：要比发射间隔长（连发时不回），连着点射时这段时间里也不回，点射也压得住 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PatternRecoil", meta = (ClampMin = 0, Units = "s"))
	float RecoveryDelay = 0.15f;

	/** 回复的指数部分（每秒）：大了回得快。只回还没被压掉的那部分 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PatternRecoil", meta = (ClampMin = 0))
	float RecoveryDecay = 8.f;

	/** 回复的线性部分（度/秒）：回到最后不拖尾 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PatternRecoil", meta = (ClampMin = 0))
	float RecoveryLinear = 18.f;

	/** 停火以后打到第几发往回退多快（发/秒）：小了点射也会一路走到模板后面，大了短停一下就回到头几发 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PatternRecoil", meta = (ClampMin = 0))
	float ShotRecoveryRate = 8.f;
};

/**
 * @struct FGinkgoRecoilKick : 一发的增量，正在摊到视线上
 */
struct FGinkgoRecoilKick
{
	/** 这一发一共要走多少（度）：X 偏航向右为正，Y 俯仰向上为正 */
	FVector2D Delta = FVector2D::ZeroVector;
	/** 走了多久 */
	double Elapsed = 0.0;
	/** 多久走完 */
	double Duration = 0.0;
};

/**
 * @struct FGinkgoRecoilState : 瞄准后坐的状态（Ginkgo::Weapon::AddRecoilKick、StepRecoil）
 */
struct FGinkgoRecoilState
{
	/** 打到第几发：开枪时取整数部分，停火时往回退 */
	float ShotIndex = 0.f;
	/** 这串是哪个模板的：换了模板从头算 */
	TWeakObjectPtr<const UGinkgoRecoilPattern> Pattern;
	/** 还没走完的增量 */
	TArray<FGinkgoRecoilKick, TInlineAllocator<8>> Kicks;
	/** 后坐加上去、玩家还没压掉的（度）：停火以后回的就是它 */
	FVector2D Accumulated = FVector2D::ZeroVector;
	/** 后坐对视线一共动了多少（加上去的减回复的，不管玩家压没压，度）：回放的视角是绝对值，要加上它 */
	FVector2D Applied = FVector2D::ZeroVector;
	/** 离上一发多久 */
	double SinceShot = TNumericLimits<float>::Max();
	/** 回复的参数：开枪时从枪上记下来，收枪、换枪以后照这把枪的回完 */
	float RecoveryDelay = 0.f;
	float RecoveryDecay = 0.f;
	float RecoveryLinear = 0.f;
	float ShotRecoveryRate = 0.f;
};

/**
 * @struct FGinkgoCameraRecoil : 开一枪相机的一层晃动（你说的：参数在武器配置里，每把枪不一样，可以有随机范围，Docs/Design/ActionStates.md「后坐」）
 * 一层一个弹簧，只晃画面。每开一枪在这些范围里各取一个数（Min、Max 写成一样就是固定的），算成冲量带在开火事件上，相机不认识武器。
 * 写的都是峰值：冲量由它们反算（Ginkgo::Spring::DampedImpulseForPeak），调弹簧不改幅度
 */
USTRUCT(BlueprintType)
struct FGinkgoCameraRecoil
{
	GENERATED_BODY()

	/** 这一层叫什么：只给人看（细节面板里数组的标题） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CameraRecoil")
	FName Name;

	/** 俯仰的峰值（度）：为正是抬头 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CameraRecoil")
	FFloatInterval Pitch = FFloatInterval(0.f, 0.f);

	/** 偏航的峰值（度）：为正是向右 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CameraRecoil")
	FFloatInterval Yaw = FFloatInterval(0.f, 0.f);

	/** 侧倾的峰值（度）：为正是向右倾 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CameraRecoil")
	FFloatInterval Roll = FFloatInterval(0.f, 0.f);

	/** 偏航、侧倾每枪各自随机往一边：上面两个范围写的是大小。关着时写的就是带符号的值，可以偏向一边 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CameraRecoil")
	bool bRandomSide = false;

	/** 视场的峰值（度）：为正是变宽 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CameraRecoil")
	FFloatInterval FieldOfView = FFloatInterval(0.f, 0.f);

	/** 弹簧的角频率（rad/s）：大了到峰值快、晃得快。回弹不单独配时回弹也走这个弹簧 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CameraRecoil", meta = (ClampMin = 0))
	FFloatInterval Omega = FFloatInterval(40.f, 40.f);

	/** 弹簧的阻尼比：小了多晃几下，0.7 以上基本不过冲。不能是 0：一直晃，连开时弹簧越攒越多 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CameraRecoil", meta = (ClampMin = 0.05, ClampMax = 1))
	FFloatInterval Zeta = FFloatInterval(0.3f, 0.3f);

	/** 回弹单独配（你说的：很多游戏偏移和回弹是两组参数）：到峰值以后换成下面这组弹簧。写的峰值不变，多久到峰值只看上面那组。关着时上面那组走到底 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CameraRecoil")
	bool bSeparateReturn = false;

	/** 回弹的角频率（rad/s）：小了回得慢。阻尼比是 1 时，约 1.7/ω 秒回到一半、3.9/ω 秒回到 10% */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CameraRecoil", meta = (EditCondition = "bSeparateReturn", ClampMin = 1))
	FFloatInterval ReturnOmega = FFloatInterval(8.f, 8.f);

	/** 回弹的阻尼比：1 平滑地回到 0，不过冲；小了过了 0 再晃几下 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CameraRecoil", meta = (EditCondition = "bSeparateReturn", ClampMin = 0.05, ClampMax = 1))
	FFloatInterval ReturnZeta = FFloatInterval(1.f, 1.f);
};

/**
 * @struct FGinkgoCueCameraShake : 枪的一个时刻（Weapon.Cue.*）带的相机震动（2026-11-12 你说的：拉栓时也给一些小的相机震动，同《使命召唤》）。
 * 层同开枪的相机后坐（FGinkgoCameraRecoil：每层一个弹簧、写峰值、可以有随机范围），这个时刻到了每层取一次数，带在 Weapon.Event.Handling 事件上，只晃画面
 */
USTRUCT(BlueprintType)
struct FGinkgoCueCameraShake
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CameraShake", meta = (TitleProperty = "Name"))
	TArray<FGinkgoCameraRecoil> Layers;
};

/**
 * @struct FGinkgoGunKickState : 枪的踢动的弹簧：值与速度。按解析解推进，与帧率无关
 */
struct FGinkgoGunKickState
{
	double Back = 0.0;
	double BackVelocity = 0.0;
	/** Pitch 是枪口上扬，Yaw、Roll 同枪的坐标（度） */
	FRotator Rotation = FRotator::ZeroRotator;
	FRotator RotationVelocity = FRotator::ZeroRotator;
};

/**
 * @struct FGinkgoHipAimState : 腰射操作性的弹簧（Docs/Design/Weapons.md「腰射操作性」）：瞄准方向用欠阻尼弹簧追视线
 * 记的是瞄准方向相对视线的偏差，视线自己转多少不用记。按解析解推进，与帧率无关
 */
struct FGinkgoHipAimState
{
	/** 瞄准方向减视线（度）：X 偏航，Y 俯仰 */
	FVector2D Lag = FVector2D::ZeroVector;
	/** 瞄准方向自己的角速度（度/秒） */
	FVector2D Velocity = FVector2D::ZeroVector;
};

/**
 * @struct FGinkgoHoldTimings : 两个进度走多快（一把枪的属性，来自它的定义）
 */
struct FGinkgoHoldTimings
{
	/** 低持 → 抵肩的时间（s）：低持时按开火要等它走满才开 */
	float RaiseTime = 0.18f;
	/** 抵肩 → 低持的时间（s） */
	float LowerTime = 0.18f;
	/** 开镜瞄准速度：抵肩 → 开镜的时间（s），开镜退出也用它 */
	float AdsTime = 0.3f;
	/** 腰射开火以后留在腰射多久（s） */
	float HipHoldTime = 1.5f;
	/** 低持 ↔ 冲刺持枪的时间（s）：冲刺进度走满，进出冲刺都用它 */
	float SprintTime = 0.2f;
};

/**
 * @struct FGinkgoHoldState : 持枪状态的快照
 * 武器组件每帧写（本地控制的角色在控制器的 PlayerTick 里、移动之前），动画、相机读。
 * 三个进度都按时间线性地走，到满的时间是确定的；给动画、相机的是两头缓过的（GetRaiseAlpha、GetAdsAlpha、GetSprintAlpha）
 */
USTRUCT()
struct CASEGINKGO_API FGinkgoHoldState
{
	GENERATED_BODY()

	/** 手上的枪；空的是徒手 */
	UPROPERTY(Transient)
	TObjectPtr<const UGinkgoWeaponDefinition> Weapon;

	UPROPERTY(Transient)
	EGinkgoHoldStance Stance = EGinkgoHoldStance::Unarmed;

	/** 抬枪进度：0 低持，1 抵肩（线性） */
	UPROPERTY(Transient)
	float RaiseProgress = 0.f;

	/** 开镜进度：0 抵肩，1 开镜（线性） */
	UPROPERTY(Transient)
	float AdsProgress = 0.f;

	/** 冲刺进度：0 照低持握，1 冲刺的持法（线性） */
	UPROPERTY(Transient)
	float SprintProgress = 0.f;

	/** 腰射还要留多久（s）；> 0 时不开镜、不冲刺就是腰射 */
	UPROPERTY(Transient)
	float HipHoldRemaining = 0.f;

	/** 这一刻的扩散（度，圆锥的半角，Ginkgo::Weapon::ComputeSpread）：开枪按它偏，准星画它 */
	UPROPERTY(Transient)
	float Spread = 0.f;

	/** 这一步两只手都在枪上（FGinkgoWeaponStepTask）：单手跑的枪这时不放跑的循环 */
	UPROPERTY(Transient)
	bool bBothHandsOnGun = false;

	/** 抬枪进度，两头缓过 */
	float GetRaiseAlpha() const;

	/** 开镜进度，两头缓过 */
	float GetAdsAlpha() const;

	/** 冲刺进度，两头缓过 */
	float GetSprintAlpha() const;

	/** 抬到位了：低持时按开火要等它 */
	bool IsRaised() const { return RaiseProgress >= 1.f; }
};

/**
 * @struct FGinkgoWeaponHandTargets : 两只手在枪上的目标
 * 枪的坐标（等于枪挂的虚拟骨骼的坐标）里两只手的变换。每把枪低持、冲刺、腰射、开镜各一份（姿势里手的握法不同：低持食指在护圈外），
 * Control Rig 的双手全身 IK 按三个进度同姿势一样混（Docs/Design/Weapons.md「手与瞄准」「持枪循环」）
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoWeaponHandTargets
{
	GENERATED_BODY()

	/** 左手（护木） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grip")
	FTransform Left = FTransform::Identity;

	/** 右手（握把） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grip")
	FTransform Right = FTransform::Identity;
};

/** @struct FGinkgoPickupSettleStart : 捡枪抓住的那一刻（角色网格的坐标）：收拢从这里出发（Ginkgo::Weapon::BlendPickupSettle） */
struct FGinkgoPickupSettleStart
{
	/** 枪在地上的朝向 */
	FQuat GunRotation = FQuat::Identity;
	/** 右手（hand_r）抓住时的朝向：手按这个握法握着枪转，再换到持枪的握法 */
	FQuat HandRotation = FQuat::Identity;
	/** 东西在右手骨里的挂点（AGinkgoPickup::GetItemInHand） */
	FVector ItemInHand = FVector::ZeroVector;
};

/** @struct FGinkgoPickupSettlePose : 捡枪收拢的一帧（Ginkgo::Weapon::BlendPickupSettle）：枪与右手的目标，同给的坐标 */
struct FGinkgoPickupSettlePose
{
	FTransform Gun = FTransform::Identity;
	FTransform RightHand = FTransform::Identity;
};

/**
 * @struct FGinkgoHoldInput : 选姿态要的输入
 * 第一批的临时规则只有两条（手上有枪、不在地面冲刺就能开镜），第 4 批换成动作状态的树
 */
struct FGinkgoHoldInput
{
	/** 手上有枪 */
	bool bArmed = false;
	/** 按着开镜 */
	bool bAimHeld = false;
	/** 上一帧在地面上冲刺（移动组件的 IsSprintFacing） */
	bool bGroundSprinting = false;
	/** 腰射还要留多久（s） */
	float HipHoldRemaining = 0.f;
	/** 两只手都在枪上做事（拉栓、装填，动作状态给的约束）：冲刺时枪不用单手举着，放低，见 Docs/Design/ActionStates.md「开火与拉栓：实现」 */
	bool bBothHandsOnGun = false;
};

/**
 * 持枪里与世界无关的计算：选姿态、两个进度。写成纯函数，测试直接调（CaseGinkgo.Weapon.Hold.*）。
 * 规则见 Docs/Design/Weapons.md「持枪状态」。开镜的视场在相机里算（Ginkgo::Camera::ZoomedFieldOfView），武器只写倍率。
 */
namespace Ginkgo::Weapon
{
	/** 武器槽的布局：前两个是主武器，第三个是手枪，键 1、2、4 对着它们；第四个是重武器，没有键，只在拿着时有枪（Docs/Design/Weapons.md「武器槽」） */
	inline constexpr int32 SlotCount = 4;
	inline constexpr int32 SidearmSlot = 2;
	inline constexpr int32 HeavySlot = 3;

	/** @brief 这个槽是哪一种 */
	CASEGINKGO_API EGinkgoWeaponSlotKind GetSlotKind(int32 Slot);

	/** @brief 这一种收得起来（背上、枪套里）；重武器收不起来，离手就是扔在地上 */
	CASEGINKGO_API bool CanStow(EGinkgoWeaponSlotKind Kind);

	/** @return 从一把主武器换到另一把主武器：两把都在右肩后，收枪放到背上时手就在取枪的地方 */
	CASEGINKGO_API bool IsBackToBackSwap(int32 FromSlot, int32 ToSlot);

	/**
	 * @brief 这一种的第一个空槽
	 * @param Carried - 每个槽有没有枪
	 * @return 这一种都满了时为 INDEX_NONE
	 */
	CASEGINKGO_API int32 FindFreeSlot(EGinkgoWeaponSlotKind Kind, TConstArrayView<bool> Carried);

	/**
	 * @struct FGinkgoWeaponPickupPlan : 捡一把枪要怎么腾地方（Docs/Design/Interaction.md「丢枪与捡枪」）
	 */
	struct FGinkgoWeaponPickupPlan
	{
		/** 捡到哪个槽 */
		int32 Slot = INDEX_NONE;
		/** 这个槽的枪先掉到地上：手上的从手里丢出去，身上的直接从背上、枪套里掉下来；不用掉时为 INDEX_NONE */
		int32 DropSlot = INDEX_NONE;
		/** 手上的枪要先收起来（它不在要捡的那一种槽位里，收得回去），右手空了才伸手 */
		bool bStowHeld = false;
		/** 手上的枪收不起来（重武器）：先扔在地上，代替收枪。同 DropSlot 可以都有（捡主武器、主武器满了时还掉一把背上的） */
		bool bDropHeld = false;
	};

	/**
	 * @brief 捡一把枪要怎么腾地方。只能右手捡，所以手上不能有枪；关键看要捡的枪和手上的是不是同一种槽位（你说的）：
	 *        这一种有空槽就放进去，手上有枪先收；满了时，手上的就是这一种，丢手上的；手上的是别的一种或者徒手，
	 *        身上这一种的枪直接掉到地上（不用拿出来再扔），手上的照样收。这一种有两把时掉上一次拿过的那把，没拿过就掉靠后的。
	 *        手上的收不起来（重武器）时不收，扔在地上（你说的：捡别的枪就是直接替换它）
	 * @param Kind - 要捡的枪的槽位
	 * @param Carried - 每个槽有没有枪
	 * @param HeldSlot - 手上哪个槽；徒手时为 INDEX_NONE
	 * @param LastHeldSlot - 上一次手上的槽
	 */
	CASEGINKGO_API FGinkgoWeaponPickupPlan PlanWeaponPickup(EGinkgoWeaponSlotKind Kind, TConstArrayView<bool> Carried, int32 HeldSlot, int32 LastHeldSlot);

	/** @brief 骨头在参考姿势里的组件空间变换 */
	CASEGINKGO_API FTransform GetRefPoseComponentTransform(const FReferenceSkeleton& RefSkeleton, int32 BoneIndex);

	/**
	 * @brief 这一刻的扩散（度，圆锥的半角）：(默认 + 连射累积，到上限为止) × 速度系数 × 姿态系数（Docs/Design/Weapons.md「散布」）
	 */
	CASEGINKGO_API float ComputeSpread(const FGinkgoWeaponSpread& Spread, const FGinkgoSpreadInput& Input);

	/** @brief 姿态系数：空中、蹲着、滑铲三个乘起来，每个按它的比重从 1 混到配的数 */
	CASEGINKGO_API float GetSpreadPostureFactor(const FGinkgoWeaponSpread& Spread, const FGinkgoSpreadInput& Input);

	/**
	 * @brief 连射累积最多多少：上限减默认，按开镜进度混。系数不算在里面
	 * @param HipScale、AdsScale - 下一枪的子弹改的扩散（FGinkgoSpreadInput 的同名项），默认与上限一起乘
	 */
	CASEGINKGO_API float GetMaxSpreadBloom(const FGinkgoWeaponSpread& Spread, float AdsAlpha, float HipScale = 1.f, float AdsScale = 1.f, float HipBaseScale = 1.f, float AdsBaseScale = 1.f);

	/**
	 * @brief 这一枪的方向：瞄准的方向偏一个随机的角度。半径在 0 到扩散之间均匀取，方位均匀取，所以中心密，一半的子弹落在半径的一半以内（同《反恐精英》）
	 * @param Aimed - 瞄准的方向（单位向量）
	 * @param SpreadDegrees - 扩散（度）。总是取两个数：哪一枪的扩散是 0，后面的枪取到的数也不变
	 * @return 单位向量
	 */
	CASEGINKGO_API FVector SampleSpreadDirection(const FVector& Aimed, float SpreadDegrees, FRandomStream& Random);

	/**
	 * @brief 扩散的圆锥在画面上的半径（1080p 高的像素）
	 * @param ViewFov - 相机的视场（度）：4:3 的水平视场，引擎按竖直视场换算，所以与窗口的宽高比无关；开镜拉近时同样的扩散画得大
	 */
	CASEGINKGO_API double SpreadToPixels(float SpreadDegrees, float ViewFov);

	/**
	 * @brief 准星两边的横线（"- · -"）的内端离中心多远：就是扩散的圆锥在画面上的半径，子弹落在两根横线之间
	 * @param Gap - 最少离中心多远（逻辑 px）：扩散很小时不贴到点上。≤ 0 是没有横线，结果是 0
	 * @param SpreadPixels - 圆锥的半径（SpreadToPixels）
	 */
	CASEGINKGO_API float CrosshairDashOffset(float Gap, float SpreadPixels);

	/**
	 * @brief 左手握枪的比重（Control Rig 的双手全身 IK）：单手冲刺、单手跑时放开（抬枪以后回到枪上），片段里左手离开枪时放开
	 * @param SprintAlpha、RunAlpha、RaiseAlpha - 两头缓过的冲刺进度、跑的循环的权重、抬枪进度
	 * @param ClipGrip - 片段的曲线给的（HandIK_L，按片段的比重混过），没有这条曲线时是 1
	 */
	CASEGINKGO_API float MakeLeftHandGrip(bool bSprintTwoHanded, float SprintAlpha, bool bRunTwoHanded, float RunAlpha, float RaiseAlpha, float ClipGrip);

	/**
	 * @brief 选姿态（第一批的临时规则）：没枪是徒手；地面上冲刺时枪放低（冲刺打断开镜）；按着开镜是开镜；腰射还没留完是腰射；否则低持
	 */
	CASEGINKGO_API EGinkgoHoldStance SelectStance(const FGinkgoHoldInput& Input);

	/** @brief 这个姿态要不要抵肩：腰射、开镜要 */
	CASEGINKGO_API bool WantsRaised(EGinkgoHoldStance Stance);

	/** @brief 这个姿态要不要开镜 */
	CASEGINKGO_API bool WantsAds(EGinkgoHoldStance Stance);

	/** @brief 这个姿态要不要冲刺的持法 */
	CASEGINKGO_API bool WantsSprint(EGinkgoHoldStance Stance);

	/**
	 * @brief 进度朝目标按时间线性地走
	 * @param Progress - 现在的进度（0–1）
	 * @param bFull - 目标是 1（否则是 0）
	 * @param RiseTime - 0 → 1 要多久（s）；≤ 0 时直接到
	 * @param FallTime - 1 → 0 要多久（s）；≤ 0 时直接到
	 */
	CASEGINKGO_API float StepProgress(float Progress, bool bFull, float RiseTime, float FallTime, float DeltaSeconds);

	/** @brief 两头缓：3t² − 2t³（t 先夹到 0–1） */
	CASEGINKGO_API float EaseProgress(float Progress);

	/**
	 * @brief 推进一帧：姿态换成 Stance，三个进度朝它要的方向走，腰射留的时间往下减
	 * @note 三个进度各走各的：从低持直接按开镜时抬枪与开镜一起走，抬枪先到（0.18 s 对 0.3 s）；松开冲刺接着开镜时冲刺进度往回走、另两个往上走
	 */
	CASEGINKGO_API void StepHoldState(FGinkgoHoldState& State, EGinkgoHoldStance Stance, const FGinkgoHoldTimings& Timings, float DeltaSeconds);

	/**
	 * @brief 两只手的目标按三个进度混，同层里的姿势：低持 → 冲刺按冲刺进度，再 → 腰射按抬枪进度，再 → 开镜按开镜进度（都是两头缓过的）
	 * @note 位置线性插值、旋转球面插值。姿势里胳膊按关节插值，手走的不是这条路，所以混合中间手会离开枪，IK 把它拉回这里
	 */
	CASEGINKGO_API FGinkgoWeaponHandTargets BlendHandTargets(const FGinkgoWeaponHandTargets& Lowered, const FGinkgoWeaponHandTargets& Sprint,
		const FGinkgoWeaponHandTargets& Hip, const FGinkgoWeaponHandTargets& Aim, float SprintAlpha, float RaiseAlpha, float AdsAlpha);

	/**
	 * @brief 两个朝向之间转，路不经过角色的正后方：在角色的坐标里偏航、俯仰直线插值（偏航两头都在 −180°–180° 里，不跨过 ±180°），
	 *        横滚走近的那边。球面插值走最短的那条，两头差不多反着时会从这边翻到那边（捡枪：枪口从肩上往后甩，再一帧翻 100°）
	 * @param Body - 角色的朝向（同 From、To 的坐标）：前是 X、上是 Z
	 * @note 俯仰接近 ±90° 时偏航、横滚分不开；地上的枪、持枪的枪都离那里很远
	 */
	CASEGINKGO_API FQuat BlendTurnInFront(const FQuat& Body, const FQuat& From, const FQuat& To, float Alpha);

	/**
	 * @brief 捡枪收拢的一帧（Docs/Design/Interaction.md「丢枪与捡枪」）：开始时枪保持抓住时的朝向，枪上被握的那一点（持枪时右手挂点的位置）
	 *        在右手的挂点上；按进度转到持枪的朝向（BlendTurnInFront：枪口从身前扫过去），那一点直线走到持枪时的地方。
	 *        右手按抓住时的握法握着枪一起转，握法按进度换到持枪的，挂点一直在那一点上
	 * @param Start - 抓住的那一刻（同 Hand、Gun 的坐标）
	 * @param Body - 角色的朝向（同上）：前是 X、上是 Z
	 * @param Hand - 右手这一帧（片段的，伸手拉过的）：只用它的挂点的位置
	 * @param Gun - 持枪时的枪（虚拟骨骼这一帧的）
	 * @param GunHand - 持枪时右手在枪上的变换（枪的坐标）
	 * @param Alpha - 收拢的进度（两头缓过的，0–1）：0 时枪是地上的朝向、右手是抓住时的朝向，1 时枪就是 Gun、右手就是 GunHand × Gun
	 */
	CASEGINKGO_API FGinkgoPickupSettlePose BlendPickupSettle(const FGinkgoPickupSettleStart& Start, const FQuat& Body, const FTransform& Hand, const FTransform& Gun,
		const FTransform& GunHand, float Alpha);

	/**
	 * @brief 根树选哪个状态
	 * @param HeldSlot - 手上的槽（拔枪开始时就换成要拔的，枪落到背上时换回徒手）；INDEX_NONE 是徒手
	 * @param TargetSlot - 目标槽；INDEX_NONE 是要徒手
	 */
	CASEGINKGO_API EGinkgoWeaponRootState SelectRootState(int32 HeldSlot, int32 TargetSlot);

	/**
	 * @brief 徒手时按收枪键拔哪个槽：上一次手上的；它不在了（丢了）或还没拿过时是第一个有枪的
	 * @param Carried - 每个槽有没有枪
	 * @return 槽；一把枪也没有时是 INDEX_NONE
	 */
	CASEGINKGO_API int32 FindDrawSlot(int32 LastSlot, TConstArrayView<bool> Carried);

	/**
	 * @brief 按下一个换枪的键，目标槽改成什么（规则见 Docs/Design/ActionStates.md「收枪键」）
	 * @note 按目标槽算，不按手上的槽算：放到一半再按，改的是放完以后去哪，所以连按不会抖。空的槽不响应
	 * @param Key - 一个键（不是几个键的组合）
	 * @param TargetSlot - 现在的目标槽
	 * @param LastSlot - 上一次手上的槽
	 * @param Carried - 每个槽有没有枪
	 */
	CASEGINKGO_API int32 ApplyWeaponKey(EGinkgoWeaponKey Key, int32 TargetSlot, int32 LastSlot, TConstArrayView<bool> Carried);

	/**
	 * @brief 枪的子树现在该做什么：闭栓、拉栓、装填先于开火，所以想开火的等它们做完（Docs/Design/ActionStates.md「每种枪机一棵子树」「枪记住什么」）
	 * @note 手不空（比枪上的动作优先级高的占着，以后爬梯子这类）时都是就绪，等放开。意图还记着
	 */
	CASEGINKGO_API EGinkgoWeaponAction SelectWeaponAction(const FGinkgoWeaponActionFacts& Facts);

	/**
	 * @brief 这一下压两发还是一发（霰弹枪的竞赛装填，你定的）：每次两发，只剩一发（后备）或者只缺一发（管里）时压一发
	 * @param Room - 供弹里还空几个位置
	 * @param Reserve - 后备里这种弹几发
	 */
	CASEGINKGO_API bool ShouldLoadTwo(int32 Room, int32 Reserve);

	/**
	 * @brief 装填键推进一帧（Docs/Design/Weapons.md「切换弹种」）
	 * @param bPressed - 这一帧按下了（按下沿）
	 * @param bHeld - 这一帧按着（同一帧里按下又松开的是假）
	 * @param bHasWheel - 手上的枪有轮盘（同一口径有几种弹、能装填）：没有时按下就装填，同以前
	 * @param bCanOpenWheel - 现在开得了（枪在手上、没在开镜）。按够了还开不了时接着等，松开时当短按
	 * @param HoldTime - 按多久算长按
	 * @note 轮盘被取消（开镜、开火、换枪）时调的一方把 bPending、bWheelOpen 清掉，松开就什么也不做
	 */
	CASEGINKGO_API EGinkgoReloadKeyAction StepReloadKey(FGinkgoReloadKey& Key, bool bPressed, bool bHeld, bool bHasWheel, bool bCanOpenWheel, float HoldTime, float DeltaSeconds);

	/**
	 * @brief 一个方向落在轮盘的第几项：第 0 项在正上方，往右（顺时针）排，Count 项平分一圈，每项占它中心左右各半格
	 * @param Direction - X 向右、Y 向上；零向量是第 0 项
	 */
	CASEGINKGO_API int32 PickWheelSector(const FVector2D& Direction, int32 Count);

	/** @brief 轮盘第 Index 项的中心方向（单位向量，X 向右、Y 向上）：界面按它摆 */
	CASEGINKGO_API FVector2D GetWheelSectorDirection(int32 Index, int32 Count);

	/**
	 * @brief 鼠标最近往哪动（你说的，2026-11-08：直接按鼠标相对运动的方向选，不要拨一个指针）：先按 DecayTime（时间常数）收回去，加上这一帧的量（度），夹在半径以内（方向不变）。
	 *        半径小，换方向时动一点就转过去；不动时收回到 0，下一次往哪动就是哪
	 */
	CASEGINKGO_API FVector2D StepWheelMotion(const FVector2D& Motion, const FVector2D& Delta, float Radius, float DecayTime, float DeltaSeconds);

	/**
	 * @brief 轮盘选第几项：摇杆推过 StickDeadZone 按摇杆的方向；不然鼠标的方向过了 MotionDeadZone 按它；都没有时留着 Current
	 * @note 摇杆松开回中、鼠标停下来收回死区里时留着上一次选的
	 */
	CASEGINKGO_API int32 SelectWheelEntry(const FVector2D& Motion, const FVector2D& Stick, int32 Count, int32 Current, float MotionDeadZone, float StickDeadZone);

	/**
	 * @brief 开一枪：给枪的踢动的弹簧一个冲量
	 * @param YawAlpha、RollAlpha - 这一枪的左右偏、滚转，−1 到 1（随机）
	 */
	CASEGINKGO_API void AddGunKick(FGinkgoGunKickState& State, const FGinkgoGunKick& Kick, float YawAlpha, float RollAlpha);

	/** @brief 推进枪的踢动的弹簧 */
	CASEGINKGO_API void StepGunKick(FGinkgoGunKickState& State, const FGinkgoGunKick& Kick, float DeltaSeconds);

	/**
	 * @brief 推进腰射操作性的弹簧一帧：视线在这一帧里匀速转过 ViewDelta，阻尼按瞄准方向自己的速度算，所以匀速转时稳态落后 2ζΩ/ω
	 * @param ViewDelta - 视线这一帧转了多少（度）：X 偏航，Y 俯仰
	 * @param Omega、Zeta - 枪上的腰射操作性。Omega ≤ 0 表示没有弹簧，瞄准方向就是视线
	 * @param MaxLag - 最多落后多少度，贴着上限时跟着视线走；≤ 0 不限
	 */
	CASEGINKGO_API void StepHipAim(FGinkgoHipAimState& State, const FVector2D& ViewDelta, double Omega, double Zeta, double MaxLag, double DeltaSeconds);

	/**
	 * @brief 枪机开着时弹仓最上面那一发（枪的坐标）：枪栓拉到底时躺在托弹板上，枪栓推回来时被枪栓面推着往前走，抬到枪管的轴
	 * @param Travel - 枪栓拉开多少（0 关着，1 拉到底）
	 */
	CASEGINKGO_API FTransform MakeFeedRoundTransform(const FGinkgoRoundVisuals& Rounds, const FGinkgoBoltRig& Bolt, float Travel);

	/**
	 * @brief 抛出去的速度（世界）：抛的方向散一点，取一个速度，加上枪自己的速度
	 * @param GunRotation - 枪这一刻的朝向
	 * @param GunVelocity - 枪这一刻的速度（世界）
	 * @param Random - 专用的随机流：不动开枪的那一条，开枪取到的数不变
	 */
	CASEGINKGO_API FVector SampleEjectVelocity(const FGinkgoRoundVisuals& Rounds, const FQuat& GunRotation, const FVector& GunVelocity, FRandomStream& Random);

	/**
	 * @brief 抛出去的角速度（世界，度/秒）：绕枪的上方，散一点
	 */
	CASEGINKGO_API FVector SampleEjectSpin(const FGinkgoRoundVisuals& Rounds, const FQuat& GunRotation, FRandomStream& Random);

	/**
	 * @brief 弹壳撞一下的音量：撞的速度到 HitSpeed.Min 才出声，按平方根长到 1（响度跟能量走，轻轻碰一下也听得见）
	 * @param Speed - 撞的速度（cm/s）：沿法线的冲量除以质量
	 * @return 0 是不出声
	 */
	CASEGINKGO_API float ShellHitVolume(const FGinkgoRoundVisuals& Rounds, double Speed);

	/**
	 * @brief 红 X 的缩放：实际着弹和准星对得上时不画（0），差得越多越大，到 1 为止
	 * @param Gap - 两者在画面上差多少（1080p 高的像素）：不到 8 算对得上（正常瞄准时最多 6.4，枪刚停下还在收），16 画满（枪口被挡时 70 以上）
	 */
	CASEGINKGO_API float HitMarkScale(double Gap);

	/**
	 * @brief 瞄准方向：视线加上偏差，偏差乘 1 − 开镜进度（开镜满了瞄准方向就是视线）
	 * @param AdsAlpha - 开镜进度（两头缓过）
	 */
	CASEGINKGO_API FRotator MakeHipAimRotation(const FRotator& View, const FVector2D& Lag, float AdsAlpha);

	/** @brief 枪的踢动的变换（枪的坐标）：绕枪托转、往后顶。枪的最终变换 = 它 × 枪口对准以后的变换 */
	CASEGINKGO_API FTransform MakeGunKickTransform(const FGinkgoGunKickState& State, const FGinkgoGunKick& Kick);

	/**
	 * @brief 枪口的细烟这一刻冒出来有多浓：开枪以后先冒得足，后面越来越细，最后慢慢断掉
	 * @param Remaining - 还剩多少，开枪时是 1，冒完是 0
	 * @param Opacity - 刚冒出来的浓度（枪上配的）
	 */
	CASEGINKGO_API float MuzzleWispAlpha(float Remaining, float Opacity);

	/** 开枪那一下的热气退掉的时间常数（s）：热浪那一份按 e^(−t/它) 退 */
	inline constexpr float HeatPuffTime = 0.25f;

	/**
	 * @brief 枪管凉一帧（Docs/Design/Weapons.md「枪管的热」）：牛顿冷却，比环境高出来的热按 e^(−dt/τ) 退，同帧长无关
	 * @param Heat - 比环境高出多少（°C）
	 * @param CoolTime - 时间常数 τ（s）
	 */
	CASEGINKGO_API float CoolBarrel(float Heat, float CoolTime, float DeltaSeconds);

	/**
	 * @brief 热浪有多强（0–1）：枪管的热在 Start 到 Full 之间两头缓过，加上开枪那一下的热气，夹到 1
	 * @param Heat - 枪管比环境高出多少（°C）
	 * @param Puff - 开枪那一下的热气这一刻还剩多少（0–1，乘过枪上的 HeatShotPuff）
	 */
	CASEGINKGO_API float HeatHazeAlpha(float Heat, float Start, float Full, float Puff);

	/**
	 * @brief 枪栓这一帧的姿势：不放片段时读机构状态（枪机开着就是拉到底、抬到头），放片段时读片段的曲线，片段淡入淡出时在两者之间
	 * @param ClipWeight - 带这两条曲线的片段的比重，0–1。背上的枪是 0
	 */
	CASEGINKGO_API FGinkgoBoltPose BlendBoltPose(bool bActionOpen, float ClipWeight, const FGinkgoBoltPose& Clip);

	/** @brief 滑动的骨骼在枪的空间里的变换 */
	CASEGINKGO_API FTransform MakeBoltCarrierTransform(const FTransform& RefPose, const FGinkgoBoltRig& Rig, const FGinkgoBoltPose& Pose);

	/** @brief 拉机柄的骨骼在枪的空间里的变换：跟着滑，再绕枪管转 */
	CASEGINKGO_API FTransform MakeBoltHandleTransform(const FTransform& RefPose, const FGinkgoBoltRig& Rig, const FGinkgoBoltPose& Pose);

	/**
	 * @brief 连发的下一枪最早什么时候响
	 * @param PreviousNextShot - 上一枪算出来的这个时刻
	 * @param Now - 这一枪真的响的时刻
	 * @param Interval - 发射间隔
	 * @param bChained - 这一枪是上一枪一到点就接着响的：扳机按着，或者上一枪没打完时又点了一下
	 * @return 接着响的从该响的时刻往后排，一帧的迟到不累积，射速与帧率无关（卡了一下、晚了一个间隔以上的不补）；
	 *         别的从现在算：到点以后才点的，晚了多少都不抵下一枪，速点快不过射速
	 */
	CASEGINKGO_API double GetNextShotTime(double PreviousNextShot, double Now, double Interval, bool bChained);

	/**
	 * @brief 这一枪排在什么时刻（GetNextShotTime 的另一半，下一枪 = 这一枪排的时刻 + 间隔）：接着响的是该响的那个时刻，别的是现在
	 * @note 按排的时刻数间隔同帧率无关：144 fps 时 600 发/分的两枪真的隔 14、15 帧（0.097、0.104 s），排的都是 0.1 s
	 */
	CASEGINKGO_API double GetScheduledShotTime(double PreviousNextShot, double Now, double Interval, bool bChained);

	/**
	 * @brief 这一枪留不留弹道的烟（Docs/Design/Weapons.md「弹道的烟」）：同上一道烟那一枪隔够 MinInterval 才留，按排的时刻（GetScheduledShotTime）数，差 0.1 ms 以内也算
	 * @param LastTrailShot - 上一道烟那一枪排的时刻；还没留过时给负的很大
	 * @param MinInterval - 0 或负的每枪都留
	 */
	CASEGINKGO_API bool ShouldLeaveTrail(double ShotTime, double LastTrailShot, double MinInterval);

	/**
	 * @brief 打了一发：取模板里到下一发的增量，开始摊到视线上
	 * @param Scale - 这一刻的总倍率（X 偏航、Y 俯仰：枪的倍率乘状态倍率）
	 * @param Jitter - 这一发的随机偏移（度），已经取好的
	 */
	CASEGINKGO_API void AddRecoilKick(FGinkgoRecoilState& State, const UGinkgoRecoilPattern& Pattern, const FGinkgoPatternRecoil& Recoil, const FVector2D& Scale, const FVector2D& Jitter);

	/**
	 * @brief 每帧：摊这一帧的增量，算玩家压掉了多少，停火以后回复。解析解，加起来的结果与帧率无关
	 * @param PlayerDelta - 这一帧玩家动了视线多少（度）：同还没回复的方向相反的部分算压掉
	 * @return 这一帧视线要动多少（度）：X 偏航向右为正，Y 俯仰向上为正
	 */
	CASEGINKGO_API FVector2D StepRecoil(FGinkgoRecoilState& State, const FVector2D& PlayerDelta, double DeltaSeconds);

	/**
	 * @brief 开一枪：相机的一层晃动在它的范围里各取一个数，算成冲量
	 * @param Random - 取数的次数只看配置，同一个种子每枪取到的数与帧率无关
	 */
	CASEGINKGO_API FGinkgoCameraImpulse SampleCameraRecoil(const FGinkgoCameraRecoil& Recoil, const FRandomStream& Random);
}
