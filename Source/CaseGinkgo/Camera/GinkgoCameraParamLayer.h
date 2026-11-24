#pragma once

#include "CoreMinimal.h"
#include "GinkgoCameraParams.h"
#include "GinkgoSpring.h"

class UGinkgoCameraConfig;
class UGinkgoCameraContextComponent;

/**
 * @struct FGinkgoCameraParamLayer : ② 参数层的状态与求值器
 * 玩家相机组件持有（配置资产里只有参数）。每帧从零重建每个参数的贡献再求值，不缓存，也没有增删事件（Docs/Design/CameraPipeline.md「求值器」）：
 * 1. 权重：激活条件定目标是 0 还是 1，权重按淡入、淡出时间线性地走。目标在帧里哪一刻变的：标签查询变了算在帧的开头
 *    （移动组件在每次移动的开头改状态），连续量的条件按前后两帧的余量插出跨过的时刻；权重从那一刻起换方向，结果与帧率无关
 * 2. 覆盖：从基础值 P₀ 开始，按优先级从低到高逐层插值 P = P + w·(值 − P)，两个覆盖同时淡入淡出时结果仍然连续
 * 3. 增量：P += (1 − m)·w·Δ，m 是屏蔽这个增量类别的覆盖里权重最大的那个（开镜淡入一半时速度类增量也只剩一半）
 * 4. 参数弹簧：合成后的参数过临界阻尼弹簧，把线性淡入淡出的折角磨圆。权重在帧里分段线性（目标变的那一刻、淡到 0 或 1 的那一刻是折点），
 *    合成的参数在折点之间也是线性的：弹簧在折点上拆开推进，每一段里目标匀速地变（跟随点的做法），目标跳的地方（淡入淡出时间为 0）
 *    跳在它真正发生的那一刻。所以权重与参数都与帧率无关（连续量的值在帧里当作不变）
 * 5. 盖在弹簧之后的覆盖（开镜的视场）：权重沿淡入、淡出的曲线走（Ginkgo::Camera::StepCurvedWeight），P = P + w·(值 − P)；它屏蔽的增量照第 3 条算
 * 纯计算，不碰世界，单独测试（CaseGinkgo.Camera.ParamLayer.*）。
 */
struct CASEGINKGO_API FGinkgoCameraParamLayer
{
	/** 一个修改器的状态，下标同配置里的修改器 */
	struct FModifierState
	{
		/** 权重（0–1） */
		float Weight = 0.f;
		/** 上一帧的激活、标签查询与锁住 */
		bool bTarget = false;
		bool bQuery = false;
		bool bLatched = false;
		/** 上一帧各条件的余量：这一帧插出跨过 0 的时刻 */
		TArray<double, TInlineAllocator<2>> Margins;
		/** 这一帧的值与被屏蔽的程度（调试与回放） */
		float Value = 0.f;
		float Block = 0.f;
		/** 这一帧目标变了的时刻（离帧的开头，s），没变时为负（调试与测试） */
		double FlipTime = -1.0;
	};

	/**
	 * @brief 求值一帧
	 * @param Config - 相机配置：基础值、修改器与参数弹簧，每帧读取
	 * @param Context - 视角目标的相机上下文；没有时只有空的查询成立
	 * @param DeltaTime - 帧长（s）
	 * @param bEnabled - 参数层开着；关着时（调试的 ginkgo.camera.modifiers 0）直接是基础值、状态清空，对照用
	 * @return 这一帧的参数
	 * @note 第一次求值（Reset 之后）权重与弹簧直接放到位：切换视角目标、传送时不从基础值淡过去
	 */
	FGinkgoCameraParams Evaluate(const UGinkgoCameraConfig& Config, const UGinkgoCameraContextComponent* Context, double DeltaTime, bool bEnabled = true);

	/** @brief 下一次求值直接放到位：第一帧、换视角目标、传送之后 */
	void Reset() { bActive = false; }

	/** 各修改器的状态，下标同配置 */
	TConstArrayView<FModifierState> GetModifierStates() const { return Modifiers; }

	/** 这一帧的目标：合成以后、没过参数弹簧的参数（盖在弹簧之后的覆盖也算在内） */
	const FGinkgoCameraParams& GetTarget() const { return Target; }

	/**
	 * @brief 权重按线性的淡入淡出走一段时间
	 * @param bTarget - 往 1 走（否则往 0）
	 * @param Time - 走多久（s）
	 */
	static float StepWeight(float Weight, bool bTarget, double Time, float BlendInTime, float BlendOutTime);

private:
	/**
	 * @brief 按给定的权重合成参数：基础值上按优先级逐层插值覆盖，再累加（被屏蔽过的）增量。值取这一帧求过的（FModifierState::Value）
	 * @note 顺带记下每个增量被屏蔽的程度（调试与回放）
	 */
	FGinkgoCameraParams Compose(const UGinkgoCameraConfig& Config, TConstArrayView<float> Weights);

	TArray<FModifierState> Modifiers;

	/** 下标同配置里的参数弹簧 */
	TArray<TGinkgoCriticalSpring<double>> Springs;

	FGinkgoCameraParams Target;

	bool bActive = false;
};
