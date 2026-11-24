#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"         // 弹种
#include "Weapon/GinkgoWeaponAttachment.h"
#include "Weapon/GinkgoWeaponTypes.h"
#include "GinkgoWeaponDefinition.generated.h"

class UAnimInstance;
class UAnimMontage;
class UAnimSequence;
class UAnimSequenceBase;
class UBlendSpace;
class UGinkgoWeaponMechanism;
class UNiagaraSystem;
class USkeletalMesh;
class UStateTree;
class UStaticMesh;
class USoundBase;
class USoundAttenuation;

/**
 * @struct FGinkgoWeaponStep : 动作状态的一步用的片段（Docs/Design/ActionStates.md「任务只有一个」）
 */
USTRUCT(BlueprintType)
struct FGinkgoWeaponStep
{
	GENERATED_BODY()

	/** 这一步的蒙太奇：事件（枪机开、关）是它的通知 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Step")
	TObjectPtr<UAnimMontage> Montage;

	/**
	 * 右手跟着哪份片段走（枪的坐标里）：取它的 ik_hand_r（相对 ik_hand_gun，就是手相对枪）。放的是叠加、枪照姿态走的动作要它：
	 * 右手一直在枪上的拉柄那里，不管枪现在是腰射、开镜还是冲刺的持法。空的照常握枪
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Step")
	TObjectPtr<UAnimSequence> RightHandTrack;

	/**
	 * 枪自己的动画（枪的骨架上的片段，M1911 的 A_M1911_*：套筒、枪管、击锤、扳机、卡榫、挂机杆、弹匣）：放这一步时按蒙太奇的时刻摆到枪上。
	 * 空的枪只听枪栓的曲线（Bolt）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Step")
	TObjectPtr<UAnimSequence> GunTrack;

	/** 从蒙太奇的哪一段开始。空的从头 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Step")
	FName StartSection;

	/**
	 * 这一步左手整段不在枪上（霰弹枪压弹：左手去拿弹），左手的 IK 随片段的比重放开。片段里的 HandIK_L 本来能说这个，
	 * 可整段都是 0 的曲线在叠加片段压缩时被丢掉（引擎烘叠加数据时出现过非 0 的值才建这条曲线），运行时读不到，左手照常握枪。
	 * GinkgoWeaponStateTree -Weapon= 按片段里的这条曲线填；曲线有 0 有 1 的（换弹匣、压弹的开始与结束）照读曲线，不用它
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Step")
	bool bLeftHandOff = false;

	/**
	 * 这一步的片段自己带着进膛的那一发（抛壳口装填：手从腰带上拿来、放进抛壳口的就是枪上的 shell_feed，定义的 NextRoundBone）：
	 * 放这一步时不按"供弹空了、膛里没有活弹"缩它，不然枪里一发也没有时手里是空的
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Step")
	bool bNextRoundInClip = false;
};

/**
 * @class UGinkgoWeaponDefinition : 一把具体设定的枪
 * 见 Docs/Design/Weapons.md「定义」：一个数据资产就是一把具体设定的枪，同一个模型换了设定就是另一个资产。
 * 武器槽里存的、地上的枪复制的都是它；动画层、相机、移动从这里取这把枪的参数，各处都不按枪写分支。
 * 98k 的资产 DA_Mauser98k 由命令 GinkgoAnimBlueprint -AddWeaponLayers 生成（不在仓库里），之后在编辑器里调
 */
UCLASS(BlueprintType)
class CASEGINKGO_API UGinkgoWeaponDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UGinkgoWeaponDefinition();

	/**
	 * AI 持有本枪时使用的默认决策 StateTree（Ginkgo Enemy AI schema），与武器自身的机构树分开。
	 * 敌人实例的有效覆盖树优先；两者都未指定时回退到敌人定义。换树时保留感知记忆和巡逻进度。
	 * 使用硬引用随武器定义一起加载，避免交战换枪时再同步读取决策资产。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI", meta = (DisplayName = "AI默认行为树"))
	TObjectPtr<UStateTree> DefaultAIStateTree;

	/** 两头的进度要的时间，给 Ginkgo::Weapon::StepHoldState */
	FGinkgoHoldTimings GetHoldTimings() const;

	/** 开一枪以后多久能接下一步：有射速时按射速，否则 ShotRecoveryTime */
	float GetShotInterval() const { return FireRate > 0.f ? 60.f / FireRate : ShotRecoveryTime; }

	//~ Begin UPrimaryDataAsset
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset

	/** 游戏里的名字：拾取的提示（"拾取 Karabiner 98 kurz 7.92mm"）与右下角的武器栏 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Display")
	FText DisplayName;

	/** 口径：武器栏写在膛里那一发旁边（"7.92 × 57mm Mauser"）。这把枪只吃这一种口径的弹，膛里那一发就是它 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Display")
	FText CaliberName;

	/**
	 * 武器栏的图标：页面 Content/UI/WeaponHud 认的名字，页面按枪栓的状态换图（98k 是 Mauser98k：枪栓复位、拉开两张，
	 * 从 Private/anim/Mauser98k/ui 的渲染图做成白色的）。空的不画图
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Display")
	FName HudIcon;

	/** 准星样式。ZF39 对应瞄具，SpreadRing 在腰射和开镜时都画扩散圈和固定大小的中心点。留空用默认点。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Display")
	FName AdsReticle;

	/** 准星两边的横线（"- · -"）的内端最少离中心多远（逻辑 px）：横线画在扩散的圆锥上，扩散很小时停在这里，不贴到点上。0 是只有点 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Display", meta = (ClampMin = 0))
	float CrosshairGap = 5.f;

	/** 开镜准星的两根横柱之间空多少的一半（逻辑 px，同页面画的）：扩散的圆锥比它大时两根横柱张开。98k 的 ZF39 缩小版是 4.5 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Display", meta = (ClampMin = 0))
	float AdsReticleGap = 4.5f;

	/** 进哪一种武器槽（Docs/Design/Weapons.md「武器槽」）：拿到时进这一种的空槽，种类也定了枪收在哪里 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slot")
	EGinkgoWeaponSlotKind SlotKind = EGinkgoWeaponSlotKind::Primary;

	/** 枪的网格：轴心在握把上（片段里 ik_hand_gun 的位置），挂上去不用偏移。地上的枪用它 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mesh")
	TObjectPtr<UStaticMesh> Mesh;

	/** 工作台按这个顺序列出配件，同一个挂点可以给出几种选择 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachments", meta = (TitleProperty = "Mount"))
	TArray<FGinkgoWeaponAttachmentMount> Attachments;

	/** 复杂模块外观与组合规则；目前 M4 使用，空值沿用独立静态配件的安装流程 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachments")
	TObjectPtr<class UGinkgoModularWeaponData> Modularity;

	/** 新武器的默认模块集合，按适配表顺序返回；普通附加配件不会自动安装 */
	FGinkgoInstalledAttachments GetDefaultAttachments() const;

	/** 检查替换后的整套装配；拒绝重复选择、不适配的配件与双向插槽冲突，不改动原集合 */
	bool CanInstall(const FGinkgoInstalledAttachments& Installed, const UGinkgoWeaponAttachment* Part) const;

	/** 生成替换后的集合，同插槽只保留新件；调用方须先通过 CanInstall */
	FGinkgoInstalledAttachments WithAttachment(const FGinkgoInstalledAttachments& Installed, UGinkgoWeaponAttachment* Part) const;

	/** 配件在这把枪上的挂点。没有配置或挂点名为空时不能安装 */
	const FGinkgoWeaponAttachmentMount* FindAttachment(const UGinkgoWeaponAttachment* Part) const;

	/** 手上、背上的枪：同样的形状与轴心，多了活动部件（枪栓）的骨骼。类默认值是 98k 的，资产里没存过这一项时跟着默认值走 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mesh")
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Weapons/Mauser98k/SK_Mauser98k.SK_Mauser98k")));

	/** 枪栓怎么动：类默认值是 98k 的 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mesh")
	FGinkgoBoltRig Bolt;

	/**
	 * 枪自己的动画不放时枪机关着的样子：这个片段的第一帧（M1911 是 A_M1911_Fire）。配了它这把枪的活动部件都由片段摆（FGinkgoWeaponStep::GunTrack），
	 * 枪栓的曲线只给回放记。空的按 Bolt 摆
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mesh")
	TObjectPtr<UAnimSequence> GunRestClosed;

	/** 枪机开着（挂住）的样子：这个片段的最后一帧（M1911 是 A_M1911_FireLast） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mesh")
	TObjectPtr<UAnimSequence> GunRestOpen;

	/** 弹匣的骨骼：弹匣不在枪上、又没放片段时缩成看不见 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mesh")
	FName MagazineBone;

	/**
	 * 枪自己的片段里从供弹出来进膛的那一发（霰弹枪的 shell_feed：泵动时从管里退到托弹板上、被枪机顶进膛）：
	 * 供弹空了、膛里也没有活弹时缩成看不见，打空了再泵不会凭空出来一发。空的不管
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mesh")
	FName NextRoundBone;

	/** 枪自己的片段里随枪机退出来的空壳（霰弹枪的 shell_hull）：膛里不是空壳时缩成看不见（压完膛空着时的泵动没有壳）。空的不管 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mesh")
	FName SpentCaseBone;

	/** 骨骼网格上弹药部件使用的材质槽序号；非负时按当前膛内或待装弹种的 RoundMaterial 动态切换弹头涂装，INDEX_NONE 表示不覆盖 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mesh")
	int32 RoundMaterialSlot = INDEX_NONE;

	/**
	 * 枪上看得见的供弹那几发，从最先进膛的那一发往后（MG3 露在进弹口和弹箱之间的弹链 belt_1 … belt_7，一发挂在前一发上）：
	 * 供弹里剩下的不够这么多时后面的缩成看不见（剩 3 发时只留 belt_1 … belt_3），打空了一发也没有。在枪自己的片段之后缩
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mesh")
	TArray<FName> VisibleFeedBones;

	/**
	 * 拿在手上时链接的动画层（实现 ALI_GinkgoWeaponLayers）。同一种持法的枪共用一个：步枪的层读下面的姿势与瞄准偏移，
	 * 一把枪就不用另做一个层的蓝图
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TSubclassOf<UAnimInstance> AnimLayerClass;

	/** 低持（常态）：食指在护圈外 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequenceBase> LoweredPose;

	/** 腰射：枪抵肩、平举，食指在扳机上 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequenceBase> HipPose;

	/** 开镜 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequenceBase> AimPose;

	/** 瞄准偏移（网格空间叠加，偏航 × 俯仰）：三种姿态都叠，冲刺时不叠 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UBlendSpace> AimOffset;

	/** 低持时瞄准偏移的俯仰用多少（偏航全用）；抬起来以后全用 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = 0, ClampMax = 1))
	float LoweredAimPitchScale = 0.5f;

	/**
	 * 低持跑动的循环：在 GASP 向前跑的循环上逐帧做的，上半身与枪随步子起伏、转动（98k 的 M_Mauser98k_Run_Loop_F）。
	 * 低持时按地速从低持的姿势混过去，步伐跟着移动所放的片段（UGinkgoWeaponAnimLayers）。空的照低持的姿势放（跑起来上半身不动）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequence> RunLoop;

	/** 冲刺的持枪循环：冲刺时按冲刺进度换过去，步伐同上（98k 照 COD19 的举枪冲刺，右手单手竖握）。空的冲刺照低持放 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequence> SprintLoop;

	/** 冲刺的循环两只手都握着枪。为假时（98k 的举枪冲刺）左手的 IK 权重随冲刺进度放到 0，左手照循环里的摆臂 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	bool bSprintTwoHanded = false;

	/** 跑的循环两只手都握着枪。为假时（M1911 单手跑）左手的 IK 权重随跑的循环放到 0，抬枪时回到枪上 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	bool bRunTwoHanded = true;

	/**
	 * 左手握在跟着枪机走的护木上（泵动霰弹枪）：泵动时左手的目标沿枪往后挪"枪机的行程 × 枪机走了多少"，手一直在护木上，
	 * 枪踢、瞄准挪了枪也跟着；泵动的片段（拉栓、闭栓那几步）里 HandIK_L 曲线不看（片段里为 0 是手随叠加走）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	bool bLeftHandOnForend = false;

	/** 低持的持枪循环：第一帧就是低持的姿势，时刻按动画实例的时钟一直走。空的低持照姿势放（静止） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequence> LoweredLoop;

	/** 腰射的持枪循环：同上 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequence> HipLoop;

	/** 低持、站着不动多久放一次放松（Steps 里 Weapon.Step.IdleRelax 的片段，没配的枪不放）：每次在这个范围里取一个数 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation", meta = (Units = "s"))
	FFloatInterval IdleRelaxDelay = FFloatInterval(15.f, 30.f);

	/**
	 * 开镜的持枪循环：在开镜的姿势上做的，身体慢慢晃、呼吸，肩与肘沉下再抬起，手指隔一阵放松再握住（98k 的 M_Mauser98k_Aim_Loop，16 s）。
	 * 第一帧就是开镜的姿势；枪管方向每一帧都与开镜的相同、只平移，两只手相对枪不动，所以开镜的握法与枪口对准照旧。
	 * 开镜时代替开镜的姿势放，时刻跟着动画实例的时钟一直走，不随开镜从头放（UGinkgoWeaponAnimLayers）。空的开镜照开镜的姿势放（静止）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimSequence> AimLoop;

	/** 低持的姿势里两只手在枪上的变换（枪的坐标）：生成命令 GinkgoAnimBlueprint 从姿势里量出来 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grip")
	FGinkgoWeaponHandTargets LoweredHands;

	/** 腰射的姿势里两只手在枪上的变换 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grip")
	FGinkgoWeaponHandTargets HipHands;

	/** 开镜的姿势里两只手在枪上的变换 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grip")
	FGinkgoWeaponHandTargets AimHands;

	/** 冲刺的循环里两只手在枪上的变换（第一帧的）。单手冲刺时左手不在枪上，只用右手的 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grip")
	FGinkgoWeaponHandTargets SprintHands;

	/** 枪口在枪的坐标里的位置；枪管方向是枪的 X 轴。生成命令取网格最前端那圈顶点的中心 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Grip")
	FVector MuzzleOffset = FVector::ZeroVector;

	/** 拔枪（从背上到手上）的蒙太奇：带 InHand 事件 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Actions")
	TObjectPtr<UAnimMontage> DrawMontage;

	/** 收枪（从手上到背上）的蒙太奇：带 OnBack 事件 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Actions")
	TObjectPtr<UAnimMontage> StowMontage;

	/**
	 * 这把枪的动作状态子树（Docs/Design/ActionStates.md「每种枪机一棵子树」）：同一种枪机共用一棵，差别是这里的片段与机构的开关。
	 * 空的用根树自己链着的那棵（只有就绪）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Actions", meta = (RequiredAssetDataTags = "Schema=/Script/CaseGinkgo.GinkgoWeaponStateTreeSchema"))
	TObjectPtr<UStateTree> ActionTree;

	/** 子树里每一步（Weapon.Step.*）的片段：同一种枪机的差别在这里 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Actions", meta = (Categories = "Weapon.Step"))
	TMap<FGameplayTag, FGinkgoWeaponStep> Steps;

	/** 低持 → 抵肩的时间：低持时按开火要等它走满才开 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = 0, Units = "s"))
	float RaiseTime = 0.18f;

	/** 抵肩 → 低持的时间 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = 0, Units = "s"))
	float LowerTime = 0.18f;

	/** 腰射开火以后留在腰射多久，再放回低持 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = 0, Units = "s"))
	float HipHoldTime = 1.5f;

	/**
	 * 开一枪以后多久接下一步（拉栓、装填）。片段自己 0.53 s 才回到开镜的姿势，你说开枪到拉栓有些慢：不等它放完，
	 * 下一步淡入时开火的后坐交叉淡出。小了枪口还在最高处手就离开握把了
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = 0, Units = "s"))
	float ShotRecoveryTime = 0.2f;

	/**
	 * 射速（发/分）：开一枪以后隔 60/射速 秒才能开下一枪，也才接下一步，这时不看上面的 ShotRecoveryTime。
	 * 0 是没有射速这回事（栓动的枪，下一枪要等拉栓）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = 0))
	float FireRate = 0.f;

	/** 射击模式：连发的枪按着开火键就按射速接着打，下一枪的时刻按该响的时刻往后排，与帧率无关 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling")
	EGinkgoFireMode FireMode = EGinkgoFireMode::Single;

	/** 实际开火的 AI 听觉传播半径（cm），与播放音量无关；0 禁用，已装配件可以乘一个距离系数。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Noise", meta = (ClampMin = "0", Units = "cm"))
	float FireNoiseRange = 6000.f;

	/**
	 * 一枪几颗弹丸（霰弹枪的一发是一把弹丸，你说的）：每颗各自按这一刻的散布偏、各自打一条线，扣一次扳机只用一发子弹，
	 * 扩散、后坐、硝烟也只算一次。步枪、手枪是 1
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = 1, ClampMax = 32))
	int32 PelletCount = 1;

	/** 枪的伤害倍率（Docs/Design/Damage.md）：伤害、护甲伤害都是子弹的（UGinkgoAmmoDefinition），枪只乘这一个数（你说的，比如 1.1） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 0))
	float DamageMultiplier = 1.f;

	/**
	 * 瞄准后坐（连发的枪，Docs/Design/Weapons.md「后坐模板」）：弹道在模板里，写进视线，子弹永远打准星，压枪 1:1 抵消。
	 * 下面的枪踢、相机的晃动是每枪的手感，不进弹道，配了模板的枪把相机的踢动调小（持续的上抬归模板）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
	FGinkgoPatternRecoil PatternRecoil;

	/** 枪的踢动：程序化的后坐，开一枪给一个冲量 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
	FGinkgoGunKick GunKick;

	/** 相机的晃动：开一枪每层一个弹簧，加起来只晃画面。默认是 98k 的两层：低频的踢动、高频的抖动 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (TitleProperty = "Name"))
	TArray<FGinkgoCameraRecoil> CameraRecoil;

	/**
	 * 操作枪的几个时刻的相机震动（2026-11-12 你说的：拉栓时也给一些小的相机震动，不用很大）：按声音的时刻（Weapon.Cue.*）找，枪上配了这个时刻的声音才发。
	 * 落在声音的撞击点上（动画的提示点按提前量早发，武器组件等到那一刻才给相机事件 Weapon.Event.Handling），每层一个弹簧、只晃画面。
	 * 类默认值是 98k 拉栓的四下（扳起、拉到底、推到底、压下）；资产里没存过这一项的枪跟着默认值走，AK 的拉机柄、霰弹枪的泵动也会震，不要就在它的资产里清空
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (Categories = "Weapon.Cue"))
	TMap<FGameplayTag, FGinkgoCueCameraShake> CueCameraShakes;

	/**
	 * 动画接触时的镜头冲量，以通知名索引。RoundIn、RoundChamber、MagOut、MagIn 对应装填动作，
	 * InHand、OnBack 对应换挂点。这里跟着接触帧走；要按声音提前量排期的拉栓冲量配在 CueCameraShakes
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
	TMap<FName, FGinkgoCameraRecoil> ContactCameraShakes;

	/** 声音：哪个时刻（Weapon.Cue.*）放什么、多响、提前多少。类默认值是 98k 的，资产里没存过这一项时跟着默认值走 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio", meta = (Categories = "Weapon.Cue"))
	TMap<FGameplayTag, FGinkgoWeaponCue> Cues;

	/**
	 * 其他角色枪声的空间化与距离衰减，声音从发射时的枪口位置播放；普通弹、特殊弹和配件替换声共用。
	 * 本地玩家保留不受相机位置影响的近身声音。空值不播放世界枪声，避免退回全地图等响；不影响 AI 的 FireNoiseRange。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio")
	TSoftObjectPtr<USoundAttenuation> FireAttenuation = TSoftObjectPtr<USoundAttenuation>(FSoftObjectPath(TEXT("/Game/Audio/SA_WeaponFire.SA_WeaponFire")));

	/** 散布（精度）：Docs/Design/Weapons.md「散布」。类默认值是 98k 的 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spread")
	FGinkgoWeaponSpread Spread;

	/** 看得见的子弹与弹壳：压弹时手里的、枪机开着时弹仓最上面的、开栓抛出去的（Docs/Design/ActionStates.md「子弹与弹壳」） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rounds")
	FGinkgoRoundVisuals Rounds;

	/** 枪口的火光与硝烟：开一枪在枪口放一次（朝枪管方向）。每把枪选自己的，空的不放 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
	TSoftObjectPtr<UNiagaraSystem> MuzzleEffect = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Weapons/FX/NS_MuzzleBlast.NS_MuzzleBlast")));

	/** 硝烟放多大：长得一样，大小、喷多远都乘这个数（系统的烟开了 Apply Owner Scale To Attributes）。火光不变 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 0))
	float MuzzleSmokeScale = 1.f;

	/** 隔多久才冒一次硝烟（秒）：每枪都冒的话，一秒十发烟浓得看不见前面。这段时间里的枪只放 MuzzleFlashOnly。0 是每枪都冒 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 0, Units = "s"))
	float MuzzleSmokeInterval = 0.f;

	/** 只有火光、没有硝烟的枪口效果（NS_MuzzleBlast 去掉烟的发射器，GinkgoWeaponFx -FlashOnly） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
	TSoftObjectPtr<UNiagaraSystem> MuzzleFlashOnly = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Weapons/FX/NS_MuzzleFlash.NS_MuzzleFlash")));

	/** 枪口冒的一缕细烟（Docs/Design/ActionStates.md「后坐」）：跟着枪口走，在世界里留下一条飘带。开一枪冒一阵，越来越淡。每把枪选自己的，空的不冒 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil")
	TSoftObjectPtr<UNiagaraSystem> MuzzleWisp = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Weapons/FX/NS_MuzzleWisp.NS_MuzzleWisp")));

	/** 开一枪细烟冒多久 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 0, Units = "s"))
	float MuzzleWispTime = 4.f;

	/** 细烟刚冒出来有多浓 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Recoil", meta = (ClampMin = 0, ClampMax = 1))
	float MuzzleWispOpacity = 0.8f;

	/**
	 * 枪口的热浪（Docs/Design/Weapons.md「枪管的热」）：枪管热了，上方的空气往上走、折射着后面的东西晃。挂在枪口往后 MuzzleHeatOffset，
	 * 冒得多密、折射多强都按枪管多热（Ginkgo::Weapon::HeatHazeAlpha）。每把枪选自己的，空的不冒
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heat")
	TSoftObjectPtr<UNiagaraSystem> MuzzleHeat = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Weapons/FX/NS_MuzzleHeat.NS_MuzzleHeat")));

	/** 开一枪枪管热多少（°C，比环境高出来的那部分）。默认按 AK 的薄枪管 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heat", meta = (ClampMin = 0))
	float HeatPerShot = 3.5f;

	/** 枪管凉下来的时间常数：高出来的热按 e^(−t/τ) 退（真的枪管要几分钟，这里缩短了） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heat", meta = (ClampMin = 0.1, Units = "s"))
	float HeatCoolTime = 15.f;

	/** 枪管比环境高出多少（°C）开始有热浪：连着打 10–15 发才看得见（射手们说的），一两发只有开枪那一下的热气 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heat", meta = (ClampMin = 0))
	float HeatHazeStart = 20.f;

	/** 枪管比环境高出多少（°C）热浪最强 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heat", meta = (ClampMin = 0))
	float HeatHazeFull = 110.f;

	/** 每一枪枪口喷出来的热气：开枪那一下加的热浪（0–1，加在枪管的那份上），0.25 s 退掉。冷枪开一枪也晃一下 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heat", meta = (ClampMin = 0, ClampMax = 1))
	float HeatShotPuff = 0.3f;

	/** 热浪的中心在枪口往后多远（cm，沿枪管）：冒出来的地方是沿枪管 ±10 cm 的一段 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Heat", meta = (ClampMin = 0))
	float MuzzleHeatOffset = 8.f;

	/** 低持 ↔ 冲刺持枪的时间：冲刺进度走满的时间，进出冲刺都用它。默认同状态机换步态的过渡（0.2 s）；大了举枪、放枪拖沓 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = 0, Units = "s"))
	float SprintTime = 0.2f;

	/**
	 * 开镜瞄准速度：开镜进度走满的时间。相机拉近、上半身抬到开镜、灵敏度都按它走；
	 * 以后有散布时，腰射的散布插到开镜的散布也按它
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = 0, Units = "s"))
	float AdsTime = 0.3f;

	/** 开镜缩放倍率：开镜时相机的 FOV = 2·atan(tan(常态 FOV / 2) / 倍率)。1.73 时常态 90° 开镜 60° */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = 1))
	float AdsZoom = 1.732f;

	/**
	 * 腰射操作性：不开镜时瞄准方向追相机视线的欠阻尼弹簧的角频率。小了转镜头时准星落后得多。
	 * 以 Ω 转镜头时稳态落后 2ζΩ/ω（Docs/Design/Weapons.md「腰射操作性」）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = 0, ForceUnits = "rad/s"))
	float HipHandlingOmega = 20.f;

	/** 腰射操作性：同上那个弹簧的阻尼比（0–1）。小了停下以后过冲得多 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = 0, ClampMax = 1))
	float HipHandlingZeta = 0.7f;

	/** 腰射操作性：瞄准方向最多落后视线多少度：甩镜头时枪不被扯得太远，白点不跑出屏幕。0 不限 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Handling", meta = (ClampMin = 0, Units = "Degrees"))
	float HipHandlingMaxLag = 30.f;

	/**
	 * 持枪的地速倍率：枪在手上时乘地面上的跑速、冲刺速度、蹲速与滑铲的起步加速和上限。
	 * 我们的移动参数取的是 Apex 收枪时的值，一般的枪从 Apex 的 0.87（1 / 1.15）起
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = 0, ClampMax = 1))
	float HeldSpeedScale = 0.87f;

	/** 开镜的地速倍率：开镜时按开镜进度从 1 缓到它，乘地面上的跑速、蹲速，冲刺时不乘 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = 0, ClampMax = 1))
	float AdsSpeedScale = 0.5f;

	/**
	 * 机构：供弹、膛、枪机的规则（编辑器里内联的对象，Docs/Design/Weapons.md「机构」）。98k 是内置弹仓 5 发、逐发压、压弹要开栓、手动拉栓。
	 * 空的是没有子弹的武器：武器栏不画供弹与膛
	 */
	UPROPERTY(EditDefaultsOnly, Instanced, BlueprintReadOnly, Category = "Mechanism")
	TObjectPtr<UGinkgoWeaponMechanism> Mechanism;

	/** 这把枪的弹种：拿到时装满它，装填默认压它，武器栏的后备数的是它（98k 是 Ammo.Mauser792x57.Standard） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mechanism", meta = (Categories = "Ammo"))
	FGameplayTag AmmoType;

	/**
	 * 拿到这把枪时带的后备子弹（AmmoType）。98k 是 60 发：二战德军步兵的两组三格弹盒，每格两个桥夹。
	 * 丢枪时带不带走以后做丢枪时定，现在每拿到一次给一次
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mechanism", meta = (ClampMin = 0))
	int32 StartingReserve = 0;
};
