#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GinkgoCameraShoulder.generated.h"

class AActor;
class UWorld;
struct FGinkgoCameraParams;

/** 一条自动换肩检测射线：角度与长度决定探测区域，命中分值决定这片区域对换肩判断的影响 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoCameraShoulderRay
{
	GENERATED_BODY()

	/** 相对视线的偏航角（度，−180～180）：负为左、正为右、零度为正前方。两个肩位使用相同方向，保留相机俯仰 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ray", meta = (DisplayName = "角度", ClampMin = -180, ClampMax = 180, Units = "Degrees"))
	float Angle = 0.f;

	/** 从各自肩位的相机位置起算的射程（cm，1～10000）；每条独立设置，默认 350 cm */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ray", meta = (DisplayName = "长度", ClampMin = 1, ClampMax = 10000, Units = "cm"))
	float Length = 350.f;

	/** 第一次阻挡命中的分值（0～1000，支持小数）；零仍检测和显示，但不影响左右分差 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ray", meta = (DisplayName = "命中分值", ClampMin = 0, ClampMax = 1000))
	float Score = 1.f;

	FGinkgoCameraShoulderRay() = default;
	FGinkgoCameraShoulderRay(float InAngle, float InLength = 350.f, float InScore = 1.f) : Angle(InAngle), Length(InLength), Score(InScore) {}

	/** @brief 校验编辑器、调参文件和页面输入共用的范围；非法配置不参与物理查询 */
	bool IsValid() const;
};

/**
 * @struct FGinkgoCameraShoulderSettings : 换肩行为的配置
 * 相机资产只保存参数；当前肩位、检测间隔与冷却由各玩家相机组件上的 FGinkgoCameraShoulder 独立持有。
 * 辅助功能设置控制自动检测和退出 ADS 的肩位复原，手动换肩与平滑始终可用。
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoCameraShoulderSettings
{
	GENERATED_BODY()

	/** 每个肩位的射线数上限；编辑器与运行时共用，一次采样最多查询两组 */
	static constexpr int32 MaxRays = 64;

	/** 两肩共用的检测列表；每肩默认九条（正前 2.0 分、±15° 1.4 分、±30° 1.2 分、±45°/±60° 1.0 分，射程 350 cm），空列表表示不检测环境 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shoulder", meta = (DisplayName = "检测射线"))
	TArray<FGinkgoCameraShoulderRay> Rays = {
		{-15.f, 350.f, 1.4f}, {15.f, 350.f, 1.4f},
		{-30.f, 350.f, 1.2f}, {30.f, 350.f, 1.2f},
		{-45.f, 350.f, 1.f},  {45.f, 350.f, 1.f},
		{-60.f, 350.f, 1.f},  {60.f, 350.f, 1.f},
		{0.f, 350.f, 2.f}
	};

	/** 两次环境检测的最小间隔（秒）；进入 ADS 时立即检测，低帧率下不补跑错过的检测 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shoulder", meta = (DisplayName = "检测间隔", ClampMin = 0.01, Units = "s"))
	float ScanInterval = 0.25f;

	/** 射线碰撞通道：每条射线遇到首个 Block 就累加该条的分值，Overlap 与 Ignore 不计分，并忽略角色自身 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shoulder", meta = (DisplayName = "检测通道"))
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Camera;

	/** 当前侧总分减另一侧总分严格大于此值才换肩；支持小数，与射线分值使用同一尺度 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shoulder", meta = (DisplayName = "分差容差", ClampMin = 0))
	float ScoreTolerance = 1.f;

	/** 换肩后的冷却（秒）：期间不检测、不重新计分；退出 ADS、暂停玩家视角或硬切相机时立即清零 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shoulder", meta = (DisplayName = "换肩冷却", ClampMin = 0, Units = "s"))
	float Cooldown = 15.f;

	/** 横向构图的临界阻尼角频率（rad/s）；≤ 0 直接到位，默认约 0.4 秒完成主要过渡 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shoulder", meta = (DisplayName = "换肩角频率", ClampMin = 0, Units = "rad/s"))
	float BlendOmega = 12.f;
};

/**
 * @struct FGinkgoCameraShoulder : 换肩行为通道
 * 参数层之后平滑镜像横向偏移，沿用后续碰撞与瞄准解算。环境检测放在最终视角求出之后，
 * 左右肩各从对应视点检测整组射线，命中分值累加到发出射线的肩位；结果驱动下一帧的肩位目标。
 * 不修改 ControlRotation，也不改变持枪手或第一人称瞄具的出瞳。
 */
struct CASEGINKGO_API FGinkgoCameraShoulder
{
	/** @brief 手动切换目标肩位；同样进入冷却，避免辅助行为立即撤销玩家的选择。非 ADS 时下一次更新会清空冷却 */
	void Toggle(double Time, const FGinkgoCameraShoulderSettings& Settings);

	/** @brief 推进肩位平滑并镜像参数中的横向偏移；每次相机求值仅调用一次，冷却不阻止已开始的镜头过渡 */
	void Apply(FGinkgoCameraParams& Params, float DeltaTime, const FGinkgoCameraShoulderSettings& Settings);

	/**
	 * @brief 用同一帧的最终视角方向比较两个肩位；返回本次是否实际执行了射线检测
	 * @param GetOrigin - 按肩位（true 右、false 左）求对应的世界空间镜头位置；仅在采样时对每肩调用一次
	 * @param Time - 世界游戏时间（秒），与暂停、时间缩放一致
	 * @param bAds - 武器上下文的 ADS 状态标签，不使用尚在淡出的 ADS 权重
	 * @param bRestoreOnExit - 自动换肩开启时，退出 ADS 是否回到本次进入 ADS 的肩位；只恢复左右侧，不恢复旧的世界位置
	 * @note 冷却、间隔或开关不允许检测时，不建立查询、不发射射线、不重新计分；退出 ADS 先清空冷却。
	 */
	bool UpdateAuto(const UWorld& World, const AActor& Self, TFunctionRef<FVector(bool)> GetOrigin, const FRotator& CameraRotation,
		bool bAds, bool bEnabled, double Time, const FGinkgoCameraShoulderSettings& Settings, bool bRestoreOnExit = false);

	/** @brief 结束本次 ADS：按最近一次求值的复原选项恢复进入时的目标肩位，再清空会话和检测；暂停玩家视角时也走此路径 */
	void EndAds();

	/** @brief 只清空检测计时、分数和调试采样；保留本次 ADS 的进入肩位，关闭辅助功能后再开启也不会重记基准 */
	void ResetDetection();

	/** @brief 硬切时直接到目标肩位并清空检测，避免传送后沿旧位置或旧冷却继续推算 */
	void Snap();

	/** 目标肩位：默认右肩；过渡期间使用 GetBlend 判断当前构图所在的一侧 */
	bool IsRightShoulder() const { return bRightShoulder; }

	/** 当前平滑系数：右肩 +1，左肩 −1，过渡时位于两者之间 */
	double GetBlend() const { return Blend; }

	/** 最近一次采样的左右得分（X 左、Y 右），未采样或 ResetDetection 后为零；冷却期间保持 */
	FVector2D GetScores() const { return Scores; }

	/** 剩余冷却（秒），只读查询，不推进计时、不发射射线 */
	double GetCooldownRemaining(double Time) const { return FMath::Max(0.0, CooldownUntil - Time); }

	/** 当前是否处于 ADS 会话；不受自动检测开关或冷却影响 */
	bool IsAdsActive() const { return AdsEntryShoulder.IsSet(); }

#if !UE_BUILD_SHIPPING
	/** 一条真实检测射线的结果；由采样产生，调试绘制只读，禁止为了显示另发查询 */
	struct FDebugRay
	{
		/** 射线的完整终点（世界坐标，cm），即使被挡住也保留完整长度供对照 */
		FVector End = FVector::ZeroVector;

		/** 第一个阻挡点（世界坐标，cm）；未命中时等于 End */
		FVector HitPoint = FVector::ZeroVector;

		/** 相对采样相机的偏航角（度），负为左侧，零为正前方 */
		float YawOffset = 0.f;

		/** 这条射线的配置下标，调试标签与编辑器列表按它对应；仅对本次采样的配置有效 */
		int32 Index = INDEX_NONE;

		/** 命中时实际累加的分值；未命中为零，采样后修改配置也不改写历史记录 */
		float Score = 0.f;

		/** 发出这条射线的肩位，同时决定命中时分值计入哪一侧 */
		bool bRightOrigin = false;

		/** 是否遇到该碰撞通道上的 Block；与计分使用同一次查询 */
		bool bBlocked = false;
	};

	/** 最近一次完整检测的快照；等待间隔或冷却时冻结，不跟随之后的相机位置移动 */
	struct FDebugSample
	{
		/** 左肩检测扇面的世界空间起点（cm），只在本次采样有合法射线时有效 */
		FVector LeftOrigin = FVector::ZeroVector;

		/** 右肩检测扇面的世界空间起点（cm），与左肩取自同一帧 */
		FVector RightOrigin = FVector::ZeroVector;

		/** 采样相机的世界朝向（度），观察模式据此定位，不用于补发射线 */
		FRotator Rotation = FRotator::ZeroRotator;

		/** 实际采样的世界游戏时刻（秒）；只有 Rays 非空时有效 */
		double Time = 0.0;

		/** 本次比较前选择的肩位，换肩后保留，便于对照决策前后的肩位 */
		bool bOnRight = true;

		/** 两肩的真实查询结果，每个合法配置条目对应两条；默认共十八条，ResetDetection 后为空 */
		TArray<FDebugRay, TInlineAllocator<18>> Rays;
	};

	/** 最近一次真实采样，供 CaseGinkgoDebug 绘制射线和命中点 */
	const FDebugSample& GetDebugSample() const { return DebugSample; }
#endif

private:
	/** 本次进入 ADS 时的目标肩位（true 右、false 左）；会话外为空，不在每次自动换肩时覆盖 */
	TOptional<bool> AdsEntryShoulder;

	/** 最近一帧是否同时开启自动换肩和退出复原；EndAds 使用它，保证暂停玩家视角时也能完成会话清理 */
	bool bRestoreAdsEntryShoulder = false;

	/** 玩家或自动行为选择的目标侧；结束 ADS 时可按选项改回进入侧，ResetDetection 本身不改它 */
	bool bRightShoulder = true;

	/** 横向偏移系数，由 Apply 的临界阻尼弹簧逐帧推进 */
	double Blend = 1.0;

	/** Blend 的变化速度（每秒），硬切时清零 */
	double BlendVelocity = 0.0;

	/** 下次允许采样的世界游戏时刻（秒），退出 ADS 时归零 */
	double NextScanTime = 0.0;

	/** 换肩冷却截止的世界游戏时刻（秒），零表示没有冷却 */
	double CooldownUntil = 0.0;

	/** 最近一次完整采样的左右分数（X 左、Y 右）；只在实际检测或重置时修改 */
	FVector2D Scores = FVector2D::ZeroVector;

#if !UE_BUILD_SHIPPING
	/** 仅实际采样时更新，间隔及冷却期间冻结，退出 ADS 或关闭自动行为时清空 */
	FDebugSample DebugSample;
#endif
};
