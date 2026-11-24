#pragma once

#include "CoreMinimal.h"
#include "Control/GinkgoInputSettings.h"        // EGinkgoInputDirectory：键鼠、手柄各一套
#include "GinkgoAimAssist.generated.h"

class AActor;
class UGinkgoDamageableComponent;
class UWorld;

/**
 * @struct FGinkgoAimAssistProfile : 一种设备的辅助瞄准（Docs/Design/Control.md「辅助瞄准」）
 * 半楞次定律：准星相对目标往外走的那部分速度按强度减掉，往里来的不管。强度在目标的中线附近是满的，往外按距离淡到零。
 * 数是玩家在设置界面里给的（FGinkgoInputSettings 的 *AimAssist* 几项，默认值在那里），Ginkgo::AimAssist::MakeProfile 换过来；这里的初始值是关
 */
struct FGinkgoAimAssistProfile
{
	/** 总开关：关着时不选目标、不修（强度是 0 时照样选目标，只是不修） */
	bool bEnabled = false;
	/** 不开镜时的强度：准星离开目标的那部分速度减掉多少（0 不管，0.6 只剩四成，最多 MaxStrength）。开镜时按开镜进度过渡到 AdsStrength */
	float HipStrength = 0.f;
	/** 开镜满了时的强度 */
	float AdsStrength = 0.f;
	/** 满强度的范围（cm）：离目标的中线（骨盆到头的线段）这么近以内（在目标那里量，按距离换成角度） */
	float InnerRadius = 0.f;
	/** 从满强度淡到零（cm）：离中线这么远以外没有辅助 */
	float OuterRadius = 0.f;
	/** 比这远（cm）的目标不管 */
	float MaxDistance = 0.f;
	/**
	 * 目标在画面上的运动（它自己走、自己走动带来的）算进相对速度的比例。
	 * 1：目标往外走时也拖着准星走（手柄游戏常见的“转动辅助”）；0：只看玩家自己转的，准星不会跟着目标动
	 */
	float TargetMotionScale = 0.f;
};

/**
 * @struct FGinkgoAimAssistConfig : 辅助瞄准里不给玩家调的：目标的中线用哪两根骨头、要不要有输入。
 * 强度、范围、跟随目标两种设备各一套（你说的），11-10 起在玩家的设置里（你手测说最大强度太低，要多暴露参数、放宽范围）
 */
USTRUCT()
struct CASEGINKGO_API FGinkgoAimAssistConfig
{
	GENERATED_BODY()

	/** 目标的中线：这两根骨头之间的线段（人体模型的骨盆到头）。网格上没有这两根骨头时用包围盒的竖直中线 */
	UPROPERTY(EditAnywhere, Category = "AimAssist")
	FName LowerBone = TEXT("pelvis");

	UPROPERTY(EditAnywhere, Category = "AimAssist")
	FName UpperBone = TEXT("head");

	/** 只在玩家给着输入（转视角或移动）时辅助：手离开设备时视角不会自己动 */
	UPROPERTY(EditAnywhere, Category = "AimAssist")
	bool bRequireInput = true;
};

/**
 * @struct FGinkgoAimAssistField : 一帧开始时目标在准星周围的样子
 * 角度（度）的局部坐标：准星在原点，X 往右（偏航乘俯仰的余弦，画面上的宽度），Y 往上（俯仰）
 */
struct FGinkgoAimAssistField
{
	/** 目标中线的两端 */
	FVector2D SegmentA = FVector2D::ZeroVector;
	FVector2D SegmentB = FVector2D::ZeroVector;
	/** 中线在画面上怎么动（度 / 秒）：目标自己走、自己走动都算，上一帧量的 */
	FVector2D Velocity = FVector2D::ZeroVector;
	/** 满强度的半径、淡到零的半径（度） */
	float InnerRadius = 0.f;
	float OuterRadius = 0.f;
	/** 满强度（0 到 0.95） */
	float Strength = 0.f;
	/**
	 * 视角转一度（局部的 X：偏航乘余弦；Y：俯仰）准星在目标上走多远（两列）。
	 * 越肩的相机绕着角色转，转视角时相机也挪了，静止的目标在画面上跟着动一点（视差，10 m 外约 13%），所以不是 1；没有视差时是单位阵
	 */
	FVector2D ScreenX = FVector2D(1.0, 0.0);
	FVector2D ScreenY = FVector2D(0.0, 1.0);
};

namespace Ginkgo::AimAssist
{
	/** 积分的步长上限（秒）：一帧拆成这么小的步，30 fps 与 240 fps 积出来一样 */
	inline constexpr float MaxStep = 1.f / 960.f;

	/** 强度最多到这里：1 时往外的速度全减掉，出不去了（设置界面的强度也到这里为止） */
	inline constexpr float MaxStrength = 0.95f;

	/** @brief 玩家给这种设备的设置换成算法用的：最远从 m 换成 cm，强度夹到 MaxStrength 以内（文件是手改的也一样） */
	CASEGINKGO_API FGinkgoAimAssistProfile MakeProfile(const FGinkgoInputSettings& Settings, EGinkgoInputDirectory Device);

	/** @brief 线段上离 Point 最近的点 */
	CASEGINKGO_API FVector2D ClosestPointOnSegment(const FVector2D& Point, const FVector2D& A, const FVector2D& B);

	/** @brief 视角这样转（局部坐标），准星在目标上走多远（FGinkgoAimAssistField::ScreenX、ScreenY 乘过去） */
	CASEGINKGO_API FVector2D ScreenFromView(const FGinkgoAimAssistField& Field, const FVector2D& View);

	/** @brief 反过来：要准星在目标上走这么远，视角转多少。矩阵差不多退化时当作没有视差 */
	CASEGINKGO_API FVector2D ViewFromScreen(const FGinkgoAimAssistField& Field, const FVector2D& Screen);

	/** @brief 强度随距离淡掉：Inner 以内 1，Outer 以外 0，中间两头缓过 */
	CASEGINKGO_API float Falloff(float Distance, float Inner, float Outer);

	/**
	 * @brief 半楞次定律的修正速度
	 * 楞次定律“来拒去留”：感应出来的力总是阻碍相对运动，靠近时推开，离开时拉住，大小同相对速度成正比。
	 * 这里去掉“来拒”（往里的那部分不管，你说的），只留“去留”：相对速度沿径向往外的部分乘 K 减掉；切向的不管。
	 * @param Offset - 准星减目标上最近的点（度）
	 * @param RelativeVelocity - 准星相对目标的速度（度 / 秒）
	 * @param K - 这里的强度（0 到 MaxStrength）
	 * @return 加到准星上的速度（度 / 秒）
	 */
	CASEGINKGO_API FVector2D HalfLenzVelocity(const FVector2D& Offset, const FVector2D& RelativeVelocity, float K);

	/**
	 * @brief 一帧的修正：这一帧拆成小步积分，每一步按准星当时在目标上的位置算强度与径向（同帧率无关）
	 * 准星相对目标的速度 = 视差乘玩家转的 − 目标在画面上自己走的（乘 TargetMotionScale）；按它算出画面上要减掉的，再换回视角要转多少
	 * @param InputVelocity - 玩家这一帧转的速度（度 / 秒，局部坐标）
	 * @param TargetMotionScale - 目标的运动算进相对速度的比例（FGinkgoAimAssistProfile::TargetMotionScale）
	 * @return 这一帧视角修正的量（度，局部坐标）
	 */
	CASEGINKGO_API FVector2D IntegrateFrame(const FGinkgoAimAssistField& Field, const FVector2D& InputVelocity, float TargetMotionScale, float DeltaTime);
}

/** @struct FGinkgoAimAssistFrame : 这一帧辅助瞄准的样子：调试画、测量记（只是数据） */
struct FGinkgoAimAssistFrame
{
	EGinkgoInputDirectory Device = EGinkgoInputDirectory::KeyboardMouse;
	/** 这种设备的总开关开着没有 */
	bool bEnabled = false;
	/** 选中的目标；没有时为空 */
	TWeakObjectPtr<const AActor> Target;
	/** 目标的中线（世界）、中线上离准星最近的点、它离相机多远（cm） */
	FVector SegmentA = FVector::ZeroVector;
	FVector SegmentB = FVector::ZeroVector;
	FVector Nearest = FVector::ZeroVector;
	float Distance = 0.f;
	/** 这一帧开始时准星减最近的点（度，X 右、Y 上） */
	FVector2D Offset = FVector2D::ZeroVector;
	/** 目标在画面上自己走的速度（度 / 秒，不算转视角带来的） */
	FVector2D TargetVelocity = FVector2D::ZeroVector;
	/** 视差：视角转一度准星在目标上走多远（FGinkgoAimAssistField::ScreenX、ScreenY） */
	FVector2D ScreenX = FVector2D(1.0, 0.0);
	FVector2D ScreenY = FVector2D(0.0, 1.0);
	/** 满强度、淡到零的半径（度） */
	float InnerRadius = 0.f;
	float OuterRadius = 0.f;
	/** 这一帧的满强度（配置乘开镜、冲刺），与准星所在处的强度 */
	float Strength = 0.f;
	float K = 0.f;
	/** 加到视角上的修正（度） */
	FRotator Correction = FRotator::ZeroRotator;
	/** 这一帧有没有动手（转视角或移动）：配置要求有输入时，没有就不修 */
	bool bHasInput = false;
};

/**
 * @class FGinkgoAimAssist : 辅助瞄准（Docs/Design/Control.md「辅助瞄准」）
 * 控制器每帧在把视角输入加进 ControlRotation 之前调 Update，加上它返回的修正。只是普通的 C++，状态是量目标速度用的上一帧的位置
 */
class CASEGINKGO_API FGinkgoAimAssist
{
public:
	struct FInputs
	{
		const UWorld* World = nullptr;
		/** 自己（选目标时跳过、看不看得见时不挡） */
		const AActor* Self = nullptr;
		/**
		 * 视角是这样时相机在哪（AGinkgoPlayerCameraManager::PredictViewLocation）：准星就是它的正前方。
		 * 用来算视差（视角转一点点时相机挪多少）；没给时相机固定在 CameraLocation，没有视差
		 */
		TFunction<FVector(const FRotator&)> PredictCamera;
		FVector CameraLocation = FVector::ZeroVector;
		/** 这一帧加输入之前的视角 */
		FRotator View = FRotator::ZeroRotator;
		/** 玩家这一帧转的（度） */
		FRotator LookInput = FRotator::ZeroRotator;
		/** 这一帧有没有移动的输入 */
		bool bMoveInput = false;
		EGinkgoInputDirectory Device = EGinkgoInputDirectory::KeyboardMouse;
		/** 骨头、要不要有输入（项目设置） */
		const FGinkgoAimAssistConfig* Config = nullptr;
		/** 这种设备的强度、范围、跟随目标（玩家的设置，MakeProfile） */
		FGinkgoAimAssistProfile Profile;
		/** 乘在强度上：开镜进度另外算，手上没枪、冲刺时是 0 */
		float StrengthScale = 1.f;
		float AdsAlpha = 0.f;
	};

	/** @return 加到这一帧视角输入上的修正（度） */
	FRotator Update(const FInputs& In, float DeltaTime);

	/** 忘掉上一帧的目标（换了角色、回放开始） */
	void Reset();

	const FGinkgoAimAssistFrame& GetFrame() const { return Frame; }

private:
	/** 上一帧选中的目标与它的中线（世界、局部）、相机、视角、什么时候：量目标在画面上自己走的速度 */
	TWeakObjectPtr<const UGinkgoDamageableComponent> LastTarget;
	FVector LastSegmentA = FVector::ZeroVector;
	FVector LastSegmentB = FVector::ZeroVector;
	FVector2D LastLocalA = FVector2D::ZeroVector;
	FVector2D LastLocalB = FVector2D::ZeroVector;
	FVector LastCamera = FVector::ZeroVector;
	FRotator LastView = FRotator::ZeroRotator;
	double LastTime = -1.0;

	FGinkgoAimAssistFrame Frame;
};
