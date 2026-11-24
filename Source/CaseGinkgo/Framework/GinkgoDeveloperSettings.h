#pragma once

#include "CoreMinimal.h"
#include "Combat/GinkgoCombatTypes.h"   // FGinkgoTargetSpawn
#include "Control/GinkgoAimAssist.h"    // FGinkgoAimAssistConfig
#include "Engine/DeveloperSettings.h"
#include "UI/GinkgoToast.h"             // FGinkgoToastTimings
#include "UI/GinkgoUiSounds.h"          // FGinkgoUiSounds
#include "UI/GinkgoMainMenuSettings.h"  // FGinkgoTitleSounds
#include "UI/GinkgoLocationTitleSettings.h"
#include "UI/GinkgoCreditsSettings.h"
#include "Weapon/GinkgoScope.h"         // FGinkgoScope
#include "GinkgoDeveloperSettings.generated.h"

class UGinkgoCharacterModel;

class UAnimSequenceBase;
class UGinkgoAmmoDefinition;
class UGinkgoCameraConfig;
class UGinkgoMovementConfig;
class UGinkgoWeaponDefinition;
class UAnimInstance;
class UAnimMontage;
class USkeletalMesh;
class UStaticMesh;
class UStateTree;

/**
 * @class UGinkgoDeveloperSettings : CaseGinkgo 设置
 * 在 项目设置 → Game → CaseGinkgo 里配置。角色还是 C++ 类、没有蓝图子类时，配置资产从这里指定。
 */
/**
 * @struct FGinkgoWeaponSwapTiming : 换到背上的另一把主武器时，收枪、拔枪两个片段怎么接
 * 两把都在右肩后：手在肩后松开这一把的时候，就在接下一把的地方（三把主武器两个时刻的手差 0.2–1 cm），不放手落回腰边、再伸上来的那两段。
 */
USTRUCT()
struct CASEGINKGO_API FGinkgoWeaponSwapTiming
{
	GENERATED_BODY()

	/** 收枪放到片段的这一刻就换下一把：手在肩后松开枪（98k 0.40、AK 0.37、霰弹枪 0.38）。松开以后枪自己落到背上 */
	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (ClampMin = 0, Units = "s"))
	float StowRelease = 0.40f;

	/** 拔枪从片段的这一刻开始：比手接住枪（98k 0.49、AK 0.51、霰弹枪 0.51）早一点，交叉完正好接住 */
	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (ClampMin = 0, Units = "s"))
	float DrawStart = 0.44f;

	/** 两个片段交叉多久 */
	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (ClampMin = 0, Units = "s"))
	float Blend = 0.08f;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "CaseGinkgo"))
class CASEGINKGO_API UGinkgoDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UGinkgoDeveloperSettings* Get() { return GetDefault<UGinkgoDeveloperSettings>(); }

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** 移动配置资产：移动组件自己没有指定时用它；都没有时用 UGinkgoMovementConfig 的类默认值 */
	UPROPERTY(EditAnywhere, Config, Category = "Movement")
	TSoftObjectPtr<UGinkgoMovementConfig> MovementConfig;

	/** 相机配置资产：相机管理器自己没有指定时用它；都没有时用 UGinkgoCameraConfig 的类默认值 */
	UPROPERTY(EditAnywhere, Config, Category = "Camera")
	TSoftObjectPtr<UGinkgoCameraConfig> CameraConfig;

	/**
	 * 项目动作、命中部位和物品挂点使用的源网格，默认为 UEFN Mannequin。
	 * 玩家与敌人的外观从这里读取最终姿势；源网格缺失时角色使用占位体。
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Character")
	TSoftObjectPtr<USkeletalMesh> CharacterMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin")));

	/** 玩家使用的外观，所有端使用同一配置。留空时直接显示驱动项目动画的源网格。 */
	UPROPERTY(EditAnywhere, Config, Category = "Character")
	TSoftObjectPtr<UGinkgoCharacterModel> PlayerModel = TSoftObjectPtr<UGinkgoCharacterModel>(FSoftObjectPath(TEXT("/Game/Characters/Ginkgo/Models/DA_Model_Survival.DA_Model_Survival")));

	/**
	 * 角色的动画蓝图：默认是命令 GinkgoAnimBlueprint 生成的 ABP_GinkgoCharacter（父类 UGinkgoAnimInstance）。
	 * 读不到时用 UGinkgoAnimInstance 本身：数据照算，网格摆着参考姿势
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Character", meta = (AllowAbstract = false))
	TSoftClassPtr<UAnimInstance> CharacterAnimClass = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(TEXT("/Game/Characters/Ginkgo/ABP_GinkgoCharacter.ABP_GinkgoCharacter_C")));

	/**
	 * 空中蹲下、站起时模型跟上新胶囊的快慢（rad/s，临界阻尼弹簧，Docs/Design/MovementComponent.md「空中蹲下时的模型」）：
	 * 胶囊中心不动、底抬高或放低 48 cm，模型不一帧跳过去，按这个快慢跟上。25 时 0.16 s 走完九成；越大越快，0 不平滑（一帧跳过去）
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Character", meta = (ClampMin = 0))
	float CrouchMeshBlendOmega = 25.f;

	/** 设置页、调参面板与工作台的界面音效。每种提示可单独指定声音和音量；工作台还有独立的音量倍数。 */
	UPROPERTY(EditAnywhere, Config, Category = "UI")
	FGinkgoUiSounds UiSounds;

	/** 开屏两镜与主菜单揭幕转场音效（裁剪自 opening.wav）及主菜单循环背景音乐（Brother's Keeper）。 */
	UPROPERTY(EditAnywhere, Config, Category = "UI", meta = (DisplayName = "Title Audio"))
	FGinkgoTitleSounds TitleSounds;

	/** toast（Docs/Design/Control.md「Toast」）：出现、停留、消失、到下一条之前各多久 */
	UPROPERTY(EditAnywhere, Config, Category = "UI")
	FGinkgoToastTimings ToastTimings;

	/** 所有检查点共用的地点标题：字体、扁平排版和淡入淡出；下一次入队时生效，不逐点覆盖。 */
	UPROPERTY(EditAnywhere, Config, Category = "UI", meta = (DisplayName = "Location Title"))
	FGinkgoLocationTitleSettings LocationTitle;

	/** 所有署名 Volume 共用的排版和逐字模糊显现时序，作者、作品及其字面在关卡中逐条配置。 */
	UPROPERTY(EditAnywhere, Config, Category = "UI", meta = (DisplayName = "Credits"))
	FGinkgoCreditsSettings Credits;

	// ===== 武器（见 Docs/Design/Weapons.md）=====

	/**
	 * 开局放进第一个武器槽的枪，也是地上的枪没指定时的那把：98k 的数据资产（命令 GinkgoAnimBlueprint -AddWeaponLayers 生成）。
	 * 不在仓库里；读不到时角色不带枪
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon")
	TSoftObjectPtr<UGinkgoWeaponDefinition> StartingWeapon = TSoftObjectPtr<UGinkgoWeaponDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/Mauser98k/DA_Mauser98k.DA_Mauser98k")));

	/**
	 * 开局放进手枪的槽的枪：M1911 的数据资产（命令 GinkgoAnimBlueprint -AddM1911 生成，不在仓库里；读不到时不带）。
	 * 测量（-unattended）不带：之前的路线都是按一把枪写的，要手枪的路线加 -GinkgoSidearm
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon")
	TSoftObjectPtr<UGinkgoWeaponDefinition> StartingSidearm = TSoftObjectPtr<UGinkgoWeaponDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/M1911/DA_M1911.DA_M1911")));

	/**
	 * 开局放进第二个主武器槽的枪：AK-47 的数据资产（命令 GinkgoAnimBlueprint -AddAK47 生成）。读不到时不带。
	 * 测量（-unattended）不带：之前的路线都是按一把主武器写的，要它的路线加 -GinkgoSecondPrimary
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon")
	TSoftObjectPtr<UGinkgoWeaponDefinition> StartingSecondPrimary = TSoftObjectPtr<UGinkgoWeaponDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/AK47/DA_AK47.DA_AK47")));

	/**
	 * 开局放在角色面前地上的枪（一排，枪身横着），用捡枪拿（Docs/Design/Interaction.md「丢枪与捡枪」）：泵动霰弹枪、MG3、模块化 M4。
	 * 读不到的不放。测量（-unattended）不放：之前的路线地上没有枪，要它的路线加 -GinkgoGroundWeapons
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon")
	TArray<TSoftObjectPtr<UGinkgoWeaponDefinition>> StartingGroundWeapons = {TSoftObjectPtr<UGinkgoWeaponDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/Shotgun/DA_Shotgun.DA_Shotgun"))),
		TSoftObjectPtr<UGinkgoWeaponDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/MG3/DA_MG3.DA_MG3"))),
		TSoftObjectPtr<UGinkgoWeaponDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/M4/DA_M4.DA_M4"))),
		TSoftObjectPtr<UGinkgoWeaponDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/M79/DA_M79.DA_M79")))};

	/** 地上那排枪离角色的水平距离（cm），沿开局角色前方放置 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon", meta = (Units = "cm"))
	float StartingGroundDistance = 160.f;

	/** 相邻两把开局地面武器的水平间距（cm），沿角色右侧排列 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon", meta = (Units = "cm"))
	float StartingGroundSpacing = 45.f;

	/** 玩家开局是否发放 AGinkgoGameMode::StartingLoadout 中的武器；关闭时开局空手，枪从场景拾取 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon")
	bool bStartWithWeapon = true;

	/** 开局的枪拿在手上（低持）；关掉时背在背上，要拔枪（ginkgo.action.test draw） */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon")
	bool bStartWeaponInHand = true;

	/**
	 * 捡枪抓住以后收拢到低持要多久（Docs/Design/Interaction.md「丢枪与捡枪」）：枪先在右手里，按两头缓过的进度带到持枪的位置，
	 * 持枪的层同样盖上来，拾取的片段同样淡出，左手后半段才握上去。原来是放拔枪片段的最后几帧，约 0.2 s 就到了（你说太快）。我定的，手测再调
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon", meta = (ClampMin = 0.1, Units = "s"))
	float PickupSettleTime = 0.6f;

	/**
	 * 弹道的烟最短隔多久留一道（Docs/Design/Weapons.md「弹道的烟」，你说的，2026-11-08：AK-47 的射速刚好每枪都留，MG3 对齐到它，射速太快没必要每发都有）。
	 * 0.1 s 是 AK-47 600 发/分一枪的间隔：AK 每枪都有，MG3（1200 发/分）隔一枪一道，打得慢的枪每枪都有。按这一枪排的时刻数（Ginkgo::Weapon::GetScheduledShotTime），同帧率无关。
	 * 枪口的硝烟另算（每把枪的 MuzzleSmokeInterval）
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon", meta = (ClampMin = 0, Units = "s"))
	float BulletTrailMinInterval = 0.1f;

	/**
	 * 弹种轮盘（Docs/Design/Weapons.md「切换弹种」，你说的：同一口径有几种弹的枪长按装填呼出）：按着装填多久算长按。
	 * 短按在松开时装填（这几把枪），所以短按装填晚了按着的那一会儿。我定的，手测再调
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon|Ammo Wheel", meta = (ClampMin = 0.05, Units = "s"))
	float AmmoWheelHoldTime = 0.25f;

	/**
	 * 鼠标选轮盘（你说的，2026-11-08：直接按鼠标相对运动的方向选，死区不用太大）：鼠标动的量按灵敏度换成度（同转视角，不反转、不加速、不乘开镜），攒成一个很短的向量，长过这么多就按它的方向选。
	 * 默认的灵敏度 0.175°/count 时 2.5° 是 14 count（800 DPI 约 0.45 mm）
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon|Ammo Wheel", meta = (ClampMin = 0, Units = "Degrees"))
	float AmmoWheelMotionDeadZone = 2.5f;

	/** 鼠标的方向最多攒多长（度）：短，一直往一个方向动以后，换方向时动这么多再加上死区就转过去了 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon|Ammo Wheel", meta = (ClampMin = 0, Units = "Degrees"))
	float AmmoWheelMotionRadius = 6.f;

	/** 鼠标停下来以后方向收回去的时间常数：停一下，下一次往哪动就是哪（选着的留着） */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon|Ammo Wheel", meta = (ClampMin = 0, Units = "s"))
	float AmmoWheelMotionDecay = 0.12f;

	/** 手柄选轮盘（视角那根摇杆，过了视角的死区以后的偏转）：推过这么多就按摇杆的方向选，松开回中留着最后选的 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon|Ammo Wheel", meta = (ClampMin = 0, ClampMax = 1))
	float AmmoWheelStickDeadZone = 0.2f;

	/** 子弹的数据（Docs/Design/Damage.md）：按弹种找（UGinkgoAmmoDefinition::Find），命令 GinkgoCombat -Ammo 生成。读不到的弹种打到东西不扣血。同一口径的几种就是弹种轮盘上的几项 */
	UPROPERTY(EditAnywhere, Config, Category = "Combat")
	TArray<TSoftObjectPtr<UGinkgoAmmoDefinition>> AmmoDefinitions = {
		TSoftObjectPtr<UGinkgoAmmoDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/Ammo/DA_Ammo_NATO556x45.DA_Ammo_NATO556x45"))),
		TSoftObjectPtr<UGinkgoAmmoDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/Ammo/DA_Ammo_Mauser792x57.DA_Ammo_Mauser792x57"))),
		TSoftObjectPtr<UGinkgoAmmoDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/Ammo/DA_Ammo_M43762x39.DA_Ammo_M43762x39"))),
		TSoftObjectPtr<UGinkgoAmmoDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/Ammo/DA_Ammo_NATO762x51.DA_Ammo_NATO762x51"))),
		TSoftObjectPtr<UGinkgoAmmoDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/Ammo/DA_Ammo_ACP45.DA_Ammo_ACP45"))),
		TSoftObjectPtr<UGinkgoAmmoDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/Ammo/DA_Ammo_Gauge12Buckshot.DA_Ammo_Gauge12Buckshot"))),
		TSoftObjectPtr<UGinkgoAmmoDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/Ammo/DA_Ammo_Gauge12Slug.DA_Ammo_Gauge12Slug"))),
		TSoftObjectPtr<UGinkgoAmmoDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/Ammo/DA_Ammo_Mauser792x57SmK.DA_Ammo_Mauser792x57SmK"))),
		TSoftObjectPtr<UGinkgoAmmoDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/Ammo/DA_Ammo_Grenade40x46.DA_Ammo_Grenade40x46"))),
		TSoftObjectPtr<UGinkgoAmmoDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/Ammo/DA_Ammo_Grenade40x46Flash.DA_Ammo_Grenade40x46Flash"))),
		TSoftObjectPtr<UGinkgoAmmoDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/Ammo/DA_Ammo_Grenade40x46Flechette.DA_Ammo_Grenade40x46Flechette")))};

	/**
	 * 辅助瞄准（Docs/Design/Control.md「辅助瞄准」）里不给玩家调的：目标的中线用哪两根骨头、要不要有输入。
	 * 强度、范围、跟随目标键鼠、手柄各一套（你说的），在玩家的设置界面里（FGinkgoInputSettings）。
	 * 调试：ginkgo.aimAssist.show 画出目标的中线与范围，ginkgo.aimAssist.scale 0 关掉
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Control")
	FGinkgoAimAssistConfig AimAssist;

	/**
	 * 开局放的人形靶（Docs/Design/Damage.md「靶子」，我定的）：在角色面前一排，几个距离，有的穿背心、有的再戴头盔。
	 * 测量（-unattended）不放：之前的路线前面没有东西，要它的路线加 -GinkgoTargets
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Combat")
	TArray<FGinkgoTargetSpawn> StartingTargets = {
		FGinkgoTargetSpawn(700.f, -150.f, false, false),
		FGinkgoTargetSpawn(1000.f, 120.f, true, false),
		FGinkgoTargetSpawn(1500.f, -250.f, true, true),
		FGinkgoTargetSpawn(2200.f, 220.f, false, false),
		FGinkgoTargetSpawn(3000.f, -80.f, true, true)};

	/** 人形靶的网格（人体模型：骨骼名对得上部位的表）、站着放的循环 */
	UPROPERTY(EditAnywhere, Config, Category = "Combat")
	TSoftObjectPtr<USkeletalMesh> TargetMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin")));

	UPROPERTY(EditAnywhere, Config, Category = "Combat")
	TSoftObjectPtr<UAnimSequenceBase> TargetIdle = TSoftObjectPtr<UAnimSequenceBase>(
		FSoftObjectPath(TEXT("/Game/Characters/UEFN_Mannequin/Animations/Idle/M_Neutral_Stand_Idle_Loop.M_Neutral_Stand_Idle_Loop")));

	/** 人形靶打死以后多久复位（血、护甲回满） */
	UPROPERTY(EditAnywhere, Config, Category = "Combat", meta = (ClampMin = 0, Units = "s"))
	float TargetRespawnDelay = 4.f;

	/** 从爆点到敌人眼位的最远距离（cm）。爆点须在敌人的视锥内且视线无遮挡；0 关闭此项。 */
	UPROPERTY(EditAnywhere, Config, Category = "Combat|Flashbang", meta = (DisplayName = "闪光视线半径", ClampMin = "0", Units = "cm"))
	float FlashRadius = 10000.f;

	/** 爆点周围的球形重叠半径（cm）。接触球体的敌人直接受闪，忽略朝向与遮挡；0 关闭此项。 */
	UPROPERTY(EditAnywhere, Config, Category = "Combat|Flashbang", meta = (DisplayName = "闪光近身半径", ClampMin = "0", Units = "cm"))
	float FlashProximityRadius = 1500.f;

	/** 敌人暂停感知与行动的游戏秒数，反复命中刷新截止时间。 */
	UPROPERTY(EditAnywhere, Config, Category = "Combat|Flashbang", meta = (ClampMin = "0", Units = "s"))
	float FlashDuration = 3.f;

	/** 背心（盖上胸、腹部）、头盔（盖头部）的耐久：护甲是第二条血（你选的 COD 战区式） */
	UPROPERTY(EditAnywhere, Config, Category = "Combat", meta = (ClampMin = 0))
	float TargetVestDurability = 100.f;

	UPROPERTY(EditAnywhere, Config, Category = "Combat", meta = (ClampMin = 0))
	float TargetHelmetDurability = 50.f;

	/**
	 * 命中的声音（Docs/Design/Damage.md「命中的声音」）：一枪打中了目标，开枪的本地玩家听到一声 2D 的，同白 X 一起。
	 * 六种按先后取一种：爆头打死、打死、打穿护甲、爆头、打在护甲上、命中。声音空着的那种不放
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Combat")
	FGinkgoHitSounds HitSounds;

	/**
	 * 武器的动作状态的根树（Docs/Design/ActionStates.md）：所有枪共用，枪的子树在枪的定义里（ActionTree）。
	 * 资产由命令 GinkgoWeaponStateTree 生成，进库。读不到时换枪的键不响应，持枪照常
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon")
	TSoftObjectPtr<UStateTree> WeaponStateTree = TSoftObjectPtr<UStateTree>(FSoftObjectPath(TEXT("/Game/WeaponStates/ST_WeaponRoot.ST_WeaponRoot")));

	/**
	 * 拿在手上时挂的骨头：虚拟骨骼 VB spine_05_ik_hand_gun（源 spine_05、目标 ik_hand_gun，导入命令 -VirtualBones 加）。
	 * 每个片段里它就是片段的 ik_hand_gun（武器的片段里跟着枪走，右手换弹时会离开枪），又跟着胸口走：上半身分层混合以后枪不离开手。
	 * 网格上没有它时挂 ik_hand_gun
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon")
	FName WeaponHandBone = TEXT("VB spine_05_ik_hand_gun");

	/** 背在背上时挂的骨头 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon")
	FName WeaponBackBone = TEXT("spine_05");

	/**
	 * 背上的插槽：收枪片段里枪落到背上以后，ik_hand_gun 相对 WeaponBackBone 的变换（从 UE 求值出来的姿势算的，
	 * 收枪第 18–28 帧都相同，差不到 0.0001 cm）
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon")
	FTransform WeaponBackOffset = FTransform(FQuat(-0.29213663610358603, -0.011724142144377586, 0.9555355482055787, -0.038347704657828996),
		FVector(-7.487133553054036, -14.570258371891393, 10.132773898461792));

	/**
	 * 第二个主武器槽相对第一个差多少（枪自己的坐标，ik_hand_gun：X 沿枪身，Y 离开背，Z 在背的平面里朝枪的上面，也就是人的右下）：
	 * 两把都在右肩后、并排，收枪、拔枪用同一套片段（Docs/Design/Weapons.md「武器槽」）。Y 是枪的厚度，弹匣从第一把的后面过去
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon")
	FTransform SecondWeaponBackShift = FTransform(FVector(0.0, 6.0, 7.0));

	/** 换到背上的另一把主武器时，收枪、拔枪两个片段怎么接（Docs/Design/ActionStates.md「第二把枪」） */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon")
	FGinkgoWeaponSwapTiming WeaponSwap;

	/**
	 * 瞄具（Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）：按枪的开镜准星的名字（定义的 AdsReticle）找。有瞄具的枪开镜时进第一人称，
	 * 只有镜内放大（你说的）；别的枪照旧越肩拉近。默认只有 98k 的 ZF39，枪的数据资产不用改
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon")
	TMap<FName, FGinkgoScope> Scopes = {{FName(TEXT("ZF39")), FGinkgoScope()}};

	/** @brief 开镜准星的名字对应的瞄具；没有时为空 */
	const FGinkgoScope* FindScope(const FName Reticle) const { return Reticle.IsNone() ? nullptr : Scopes.Find(Reticle); }

	/** 手枪收起来时挂的骨头：枪套在腰的右边（Docs/Design/Weapons.md「武器槽」） */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon")
	FName SidearmStowBone = TEXT("pelvis");

	/**
	 * 枪套的插槽：收枪片段（M1911）里枪插到底以后，ik_hand_gun 相对 SidearmStowBone 的变换
	 * （Private/anim/M1911/exports/M_M1911_Stow.socket.json，第 28 帧）
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Weapon")
	FTransform SidearmStowOffset = FTransform(FQuat(-0.02736440238197943, -0.7007936159715988, -0.70368083023894, -0.11389814067826046),
		FVector(5.891527310290706, 2.146864365615959, 15.565993182104926));

	// ===== 交互：两个距离在每个目标上，这里是交互组件共用的（见 Docs/Design/Interaction.md） =====

	/** 离准星超过这个角度的目标不当焦点。大了身后的东西也能捡，小了要对准才捡得到 */
	UPROPERTY(EditAnywhere, Config, Category = "Interaction", meta = (ClampMin = 0, ClampMax = 180, Units = "Degrees"))
	float InteractionMaxFocusAngle = 60.f;

	/** 换焦点时新的要离准星近这么多才换。大了换得迟钝，小了两件挨着的东西来回跳 */
	UPROPERTY(EditAnywhere, Config, Category = "Interaction", meta = (ClampMin = 0, Units = "Degrees"))
	float InteractionFocusSwitchMargin = 3.f;

	/** 离开提示距离、交互距离时多放的距离：站在边上提示不闪 */
	UPROPERTY(EditAnywhere, Config, Category = "Interaction", meta = (ClampMin = 0, Units = "cm"))
	float InteractionRangeHysteresis = 20.f;

	/**
	 * 服务器判断请求时在交互距离上多放的容差：服务器上角色的位置与客户端按下时不同（移动的包合并、丢失、晚到）。
	 * 大了能隔得更远捡，小了联网时在边上按会被拒绝
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Interaction", meta = (ClampMin = 0, Units = "cm"))
	float InteractionServerTolerance = 60.f;

	/** 客户端等服务器回复的上限：到时还没有结果就不再等，可以再按 */
	UPROPERTY(EditAnywhere, Config, Category = "Interaction", meta = (ClampMin = 0, Units = "s"))
	float InteractionRequestTimeout = 1.f;

	// ===== 拾取（见 Docs/Design/Interaction.md「拾取子弹盒」） =====

	/**
	 * 关卡里的子弹盒：Private/models/MauserAmmoBox 的 SM_MauserAmmoBox（盖开着，带两发散弹），导入在 /Game/Pickups/MauserAmmoBox
	 * （Content 不提交）。资产不在时子弹盒没有网格，照样能拾取
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Pickup")
	TSoftObjectPtr<UStaticMesh> AmmoBoxMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Pickups/MauserAmmoBox/SM_MauserAmmoBox.SM_MauserAmmoBox")));

	/** 拾取时手里的子弹盒：同一个盒子去掉两发散弹（Blender 里导出时不选它们，盖照样开着） */
	UPROPERTY(EditAnywhere, Config, Category = "Pickup")
	TSoftObjectPtr<UStaticMesh> AmmoBoxHandMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Pickups/MauserAmmoBox/SM_MauserAmmoBox_Hand.SM_MauserAmmoBox_Hand")));

	/**
	 * 左手、右手拾取的蒙太奇（只动那条胳膊的槽 ArmL、ArmR），命令 GinkgoAnimMontage 从 Blender 做的 Pickup_L/R 生成，带 Grab、Stow 两个事件。
	 * 资产不在时拾取不放动画，按下就给
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Pickup")
	TSoftObjectPtr<UAnimMontage> PickupMontageLeft = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(TEXT("/Game/Pickups/Animations/AM_Pickup_L.AM_Pickup_L")));

	UPROPERTY(EditAnywhere, Config, Category = "Pickup")
	TSoftObjectPtr<UAnimMontage> PickupMontageRight = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(TEXT("/Game/Pickups/Animations/AM_Pickup_R.AM_Pickup_R")));
};
