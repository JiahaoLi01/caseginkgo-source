#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraComponent.h"
#include "GinkgoCameraCollision.h"
#include "GinkgoCameraFollow.h"
#include "GinkgoCameraOverlay.h"
#include "GinkgoCameraParamLayer.h"
#include "GinkgoCameraParams.h"
#include "GinkgoCameraShoulder.h"
#include "Weapon/GinkgoScope.h"  // Ginkgo::Scope::FSightFrame：第一人称开镜时瞄具在哪
#include "GinkgoPlayerCameraComponent.generated.h"

class APawn;
class APlayerController;
class UGinkgoCameraConfig;
class UGinkgoCameraContextComponent;
class UGinkgoScopeView;
struct FGinkgoHealthVignetteSettings;

namespace Ginkgo::Camera
{
	/**
	 * @brief 按缺失生命比例计算血量暗角后处理的强度与边缘压色（纯函数，供相机求值与自动化测试共用）
	 * @param MissingHealthRatio - 缺失生命比例（0 为满血，1 为空血/死亡，函数内自动夹至 [0, 1]）
	 * @param Settings - 血量暗角后处理配置参数
	 * @param OutIntensity - 输出的 UE PostProcess VignetteIntensity
	 * @param OutColor - 输出的 UE PostProcess VignetteColor（线性空间颜色）
	 */
	CASEGINKGO_API void ComputeHealthVignette(float MissingHealthRatio, const FGinkgoHealthVignetteSettings& Settings,
		float& OutIntensity, FLinearColor& OutColor);
}

/**
 * @class UGinkgoPlayerCameraComponent : 角色上的玩家视角管线（Docs/Design/CameraPipeline.md）
 * 管理器求这一视角目标时调用 EvaluateView。移动和武器把标签、连续量和带时间的事件写进角色的相机上下文；
 * 组件在移动、动画都更新后读快照，混合参数、平滑跟随、扫碰撞，再叠加冲量与瞄具画面。
 *
 * 时序与状态：
 * - 瞄准点和动画求值更早，用 PredictViewLocation、PredictSightFollow 推算这一帧，不取走事件，也不再推进一次弹簧。
 * - AimView 保留叠加前的瞄准视角，最终画面写到 OutView；投影 HUD 或做镜头过渡时读管理器缓存的最终画面。
 * - 每个角色各自持有跟随、压缩、参数与画中画的状态。离开玩家视角时暂停镜内捕获并恢复头部显示；
 *   回到同一角色后继续推进已有弹簧，传送等硬切另由 RequestCameraCut 标记。
 */
UCLASS(ClassGroup = Camera, meta = (BlueprintSpawnableComponent))
class CASEGINKGO_API UGinkgoPlayerCameraComponent : public UCameraComponent
{
	GENERATED_BODY()

public:
	/** @brief 正在用的相机配置：指定了资产用资产，否则用类默认值 */
	const UGinkgoCameraConfig& GetConfig() const;

	/**
	 * @brief 镜头切换：下一次更新时弹簧不追过去，跟随点与碰撞直接放到位；渲染器也当作镜头切换（时间性的抗锯齿、运动模糊不拖影）
	 * @note 传送角色之后调用（输入回放放回开始状态时就调）。工作台的平滑进出保留跟随状态。
	 *       不读引擎的 bGameCameraCutThisFrame：它在视口绘制时清掉，相机更新之后（同一帧的 OnWorldPostActorTick 里）设上的，
	 *       到下e一帧相机更新时已经没了；这里只在切换的那一帧替渲染器设上它
	 */
	void RequestCameraCut() { bCutRequested = true; }

	/**
	 * @brief 推算这一帧相机会在哪：相机在所有 Actor 更新完之后才算，动画更新时这一帧的还没有（瞄准点要它，Docs/Design/Weapons.md「手与瞄准」）。
	 *        同 EvaluateView 的 ③④ 解算，用这一帧的视角；跟随点取上一帧平滑后的加上胶囊这一帧挪的量（弹簧的滞后当作不变），
	 *        压缩取上一帧的，不扫碰撞
	 * @param Pawn - 视角目标：不是现在跟着的（还没跟过、换了目标）时直接用胶囊中心当跟随点
	 * @param ViewRotation - 这一帧的视角（ControlRotation）
	 */
	FVector PredictViewLocation(const APawn& Pawn, const FRotator& ViewRotation) const;

	/**
	 * @brief 这一帧叠加之前的视角：碰撞之后的位置、视角（ControlRotation 加参数层的侧倾）与参数层的 FOV
	 * @note 叠加层只晃画面：交互按准星选焦点、以后的命中用它；画在画面上的东西（准星、提示）照旧用最终的视角（GetCameraCacheView）
	 */
	const FMinimalViewInfo& GetAimView() const { return AimView; }

	/** @brief 手动切换左右肩；从控制器的换肩意图调用，工作台等独立相机不接收该意图 */
	void ToggleShoulder();

	/** 换肩行为的只读状态，供调试显示肩位、环境得分与冷却 */
	const FGinkgoCameraShoulder& GetShoulder() const { return Shoulder; }

	/** 参数层这一帧的输出（参数弹簧与换肩镜像之后） */
	const FGinkgoCameraParams& GetParams() const { return LastParams; }

	/** 参数层的状态：各修改器的权重（调试与回放） */
	const FGinkgoCameraParamLayer& GetParamLayer() const { return ParamLayer; }

	/** 叠加层的状态：各效果的输出（调试与回放） */
	const FGinkgoCameraOverlayLayer& GetOverlayLayer() const { return OverlayLayer; }

	/** 相机的时钟：切换以来的时间，噪声是它的函数 */
	double GetClock() const { return Clock; }

	/** 第一人称开镜：这一帧相机从越肩滑到瞄具的出瞳走了多少（0–1）。手上的枪没有瞄具、没开镜时是 0 */
	float GetFirstPersonAlpha() const { return FirstPersonAlpha; }

	/** 这一帧瞄具在世界里的位置（GetFirstPersonAlpha 为 0 时没有意义）：界面的红 X 透过瞄具投影用它 */
	const Ginkgo::Scope::FSightFrame& GetSightFrame() const { return SightFrame; }

	/** 瞄具里的画面（调试与回放：拍了没有、多大） */
	const UGinkgoScopeView* GetScopeView() const { return ScopeView; }

	/**
	 * @brief 开镜的晃动在某个时刻晃多少（视角空间，度；包络为 1 时）：叠加效果里"第一人称时晃枪"的那些噪声（UGinkgoCameraNoiseSway::bMovesSight）相加。
	 *        第一人称时晃的是枪：武器组件把它乘开镜进度加到瞄准方向上，瞄具连同分划一起晃，子弹跟着分划走；画面上的那一份由叠加层收掉
	 * @param ClockTime - 相机的时钟上的时刻：武器组件在这一帧相机更新之前取，用 GetClock() 加这一帧的帧长（这一帧末，同叠加层）
	 */
	FRotator SampleSightSway(double ClockTime) const;

	/**
	 * @brief 这一帧末叠加层里"第一人称时枪跟着转"的那些效果（UGinkgoCameraOverlayEffect::bSightFollows：开枪的相机后坐）转多少（视角空间，度）。
	 *        武器组件在这一帧相机更新之前取：拷一份这些效果的状态，按相机这一帧要用的输入（还没取走的事件、帧长、帧开头的时刻）推一帧，
	 *        同相机这一帧推出来的一样。枪加上它，瞄具同画面一起跳
	 * @param DeltaSeconds - 这一帧的帧长
	 * @param FirstPerson - 第一人称的进度：输出照相机的做法乘 FirstPersonScale 的混合
	 */
	FRotator PredictSightFollow(double DeltaSeconds, float FirstPerson) const;

#if !UE_BUILD_SHIPPING
	/** 一帧管线的中间结果：ginkgo.showCamera 画出来，输入回放记进轨迹 */
	struct FDebugFrame
	{
		/** 角色胶囊中心的世界位置（cm），移动后直接采样 */
		FVector CapsuleCenter = FVector::ZeroVector;

		/** 跟随弹簧求出的中心世界位置（cm） */
		FVector FollowCenter = FVector::ZeroVector;

		/** 跟随弹簧追逐的世界目标（cm），由胶囊中心加速度前馈得到 */
		FVector FeedforwardTarget = FVector::ZeroVector;

		/** 应用姿态枢轴偏移后的跟随点世界位置（cm），第一段碰撞扫掠的目标 */
		FVector Pivot = FVector::ZeroVector;

		/** 第一段扫掠修正后的安全跟随点世界位置（cm） */
		FVector SafePivot = FVector::ZeroVector;

		/** 从安全跟随点指向期望相机位置的世界向量（cm），包含臂长和肩部偏移 */
		FVector Boom = FVector::ZeroVector;

		/** 第二段扫掠要求相机臂缩短的距离（cm），无遮挡时为 0 */
		double RequiredCompression = 0.0;

		/** 碰撞平滑状态这一帧实际缩短相机臂的距离（cm） */
		double Compression = 0.0;

		/** 压缩距离的变化速度（cm/s），显示推远过程的弹簧响应 */
		double CompressionVelocity = 0.0;

		/** 这一帧 OutView 的世界位置（cm），包含第一人称眼位和叠加效果 */
		FVector Location = FVector::ZeroVector;

		/** 这一帧 OutView 的世界旋转（度） */
		FRotator Rotation = FRotator::ZeroRotator;

		/** 这一帧 OutView 的视场角（度） */
		float FieldOfView = 0.f;

		/** 叠加层这一帧的输出，旋转使用视角空间，FOV 为角度偏移 */
		FGinkgoCameraOverlayOutput Overlay;

		/** 这一帧从相机上下文取出并送给叠加层的事件，调试面板和回放读取 */
		TArray<FGinkgoCameraEvent> Events;

		/** 本次碰撞扫掠的球半径（cm），取自相机配置 */
		float ProbeRadius = 0.f;

		/** 跟随弹簧这一帧触发了滞后距离限制 */
		bool bFollowClamped = false;

		/** 第一段从胶囊中心到跟随点的扫掠遇到阻挡 */
		bool bPivotBlocked = false;

		/** 第二段沿相机臂的扫掠遇到阻挡 */
		bool bBoomBlocked = false;

		/** 这一帧按镜头切换处理，跟随与碰撞等持续状态重新初始化 */
		bool bCut = false;

		/** 第一人称开镜的混合进度（0–1），为 0 时下面的瞄具位置和方向无效 */
		float FirstPerson = 0.f;

		/** 枪上瞄具实际出瞳的世界位置（cm），包含程序化偏移 */
		FVector SightEye = FVector::ZeroVector;

		/** 枪上瞄具实际光轴的世界方向（单位向量） */
		FVector SightAxis = FVector::ZeroVector;

		/** 去掉枪的程序化偏移后，瞄具出瞳的世界位置（cm），相机眼位以它为基准 */
		FVector SightRestEye = FVector::ZeroVector;

		/** 去掉枪的程序化偏移后，瞄具光轴的世界方向（单位向量） */
		FVector SightRestAxis = FVector::ZeroVector;

		/** 本地角色的头部是否已为第一人称视角隐藏 */
		bool bHeadHidden = false;

		/** 当前镜内画面的捕获分辨率（像素），没有活动捕获时为 0 */
		int32 ScopeCaptureSize = 0;
	};

	const FDebugFrame& GetDebugFrame() const { return DebugFrame; }

	/** @brief 两层的总开关（调试模块的 ginkgo.camera.modifiers / overlay）：关掉对照，发行版总开着 */
	static void SetLayersEnabled(bool bModifiers, bool bOverlay);
#endif

	//~ Begin UCameraComponent
	virtual void GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView) override;
	//~ End UCameraComponent

	/**
	 * @brief 推进本次玩家视角求值，供管理器或组件自身的 GetCameraView 调用
	 * @param Viewer - 观察此目标的控制器，视角旋转与视口尺寸取它的
	 * @param Override - 这次求值用的配置，空时依次取组件配置、项目设置和类默认值
	 */
	void EvaluateView(APlayerController& Viewer, float DeltaTime, FMinimalViewInfo& OutView, UGinkgoCameraConfig* Override = nullptr);

	/** 离开视角时恢复角色头部、停拍镜内画面，回来第一帧丢弃暂停期间积存的相机事件 */
	void SuspendView();

protected:
	virtual void BeginPlay() override;

	/** 相机配置资产。没有指定时用 项目设置 → Game → CaseGinkgo 里的；都没有时用 UGinkgoCameraConfig 的类默认值 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ginkgo|Camera")
	TObjectPtr<UGinkgoCameraConfig> CameraConfig;

	/** 上一次求值传入的配置，帧间预测与该帧使用同一份参数 */
	TWeakObjectPtr<UGinkgoCameraConfig> ViewerConfig;

private:
	/** ④ 碰撞的两段扫掠的结果 */
	struct FCollisionSweep
	{
		/** 第一段之后的世界跟随点（cm），从胶囊中心扫过去，把落进几何体的跟随点拉回来 */
		FVector SafePivot = FVector::ZeroVector;

		/** 第二段要求的压缩距离（cm），即安全跟随点到期望位置的连线上被挡住的那一段 */
		double RequiredCompression = 0.0;

		/** 第一段扫掠是否被阻挡，对应 SafePivot 的修正 */
		bool bPivotBlocked = false;

		/** 第二段扫掠是否被阻挡，对应 RequiredCompression 的计算 */
		bool bBoomBlocked = false;
	};

	/**
	 * @brief ④ 碰撞的两段扫掠：只找出哪里挡住了，拉近与推远由 FGinkgoCameraCollision 决定
	 * @param Pawn - 视角目标，扫掠忽略它
	 * @param CapsuleCenter - 胶囊中心：第一段从这里出发（在胶囊里面，不会一开始就碰着几何体）
	 * @param Pivot - 平滑后的跟随点
	 * @param Boom - 跟随点到期望位置
	 * @param ProbeRadius - 探测球的半径
	 */
	FCollisionSweep SweepCollision(const APawn& Pawn, const FVector& CapsuleCenter, const FVector& Pivot, const FVector& Boom, float ProbeRadius) const;

	/** 跟随点的平滑 */
	FGinkgoCameraFollow Follow;

	/** 碰撞的压缩：拉近瞬时、推远走弹簧 */
	FGinkgoCameraCollision Collision;

	/** 上面两份状态属于哪个视角目标：换了就从头开始 */
	TWeakObjectPtr<const AActor> FollowedTarget;

	/** 上一次更新时视角目标的胶囊中心：推算这一帧的相机时，跟随点跟着胶囊挪 */
	FVector LastCapsuleCenter = FVector::ZeroVector;

	/** RequestCameraCut 之后、下一次更新之前 */
	bool bCutRequested = true;

	/** 离开玩家视角后置真，恢复求值的第一帧丢弃暂停期间积存的相机事件，然后清零 */
	bool bDiscardPendingEvents = false;

	/** 视角目标上的相机上下文：换视角目标时重新找 */
	TWeakObjectPtr<UGinkgoCameraContextComponent> FollowedContext;

	/** ② 参数层：修改器的权重与参数弹簧 */
	FGinkgoCameraParamLayer ParamLayer;

	/** 换肩行为：当前肩位、平滑与 ADS 检测的计时；离开玩家视角结束 ADS 会话，按退出复原选项处理肩位 */
	FGinkgoCameraShoulder Shoulder;

	/** ⑤ 叠加层：各效果的弹簧与包络 */
	FGinkgoCameraOverlayLayer OverlayLayer;

	/** 参数层与换肩镜像后的上次输出：PredictViewLocation 使用同一份横向偏移，避免瞄准仍沿右肩推算 */
	FGinkgoCameraParams LastParams;

	/** 叠加之前的视角，见 GetAimView */
	FMinimalViewInfo AimView;

	/** 相机的时钟（s）：切换以来的时间，切换的那一帧从这一帧的帧长起（同动画的持枪循环时钟） */
	double Clock = 0.0;

	/** 第一人称开镜这一帧的混合进度（0–1），离开玩家视角时清零 */
	float FirstPersonAlpha = 0.f;

	/** 枪上瞄具这一帧的世界位置和朝向，FirstPersonAlpha 大于零时有效 */
	Ginkgo::Scope::FSightFrame SightFrame;

	/** 第一人称相机眼位的世界位置（cm），PredictViewLocation 用来推算下一帧 */
	FVector FirstPersonEye = FVector::ZeroVector;

	/** 当前视角目标上一次开镜开火的世界时刻，仅用于最终画面的眼位。 */
	TOptional<double> ScopeShotTime;

	/** 持有瞄具时准备的画中画对象；捕获组件与渲染目标在实际进入第一人称后才创建 */
	UPROPERTY(Transient)
	TObjectPtr<UGinkgoScopeView> ScopeView;

	/** 平滑后的缺失生命比例（0 为满血无额外暗角，1 为空血最大暗角），由 EvaluateView 逐帧追随角色血量并在切镜或离开视角时重置 */
	float SmoothedHealthMissing = 0.f;

	/** @brief 两层的总开关：调试时关掉对照（SetLayersEnabled），发行版总开着 */
	static bool AreModifiersEnabled();
	static bool IsOverlayEnabled();

#if !UE_BUILD_SHIPPING
	/** 最近一次视角求值的调试快照，每次求值后更新 */
	FDebugFrame DebugFrame;

	/** 调试用参数层总开关，SetLayersEnabled 修改，所有玩家相机共用 */
	static bool bModifiersEnabled;

	/** 调试用叠加层总开关，SetLayersEnabled 修改，所有玩家相机共用 */
	static bool bOverlayEnabled;
#endif
};
