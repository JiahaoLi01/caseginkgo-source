#pragma once

#include "Animation/Actions/GinkgoAnimAction.h"
#include "Animation/GinkgoAnimTypes.h"
#include "Weapon/GinkgoWeaponAttachment.h"
#include "GinkgoInstallAttachmentAction.generated.h"

class UGinkgoWeaponAttachment;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UPoseableMeshComponent;
class UMeshComponent;
class USkeletalMesh;
class UGinkgoModularWeaponVisual;

/**
 * @class UGinkgoInstallAttachmentAction : 在身体遮挡下拿出配件，送到桌上武器的挂点
 * 执行器先占用双臂，OnStart 才创建临时网格，拒绝占用不会在场景留下模型。网格归执行动作的角色，空间位置跟桌面走。
 * 网格先隐藏，探身取物时才从腰侧显出。游戏线程采样身体前倾与双手姿势；取消时销毁预览，成功时交给模块外观等待异步替换。
 * 工作台收到完成回调后才在服务器把配件写进武器槽，动作本身不改库存。
 */
UCLASS()
class UGinkgoInstallAttachmentAction : public UGinkgoAnimAction
{
	GENERATED_BODY()

public:
	/**
	 * @param Table - 枪放置处的桌面组件，提供临时网格的父组件和桌面高度
	 * @param Part - 配件定义给出网格和落位后的转腕幅度
	 * @param MountWorld - 配件装好时的世界变换，由武器相对挂点与这次放枪的位置相乘
	 * @param Stand - 角色站位的世界变换，用它的朝向决定从桌面哪一侧伸手
	 * @param Gun - 模块枪正在显示的网格，只借用，拆旧件时隐藏对应骨骼；普通附加件可为空
	 * @param Previous - 同一插槽的旧配件；有模型时先拆下，空模块直接进入装入阶段
	 * @param ModuleBone - 原枪中独占此模块的骨骼，动作取消时恢复可见性
	 * @param Visual - 模块武器的本机外观管理器，完成时接管预览等待 Mutable；普通静态配件为空
	 */
	static UGinkgoInstallAttachmentAction* Make(USceneComponent& Table, const UGinkgoWeaponAttachment& Part,
		const FTransform& MountWorld, const FTransform& Stand, UPoseableMeshComponent* Gun = nullptr,
		const UGinkgoWeaponAttachment* Previous = nullptr, FName ModuleBone = NAME_None, UGinkgoModularWeaponVisual* Visual = nullptr);

	/** 游戏线程读取本帧双手姿势，返回值在动作结束时复位 */
	const FGinkgoAttachmentHandPose& GetHandPose() const { return Hands; }

protected:
	//~ Begin UGinkgoAnimAction
	virtual void OnStart(FGinkgoAnimActionSetup& Setup) override;
	virtual void OnTick(FGinkgoAnimActionContext& Context, float DeltaSeconds) override;
	virtual void OnFinished(const FGinkgoAnimActionResult& InResult) override;
	//~ End UGinkgoAnimAction

private:
	/** 默认拧装风格（AK-47 消音器）的逐帧采样，包括跨帧的显隐与松手，与蒙太奇速率无关 */
	void SamplePose(float Time);

	/** M4A1 各模块专属利索动作的逐帧推进、接触音效触发与身体/固定相机遮挡显隐 */
	void TickModularStyle(FGinkgoAnimActionContext& Context, float Before, float Current);

	/** M4A1 专属风格的当前帧姿态、临时网格变换与双臂 IK 采样 */
	void SampleModularStyle(float Time);

	/** 计算当前 M4A1 专属风格在含拆旧、仅装新或仅拆旧三种路径下的完整动作时长（秒） */
	float ComputeModularDuration() const;

	/** 桌面上的武器放置组件，OnStart 用它挂临时网格；开始前已失效时中止动作 */
	TWeakObjectPtr<USceneComponent> Table;

	/** 创建动作时从配件定义载入的网格，动作持有它直到结束 */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> PartMesh;

	/** 归执行动作的角色所有的临时网格，取物前隐藏；成功时可移交外观管理器，未移交或取消时销毁 */
	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> Preview;

	/** 新模块的骨骼网格，动作只取静止姿势；普通静态配件为空 */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMesh> ModuleMesh;

	/** 拆下的旧模块模型，由动作持有，取消时恢复原枪可见性并销毁临时件 */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMesh> PreviousMesh;

	/** 拆旧件阶段的临时模型，完成或取消均销毁 */
	UPROPERTY(Transient)
	TObjectPtr<UPoseableMeshComponent> RemovedPreview;

	/** 桌上的枪，只借用，用于拆件时隐藏原模型对应的骨骼 */
	TWeakObjectPtr<UPoseableMeshComponent> Gun;

	/** 本槽模块外观管理器，仅借用；成功时由它接管预览直到正式网格就绪，普通配件为空。 */
	TWeakObjectPtr<UGinkgoModularWeaponVisual> Visual;

	/** 本次安装的配件身份，用于将动作末帧与异步生成的装配匹配；动作期间持有。 */
	UPROPERTY(Transient)
	TObjectPtr<const UGinkgoWeaponAttachment> Part;

	/** 动作开始前模块骨骼的显隐约束，取消或交接完成时恢复，不覆盖已有隐藏状态。 */
	bool bModuleWasHidden = false;

	/** 拆装涉及的模块骨骼，空值保留旧的附加配件动作 */
	FName ModuleBone;

	/** 卸下前握把等空选项不创建新模型，但仍完成拆旧件和收手 */
	bool bEmptyModule = false;

	/** 配件装好时的世界变换，由武器挂点和桌上枪位相乘；动作末尾与正式配件重合 */
	FTransform MountWorld;

	/** 角色站位的世界变换，X 向桌面、Y 向角色右侧、Z 向上；取物路径按这组轴计算 */
	FTransform Stand;

	/** 临时网格原点的世界起点（cm），OnStart 把模型藏在左腰侧；采样先换算为握点起点，转向不带着手绕整枪原点走弧线 */
	FVector StartLocation = FVector::ZeroVector;

	/** 已经过的动作时间（秒），OnTick 累加并交给 SamplePose 采样 */
	float Elapsed = 0.f;

	/** 从配件定义复制的第一拍转腕幅度（度），为零时落位后直接进入收手段 */
	float WristTurn = 0.f;

	/** 从配件定义复制的桌前拆装动作风格；Default 沿用既有左手拧装，其余走 M4A1 模块专属路径 */
	EGinkgoAttachmentInstallStyle InstallStyle = EGinkgoAttachmentInstallStyle::Default;

	/** 当前帧的身体与双手目标，游戏线程取走后写入动画快照；结束时复位 */
	FGinkgoAttachmentHandPose Hands;

	/** 拆旧件末尾的双手姿势；接入新件时同时混合位置、旋转、接触权重和前倾，避免 IK 在交界帧跳变 */
	FGinkgoAttachmentHandPose RemovalHands;

	/** 新件握点相对网格原点的偏移（模型空间，cm），模型抬起和转向时手与道具共用同一变换 */
	FVector GripPoint = FVector::ZeroVector;

	/** 旧件握点相对网格原点的偏移（模型空间，cm），拆旧件与切换盲区时据此与新件无缝衔接；无旧件时为零向量 */
	FVector RemovedGripPoint = FVector::ZeroVector;
};
