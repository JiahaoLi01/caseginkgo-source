#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GinkgoCameraModifiers.h"
#include "GinkgoCameraOrbit.h"
#include "GinkgoCameraParams.h"
#include "GinkgoCameraShoulder.h"
#include "GinkgoCameraConfig.generated.h"

/**
 * @struct FGinkgoFollowSettings : 跟随点的弹簧
 * 相机跟着胶囊中心走（不跟骨骼），胶囊中心先过临界阻尼弹簧：只有跟随点滞后，旋转不插值（Docs/Design/CameraPipeline.md「临界阻尼弹簧」）。
 * 水平、竖直各用一套：两个方向的屏幕范围不同，跳跃的起落也只在竖直方向。
 *
 * 弹簧追的是胶囊中心加速度前馈 v·2/ω × 前馈系数：临界阻尼弹簧追匀速移动的目标时稳态滞后是 2v/ω，前馈把它补掉，
 * 弹簧只平滑速度的突变（lurch、撞墙、起跳）。速度取移动组件的速度：蹲下、上台阶、服务器校正都只挪胶囊、不改速度，前馈不会因此跳一下。
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoFollowSettings
{
	GENERATED_BODY()

	/** 水平角频率：越大跟得越紧，lurch、撞墙这类速度突变时角色在画面里晃得越少，也越生硬；≤ 0 不平滑 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Follow", meta = (DisplayName = "水平角频率", ClampMin = 0, Units = "rad/s"))
	float Omega = 10.f;

	/** 水平速度前馈的系数：1 时匀速移动没有滞后，0 时滞后 2v/ω（冲刺 762 cm/s、ω 为 10 时 152 cm），默认 0.8 保留适度速度感 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Follow", meta = (DisplayName = "水平速度前馈", ClampMin = 0, ClampMax = 1))
	float Feedforward = 0.8f;

	/** 水平最大滞后：平滑后的胶囊中心离真实的最远多少，超过时投影回这个半径，高速时角色不被甩出画面中心；≤ 0 不限 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Follow", meta = (DisplayName = "水平最大滞后", ClampMin = 0, Units = "cm"))
	float MaxLag = 150.f;

	/** 竖直角频率：越小跳跃时相机起落越缓 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Follow", meta = (DisplayName = "竖直角频率", ClampMin = 0, Units = "rad/s"))
	float VerticalOmega = 10.f;

	/**
	 * 竖直速度前馈的系数。默认 0：跳跃的起落由弹簧的滞后吸收，相机比角色起得晚、落得缓，落地时不往下冲；
	 * 1 时相机紧跟起落，落地时会冲到角色下面再回来（落地速度越大冲得越多）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Follow", meta = (DisplayName = "竖直速度前馈", ClampMin = 0, ClampMax = 1))
	float VerticalFeedforward = 0.f;

	/** 竖直最大滞后：长时间下落、竖直的钩锁时角色离画面中心最远多少；≤ 0 不限 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Follow", meta = (DisplayName = "竖直最大滞后", ClampMin = 0, Units = "cm"))
	float VerticalMaxLag = 100.f;

	/**
	 * 前馈用的速度先过的临界阻尼低通的角频率；≤ 0 不过低通（默认）。
	 * 低通让前馈的目标变得缓，代价是速度突变时相机滞后得多、回来得慢（lurch 的数据见相机文档）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Follow", meta = (DisplayName = "前馈速度的低通", ClampMin = 0, Units = "rad/s"))
	float FeedforwardFilterOmega = 0.f;
};

/**
 * @struct FGinkgoFirstPersonSettings : 第一人称开镜（Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）
 * 手上有瞄具时，玩家相机把视点从越肩位置混合到计算出的眼位。朝向仍取控制器视角与相机叠加，因此枪的程序化偏移能让分划离开屏幕中心。
 * 进度与抬枪共用两端缓动后的开镜量。跟枪比例、拉栓避让和开火眼位偏移共同决定最终画面的眼位。
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoFirstPersonSettings
{
	GENERATED_BODY()

	/** 近裁剪面：第一人称观察时视点距目镜仅数厘米，引擎默认的 10 cm 会剖切目镜筒；仅在第一人称过渡激活期间临时覆写 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FirstPerson", meta = (DisplayName = "近裁剪面", ClampMin = 0.1, Units = "cm"))
	float NearClipPlane = 1.f;

	/**
	 * 相机距瞄具出瞳小于该距离时隐藏角色头部骨骼：出瞳位于头部体积内部，过渡轨迹必然穿过头部。
	 * 由于骨骼隐藏需等待下一帧蒙皮刷新生效（延迟一帧），考虑低帧率单帧滑行步长，阈值需略大于头颅半径
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FirstPerson", meta = (DisplayName = "藏头的距离", ClampMin = 0, Units = "cm"))
	float HeadHideDistance = 45.f;

	/**
	 * 拉栓期间视点离开出瞳的峰值避让偏移（瞄具局部坐标系，cm：X 沿光轴朝前、Y 右、Z 上）。
	 * 抬起并后拉枪栓时，右手手腕与手指会贴近至出瞳右后方数厘米处；按机件动作包络（Weapon.Value.SightClearance，取抬柄与后拉比例之较大值）
	 * 将视点向左上方偏置以避开手腕遮挡，压柄闭锁后平滑归位出瞳
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FirstPerson", meta = (DisplayName = "拉栓时眼睛让到哪", Units = "cm"))
	FVector BoltClearance = FVector(-3.0, -6.0, 6.0);
};

/**
 * @struct FGinkgoHealthVignetteSettings : 玩家血量提示暗角后处理配置
 * 由 UGinkgoPlayerCameraComponent 在阶段 ⑥ 按角色当前血量与平滑状态写入 OutView.PostProcessSettings，
 * 利用引擎 Tonemapper 原生 VignetteIntensity 与 VignetteColor 提供连续的血量边缘视觉提示，满血时不覆盖关卡默认后处理。
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoHealthVignetteSettings
{
	GENERATED_BODY()

	/** 满血时的基准暗角强度（与引擎默认 0.4 对齐，保证刚掉血起步平滑不跳变；满血稳定后不再覆写后处理） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (DisplayName = "满血基准暗角", ClampMin = 0, UIMax = 2))
	float BaseIntensity = 0.4f;

	/** 空血/濒死时的最大暗角强度（CosineFourthLaw 暗角系数，约 1.25 时四边显著压暗而中心准星视野保持通透） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (DisplayName = "空血最大暗角", ClampMin = 0, UIMax = 2))
	float MaxIntensity = 1.25f;

	/** 缺失生命比例（0–1）映射到暗角强度的幂次指数；略大于 1 使轻伤克制、中低血量明显增强 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (DisplayName = "血量响应指数", ClampMin = 0.1, UIMax = 4))
	float ResponseExponent = 1.15f;

	/** 血量比例低于该阈值时开始平滑混入低血量边缘压色（0–1，0.65 表示约 65% 血以下渐显深暗赤黑） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (DisplayName = "压色起始血量比例", ClampMin = 0.01, ClampMax = 1))
	float ColorStartHealthRatio = 0.65f;

	/** 低血量时暗角边缘的线性压色（深暗赤黑，以乘算方式在亮色场景上清晰提示危急状态且不刺眼） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (DisplayName = "低血量暗角压色"))
	FLinearColor LowHealthColor = FLinearColor(0.22f, 0.015f, 0.012f, 1.f);

	/** 暗角强度追随实时血量的指数平滑速度（1/s）；<= 0 时不平滑，镜头切镜时直接对齐 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (DisplayName = "暗角平滑速度", ClampMin = 0, UIMax = 30))
	float SmoothSpeed = 8.f;
};

/**
 * @class UGinkgoCameraConfig : 相机配置
 * 相机管线的全部手感参数：结构（阶段的顺序、修改器的类型）写在代码里，参数放在这里。玩家相机组件求值时读取，PIE 中改资产立即生效。
 *
 * 只放配置，不放状态：弹簧的值与速度、修改器的淡入淡出权重由玩家相机组件持有。调参面板直接改这份资产，
 * 同一份资产也可以给几个相机用，互不干扰。
 */
UCLASS(BlueprintType)
class CASEGINKGO_API UGinkgoCameraConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	UGinkgoCameraConfig();

	/** @brief 参数下拉框的选项（FGinkgoCameraParamField 的 GetOptions）：FGinkgoCameraParams 里全部 float 参数 */
	UFUNCTION()
	static TArray<FName> GetParamFieldOptions();

	/**
	 * 参数的基础值 P₀：无修改器生效时的静态基准构图。
	 * 采用近距越肩三分构图：站立平视时角色躯干中线约位于画面宽度的 33%，头顶约位于画面高度的 36%，
	 * 身体右沿与屏幕中央准星保留充足余量，兼顾近身临场感与中心射击视野无遮挡
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Params", meta = (DisplayName = "基础参数"))
	FGinkgoCameraParams BaseParams;

	/** 三环轨道：平视保留参数层臂长，俯视、仰视按各自的环半径平滑改变距离，再叠加肩部偏移 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Params", meta = (DisplayName = "三环轨道"))
	FGinkgoCameraOrbitSettings Orbit;

	/**
	 * 参数层的修改器（Docs/Design/CameraPipeline.md「参数层」）：每帧按上下文算出权重，按优先级应用覆盖、累加增量。
	 * 类默认值带滑铲的 FOV 与臂长（构造函数里）。运行中增删条目时权重从头开始
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Params", meta = (DisplayName = "参数修改器"))
	TArray<FGinkgoCameraParamModifier> Modifiers;

	/** 参数弹簧：覆盖与增量合成以后的参数再过临界阻尼弹簧；没列出的参数不平滑 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Params", meta = (DisplayName = "参数弹簧"))
	TArray<FGinkgoCameraParamSpring> ParamSprings;

	/** 换肩行为通道：手动左右肩过渡与 ADS 自动避障，状态由玩家相机组件独立持有 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behavior", meta = (DisplayName = "换肩"))
	FGinkgoCameraShoulderSettings Shoulder;

	/** 跟随点的弹簧 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Follow", meta = (DisplayName = "跟随点"))
	FGinkgoFollowSettings Follow;

	/** 碰撞探测球的半径：越大离墙越远，贴墙时越早拉近 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", meta = (DisplayName = "碰撞探测球半径", ClampMin = 0, Units = "cm"))
	float ProbeRadius = 12.f;

	/** 推远的角频率：遮挡消失后相机回到原来距离的快慢（拉近是瞬时的，不经弹簧）；≤ 0 时推远也是瞬时的 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Collision", meta = (DisplayName = "推远的角频率", ClampMin = 0, Units = "rad/s"))
	float PushOutOmega = 6.f;

	/**
	 * 叠加层的效果（Docs/Design/CameraPipeline.md「叠加层」）：碰撞之后只改旋转与 FOV，输出相加。
	 * 类默认值带滑铲开始的震动、冲刺落脚的震动与开镜的晃动（构造函数里）。只晃画面：瞄准、命中照旧沿视角（ControlRotation），准星画在枪真正对着的地方
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Overlay", meta = (DisplayName = "叠加效果"))
	TArray<TObjectPtr<UGinkgoCameraOverlayEffect>> OverlayEffects;

	/** 叠加的旋转绕哪里转（相机文档待定问题 1）：默认绕相机自身，碰撞之后不挪位置 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Overlay", meta = (DisplayName = "叠加旋转的转轴"))
	EGinkgoCameraOverlayPivot OverlayPivot = EGinkgoCameraOverlayPivot::Camera;

	/** 第一人称开镜（有瞄具的枪） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FirstPerson", meta = (DisplayName = "第一人称开镜"))
	FGinkgoFirstPersonSettings FirstPerson;

	/** 玩家当前血量的暗角后处理提示参数 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PostProcess", meta = (DisplayName = "血量暗角"))
	FGinkgoHealthVignetteSettings HealthVignette;
};
