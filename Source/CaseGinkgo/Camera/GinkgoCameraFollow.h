#pragma once

#include "CoreMinimal.h"
#include "GinkgoSpring.h"

struct FGinkgoFollowSettings;

/**
 * @struct FGinkgoCameraFollow : 跟随点的平滑
 * 玩家相机组件持有，每帧推进一次：给胶囊中心与移动组件的速度，得到平滑后的胶囊中心，位姿解算在它上面加跟随点偏移。
 * 平滑的是胶囊中心，跟随点偏移（随视角的 Yaw 转）在平滑之后才加：转视角时跟随点立即跟着转，旋转不插值。
 * 纯计算，不碰世界，单独测试（CaseGinkgo.Camera.Follow.*）。
 *
 * 每帧三步：
 * 1. 前馈的偏移 = 速度 × 2/ω × 前馈系数，水平、竖直各用各的 ω 与系数；速度可以先过低通
 * 2. 水平、竖直两个临界阻尼弹簧追"胶囊中心 + 前馈的偏移"（TGinkgoCriticalSpring::StepMoving）：胶囊中心在这一帧里从上一帧的位置匀速移过来，
 *    前馈的偏移整帧取这一帧的速度。匀速时与帧率无关，前馈系数为 1 时稳态滞后恰好是 0
 * 3. 离胶囊中心超过最大滞后时投影回来（水平是圆，竖直是区间），速度不动
 *
 * 为什么前馈的偏移整帧不变、不随胶囊中心一起在帧里匀速变过去：移动组件在每次移动的开头改速度（输入的加速、lurch、起跳、滑铲起步），
 * 之后按新速度推进位置，所以速度、也就是前馈的偏移，是在帧的开头跳变的。按帧中间算时，lurch 这种速度突变下
 * 30 fps 比 144 fps 多滞后 7.5 cm；整帧不变时两者一样（camera.py 的 lurch）。代价是速度在帧里连续变化时（下落中的重力）
 * 前馈的目标偏 系数 × a·Δt/ω：竖直默认不前馈；水平在空中只有扫射，移动组件每次移动加的速度有上限，这项偏差各帧率相同。
 * 速度过了低通时是连续变化的，偏移在帧里从上一帧的匀速变过去
 */
struct CASEGINKGO_API FGinkgoCameraFollow
{
	/**
	 * @brief 推进一帧
	 * @param Target - 胶囊中心
	 * @param Velocity - 移动组件的速度，前馈用
	 * @param DeltaTime - 帧长（s）；≤ 0 时弹簧不动
	 * @param Settings - 相机配置里的跟随参数，每帧读取
	 * @return 平滑后的胶囊中心
	 * @note 第一次推进（或 Reset 之后）直接放到目标上，速度取目标的速度：匀速移动时从稳态接着跟，不先落后再追上来
	 */
	FVector Step(const FVector& Target, const FVector& Velocity, double DeltaTime, const FGinkgoFollowSettings& Settings);

	/** @brief 下一次推进直接放到目标上：第一帧、换视角目标、传送之后 */
	void Reset() { bActive = false; }

	/** 平滑后的胶囊中心 */
	FVector GetValue() const { return FVector(Horizontal.Value, Vertical.Value); }

	/** 这一帧弹簧追的目标：胶囊中心加前馈的偏移 */
	FVector GetFeedforwardTarget() const { return LastTarget + LastOffset; }

	/** 这一帧离胶囊中心超过了最大滞后、被投影回来 */
	bool WasClamped() const { return bClamped; }

private:
	/** 速度前馈的偏移：水平 v·2/ω × 系数，竖直同理；ω ≤ 0 的方向不平滑，也不前馈 */
	static FVector ComputeFeedforwardOffset(const FVector& Velocity, const FGinkgoFollowSettings& Settings);

	TGinkgoCriticalSpring<FVector2D> Horizontal;
	TGinkgoCriticalSpring<double> Vertical;

	/** 前馈用的速度（过了低通的；不过低通时就是移动组件的速度） */
	TGinkgoCriticalSpring<FVector> FilteredVelocity;

	/** 上一帧的胶囊中心与前馈的偏移：这一帧里胶囊中心从这里匀速移过来 */
	FVector LastTarget = FVector::ZeroVector;
	FVector LastOffset = FVector::ZeroVector;

	bool bActive = false;
	bool bClamped = false;
};
