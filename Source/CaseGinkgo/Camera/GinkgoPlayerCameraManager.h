#pragma once

#include "Camera/PlayerCameraManager.h"
#include "GinkgoPlayerCameraComponent.h"
#include "GinkgoPlayerCameraManager.generated.h"

/**
 * @class AGinkgoPlayerCameraManager : 本地玩家的视角选择与过渡（Docs/Design/CameraPipeline.md「求值时序与宿主」）
 * UpdateViewTarget 遇到启用的玩家相机组件，就请它求出目标的 POV；改装桌这样的普通相机走引擎原有的求值。
 * 混合由引擎推进，武器、交互和输入回放仍从这里读取玩家相机的结果。最终显示的画面取 GetCameraCacheView。
 */
UCLASS()
class CASEGINKGO_API AGinkgoPlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	/**
	 * @brief 从当前显示的画面平滑转到 Target。进桌途中退出时，反向过渡从上一帧已经混合过的 POV 起步
	 * @param Duration - 过渡时长（秒），使用三次曲线；出发端的画面固定，目的端继续求值
	 */
	void BlendTo(AActor& Target, float Duration);

	/** 传送或回放跳转后要求玩家相机重置弹簧和渲染历史，下一次求值时生效 */
	void RequestCameraCut();

	/** 以下查询转给玩家相机，工作台占据画面期间也可读取它上一次的结果 */
	const UGinkgoCameraConfig& GetConfig() const;
	FVector PredictViewLocation(const APawn& Pawn, const FRotator& ViewRotation) const;
	const FMinimalViewInfo& GetAimView() const { return GetPlayerCamera().GetAimView(); }
	const FGinkgoCameraParams& GetParams() const { return GetPlayerCamera().GetParams(); }
	/** 玩家相机的换肩行为状态；调试只读最近采样，不从观察镜头补做检测 */
	const FGinkgoCameraShoulder& GetShoulder() const { return GetPlayerCamera().GetShoulder(); }

	const FGinkgoCameraParamLayer& GetParamLayer() const { return GetPlayerCamera().GetParamLayer(); }
	const FGinkgoCameraOverlayLayer& GetOverlayLayer() const { return GetPlayerCamera().GetOverlayLayer(); }
	double GetClock() const { return GetPlayerCamera().GetClock(); }
	float GetFirstPersonAlpha() const { return GetPlayerCamera().GetFirstPersonAlpha(); }
	const Ginkgo::Scope::FSightFrame& GetSightFrame() const { return GetPlayerCamera().GetSightFrame(); }
	const UGinkgoScopeView* GetScopeView() const { return GetPlayerCamera().GetScopeView(); }
	FRotator SampleSightSway(double ClockTime) const { return GetPlayerCamera().SampleSightSway(ClockTime); }
	FRotator PredictSightFollow(double DeltaSeconds, float FirstPerson) const { return GetPlayerCamera().PredictSightFollow(DeltaSeconds, FirstPerson); }

#if !UE_BUILD_SHIPPING
	using FDebugFrame = UGinkgoPlayerCameraComponent::FDebugFrame;
	const FDebugFrame& GetDebugFrame() const { return GetPlayerCamera().GetDebugFrame(); }
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDebugView, AGinkgoPlayerCameraManager&, FTViewTarget&);
	static FOnDebugView OnDebugView;
	static void SetLayersEnabled(bool bModifiers, bool bOverlay) { UGinkgoPlayerCameraComponent::SetLayersEnabled(bModifiers, bOverlay); }
#endif

protected:
	//~ Begin APlayerCameraManager
	virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) override;
	//~ End APlayerCameraManager

	/** 控制器对玩家相机配置的覆盖。未指定时由角色组件选配置 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ginkgo|Camera")
	TObjectPtr<UGinkgoCameraConfig> CameraConfig;

private:
	/** 优先取上次求值的组件；尚未求值时查当前 Pawn，未附身时只读类默认值 */
	const UGinkgoPlayerCameraComponent& GetPlayerCamera() const;

	/** 上次求值的玩家相机组件，切换视角时用来停拍瞄具、恢复头部并清理暂停状态 */
	TWeakObjectPtr<UGinkgoPlayerCameraComponent> LastCamera;
};
