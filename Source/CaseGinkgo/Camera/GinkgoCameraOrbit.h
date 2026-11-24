#pragma once

#include "CoreMinimal.h"
#include "GinkgoCameraOrbit.generated.h"

/**
 * @struct FGinkgoCameraOrbitSettings : 越肩相机的三环轨道
 * 中环为平视时的臂长，上、下环按相机相对跟随点的高度命名（低头看时相机在上环）。
 * 外环以俯仰角和水平半径定义；半径相对参数层输出的 ArmLength，因此滑铲等臂长修改会等比例改变整条轨道。
 * 解算仍沿视线反方向后退，只平滑改变距离，不平滑输入旋转；肩部偏移随后在视角空间叠加，不计入环半径。
 * 这是配置，不持有运行时状态。玩家视角与瞄准预测共同读取，碰撞和第一人称眼位混合在轨道解算之后进行。
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoCameraOrbitSettings
{
	GENERATED_BODY()

	/** 启用三环距离变化；关闭时恢复原有固定臂长的球面轨道，肩部偏移与碰撞规则相同 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (DisplayName = "启用三环轨道"))
	bool bEnabled = true;

	/** 上环的水平半径 / 当前 ArmLength（0.05–1，不含肩部偏移）；在俯视角达到 UpperPitch 时精确经过该环 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (DisplayName = "上环半径比例", ClampMin = 0.05, ClampMax = 1))
	float UpperRadiusScale = 0.35f;

	/** 到达上环时低头看的角度绝对值（5–80°，对应负的 ControlRotation.Pitch）；平视到此角度间平滑改变距离 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (DisplayName = "上环俯视角", ClampMin = 5, ClampMax = 80, Units = "Degrees"))
	float UpperPitch = 60.f;

	/** 下环的水平半径 / 当前 ArmLength（0.05–1，不含肩部偏移）；在仰视角达到 LowerPitch 时精确经过该环 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (DisplayName = "下环半径比例", ClampMin = 0.05, ClampMax = 1))
	float LowerRadiusScale = 0.5f;

	/** 到达下环时抬头看的角度（5–80°，对应正的 ControlRotation.Pitch）；超过外环角度后保持外环的视线距离，仍能继续转动 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Orbit", meta = (DisplayName = "下环仰视角", ClampMin = 5, ClampMax = 80, Units = "Degrees"))
	float LowerPitch = 45.f;
};
