#pragma once

#include "CoreMinimal.h"

/**
 * Source 式速度模型的纯函数：不依赖 CMC 与任何 UObject，单独测试（CaseGinkgo.Movement.Math.*）。
 * 长度单位是厘米，时间是秒。
 */
namespace Ginkgo::Movement
{
	/** 1 Source 单位约 1.905 cm：调试显示与日志里的速度同时以 u/s 给出，便于和 Source 的数值对照 */
	inline constexpr float CentimetersPerSourceUnit = 1.905f;

	/**
	 * @brief 投影加速：只限制 V 在 WishDir 上的分量不超过 SpeedCap，不截断 V 的大小
	 * @param V - 速度，原地修改
	 * @param WishDir - 期望方向，单位向量
	 * @param SpeedCap - V 在 WishDir 上的分量上限
	 * @param AccelStep - 这一步最多加的速度
	 * @note 已经比 SpeedCap 快的分量不会被拉慢，动量因此能在能力之间传递；反向输入仍然会减速。
	 *       空中 SpeedCap 很小、AccelStep 却按完整 wishspeed 计算，速度与输入接近垂直时投影约为 0，总能继续加——空中扫射的来源。
	 */
	CASEGINKGO_API void Accelerate(FVector& V, const FVector& WishDir, float SpeedCap, float AccelStep);

	/**
	 * @brief 地面加速：投影加速之后，速度大小不超过 max(加速前的速度, WishSpeed)
	 * @param V - 速度，原地修改
	 * @param WishDir - 期望方向，单位向量
	 * @param WishSpeed - wishspeed，同时是投影上限
	 * @param AccelStep - 这一步最多加的速度
	 * @note 只用投影规则时，转向会让速度短暂超过 wishspeed：旧速度在新 wishdir 上的投影变小，又能加满到 wishspeed，
	 *       垂直于 wishdir 的分量却只靠摩擦慢慢消掉。按住 W 时交替按 A / D，平均速度能比直跑快两成。
	 *       这里只截掉这一步加速多出来的部分；已有的更快来速（落地免摩擦窗口里的连跳、以后的滑铲）不受影响。
	 */
	CASEGINKGO_API void AccelerateOnGround(FVector& V, const FVector& WishDir, float WishSpeed, float AccelStep);

	/**
	 * @brief Source 的地面摩擦：减速量 = max(速度, StopSpeed) × Friction × Δt，减到 0 为止，不反向
	 * @note 低于 StopSpeed 时按 StopSpeed 算，松手后能在有限时间内停住，而不是无限逼近 0
	 */
	CASEGINKGO_API void ApplyFriction(FVector& V, float Friction, float StopSpeed, float DeltaTime);

	/** @brief 由跳跃高度和重力大小求起跳速度：v = √(2gh) */
	CASEGINKGO_API float JumpVelocityForHeight(float Height, float Gravity);

	// ===== 滑铲 =====

	/**
	 * @brief 滑铲的起步加速：水平速度变为 min(v + Boost, BoostCap)，只放大水平分量
	 * @param V - 速度，原地修改
	 * @param Boost - 加多少
	 * @param BoostCap - 加完后水平速度的上限
	 * @return 实际加了多少；已经达到或超过上限时为 0，速度不变（只加不减，同投影规则）
	 */
	CASEGINKGO_API float ApplySlideBoost(FVector& V, float Boost, float BoostCap);

	/**
	 * @brief 滑铲的减速：dv/dt = −Decel − Decay × v（线性减速加指数衰减），按解析解推进，减到 0 为止
	 * @param Speed - 速度大小
	 * @param Decel - 线性减速（cm/s²）
	 * @param Decay - 指数衰减率（1/s），每秒保留 e^−Decay
	 * @param DeltaTime - 推进的时间
	 * @return 推进后的速度大小
	 * @note 用解析解而不是逐帧积分，所以与帧率无关：先推进 a 再推进 b，与一次推进 a + b 结果相同
	 */
	CASEGINKGO_API float SlideDecelerate(float Speed, float Decel, float Decay, float DeltaTime);

	/**
	 * @brief 去掉输入沿速度方向的正分量：滑铲中只能转向和刹车，不能向前加速
	 * @param Input - 移动输入，大小为模拟量（0–1）
	 * @param Velocity - 当前速度
	 * @return 侧向与反向（刹车）的部分原样保留，与速度同向的部分去掉；速度为 0 时原样返回
	 */
	CASEGINKGO_API FVector RemoveForwardInput(const FVector& Input, const FVector& Velocity);

	/**
	 * @brief 滑铲的转向：按输入推一下速度，之后速度大小不超过推之前
	 * @param V - 速度，原地修改
	 * @param Steer - RemoveForwardInput 的结果
	 * @param SteerStep - 这一步最多推多少（转向加速度 × Δt）
	 * @note 侧向输入只改方向、不改大小，转向不会越转越快；反向输入按 SteerStep 刹车
	 */
	CASEGINKGO_API void SteerSlide(FVector& V, const FVector& Steer, float SteerStep);

	/**
	 * @brief 能不能朝这个方向开始滑铲：有输入时，输入与视角前向的水平夹角不超过最大角；没有输入时不限制
	 * @param Input - 移动输入
	 * @param ViewForward - 视角前向
	 * @param MaxAngleCos - 最大夹角的余弦（53° 约为 0.6）
	 * @note 防止向后、向侧面铲。泰坦陨落 2 的代码按输入判断；Apex 不按方向键也能铲，所以没有输入时放行
	 */
	CASEGINKGO_API bool IsSlideDirectionAllowed(const FVector& Input, const FVector& ViewForward, float MaxAngleCos);

	/**
	 * @brief 角色的偏航：在地面上冲刺时面向水平速度，否则面向视角（射击游戏的常态，移动是平移）
	 * @param bSprintFacing - 在地面上冲刺：按着冲刺、没有蹲下、在地面上
	 * @param Velocity - 速度
	 * @param ViewYaw - 这次移动的视角偏航（度）
	 * @param MinSpeed - 冲刺时取速度方向的最低水平速度；更慢时速度的方向不可信，照常面向视角
	 * @return 偏航（度）
	 * @note 冲刺不限方向：向后、向侧面冲刺时角色转过身去跑，动画只要向前的冲刺循环。设计见移动文档的"角色的朝向"
	 */
	CASEGINKGO_API double ComputeFacingYaw(bool bSprintFacing, const FVector& Velocity, double ViewYaw, double MinSpeed);

	// ===== lurch =====

	/**
	 * @brief lurch：把水平速度一下子转向新的输入方向，v′ = v + k(|v|ŵ − v)，竖直速度不变
	 * @param V - 速度，原地修改；只改水平分量
	 * @param WishDir - 新的输入方向，水平的单位向量
	 * @param Strength - k，0 到 1。1 时大小不变、方向完全换过去；中间值是两个等长向量的线性插值，结果落在弦上：
	 *        转过的角度比输入的夹角小，夹角大时掉速（90° 时剩 √(k² + (1 − k)²)，180° 时反向、剩 |2k − 1|）
	 * @note 一次按下只转一次，转多少只看 k，与帧率无关
	 */
	CASEGINKGO_API void Lurch(FVector& V, const FVector& WishDir, float Strength);

	/**
	 * @brief 方向键的朝向（X 向右、Y 向前，各为 −1、0、1）打包成 4 位：低 2 位是 X，高 2 位是 Y，每 2 位 0 为没有、1 为正、2 为负
	 * @note 扩展的移动数据与存档用；超出 −1 到 1 的分量按符号取
	 */
	CASEGINKGO_API uint8 PackDirectionKeys(FIntPoint Keys);

	/** @brief PackDirectionKeys 的逆过程；不认识的值（3）当作没有 */
	CASEGINKGO_API FIntPoint UnpackDirectionKeys(uint8 Packed);

	/**
	 * @brief 方向键的朝向转到世界的水平方向
	 * @param Keys - X 向右、Y 向前
	 * @param Yaw - 视角的 Yaw（度）。先按网络上的精度压一遍（16 位，同 FRotator::NetSerialize）：服务器收到的就是压过的，
	 *        客户端第一次模拟、重放与服务器因此算出逐位相同的方向
	 * @return 水平的单位向量；没按方向键时为零
	 */
	CASEGINKGO_API FVector DirectionKeysToWorld(FIntPoint Keys, double Yaw);

	/**
	 * @brief 地速倍率量化成一个字节，进扩展的移动数据（Docs/Design/Weapons.md「移动：持枪与开镜的地速」）
	 * @note 一格 1/255（约 0.4%），先夹到 0–1，所以改过的客户端只能让自己更慢。两端都用量化以后的值算：客户端第一次模拟、重放与服务器逐位相同
	 */
	CASEGINKGO_API uint8 QuantizeSpeedScale(float Scale);

	/** @brief QuantizeSpeedScale 的逆过程：255 正好是 1 */
	CASEGINKGO_API float UnquantizeSpeedScale(uint8 Quantized);

	/**
	 * @brief 地面上的 wishspeed：跑、冲刺、蹲的速度乘持枪倍率；跑与蹲再乘开镜倍率，冲刺不乘
	 * @note 按着开镜去冲刺的第一帧开镜倍率还在（动作状态读的是上一帧的冲刺），规则写在这里两端才一样。滑铲、空中不走这里
	 */
	CASEGINKGO_API float GroundWishSpeed(float RunSpeed, float SprintSpeed, float CrouchSpeed, bool bCrouching, bool bSprinting, float HeldScale, float AdsScale);
}
