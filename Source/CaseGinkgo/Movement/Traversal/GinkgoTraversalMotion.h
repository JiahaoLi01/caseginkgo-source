#pragma once

#include "CoreMinimal.h"
#include "GinkgoTraversalMotion.generated.h"

class UAnimMontage;
class UWorld;
class ACharacter;
struct FGinkgoTraversalClip;
struct FGinkgoTraversalResult;

/** 动画姿势的查询胶囊关键帧；仅影响碰撞代理，不移动角色、网格或动画根。 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoTraversalBodyKey
{
	GENERATED_BODY()

	/** 蒙太奇时间（秒）；相邻关键帧连续插值。 */
	UPROPERTY(EditAnywhere)
	float Time = 0.f;

	/** 相对正常角色胶囊中心的偏移（cm，角色局部空间）。 */
	UPROPERTY(EditAnywhere)
	FVector Offset = FVector::ZeroVector;

	/** 查询半径相对角色站立半径的比例；不修改实际胶囊属性。 */
	UPROPERTY(EditAnywhere, meta = (ClampMin = .25, ClampMax = 1))
	float RadiusScale = 1.f;

	/** 查询半高相对站立半高的比例；实际值始终不小于查询半径。 */
	UPROPERTY(EditAnywhere, meta = (ClampMin = .25, ClampMax = 1))
	float HeightScale = 1.f;
};

/**
 * 一次动作的不可变根运动适配。上升段对齐触边位置与朝向，随后衔接落点；竖直曲线及侧向细节来自素材。
 * 准备成功后由攀爬组件持有，Motion Warping 与启动前预检共用同一个求值函数。
 */
struct CASEGINKGO_API FGinkgoTraversalMotion
{
	/** 所选蒙太奇，由攀爬素材资产保持强引用；计划不跨越素材拥有者生命周期。 */
	const UAnimMontage* Montage = nullptr;

	/** 根运动结束时刻（秒），之后由收势和 Locomotion 接管。 */
	float Duration = 0.f;

	/** 动画局部平移的基础缩放；启用接触约束后前向由 WarpForward 映射，侧向与竖直仍采用此倍率。 */
	FVector Scale = FVector::OneVector;

	/** 上升段结束的素材时间（秒）；零表示不启用接触约束，完整保留基础缩放轨迹。 */
	float RiseEndTime = 0.f;

	/** 素材局部空间的水平前向单位向量；由角色网格基础朝向确定，不依赖素材是否有 XY 根运动。 */
	FVector Forward = FVector::RightVector;

	/** 原素材在触边时刻的前向累计位移（cm）；与 SourceEnd 一起定义水平进度映射。 */
	double SourceContact = 0.;

	/** 原素材根运动结束时的前向累计位移（cm）；阶段净位移为零时使用时间曲线补充水平运动。 */
	double SourceEnd = 0.;

	/** 本次从起点到触边根位置的前向距离（素材局部 cm），由边沿与素材接触间距确定。 */
	double ContactDistance = 0.;

	/** 本次从起点到落点的前向距离（素材局部 cm）。 */
	double EndDistance = 0.;

	/** 起步切线比例 [0,1]；贴墙时平滑降至零，避免接触状态下先向墙内冲再抬升。 */
	double EntrySlopeScale = 1.;

	/** 从检测方向的素材坐标转到起步网格坐标的旋转；位移按它转换，身体在上升段平滑转向。 */
	FQuat FacingCorrection = FQuat::Identity;

	/** 端点高度差（动画局部 cm），以五次平滑函数分布，起止修正速度均为零。 */
	float HeightCorrection = 0.f;

	/** 起点脚底到世界的变换；包含起始朝向、网格基础旋转与网格世界缩放。 */
	FTransform Origin;

	/** 素材声明的碰撞代理快照；预检与执行采用相同配置，不由碰撞反推轨迹。 */
	TArray<FGinkgoTraversalBodyKey> Body;

	/** 提取从片段起点到 Time 的适配后累积根变换，单位 cm；不夹方向、不按当前位置追赶目标。 */
	FTransform Evaluate(float Time) const;

	/** 对任意时间段求增量，跨帧或跨结束时刻仍保持累积一致性。 */
	FTransform Delta(float From, float To) const;

	/** 按素材时间取得独立碰撞代理，边界外使用端点。 */
	FGinkgoTraversalBodyKey BodyAt(float Time) const;

	/** 分别映射触边前后的水平进度；公共切线连续，零水平位移的素材也能生成入场距离。 */
	double WarpForward(double SourceProgress, float Time) const;
};

namespace Ginkgo::Traversal
{
	/** 只依据素材及几何锚点生成有界适配；超出素材比例范围时失败，不修改角色。 */
	CASEGINKGO_API bool PrepareMotion(const FGinkgoTraversalClip& Clip, const FGinkgoTraversalResult& Result,
		const ACharacter& Character, FGinkgoTraversalMotion& Out, const FTransform* RecordedOrigin = nullptr);

	/** 沿实际修正曲线检查查询胶囊；使用角色真实通道/响应，不忽略被攀爬墙体。 */
	CASEGINKGO_API bool CheckMotion(const FGinkgoTraversalMotion& Motion, const ACharacter& Character, FHitResult& Hit);

	/** 一段位移的碰撞代理 Sweep；身体缩放与位置变化独立于根运动求值。 */
	CASEGINKGO_API bool SweepBody(const FGinkgoTraversalMotion& Motion, const ACharacter& Character,
		float From, float To, const FVector& StartCenter, const FVector& EndCenter, FHitResult& Hit);
}
