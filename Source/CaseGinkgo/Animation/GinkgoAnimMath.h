#pragma once

#include "CoreMinimal.h"
#include "Animation/GinkgoAnimTypes.h"

struct FAnimSyncMarker;

/**
 * 动画数据的纯函数：不依赖动画实例与任何 UObject，单独测试（CaseGinkgo.Animation.Math.*）。
 * 长度单位是厘米，时间是秒。
 */
namespace Ginkgo::Animation
{
	/**
	 * @brief 没有输入时还要滑多远：本项目的地面摩擦（v > S 时 dv/dt = −F·v，之后 dv/dt = −F·S），连续时间的解
	 * @param Speed - 现在的水平速度
	 * @param Friction - 地面摩擦 F
	 * @param StopSpeed - 停止速度 S
	 * @return (v − S/2)/F（v > S），v²/(2FS)（v ≤ S）；没有摩擦时停不下来，为 0（不做距离匹配）
	 * @note 移动组件按帧推进摩擦，停止距离随帧率略变（冲刺在 30 / 60 / 144 fps 下 144 / 156 / 162 cm，这里是 167 cm）。
	 *       距离匹配每帧用剩下的速度重算，越接近停止点误差越小。引擎的 PredictGroundMovementStopLocation 假定减速度整段不变，不能用
	 */
	CASEGINKGO_API double StopDistance(double Speed, double Friction, double StopSpeed);

	/**
	 * @brief 按反方向时，速度还要多远过零：摩擦与反向的加速一起作用（dv/dt = −F·max(v, S) − A）
	 * @param Speed - 现在的水平速度（沿原来的方向）
	 * @param Friction - 地面摩擦 F
	 * @param StopSpeed - 停止速度 S
	 * @param Deceleration - 反向的加速 A = 地面加速系数 × wishspeed（cm/s²）；为 0 时就是停止距离
	 * @return 连续时间的解：先指数式减到 S，再匀减速到 0
	 * @note 反向只要几帧（冲刺 0.08 s），按帧离散的误差比停止距离大：冲刺速度下这里是 30.2 cm，60 fps 实际约 23 cm。
	 *       同样每帧用剩下的速度重算，过零前收敛到 0
	 */
	CASEGINKGO_API double PivotDistance(double Speed, double Friction, double StopSpeed, double Deceleration);

	/**
	 * @brief 速度相对面向的角度（度，−180–180，从上往下看向右为正）
	 * @param MinSpeed - 水平速度低于它时方向不可信，返回 0
	 */
	CASEGINKGO_API double LocalAngle(const FVector& Velocity, double FacingYaw, double MinSpeed);

	// ===== 移动的片段 =====

	/** @brief 方向对应的角度（度，相对面向，向右为正）：F 0，B 180，向左的两种 −90，向右的两种 90 */
	CASEGINKGO_API double DirectionAngle(EGinkgoAnimDirection Direction);

	/**
	 * @brief 按移动方向相对面向的角度选方向，带滞回与髋的朝向
	 * 四个扇区以 ±45°、±135° 为界。还在当前方向的扇区里、或者越过边界不到 DeadZone 时不换，来回抖的输入不会让片段来回切。
	 * 换到侧向时：从前方来，髋朝移动的方向（LL、RR）；从后方来，髋朝另一侧（LR、RL）；从另一侧来（左右反向），髋不转（RL 到 LL、RR 到 LR）
	 * @param Current - 当前的方向；刚开始、没有方向时传 F
	 */
	CASEGINKGO_API EGinkgoAnimDirection SelectDirection(double LocalAngle, EGinkgoAnimDirection Current, double DeadZone);

	/** @brief 反向之后的方向：F 到 B，B 到 F，左右之间髋不转（LL 到 RL，LR 到 RR，RL 到 LL，RR 到 LR），与 GASP 反向片段的结尾一致 */
	CASEGINKGO_API EGinkgoAnimDirection PivotExitDirection(EGinkgoAnimDirection Direction);

	/**
	 * @brief 速度比（移动速度 / 片段的速度）分给播放速率与步幅：播放速率夹在 PlayRateClamp 里，剩下的给步幅
	 * @note 侧向、后退的跑比片段快 1.36 / 1.59 倍：播放速率到 1.25，步幅 1.09 / 1.27（Docs/Design/AnimationBlueprint.md 的"Source 手感与素材的时间差"）
	 */
	CASEGINKGO_API void SplitSpeedRatio(double SpeedRatio, const FVector2D& PlayRateClamp, double& OutPlayRate, double& OutStrideScale);

	/** @brief 原地转身用哪个角度的片段：45°、90°、135°、180° 里离 |偏航补偿| 最近的，返回下标 0–3 */
	CASEGINKGO_API int32 TurnInPlaceIndex(double AbsYaw);

	/**
	 * @brief 站着时根骨骼的偏航补偿：胶囊这一帧转了 YawDelta（度），网格反着转回去，脚留在原地
	 * @return 补偿夹在 ±Limit：视角转得更多时网格被带着走。已经超过上限的（别的状态接住的一下子转过去）不压回去，只是不再变大
	 */
	CASEGINKGO_API double AccumulateRootYawOffset(double Offset, double YawDelta, double Limit);

	/**
	 * @brief 在不减的曲线上找值等于 Target 的时刻（二分），曲线在 [0, Length] 上
	 * @return 第一个值不小于 Target 的时刻：Target 比首帧的值小时为 0，比末帧的大时为 Length
	 * @note 距离曲线由导入命令生成、每一帧不减；引擎的距离匹配按压缩后的键二分，这里按函数求值，进入急停、反向前比较两个片段时用
	 */
	CASEGINKGO_API float FindTimeAtCurveValue(TFunctionRef<float(float)> Curve, float Target, float Length);

	/**
	 * @brief 按走过的距离推进片段（距离匹配）：在距离曲线上找比现在多走 DistanceTraveled 的时刻，隐含的播放速率夹在 PlayRateClamp 里
	 * @param DeltaTime - 这一帧的时长；≤ 0 时不动
	 * @return 新的时刻，不超过 Length
	 * @note 与引擎 AnimDistanceMatchingLibrary 的 AdvanceTimeByDistanceMatching 做的是同一件事；自己写是为了不依赖那个 beta 的插件，也好单独测试
	 */
	CASEGINKGO_API float AdvanceTimeByDistance(TFunctionRef<float(float)> Curve, float CurrentTime, float DistanceTraveled, float Length, float DeltaTime,
		const FVector2D& PlayRateClamp);

	/**
	 * @brief 换循环片段时步伐对上：在原片段里算出现在落在哪两个同步标记之间、走到了几成，换到新片段里同名标记之后的同一段同样的几成
	 * @param FromMarkers、ToMarkers - 两个片段的同步标记，按时刻排好（导入时按接触曲线生成的 L、R）
	 * @return 新片段里的时刻；任一片段没有标记、或新片段里没有同名的标记时按归一化的时刻换算
	 */
	CASEGINKGO_API float MatchMarkerPhase(TConstArrayView<FAnimSyncMarker> FromMarkers, float FromTime, float FromLength, TConstArrayView<FAnimSyncMarker> ToMarkers,
		float ToLength);

	/**
	 * @brief 某个时刻之后的下一个同步标记的名字，按循环处理：过了最后一个标记就是第一个
	 * @param Markers - 片段的同步标记（导入时按接触曲线生成的 L、R：哪只脚下一步落地）
	 * @return 没有标记时为 NAME_None
	 */
	CASEGINKGO_API FName NextMarkerName(TConstArrayView<FAnimSyncMarker> Markers, float Time);

	/**
	 * @brief 最后一个这个名字的同步标记的时刻
	 * @return 没有这个名字的标记时为 −1
	 */
	CASEGINKGO_API float LastMarkerTime(TConstArrayView<FAnimSyncMarker> Markers, FName MarkerName);

	/**
	 * @brief 这一帧落的脚：片段的时刻从 LastTime 走到 Time，跨过的脚的标记按先后给出（相机的落脚震动用）
	 * - 往回走当作循环过了末尾、接着从头走；一帧走了半个片段以上当作时刻被设过（跳过去的），不算；
	 * - 连着同名的只算第一个：接触曲线抖了一下又起来，那只脚其实没离地（同 FollowMarkers）。LastStep 是上一次落的脚，跨帧、跨片段留着
	 * @param Markers - 片段的脚的标记（L、R），不用排好
	 * @param LastStep - 上一次落的脚，跨过的算上以后改成最后一个
	 * @return 落的脚的标记名与它在这一帧里的几成 (0, 1]：走到它的那段占这一帧走的几成
	 */
	CASEGINKGO_API TArray<TPair<FName, float>, TInlineAllocator<2>> CrossFootMarkers(TConstArrayView<FAnimSyncMarker> Markers, float LastTime, float Time,
		float Length, FName& LastStep);

	/**
	 * @brief 两个片段在同一条时间线上：时长与脚的同步标记（名字、时刻）都相同。在一个片段上逐帧做出来的片段是这样（持枪循环做在 GASP 的循环上）
	 */
	CASEGINKGO_API bool IsSameTimeline(TConstArrayView<FAnimSyncMarker> AMarkers, float ALength, TConstArrayView<FAnimSyncMarker> BMarkers, float BLength);

	/**
	 * @struct FGinkgoMarkerFollow : 一个循环片段跟着另一个片段的步伐放（FollowMarkers），跨帧留着的
	 */
	struct FGinkgoMarkerFollow
	{
		/** 跟的片段这一帧的时刻 */
		float Time = 0.f;
		/** 领头的片段上一帧落在第几个标记之后；没有标记时为 INDEX_NONE */
		int32 LeaderSegment = INDEX_NONE;
		/** 跟上了：为假时（刚用上）不看 Time，落在第一个同名标记的那一段 */
		bool bSynced = false;
	};

	/**
	 * @brief 一个循环片段跟着另一个片段的步伐放（武器层的持枪循环跟移动状态机所放的片段），推进一帧
	 * - 同一条时间线（IsSameTimeline）：取同一个时刻，两边逐帧对应；
	 * - 否则按同步标记跟，同引擎标记同步里的跟随者：留在现在这一段（两个标记之间），走到领头的同样的几成；领头的过了标记，
	 *   跟到后面第一个同名标记的那一段。时刻一直往前走，过标记时不跳，只在领头的换了片段、几成往回跳时跟着跳（换片段有惯性化接住）。
	 *   不用 MatchMarkerPhase：它总落在第一个同名标记的那一段，领头的走到第二步时跟的会跳回第一步；
	 * - 领头的或跟的没有标记：按 FallbackDelta 往前走。
	 * 连着同名的标记只算第一个（接触曲线抖了一下又起来，那只脚没离地，GASP 的 Run_Loop_RR 就有连着两个 R）
	 * @param LeaderMarkers、FollowerMarkers - 两个片段的脚的标记（L、R）
	 * @param bLeaderChanged - 领头的换了片段：上一帧落在第几个标记之后不能比
	 * @param FallbackDelta - 没有标记可跟时往前走多少（s）
	 */
	CASEGINKGO_API void FollowMarkers(FGinkgoMarkerFollow& Follow, TConstArrayView<FAnimSyncMarker> LeaderMarkers, float LeaderTime, float LeaderLength,
		bool bLeaderChanged, TConstArrayView<FAnimSyncMarker> FollowerMarkers, float FollowerLength, float FallbackDelta);

	/**
	 * @brief 持枪循环的时钟走一帧（主实例给武器层的 FGinkgoWeaponAnimData::LoopClock）：切过的那一帧从这一帧的帧长起，切发生在上一帧末
	 *        （传送、回放放回开始状态时请求的切换）。回放的轨迹也从那一刻起计时，各个帧率下同一个时刻的时钟相同
	 * @param bCut - 这一帧切过
	 */
	CASEGINKGO_API double StepLoopClock(double Clock, float DeltaSeconds, bool bCut);

	/**
	 * @brief 一直走的时钟落在一个循环片段的哪一刻：时钟除以时长的余数（开镜的循环不随开镜从头放）。时长不是正的取 0
	 */
	CASEGINKGO_API float LoopTimeAt(double Clock, float Length);

	/**
	 * @struct FGinkgoMotionDifferencer : 可见的根逐帧差分
	 * 位置取网格组件的世界位置，只取水平；本地角色被校正时胶囊跳过去，校正挪的量（移动组件给的累计值）按帧取差、从位移里扣掉。
	 * 第一次、清掉历史之后（传送、回放放回开始状态）没有上一帧，速度取移动组件的
	 */
	struct CASEGINKGO_API FGinkgoMotionDifferencer
	{
		struct FInput
		{
			FVector Location = FVector::ZeroVector;
			double FacingYaw = 0.0;
			/** 校正累计挪动胶囊的量（UGinkgoCharacterMovementComponent::GetCorrectionOffsetTotal） */
			FVector CorrectionOffsetTotal = FVector::ZeroVector;
			/** 没有上一帧可比时用的速度：移动组件的 */
			FVector FallbackVelocity = FVector::ZeroVector;
			double DeltaSeconds = 0.0;
			/** 清掉历史 */
			bool bCut = false;
		};

		struct FOutput
		{
			/** 水平速度 */
			FVector Velocity = FVector::ZeroVector;
			double VerticalSpeed = 0.0;
			/** 这一帧的水平位移，扣掉校正之后 */
			double FrameDistance = 0.0;
			/** 面向的转速（度/秒） */
			double YawRate = 0.0;
			/** 这一帧扣掉的校正挪动量 */
			FVector CorrectionOffset = FVector::ZeroVector;
			/** 这一帧没有上一帧可比：第一次，或清掉了历史 */
			bool bReset = false;
		};

		/**
		 * @brief 推进一帧
		 * @note 帧长不为正时（暂停后的第一帧、同一帧里重复更新）没有新的信息，返回上一次的结果，历史不动
		 */
		FOutput Update(const FInput& Input);

	private:
		bool bHasHistory = false;
		FVector PreviousLocation = FVector::ZeroVector;
		double PreviousFacingYaw = 0.0;
		FVector PreviousCorrectionTotal = FVector::ZeroVector;
		FOutput LastOutput;
	};
}
