#pragma once

#include "CoreMinimal.h"
#include "GinkgoSpring.h"

/**
 * @struct FGinkgoCameraCollision : ④ 碰撞的状态，臂被压缩了多少
 * 扫掠给出这一帧要求的压缩：跟随点到期望位置的连线上，被几何体挡住的那一段有多长（cm）。这里决定实际的压缩：
 * 要求的比现在多（有东西挡进来）时立即拉近、速度清零，相机不进几何体；比现在少（遮挡消失或退开）时按推远的弹簧慢慢放开。
 * 纯计算，不碰世界，单独测试（CaseGinkgo.Camera.Collision.*）。
 *
 * 为什么存压缩量而不是臂长：参数层改臂长（钩锁拉长臂，M3d）时，没有遮挡的相机直接跟着参数走，不再经过这里的弹簧；
 * 推远的快慢也就与臂长的参数弹簧分开调。压缩作用在整个"臂 + 肩部偏移"上，两者按同一比例缩短（偏移同步收缩）。
 */
struct CASEGINKGO_API FGinkgoCameraCollision
{
	/**
	 * @brief 推进一帧
	 * @param RequiredCompression - 这一帧扫掠要求的压缩（cm），没有遮挡时为 0
	 * @param DeltaTime - 帧长（s）
	 * @param PushOutOmega - 推远的角频率（rad/s），≤ 0 时推远也是瞬时的
	 * @return 实际的压缩（cm），不少于要求的
	 * @note 推远时把要求的压缩当作在这一帧里匀速变化（与跟随点一样用 StepMoving）：镜头慢慢转离遮挡时结果与帧率无关。
	 *       第一次推进（或 Reset 之后）直接用要求的压缩
	 */
	double Step(double RequiredCompression, double DeltaTime, double PushOutOmega);

	/** @brief 下一次推进直接用要求的压缩：第一帧、换视角目标、传送之后 */
	void Reset() { bActive = false; }

	/** 实际的压缩（cm） */
	double GetCompression() const { return Compression.Value; }

	/** 压缩变化的速度（cm/s），推远时为负 */
	double GetCompressionVelocity() const { return Compression.Velocity; }

private:
	TGinkgoCriticalSpring<double> Compression;

	/** 上一帧要求的压缩：这一帧里要求从这里匀速变过来 */
	double LastRequired = 0.0;

	bool bActive = false;
};
