#pragma once

#include "CoreMinimal.h"
#include "Camera/GinkgoSpring.h"

/**
 * @struct FGinkgoCrouchMeshBlend : 空中蹲下、站起时模型不跳（Docs/Design/MovementComponent.md「空中蹲下时的模型」）
 * 换胶囊大小时引擎把网格的相对高度改到新胶囊的底上。地面上胶囊同时挪了，脚底不动；空中胶囊中心不动，模型就一帧跳 48 cm。
 * 跳的那一段反着记下来（Absorb），模型的世界高度、速度都接得上，之后按临界阻尼弹簧收回 0（Step）。只改看到的模型，胶囊、移动、相机都不碰。
 */
struct FGinkgoCrouchMeshBlend
{
	/** @return 现在叠在网格相对高度上的偏移（cm） */
	double GetOffset() const { return Spring.Value; }

	/** @return 还要不要更新：收完以后不再每帧改网格的相对位置 */
	bool IsActive() const { return Spring.Value != 0.0 || Spring.Velocity != 0.0; }

	/**
	 * @brief 模型的基准高度在世界里跳了一下：偏移反着记下，看到的模型不跳
	 * @param Jump - 跳了多少（cm，向上为正）；地面上胶囊同时挪了，这里是 0
	 */
	void Absorb(const double Jump)
	{
		if (FMath::Abs(Jump) >= SettleDistance)
		{
			Spring.Value -= Jump;
		}
	}

	/**
	 * @brief 往 0 收，按解析解推进，与帧率无关
	 * @param Omega - 角频率（rad/s），≤ 0 时直接收回
	 * @param DeltaTime - 时长（s）
	 */
	void Step(const double Omega, const double DeltaTime)
	{
		Spring.Step(0.0, Omega, DeltaTime);
		if (FMath::Abs(Spring.Value) < SettleDistance && FMath::Abs(Spring.Velocity) < SettleSpeed)
		{
			Spring.Reset(0.0);
		}
	}

private:
	/** 偏移（cm）、速度（cm/s）小到这里就算收完 */
	static constexpr double SettleDistance = 0.01;
	static constexpr double SettleSpeed = 0.1;

	TGinkgoCriticalSpring<double> Spring;
};
