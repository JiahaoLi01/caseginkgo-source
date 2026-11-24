#pragma once

#include "Components/ActorComponent.h"
#include "GinkgoTraversalTypes.h"
#include "GinkgoTraversalAnimations.h"
#include "GinkgoTraversalNetworking.h"
#include "Animation/AnimInstance.h"
#include "GinkgoTraversalComponent.generated.h"

class UGinkgoTraversalAction;
class UGinkgoWeaponDefinition;

/**
 * 攀爬的角色侧协调器：查询相机意图、锁定几何方案并启动对应手部占用的 AnimAction。
 * CMC 推进可保存/重放的值状态和原根轨迹，服务器独立复检；AnimAction 根据最终状态承担姿势和手部占用。
 * 模拟代理消费服务器锚点与进度，位置由角色移动复制；校正重放期间不发手部副作用。
 */
UCLASS(ClassGroup = Movement)
class CASEGINKGO_API UGinkgoTraversalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGinkgoTraversalComponent();

	/** 游戏线程按当前水平实速求接受距离后同步查询；相机位置不参与检测，俯仰只用于求水平投影。失败不会修改角色。 */
	FGinkgoTraversalResult Evaluate(const FRotator& CameraRotation, TArray<FGinkgoTraversalProbe>* Probes = nullptr) const;

	/** 记录跳跃请求并预检本地候选；实际开始由下一次 CMC 移动处理，服务器只重做检测，不接受客户端落点。 */
	bool TryStart(const FRotator& CameraRotation, bool bMovingExit = false);

	/** 请求已入队、执行或淡出期间为真；输入与武器状态树据此暂停手部请求。 */
	bool IsActive() const { return bPendingInput || SyncState.Phase == EGinkgoTraversalPhase::Moving || ActiveAction != nullptr; }

	/** 本地预检已接受、CMC 尚未消费本次按键；不能在此期间清零用于接受距离计算的实速。 */
	bool IsPending() const { return bPendingInput; }

	/** 足部支撑已交还、全身尚在淡出；正常输入和武器状态树已恢复，可边走边取枪。 */
	bool IsRecovering() const { return IsActive() && !bPendingInput && SyncState.Phase != EGinkgoTraversalPhase::Moving; }

	/** 本地输入快照；SavedMove 记录，服务器执行与客户端重放从移动包恢复。 */
	const FGinkgoTraversalInput& GetInput() const { return Input; }

	/** 恢复一次移动携带的输入，不直接启动动画或修改物理状态。 */
	void SetInput(const FGinkgoTraversalInput& Value) { Input = Value; }

	/** 当前模拟状态，移动回包必须保存检查误差那一刻的副本，不能帧末再读取。 */
	const FGinkgoTraversalSyncState& GetSyncState() const { return SyncState; }

	/** 校正/合并时恢复模拟状态与适配缓存；不会发动画事件，表现留到重放结束统一同步。 */
	void RestoreSyncState(const FGinkgoTraversalSyncState& State);

	/** 每次 CMC 移动前消费新请求并校验几何，包含服务器补发的旧移动；重放也走同一路径。 */
	void BeginMovement(float DeltaTime);

	/** 按移动模拟时间提取原根轨迹的下一段，返回实际使用的秒数及世界位移/朝向。 */
	float AdvanceMotion(float DeltaTime, FVector& Delta, FQuat& Facing);

	/** 标记正常支撑或物理中断；只改模拟状态，移动模式由 CMC 随后恢复。 */
	void FinishMotion(bool bCompleted);

	/** 每次移动结束推进淡出计时并更新远端快照；重放期间不启动/停止表现动作。 */
	void EndMovement(float StartTime, float DeltaTime);

	/** 从最终模拟状态同步动画、IK 与武器表现；校正重放后仅调用一次。 */
	void SyncPresentation();

	/** 足部支撑交接后让出全身覆盖，不等待站直；重复请求不会重启淡出。 */
	void YieldRecoveryToMovement();

	/** 本次素材是否需要双手；只在请求入队到清理之间读取。 */
	bool RequiresBothHands() const { return ActiveClip.bTwoHanded; }

	/** AnimAction 开始时提交资源；只有与当前请求相同的动作可提交。 */
	bool BeginAction(UGinkgoTraversalAction& Action);

	/** 同一方案的支撑、角色与身体通道仍有效；不会改选另一面墙或另一种动作。 */
	bool CanContinue() const;

	/** 当前动作的蒙太奇；仅在 IsActive 时有效。 */
	UAnimMontage* GetActiveMontage() const { return ActiveClip.Montage; }

	/** 游戏线程动画快照，不向工作线程传递组件或动作对象。 */
	FGinkgoTraversalHandPose GetHandPose() const;

	/** Motion Warping 提取已预检计划的帧增量，同时记录碰撞代理应使用的时间区间。 */
	FTransform ExtractMotion(const UAnimSequenceBase* Animation, float From, float To);

	/** 移动组件提交本帧根位移前检查独立查询胶囊；失败只中断，不修改位移曲线。 */
	bool SweepMotion(const FVector& Start, const FVector& End, FHitResult& Hit) const;

	/** 最近已交给移动组件的素材时刻（秒）；供轨迹调试与验证，未开始/清理后为零。 */
	float GetMotionTime() const { return MotionInterval.Y; }

	/** 当前素材的足部支撑交接时刻（秒）；物理不依赖通知数组的排列顺序。 */
	float GetMotionEndTime() const { return ActiveClip.MotionEndTime; }

	/** 外部硬停入口；结束后仍走相同的资源清理和淡出路径。 */
	void Cancel();

#if !UE_BUILD_SHIPPING
	/** 调试开启时保存按跳跃前的查询；是否启动动作仍由 TryStart 决定，动作中途不覆盖该方案。 */
	void PreviewJump(const FRotator& CameraRotation);
#endif

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** 持续随移动发送的最近按键；客户端重放结束后恢复当帧实时输入。 */
	FGinkgoTraversalInput Input;

	/** CMC 拥有推进权的值状态；网格 Tick 和 AnimAction 回调不推进物理时间。 */
	FGinkgoTraversalSyncState SyncState;

	/** 本地预检通过但移动尚未消费按键；仅保护按键到第一次移动之间的输入。 */
	bool bPendingInput = false;

	/** 当前表现动作的序号；与模拟序号不同才替换蒙太奇，避免校正重播起手。 */
	uint16 PresentedSequence = 0;

	/** 本次副作用已应用的序号；重放不会重复打断手部动作或重复生成枪械事件。 */
	uint16 EffectsSequence = 0;

	/** 服务器复制给模拟代理的表现快照；自主代理只走 CMC 校正，避免两套状态同时抢写。 */
	UPROPERTY(ReplicatedUsing = OnRep_Traversal)
	FGinkgoTraversalReplicatedState ReplicatedTraversal;

	/** 同步晚到的远端动作，从采样时间追赶表现；物理位置仍由标准移动复制和平滑负责。 */
	UFUNCTION()
	void OnRep_Traversal();

	/** 从模拟状态的起点/几何重建只读适配计划，失败不能继续移动。 */
	bool RebuildMotion();

	/** 使用给定接受距离（胶囊前缘外 cm）查询当前几何；两端都由各自 CMC 的实速求距离。 */
	FGinkgoTraversalResult EvaluateWithReach(const FRotator& CameraRotation, float Reach, TArray<FGinkgoTraversalProbe>* Probes = nullptr) const;

	/** 角色开始时加载的素材集合；GC 可见，蒙太奇不在按键时临时构造。 */
	UPROPERTY(Transient)
	TObjectPtr<UGinkgoTraversalAnimations> Animations;

	/** 当前请求的动作，直到蒙太奇淡出完成才清空；占用本身仍由执行器管理。 */
	UPROPERTY(Transient)
	TObjectPtr<UGinkgoTraversalAction> ActiveAction;

	/** 当前模拟状态对应的素材缓存；开始或校正时重建，表现淡出不能清空 CMC 仍在使用的配置。 */
	UPROPERTY(Transient)
	FGinkgoTraversalClip ActiveClip;

	/** 启动前检测通过的几何，接触点在整个动作中固定，目标移动或销毁则取消。 */
	FGinkgoTraversalResult ActiveResult;

	/** 当前模拟状态对应的完整轨迹及碰撞代理缓存；复检后建立，校正按历史起点重建，Idle 后不再参与位移。 */
	FGinkgoTraversalMotion ActiveMotion;

	/** 本次移动提取的起止素材时间（秒），共用轨迹提取入口写入，独立碰撞代理随后只读。 */
	FVector2D MotionInterval = FVector2D::ZeroVector;

	/** 边沿、左右手和落点的组件变换快照；单手动作不依赖右侧支撑；静态支撑被脚本移动/旋转也会取消，而非拖动角色。 */
	TArray<FTransform> SupportTransforms;

	/** 开始前手持武器定义；恢复时要求槽和定义仍匹配，防止死亡掉枪后重新挂回。 */
	UPROPERTY(Transient)
	TObjectPtr<UGinkgoWeaponDefinition> StowedWeapon;

	/** 暂时背到身后的武器槽；没有手持武器时为 INDEX_NONE。 */
	int32 StowedSlot = INDEX_NONE;

	/** 提交前的根运动提取模式，退出时原样恢复。 */
	TEnumAsByte<ERootMotionMode::Type> SavedRootMotionMode = ERootMotionMode::RootMotionFromMontagesOnly;

	/** 提交时的动画实例；换 AnimClass 时只恢复旧实例的模式，不把旧状态写到新实例。 */
	TWeakObjectPtr<UAnimInstance> CommittedAnimInstance;

	/** 动作已提交动画与手部表现；物理状态单独由 CMC 拥有，启动失败时不能恢复未取得的表现资源。 */
	bool bCommitted = false;

	/** 已根据物理结局启动淡出和取枪；避免重复发送表现完成事件，不作为物理阶段判断。 */
	bool bPresentationReleased = false;

	/** 物理结局后的表现交接：让出全身姿势、请求取枪并移除本素材修正器，不切换移动模式。 */
	void ReleasePresentation();

	/** 表现结束路径共用：恢复原武器挂点与动画模式；CMC 尚未结束时保留物理计划。 */
	void Cleanup();

	/** 在移动与 AnimAction 之后检查结局，再等待蒙太奇淡出完成，保证最后一帧根运动先应用。 */
	void TickAction();

#if !UE_BUILD_SHIPPING
	/** 最近一次调试采样；由实时检查或按键检查覆盖，关闭再开启调试时重新取样。 */
	FGinkgoTraversalResult DebugResult;

	/** 同次查询的全部射线、球和胶囊记录，仅在调试开启时填充。 */
	TArray<FGinkgoTraversalProbe> DebugProbes;

	/** 同次采样的角色脚底世界位置（cm），冻结模式不跟随之后的普通跳跃。 */
	FVector DebugFeet = FVector::ZeroVector;

	/** 下次实时采样的世界时间（秒），按 0.1 秒间隔限制查询开销。 */
	double NextDebugTime = 0.;

	/** 最近一次查询耗时（毫秒），仅用于调试面板。 */
	double DebugMilliseconds = 0.;

	/** 是否有可画的采样；关闭调试清零，防止旧结果在下次开启时误显示。 */
	bool bHasDebugSample = false;

	/** 上次调试模式，切换实时/按键模式后丢弃旧的缓存。 */
	int32 LastDebugMode = 0;

	/** 取样同时记录位置与耗时，不改变普通跳跃输入。 */
	void SampleDebug(const FRotator& Rotation);

	/** 只画已记录结果，不在绘制期间追加查询；冻结模式也使用原来的体积尺寸。 */
	void DrawDebug() const;
#endif
};
