#pragma once

#include "CoreMinimal.h"
#include "Animation/Actions/GinkgoAnimActionTypes.h"  // EGinkgoHand：选哪只手去拿
#include "GameFramework/Actor.h"
#include "Interaction/GinkgoInteractableComponent.h"
#include "GinkgoPickup.generated.h"

class AGinkgoCharacter;
class AGinkgoPickup;
class UAnimMontage;
class UGinkgoAnimAction;
class UGinkgoAnimActionComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * 拾取的蒙太奇里的事件（PlayMontageNotify 的名字），命令 GinkgoAnimMontage 按片段的 Item_InHand 曲线生成：
 * Grab 是手碰到东西的那一帧（曲线第一次为 1，你说的 OnInteract），Stow 是东西收到身上的那一帧（曲线最后一次为 1：现成片段第 30 帧放进口袋）
 */
namespace Ginkgo::PickupEvents
{
	inline const FName Grab(TEXT("Grab"));
	inline const FName Stow(TEXT("Stow"));
}

/**
 * 拾取里与世界无关的规则：选手、手够不够得着、给不给效果。写成纯函数，测试直接调（CaseGinkgo.Interaction.Pickup.*）。
 * 见 Docs/Design/Interaction.md「拾取子弹盒」。
 */
namespace Ginkgo::Pickup
{
	/**
	 * @brief 用哪只手去拿
	 * @param bLeftFree - 左臂能占（占用的规则）
	 * @param bRightFree - 右臂能占：枪在手上、正在拔枪收枪、右手已经在拾取时不能
	 * @param LeftShoulder - 左肩关节（世界）
	 * @param RightShoulder - 右肩关节（世界）
	 * @param GraspPoint - 东西上手要抓的点（世界）
	 * @return 只有一只手能用时就是它；都能用时离抓的点近的那个肩膀的手，一样近时右手；都不能用时没有
	 */
	CASEGINKGO_API TOptional<EGinkgoHand> ChooseHand(bool bLeftFree, bool bRightFree, const FVector& LeftShoulder, const FVector& RightShoulder, const FVector& GraspPoint);

	/**
	 * @brief 那只手够得着的椭球的心：肩关节挪 Offset（角色的坐标：X 往前，Y 往身体中间——左手往右、右手往左，Z 往上）
	 * @param CharacterForward - 角色的朝向（只取水平的）
	 */
	CASEGINKGO_API FVector ReachCentre(const FVector& Shoulder, const FVector& CharacterForward, EGinkgoHand Hand, const FVector& Offset);

	/**
	 * @brief 抓的点在够得着的椭球的几倍处（不超过 1 就够得着）：抓的点相对球心的偏移换到角色的坐标（往前、往身体中间、往上），
	 *        各轴除以半轴（加上余量）
	 * @param Radii - 三个半轴（cm）：往前后、往两边、往上下
	 * @note 实测的（pickup.py --reach-sweep）：手转向东西、伸手时上半身参与并前倾以后，各方向能抓到的远处不是球（往两边远、往上下近），
	 *       椭球与十个方向量到的都差不到 2.8 cm，判成够得着却贴不上的不超过 0.9 cm（见 AGinkgoPickup::ArmReachRadii）
	 */
	CASEGINKGO_API double ArmReachRatio(const FVector& Centre, const FVector& GraspPoint, const FVector& CharacterForward, EGinkgoHand Hand, const FVector& Radii,
		double Margin);

	/**
	 * @brief 那只手够不够得着：抓的点在够得着的椭球里（ArmReachRatio 不超过 1）
	 * @note 不是拾取的条件（拾取的范围是以角色为中心的球），也不挡动画：够不着时照样放，手伸到最远，Grab 那一帧东西直接挂到手上（SnapToHand）
	 */
	CASEGINKGO_API bool IsWithinArmReach(const FVector& Centre, const FVector& GraspPoint, const FVector& CharacterForward, EGinkgoHand Hand, const FVector& Radii,
		double Margin);

	/**
	 * @brief 动作进行中判"走开了"用的椭球的半轴：开始时够得着的就是够得着的椭球（照旧出了它再加余量才中止）；开始时就够不着的放大到正好过开始的那个点，
	 *        比开始时再远出余量才中止，不然够不着的拾取第一帧就中止
	 * @param StartRatio - 开始时抓的点在够得着的椭球的几倍处（ArmReachRatio，余量为 0）
	 */
	CASEGINKGO_API FVector WalkAwayRadii(const FVector& Radii, double StartRatio);

	/**
	 * @brief 东西挂到手上的变换（东西的坐标 → 手骨的坐标）：朝向照此刻的，位置挪到东西上抓的点正好落在手里的挂点上。
	 *        够得着时两点只差零点几厘米，挂上去不跳；够不着时手伸到了最远，东西在这一帧直接挂到手上
	 * @param ItemToHand - 东西此刻相对手骨的变换
	 * @param GraspPoint - 东西上手要抓的点（东西的坐标）
	 * @param ItemInHand - 手里的挂点（手骨的坐标）
	 */
	CASEGINKGO_API FTransform SnapToHand(const FTransform& ItemToHand, const FVector& GraspPoint, const FVector& ItemInHand);

	/**
	 * @struct FGrant : 一次拾取在本地给不给效果
	 * 要两样都有才给，只给一次：本地到了给的时刻（动画的 Stow，或者中止：中止就是马上完成），服务器接受了。
	 * 被拒绝、等不到的不给；被拒绝以后再来的"到了时刻"也不给。
	 */
	struct CASEGINKGO_API FGrant
	{
		/** @brief 本地到了给的时刻 @return 这一下要给 */
		bool MarkDue();

		/** @brief 服务器接受了 @return 这一下要给 */
		bool MarkAccepted();

		/** @brief 服务器拒绝了，或者等不到：不给 */
		void MarkRejected();

		bool IsDue() const { return bDue; }
		bool IsAccepted() const { return bAccepted; }
		bool IsRejected() const { return bRejected; }
		bool IsGranted() const { return bGranted; }

	private:
		/** 两样都有、没被拒绝、还没给过：记成给了 */
		bool TryGrant();

		bool bDue = false;
		bool bAccepted = false;
		bool bRejected = false;
		bool bGranted = false;
	};
}

/**
 * @class UGinkgoPickupComponent : 拾取的被交互组件
 * 挂在 AGinkgoPickup 上，条件、行为与两个本地的钩子都转给它。交互距离量到胶囊中心：拾取的范围是以角色为中心的一个球，
 * 半径是 AGinkgoPickup::PickupRadius。
 */
UCLASS(ClassGroup = (Ginkgo))
class CASEGINKGO_API UGinkgoPickupComponent : public UGinkgoInteractableComponent
{
	GENERATED_BODY()

public:
	UGinkgoPickupComponent();

	//~ Begin IGinkgoInteractable
	virtual FGinkgoInteractionRanges GetInteractionRanges() const override;
	virtual EGinkgoInteractionAvailability GetAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const override;
	virtual EGinkgoInteractionAvailability GetLocalAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const override;
	virtual void Interact(const FGinkgoInteractionContext& Context) override;
	virtual void BeginLocalInteract(const FGinkgoInteractionContext& Context) override;
	virtual void OnInteractResult(const FGinkgoInteractionContext& Context, EGinkgoInteractionResult Result) override;
	//~ End IGinkgoInteractable

private:
	AGinkgoPickup* GetPickup() const;
};

/**
 * @class AGinkgoPickup : 拾取的基类
 * 关卡里能拾取的东西，见 Docs/Design/Interaction.md「拾取子弹盒」。复制的 Actor：一个静态网格加一个拾取的被交互组件。
 * 子类只写效果（GrantLocal），要的话再加条件（GetPickupAvailability）：子弹盒是 AGinkgoAmmoPickup。
 *
 * - 拾取的范围：以角色为中心的一个球（PickupRadius），玩家最容易懂的手感范围（你说的：动画放得出来的范围不等于实际能拾取的范围，
 *   以手感最好的做实际拾取）。手够不够得着另算，不挡拾取，也不挡动画。
 * - 条件：在空中、滑铲中不能拾取，写出原因；被占住了隐藏。
 * - 本地（拾取的人）：按下时 BeginLocalInteract 按占用选手（Docs/Design/AnimActions.md「占用」，拾取占那条胳膊，优先级等于持枪），放拾取的动作
 *   （只动那条胳膊的蒙太奇，全身 IK 把手拉到东西上）：
 *   Grab（你说的 OnInteract）时手里出现 HandMesh、地上的藏起来；Stow（东西收到身上）时清掉手里的、给效果。
 *   够不着时照样放（同 TLOU2 的保底）：手伸到最远，Grab 那一帧东西直接挂到手上（Ginkgo::Pickup::SnapToHand）。
 * - 服务器：Interact 占住（复制的 bClaimed：别人那里隐藏，提示也没有），过 ClaimedLifeSpan 销毁，拾取的人的动作有时间放完。
 * - 中止就是马上完成（你说的）：手抓住之前人走开了、超时（MaxDuration）、离地或滑铲、被别的蒙太奇顶掉，都马上给效果、清掉手里的东西、停掉蒙太奇。
 *   连着拾取时先用另一只手；两只手都占着时顶掉自己最早的那个拾取（记成 Cancelled、原因 Replaced），同样马上给，所以连按 E 不会被吃掉。
 *   服务器接受了就一定给，动画只决定什么时候给；只有被拒绝、等不到时不给，地上的显示回来。
 *
 * 实现注意：等不到（TimedOut）以后服务器仍可能接受，那时东西没了、本地也不给。只在往返超过 1 s 时发生，没有处理。
 */
UCLASS(Abstract)
class CASEGINKGO_API AGinkgoPickup : public AActor
{
	GENERATED_BODY()

public:
	AGinkgoPickup();

	/** @brief 条件：被占住隐藏；在空中、滑铲中不能拾取；再看子类的 */
	EGinkgoInteractionAvailability GetAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const;

	/** @brief 本地的条件：两条胳膊都占着，又没有自己的拾取可以顶掉（比如以后的爬梯子）时不能拾取，写出原因 */
	virtual EGinkgoInteractionAvailability GetLocalAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const;

	/** @brief 服务器上的行为：占住，过一会儿销毁 */
	void Interact(const FGinkgoInteractionContext& Context);

	/** @brief 本地开始：记下拾取的人，马上伸手（StartLocalReach）。枪要先腾出右手，改写这里 */
	virtual void BeginLocalInteract(const FGinkgoInteractionContext& Context);

	/** @brief 伸手：选手，放拾取的动作（够不着也放）；没有蒙太奇时直接到了给的时刻 */
	void StartLocalReach(AGinkgoCharacter& Character);

	/** 那只手够得着的椭球的心（世界）：肩关节挪 ArmReachCentre */
	FVector GetReachCentre(const AGinkgoCharacter& Character, EGinkgoHand Hand) const;

	/** @brief 本地收到结果：接受了就（到了时刻时）给；被拒绝、等不到就取消动作、不给、地上的显示回来 */
	virtual void OnInteractResult(const FGinkgoInteractionContext& Context, EGinkgoInteractionResult Result);

	/** 拾取的球的半径（cm） */
	float GetPickupRadius() const { return PickupRadius; }

	/** 手要抓的点（世界）：提示圈也画在这里 */
	FVector GetGraspLocation() const;

	/** 片段里东西被握住时在那只手的手骨里的位置（ItemInHandLeft、ItemInHandRight） */
	FVector GetItemInHand(const EGinkgoHand Hand) const { return Hand == EGinkgoHand::Left ? ItemInHandLeft : ItemInHandRight; }

	/** 动作进行中判走开用的肩关节（世界）：开始时记下的、在胶囊坐标里的位置跟着胶囊走（LocalShoulderInActor） */
	FVector GetWalkAwayShoulder(const AGinkgoCharacter& Character) const;

	/** 服务器接受过一次拾取：别人那里已经隐藏 */
	bool IsClaimed() const { return bClaimed; }

	//~ Begin AActor
	virtual void PostInitializeComponents() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor

protected:
	/** @brief 拾取的效果，在拾取的那台机器上给（子弹盒：后备加几发）；只给一次 */
	virtual void GrantLocal(AGinkgoCharacter& Character) PURE_VIRTUAL(AGinkgoPickup::GrantLocal, );

	/** @brief 子类另加的条件，默认可以拾取 */
	virtual EGinkgoInteractionAvailability GetPickupAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const;

	/** @brief 服务器接受了（占住以后）：子类在这里改只有服务器能改的东西 */
	virtual void OnServerClaimed(const FGinkgoInteractionContext& Context) {}

	/** @brief 只能用这只手拿；默认哪只都行，按占用选 */
	virtual TOptional<EGinkgoHand> GetRequiredHand() const { return TOptional<EGinkgoHand>(); }

	/** @brief 手抓住之前人走开、离地或滑铲就中止（中止就是马上完成）。枪不中止：动画永远放完到抓住（你说的） */
	virtual bool AbortsWhenInterrupted() const { return true; }

	/** @brief 抓住（Grab）时就到了给的时刻；默认收到身上（Stow）时才到 */
	virtual bool GrantsAtGrab() const { return false; }

	/** @brief 记下本地拾取的人，清掉上一次的结果 */
	void PrepareLocalInteract(AGinkgoCharacter& Character);

	/** @brief 这只手能拿：胳膊能占，或者有自己的拾取可以顶掉 */
	bool IsHandAvailable(const UGinkgoAnimActionComponent& Actions, EGinkgoHand Hand) const;

	/** 当前拾取动作的部位优先级。提示、选手与真正的占用统一使用这一值。 */
	virtual int32 GetPickupPriority() const { return Ginkgo::Occupancy::PriorityPickup; }

	/** 地上的样子：只看不碰，交互的视线忽略目标自己的 Actor */
	UPROPERTY(VisibleAnywhere, Category = "Pickup")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Pickup")
	TObjectPtr<UGinkgoPickupComponent> Interactable;

	/** 拾取的球的半径：以角色胶囊的中心为心，量到手要抓的点。大了隔着老远就能捡，小了要贴上去；按手感调 */
	UPROPERTY(EditAnywhere, Category = "Pickup", meta = (ClampMin = 0, Units = "cm"))
	float PickupRadius = 150.f;

	/** 手要抓的点（Actor 坐标）：提示圈也画在这里 */
	UPROPERTY(EditAnywhere, Category = "Pickup")
	FVector GraspPoint = FVector::ZeroVector;

	/** 手里的样子（Grab 时挂到手上）。空的就不挂，地上的照样在 Grab 时藏起来 */
	UPROPERTY(EditAnywhere, Category = "Pickup|Animation")
	TObjectPtr<UStaticMesh> HandMesh;

	/** 左手、右手拾取的蒙太奇（只动那条胳膊的槽 ArmL、ArmR）。空的用设置里的 */
	UPROPERTY(EditAnywhere, Category = "Pickup|Animation")
	TObjectPtr<UAnimMontage> MontageLeft;

	UPROPERTY(EditAnywhere, Category = "Pickup|Animation")
	TObjectPtr<UAnimMontage> MontageRight;

	/**
	 * 片段里东西被握住时在手骨里的位置（挂点；左手、右手，片段里两只手的互为镜像）。默认是现成片段 Pickup_L/R 的
	 * （Private/anim/Pickup/exports/M_Pickup_{L,R}.points.json 的 item_at_grab）
	 */
	UPROPERTY(EditAnywhere, Category = "Pickup|Animation")
	FVector ItemInHandLeft = FVector(7.82, -5.95, 0.87);

	UPROPERTY(EditAnywhere, Category = "Pickup|Animation")
	FVector ItemInHandRight = FVector(-7.82, 5.95, -0.87);

	/** 收纳目标所在的骨骼，普通口袋位于 spine_05，腰部槽位使用 pelvis。 */
	UPROPERTY(EditDefaultsOnly, Category = "Pickup|Animation")
	FName StowBone = TEXT("spine_05");

	/** 左手收纳点，StowBone 局部坐标（cm）。保留原属性名兼容既有拾取物。 */
	UPROPERTY(EditAnywhere, Category = "Pickup|Animation")
	FVector PocketInChestLeft = FVector(-7.76, 12.71, -14.44);

	/** 右手收纳点，StowBone 局部坐标（cm）。 */
	UPROPERTY(EditAnywhere, Category = "Pickup|Animation")
	FVector PocketInChestRight = FVector(-7.39, 17.64, -2.14);

	/**
	 * 手臂够得着的椭球的三个半轴（cm）：往前后、往两边、往上下；心是肩关节挪 ArmReachCentre。不是拾取的条件，也不挡动画：
	 * 够不着时手伸到最远，Grab 那一帧东西直接挂上；动作进行中判走开了没有（WalkAwayRadii）。默认值照现成片段实测（pickup.py --reach-sweep，2026-10-31；手转向东西，伸手时上半身参与：前倾最多 35°，
	 * 全身 IK 脊柱刚度 0.9、迭代 100、质量 0.3）：挂点离抓的点不超过 0.5 cm（看不出缝）的远处，往前、前下、正下 66–68 cm，
	 * 前上 61 cm，正左 63 cm，左前 60 cm，左下、左前下 67–69 cm，身前往另一边 77–78 cm。拟合的椭球与十个方向都差不到 2.8 cm，
	 * 判成够得着却贴不上的不超过 0.9 cm。胳膊 26.65 + 22.35 cm，挂点在手腕外 9.9 cm
	 */
	UPROPERTY(EditAnywhere, Category = "Pickup|Animation")
	FVector ArmReachRadii = FVector(74.0, 76.0, 66.0);

	/** 够得着的椭球的心相对肩关节（cm，角色的坐标：X 往前，Y 往身体中间，Z 往上）：手往身前另一边伸得远、往外伸得近 */
	UPROPERTY(EditAnywhere, Category = "Pickup|Animation")
	FVector ArmReachCentre = FVector(-8.0, 12.0, -6.0);

	/** 动作进行中走开的余量（加在三个半轴上）：开始时刚好够得着的，人稍微一动不至于马上中止 */
	UPROPERTY(EditAnywhere, Category = "Pickup|Animation", meta = (ClampMin = 0, Units = "cm"))
	float ArmReachMargin = 5.f;

	/** 够不着时手的目标拉回到胳膊能到的地方（从肩关节量，FGinkgoHandReach::ArmLimit）；0 是不拉回，手一直往东西上够（胳膊伸直以后手停住，等目标退回够得着的地方，一下子追上去）。胳膊伸直时肩关节到挂点 58.9 cm */
	UPROPERTY(EditAnywhere, Category = "Pickup|Animation", meta = (ClampMin = 0, Units = "cm"))
	float ArmLimit = 56.f;

	/** 保底：动作开始以后过了这么久还没结局就中止（中止就是马上完成） */
	UPROPERTY(EditAnywhere, Category = "Pickup|Animation", meta = (ClampMin = 0, Units = "s"))
	float MaxDuration = 3.f;

	/** 服务器接受以后过多久销毁：拾取的人的动作（保底 3 s）放完再走；太短的话远端客户端手还没伸到东西就没了 */
	UPROPERTY(EditAnywhere, Category = "Pickup", meta = (ClampMin = 0, Units = "s"))
	float ClaimedLifeSpan = 3.5f;

private:
	UFUNCTION()
	void OnRep_Claimed();

	/** 那只手的蒙太奇：自己的，没有就用设置里的 */
	UAnimMontage* GetMontage(EGinkgoHand Hand) const;

	/**
	 * @brief 拾取的动作：放那只手的蒙太奇，伸手，三条中止条件，Grab、Stow 两个事件，结局时收尾
	 * @param StartReachRatio - 开始时抓的点在够得着的椭球的几倍处：判走开用（WalkAwayRadii）
	 */
	UGinkgoAnimAction* MakePickupAction(AGinkgoCharacter& Character, EGinkgoHand Hand, UAnimMontage& Montage, double StartReachRatio);

	/** Grab：手里出现 HandMesh（朝向照此刻的，东西上抓的点落在手里的挂点上：够得着时不跳，够不着时直接挂上），地上的藏起来 */
	void OnGrab(AGinkgoCharacter& Character, EGinkgoHand Hand);

	/** 到了给的时刻（Stow，或者中止）：清掉手里的，给效果 */
	void OnDue(AGinkgoCharacter& Character, EGinkgoHand Hand, const TCHAR* Why);

	/** 本地给效果，藏起地上的 */
	void GrantNow();

	/** 地上的显示：拾取的人在手碰到它（或给效果）时藏，别人在被占住时藏，被拒绝时显示回来 */
	void UpdateVisibility();

	/** 服务器接受过一次拾取；复制给各端 */
	UPROPERTY(ReplicatedUsing = OnRep_Claimed)
	bool bClaimed = false;

	/** 本地正在拾取它的角色：只在拾取的那台机器上，开始时记下，被拒绝时清掉 */
	TWeakObjectPtr<AGinkgoCharacter> LocalPicker;

	/** 本地的拾取动作（放动画时） */
	TWeakObjectPtr<UGinkgoAnimAction> LocalAction;

	/** 本地手碰到它了（Grab）：地上的藏起来 */
	bool bLocalGrabbed = false;

	/** 本地开始时手就够不着（在够得着的椭球外）：照样放动画，Grab 时东西直接挂到手上；日志里写 */
	bool bLocalBeyondReach = false;

	/**
	 * 本地开始时伸手那边的肩关节在角色（胶囊）坐标里的位置：动作进行中判走开了没有，从这里按胶囊算肩关节。不用这一帧的肩关节：
	 * 前倾与全身 IK 把它拉向东西，东西一直"还够得着"（横移时盒子到了右边 1.2 m 外还没中止，胸口弯了 64°）
	 */
	FVector LocalShoulderInActor = FVector::ZeroVector;

	/** 本地给不给效果 */
	Ginkgo::Pickup::FGrant LocalGrant;
};
