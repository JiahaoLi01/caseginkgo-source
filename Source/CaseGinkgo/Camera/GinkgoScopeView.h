#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Weapon/GinkgoScope.h"  // FGinkgoScope、Ginkgo::Scope::FSightFrame
#include "GinkgoScopeView.generated.h"

class AActor;
class FGinkgoScopeResolutionExtension;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
struct FMinimalViewInfo;

/**
 * @class UGinkgoScopeView : 瞄具里的画面（画中画，Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）
 * 玩家相机组件按需创建并持有。第一人称开镜时，在本帧最终的眼位与朝向确定以后更新：
 * 1. 一个场景捕获放在物镜上、朝光轴，视场是瞄具的真实视场（加畸变的余量），排到这一帧末尾拍（同主画面同一帧的位置，镜内不落后一帧）。
 *    拍的是 HDR 的场景色（不做后处理、不带预曝光，就是亮度），视角目标（角色、手上的枪）不拍。
 * 2. 后处理材质（脚本 Scripts/Assets/scope_composite.hlsl）在色调映射之前合成：每个像素沿视线找到目镜那片镜，
 *    按出瞳、视场光阑、畸变、色差、分划算出镜内的颜色（亮度，同去掉预曝光的镜外一起由引擎乘回预曝光，再一起过色调映射）；
 *    镜外按第一人称的进度模糊、加暗角，模糊另按眼睛离开瞄具多少收掉（拉栓时眼睛让开了）。
 *    合成在 TSR 之后：镜内的画面放大了四倍，交给主画面的时间性抗锯齿会拖糊
 *
 * 不在第一人称时不拍。手上的枪有瞄具时合成一直挂在后处理里、原样返回（PassThrough）：开镜时后处理的结构不变，不在开镜那一刻换路。
 * 没有瞄具的枪不挂，没有开销。发行版也有（不是调试）
 */
UCLASS(Transient)
class CASEGINKGO_API UGinkgoScopeView : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * @brief 这一帧：放好场景捕获、排它拍，合成的材质加进主画面的后处理
	 * @param Owner - 场景捕获组件的拥有者（玩家相机所在的角色）；Update 按本帧的 Sight 单独设置捕获的世界变换
	 * @param Pawn - 视角目标：拍的时候藏起来（角色与手上的枪都是它的组件）
	 * @param Sight - 这一帧瞄具在世界里的位置（枪的网格这一帧最终的变换算的）
	 * @param Scope - 那一具瞄具
	 * @param FirstPerson - 第一人称的进度（0–1）：镜外的模糊、暗角按它淡入
	 * @param EyeAway - 拉栓时眼睛让开了多少（0–1，枪栓这一帧的样子，Weapon.Value.SightClearance）：镜外的模糊按它收掉
	 *                  （2026-11-12 你说的：开镜拉栓时其实已经退出瞄具了，模糊可以取消。眼睛不再透过瞄具对焦在远处）
	 * @param ViewportSize - 主画面的像素：拍的分辨率按镜子在屏幕上的大小取，合成按它的宽高比还原视线
	 * @param InOutView - 主画面这一帧的视角（相机的输出）：读它的位置、朝向、视场，加上合成的后处理
	 */
	void Update(AActor& Owner, const AActor* Pawn, const Ginkgo::Scope::FSightFrame& Sight, const FGinkgoScope& Scope, float FirstPerson, float EyeAway,
		const FVector2D& ViewportSize, FMinimalViewInfo& InOutView);

	/** @brief 不在第一人称：不拍，下次开始时当作镜头切换（拍的那个视角的时间性的历史不用） */
	void Deactivate();

	/**
	 * @brief 手上的枪有瞄具、不在第一人称：合成照样挂在主画面的后处理里，第一人称的进度给 0，着色器原样返回主画面（不拍）。
	 *        后处理里多一个"泛光之前"的材质时引擎换一条路做半分辨率的画面（TSR 不再顺手出，另做一次降采样；泛光、自动曝光读它），
	 *        第一次走那条路的那一帧画面四周是一圈糊的棕边（2026-11-12 量的：只在第一次开镜，关了后处理材质就没有，同 PSO 预编无关）。
	 *        一直挂着：开镜时后处理不变，那一帧落在拿到枪的时候（开局）。代价是拿着带瞄具的枪时多一遍全屏的后处理与降采样
	 * @param InOutView - 主画面这一帧的视角：加上合成的后处理
	 */
	void PassThrough(const FGinkgoScope& Scope, FMinimalViewInfo& InOutView);

	/** 这一帧拍了没有（调试与回放） */
	bool IsActive() const { return bActive; }

	/** 拍的分辨率（正方形的边长；没拍过是 0） */
	int32 GetCaptureSize() const { return TargetSize; }

	/**
	 * @brief 拍的分辨率：镜子在屏幕上的直径（像素）× 余量 × 超采样，按 32 取整，夹到上下限
	 * @param ScreenHeight - 主画面的高（像素）
	 * @param TanHalfFovY - 主画面竖直半视场的正切
	 */
	static int32 ComputeCaptureSize(const FGinkgoScope& Scope, double ScreenHeight, double TanHalfFovY);

private:
	/** 第一次用、或者分辨率变了：建场景捕获与它写的纹理 */
	void EnsureCapture(AActor& Owner, int32 Size);

	/** 合成的材质：瞄具配的那个；读不到时不合成（只有第一人称，没有镜内画面），只报一次 */
	UMaterialInstanceDynamic* EnsureComposite(const FGinkgoScope& Scope);

	/** 首次需要降低镜外分辨率时创建。退出、切换相机或捕获失效时立即恢复比例，扩展由本瞄具持有。 */
	TSharedPtr<FGinkgoScopeResolutionExtension, ESPMode::ThreadSafe> ResolutionExtension;

	/** 从物镜位置拍摄的临时场景捕获组件，视图停用时停止请求捕获。 */
	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> Capture;

	/** 镜内 HDR 纹理，尺寸按输出画面中镜片的直径计算。 */
	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> Target;

	/** 当前瞄具的合成材质实例，玩家相机在提交视图前更新其参数。 */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Composite;

	/** 合成的材质的父材质：瞄具换了材质就重建 */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> CompositeParent;

	/** 当前镜内纹理的边长（像素），初始为 0。 */
	int32 TargetSize = 0;

	/** 本次更新已排入捕获，停用后下次开镜将重置捕获的时间历史。 */
	bool bActive = false;

	/** 已报告过合成材质缺失，避免每帧重复记录。 */
	bool bReportedMissingMaterial = false;

	/** 这一次进第一人称写过光学的数没有（测量读这一行） */
	bool bLoggedOptics = false;

	/** 拍的时候藏起来的视角目标：换了才改 */
	TWeakObjectPtr<const AActor> HiddenPawn;
};
