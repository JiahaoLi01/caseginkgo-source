#pragma once

#include "CoreMinimal.h"

enum class EGinkgoCameraBlendCurve : uint8;
struct FGinkgoCameraParams;
struct FGinkgoCameraOrbitSettings;

/**
 * 相机管线里不依赖世界的计算：位姿解算的两步与碰撞的压缩。纯函数，单独测试（CaseGinkgo.Camera.Math.*）。
 */
namespace Ginkgo::Camera
{
	/**
	 * @brief 跟随点：胶囊中心加跟随点偏移
	 * @param CapsuleCenter - 胶囊中心（跟胶囊不跟骨骼，动画的起伏不传给相机）
	 * @param ViewRotation - 这一帧的视角；偏移只跟它的 Yaw 转，向上就是世界的上方
	 * @param Params - 相机参数，用 PivotOffset 的三个分量
	 */
	CASEGINKGO_API FVector ComputePivot(const FVector& CapsuleCenter, const FRotator& ViewRotation, const FGinkgoCameraParams& Params);

	/**
	 * @brief 三环轨道在当前俯仰角下沿视线反方向退开的距离（cm，不含肩部偏移）
	 * @param ArmLength - 参数层求值后的中环半径（cm），动态臂长修改等比例作用于三环
	 * @param Pitch - 控制器俯仰角（度），负值对应相机位于上环；先归一化，兼容 0–360° 输入
	 * @param Orbit - 外环的水平半径比例及角度；关闭时原样返回 ArmLength
	 * @note 中环与两个外环处距离变化率均为 0，跨环不突变；超过外环角度后距离保持，朝向仍跟输入。
	 */
	CASEGINKGO_API double ComputeOrbitArmLength(double ArmLength, double Pitch, const FGinkgoCameraOrbitSettings& Orbit);

	/**
	 * @brief 相机的期望位置：跟随点 + 瞬时旋转 × 轨道距离与肩部偏移
	 * @param Pivot - 跟随点
	 * @param ViewRotation - 这一帧的视角，不插值：玩家输入的旋转直接生效，只有跟随点滞后
	 * @param Params - 相机参数，用臂长与肩部偏移
	 * @param Orbit - 三环配置；空指针用于原有球面轨道，玩家相机及其瞄准预测应传入同一份配置
	 * @return 碰撞之前的相机位置
	 */
	CASEGINKGO_API FVector ComputeCameraLocation(const FVector& Pivot, const FRotator& ViewRotation, const FGinkgoCameraParams& Params,
		const FGinkgoCameraOrbitSettings* Orbit = nullptr);

	/**
	 * @brief 碰撞压缩后的相机位置：沿跟随点到期望位置的连线退回压缩的长度
	 * @param Pivot - 跟随点（碰撞第一段扫掠之后的）
	 * @param Boom - 跟随点到期望位置：臂长与肩部偏移，按同一比例缩短，角色不会挡住准星
	 * @param Compression - 压缩（cm），超过连线的长度时相机就在跟随点上
	 */
	CASEGINKGO_API FVector ApplyCompression(const FVector& Pivot, const FVector& Boom, double Compression);

	/**
	 * @brief 放大后的视场：2·atan(tan(视场 / 2) / 倍率)。用倍率不直接写视场：基础视场改了（设置里的视野），放大得一样多
	 * @param BaseFieldOfView - 基础视场（度）
	 * @param Zoom - 倍率；≤ 1 时不放大
	 */
	CASEGINKGO_API float ZoomedFieldOfView(float BaseFieldOfView, float Zoom);

	/**
	 * @brief 权重的曲线：进度 p（0–1）时值是多少。线性 p；两头缓 p^k / (p^k + (1−p)^k)；先快后慢 1 − (1−p)^k；先慢后快 p^k
	 * @param Strength - k，1 就是线性
	 */
	CASEGINKGO_API double BlendCurveValue(double Progress, EGinkgoCameraBlendCurve Curve, float Strength);

	/** @brief BlendCurveValue 的反函数：值是多少时进度是多少 */
	CASEGINKGO_API double BlendCurveProgress(double Value, EGinkgoCameraBlendCurve Curve, float Strength);

	/**
	 * @brief 权重沿曲线走一段时间。往 1 走时 w = S_in(p)，p 每秒走 1 / 淡入时间；往 0 走时 w = 1 − S_out(p)，p 每秒走 1 / 淡出时间；时间为 0 时一下子到
	 * @note 不存进度：从现在的权重反算出来再走。所以走两段与一口气走完相同（与帧率无关），半路换方向时权重不跳，两个方向的曲线可以不同
	 */
	CASEGINKGO_API float StepCurvedWeight(float Weight, bool bTarget, double Time, float BlendInTime, float BlendOutTime, EGinkgoCameraBlendCurve InCurve,
		EGinkgoCameraBlendCurve OutCurve, float Strength);
}
