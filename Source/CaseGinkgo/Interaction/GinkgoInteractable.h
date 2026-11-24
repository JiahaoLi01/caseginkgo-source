#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GinkgoInteractable.generated.h"

class AActor;
class AGinkgoCharacter;
class UGinkgoInteractorComponent;

/** 目标自己的条件给出的结果 */
UENUM()
enum class EGinkgoInteractionAvailability : uint8
{
	/** 可以交互 */
	Available,
	/** 不能交互：提示上写出原因，玩家知道为什么不行 */
	Blocked,
	/** 干脆不提示，比如这件东西正被别人拿 */
	Hidden,
};

/** 一个目标这一帧在提示上的样子 */
UENUM()
enum class EGinkgoInteractionState : uint8
{
	/** 不显示：看不见，或者条件给出隐藏 */
	Hidden,
	/** 在提示距离以内、看得见：空心圈 */
	Hint,
	/** 焦点，在交互距离以内，条件给出可以交互：圈里写键，旁边写"拾取 98k" */
	Ready,
	/** 焦点，在交互距离以内，条件给出不能交互：写出原因，不写键 */
	Blocked,
};

/** 服务器（单机与主机上是本地）对一次交互请求的判断 */
UENUM()
enum class EGinkgoInteractionResult : uint8
{
	/** 交互了 */
	Accepted,
	/** 目标不在表里了：被拿走了、被销毁了 */
	Gone,
	/** 服务器上的距离超过交互距离加容差 */
	OutOfRange,
	/** 服务器上目标的条件没给出"可以交互" */
	Unavailable,
	/** 没有角色或控制器 */
	NoCharacter,
	/** 客户端等不到结果（本地记的，服务器不发） */
	TimedOut,
};

/** 两个距离（cm），默认都量到角色胶囊的中轴线段 */
struct FGinkgoInteractionRanges
{
	/** 离多远开始提示（空心圈） */
	float Prompt = 600.f;

	/** 离多远可以交互 */
	float Interact = 180.f;

	/**
	 * 交互距离量到胶囊的中心，不量到中轴线段：以角色为中心的一个球。拾取用它（Docs/Design/Interaction.md「拾取子弹盒」）：
	 * 球是玩家最容易懂的手感范围，太高、太低的东西也挡掉。提示距离照旧量到中轴线段
	 */
	bool bInteractFromCenter = false;
};

/**
 * @struct FGinkgoInteractionContext : 一次判断或交互的上下文
 * 条件与行为只读它、不存状态：同一个目标，每个角色、每一帧都拿来判断
 */
struct FGinkgoInteractionContext
{
	/** 交互的角色 */
	AGinkgoCharacter* Character = nullptr;

	/** 它的交互组件 */
	UGinkgoInteractorComponent* Interactor = nullptr;

	/** 请求发起时本机手持的武器槽。服务器收到后按随身武器复查，空槽时工作台先打开选枪菜单 */
	int32 WeaponSlot = INDEX_NONE;
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UGinkgoInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * @class IGinkgoInteractable : 能被交互的目标
 * 交互方（UGinkgoInteractorComponent）、子系统与提示的界面只认它，见 Docs/Design/Interaction.md「组件加接口」。
 * 默认实现是 UGinkgoInteractableComponent，一种交互写它的一个子类；不好挂组件的目标由 Actor 自己实现，开始时登记到
 * UGinkgoInteractionSubsystem、结束时注销。
 *
 * 比 CRPGFramework 的交互简单：一个目标只有一种交互（要几种就挂几个组件），没有交互定义的类、标签、优先级与菜单，条件写在代码里。
 * 距离、看不看得见不算条件，由交互组件统一判断。
 */
class CASEGINKGO_API IGinkgoInteractable
{
	GENERATED_BODY()

public:
	/** @brief 提示圈画在哪、距离从哪量（世界坐标） */
	virtual FVector GetInteractionLocation() const = 0;

	/** @brief 离多远开始提示、离多远可以交互 */
	virtual FGinkgoInteractionRanges GetInteractionRanges() const = 0;

	/** @brief 提示上的字，比如"拾取 98k" */
	virtual FText GetInteractionText() const = 0;

	/**
	 * @brief 隔着墙也显示提示圈（可选，默认不显示）
	 * @note 只管显示：能不能交互照样要从角色的眼睛看得见
	 */
	virtual bool ShowsPromptThroughWalls() const { return false; }

	/**
	 * @brief 条件
	 * @param Context - 谁在判断
	 * @param OutReason - 不能交互时写出原因，提示上显示
	 * @return 可以交互、不能交互或隐藏
	 * @note 每帧都调：本地控制的角色判断提示，服务器在执行前再判断一遍。只读，不改状态
	 */
	virtual EGinkgoInteractionAvailability GetAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const = 0;

	/**
	 * @brief 只有本地知道的条件（可选，默认可以交互）：手上有没有空（占用不复制）。GetAvailability 给出可以交互时才看它
	 * @note 每帧都调，只在本地控制的角色上，服务器不判断：不然它不知道拾取的胳膊占着，主机上自己刚开始的拾取还会把自己否掉。只读，不改状态
	 */
	virtual EGinkgoInteractionAvailability GetLocalAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const
	{
		return EGinkgoInteractionAvailability::Available;
	}

	/**
	 * @brief 行为：交互的结果
	 * @note 只在服务器上调用（单机与主机上本地就是服务器），条件已经判断过；结果靠复制回到各端
	 */
	virtual void Interact(const FGinkgoInteractionContext& Context) = 0;

	/**
	 * @brief 本地开始交互：按下、请求发出去的同时调用，只在本地控制的角色上
	 * @note 要放动画的交互在这里开始动作，不等服务器：伸手的那一段盖住往返（Docs/Design/Interaction.md「拾取子弹盒」）。
	 *       单机与主机上在 Interact 之前调用。默认什么也不做
	 */
	virtual void BeginLocalInteract(const FGinkgoInteractionContext& Context) {}

	/**
	 * @brief 本地收到这次请求的结果：服务器回的，单机与主机上是当场执行的，或者等不到（TimedOut）
	 * @note 本地开始的东西靠它收尾：接受了才给本地的效果，被拒绝、等不到时收回。默认什么也不做
	 */
	virtual void OnInteractResult(const FGinkgoInteractionContext& Context, EGinkgoInteractionResult Result) {}
};

namespace Ginkgo::Interaction
{
	/** @brief 目标所在的 Actor：组件的拥有者，或 Actor 自己；看视线时忽略它 */
	CASEGINKGO_API AActor* GetInteractableActor(const UObject* Interactable);

	/** @brief 日志与轨迹里的名字：组件写成"Actor.组件"，同一种组件挂在几个 Actor 上时分得开 */
	CASEGINKGO_API FString DescribeInteractable(const UObject* Interactable);
}
