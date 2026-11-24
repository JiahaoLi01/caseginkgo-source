#include "GinkgoCameraDebug.h"

#include "Camera/GinkgoCameraConfig.h"
#include "Camera/GinkgoCameraMath.h"
#include "Camera/GinkgoPlayerCameraManager.h"
#include "Debug/DebugDrawService.h"
#include "DrawDebugHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Utilities/Debug/DebugRollingCurve.h"

namespace
{
	TAutoConsoleVariable<int32> CVarShowCamera(
		TEXT("ginkgo.showCamera"),
		0,
		TEXT("显示相机管线：0 关，1 跟随点，2 再加上碰撞的扫掠与相机本身，画面换到角色右侧上方的观察位置。\n")
		TEXT("跟随点：白点是胶囊上的跟随点，黄点是加了速度前馈的目标，绿球是平滑后的（相机绕它转），白圈是水平最大滞后；屏幕上写出滞后与压缩。\n")
		TEXT("碰撞：灰线是两段扫掠（胶囊中心到跟随点、跟随点到期望位置），灰球是期望位置，红球是挡住的地方，青色是相机。")
		TEXT("这些在玩家的画面里画在镜头上，所以只在观察位置画；2 时只换画面，相机本身照常计算，回放轨迹里记下的相机输出这时是观察位置。"));

	TAutoConsoleVariable<FString> CVarPoseView(
		TEXT("ginkgo.camera.poseView"),
		TEXT(""),
		TEXT("截图看姿势的近景：\"偏航 俯角 距离 高度 往前\"（空格隔开），空的时候关。相机放在角色周围，对着注视点：")
		TEXT("偏航从角色的朝向量（0 在正前方看着角色，90 在它右侧，-90 在左侧，180 在背后），俯角是往下看多少度，距离是到注视点多远（cm），")
		TEXT("注视点在胶囊中心上方这么高（cm，不写为 0）、沿角色的朝向往前这么远（cm，不写为 0：看枪口前面的东西，例如弹道的烟）。")
		TEXT("只换画面，相机本身照常计算；回放轨迹里记下的相机输出这时是近景的位置。"));

	/** 两层的开关一改就交给相机（命令行的 -ExecCmds 也是）：相机在这一帧的更新里就用上 */
	void OnLayersChanged(IConsoleVariable* Variable);

	TAutoConsoleVariable<int32> CVarModifiers(
		TEXT("ginkgo.camera.modifiers"),
		1,
		TEXT("相机的参数层：1 开（默认），0 关（直接用基础值，修改器的权重与参数弹簧清空，再开时直接放到位）。对照用。"),
		FConsoleVariableDelegate::CreateStatic(&OnLayersChanged));

	TAutoConsoleVariable<int32> CVarOverlay(
		TEXT("ginkgo.camera.overlay"),
		1,
		TEXT("相机的叠加层（滑铲的震动、冲刺落脚的震动、开镜的晃动）：1 开（默认），0 关（输出 0、弹簧清空）。对照用。"),
		FConsoleVariableDelegate::CreateStatic(&OnLayersChanged));

	TAutoConsoleVariable<int32> CVarShowLayers(
		TEXT("ginkgo.camera.showLayers"),
		0,
		TEXT("屏幕上列出相机的两层：参数层每个修改器的权重、值与屏蔽，合成后与弹簧后的臂长、FOV；叠加层每个效果的输出、包络与吃过的事件，这一帧取出的事件。"));

	TAutoConsoleVariable<int32> CVarShowShoulder(
		TEXT("ginkgo.camera.showShoulder"),
		0,
		TEXT("ADS 换肩检测：0 关，1 绘制真实配置射线与左右得分，2 另切到侧上方观察整个扇面。\n")
		TEXT("青色为左肩起点与射线，橙色为右肩；红点标出阻挡，灰线为剩余射程。\n")
		TEXT("冷却时冻结最近采样并显示 COOLDOWN / FROZEN 与倒计时，不增加射线查询；采样位置不会跟随当前相机伪更新。2 只改变显示视角，检测仍来自玩家相机。"));

	TAutoConsoleVariable<int32> CVarCurves(
		TEXT("ginkgo.camera.curves"),
		0,
		TEXT("相机叠加层的滚动曲线（右下角，最近 4 s）：叠加的俯仰（度）、第一个冲量弹簧（滑铲的震动）俯仰的角速度（度/秒，弹簧的 v）、参数层的 FOV 减基础值（度）。"));

	void OnLayersChanged(IConsoleVariable*)
	{
		AGinkgoPlayerCameraManager::SetLayersEnabled(CVarModifiers.GetValueOnGameThread() != 0, CVarOverlay.GetValueOnGameThread() != 0);
	}

	/** 观察位置：在臂的中点的右方、稍后、上方，整根臂、角色与相机都在画面里 */
	const FVector ObserverOffset(-100.0, 420.0, 160.0);

	/** 探测球画几段：够看出是个球，又不至于太密 */
	constexpr int32 SphereSegments = 12;

	/** ginkgo.camera.curves 的三条滚动曲线：俯仰的偏差（度）、第一个冲量弹簧俯仰的角速度（度/秒）、参数层的 FOV（度，减去基础值）。跟着本地相机 */
	struct FOverlayCurves
	{
		TWeakObjectPtr<const AGinkgoPlayerCameraManager> Camera;
		FDebugRollingCurve OverlayPitch{4.0};
		FDebugRollingCurve ShakeVelocity{4.0};
		FDebugRollingCurve FieldOfView{4.0};
	};
	FOverlayCurves Curves;

	FDelegateHandle DebugViewHandle;
	FDelegateHandle DebugDrawHandle;

	/** 屏幕消息的键：每行一个，不和别的调试消息抢 */
	uint64 LayerLineKey(const AActor& Owner, const int32 Line)
	{
		return (static_cast<uint64>(Owner.GetUniqueID()) << 8) + 0x40 + Line;
	}

	/** ginkgo.camera.showLayers：屏幕上列出两层（屏幕文字用英文：引擎的屏幕调试字体没有中文字形） */
	void DrawLayers(const AGinkgoPlayerCameraManager& Camera)
	{
		const UGinkgoCameraConfig& Config = Camera.GetConfig();
		const AGinkgoPlayerCameraManager::FDebugFrame& Frame = Camera.GetDebugFrame();
		const FGinkgoCameraParams& Params = Camera.GetParams();
		int32 Line = 0;
		const auto AddLine = [&Camera, &Line](const FString& Text, const FColor Color)
		{
			GEngine->AddOnScreenDebugMessage(LayerLineKey(Camera, Line++), 0.f, Color, Text);
		};
		const FGinkgoCameraParams& Target = Camera.GetParamLayer().GetTarget();
		AddLine(FString::Printf(TEXT("params%s   arm %.1f cm (target %.1f)   fov %.2f (target %.2f)   roll %+.2f (target %+.2f)   clock %.3f s"),
			CVarModifiers.GetValueOnGameThread() != 0 ? TEXT("") : TEXT(" OFF"), Params.ArmLength, Target.ArmLength, Params.FieldOfView, Target.FieldOfView,
			Params.Roll, Target.Roll, Camera.GetClock()), FColor::Cyan);
		const TConstArrayView<FGinkgoCameraParamLayer::FModifierState> Modifiers = Camera.GetParamLayer().GetModifierStates();
		for (int32 Index = 0; Index < Modifiers.Num() && Index < Config.Modifiers.Num(); ++Index)
		{
			const FGinkgoCameraParamModifier& Modifier = Config.Modifiers[Index];
			const FGinkgoCameraParamLayer::FModifierState& State = Modifiers[Index];
			AddLine(FString::Printf(TEXT("  %s -> %s %s %+.2f   weight %.3f%s%s%s"), *Modifier.Name.ToString(), *Modifier.Param.Name.ToString(),
				Modifier.Blend == EGinkgoCameraBlend::Delta ? TEXT("delta") : TEXT("override"), State.Value, State.Weight, State.bTarget ? TEXT("  on") : TEXT(""),
				State.bLatched ? TEXT("  latched") : TEXT(""), State.Block > 0.f ? *FString::Printf(TEXT("  blocked %.2f"), State.Block) : TEXT("")),
				State.Weight > 0.f ? FColor::White : FColor(150, 150, 150));
		}
		const FGinkgoCameraOverlayOutput& Total = Frame.Overlay;
		AddLine(FString::Printf(TEXT("overlay%s (%s)   pitch %+.3f  yaw %+.3f  roll %+.3f  fov %+.3f"), CVarOverlay.GetValueOnGameThread() != 0 ? TEXT("") : TEXT(" OFF"),
			Config.OverlayPivot == EGinkgoCameraOverlayPivot::Pivot ? TEXT("about pivot") : TEXT("about camera"), Total.Rotation.Pitch, Total.Rotation.Yaw,
			Total.Rotation.Roll, Total.FieldOfView), FColor::Cyan);
		const TConstArrayView<FGinkgoCameraOverlayState> Effects = Camera.GetOverlayLayer().GetStates();
		for (int32 Index = 0; Index < Effects.Num() && Index < Config.OverlayEffects.Num(); ++Index)
		{
			const UGinkgoCameraOverlayEffect* Effect = Config.OverlayEffects[Index];
			const FGinkgoCameraOverlayState& State = Effects[Index];
			const bool bNoise = Effect && Effect->IsA<UGinkgoCameraNoiseSway>();
			AddLine(FString::Printf(TEXT("  %s   pitch %+.3f  yaw %+.3f  roll %+.3f%s"), Effect ? *Effect->Name.ToString() : TEXT("(none)"), State.Output.Rotation.Pitch,
				State.Output.Rotation.Yaw, State.Output.Rotation.Roll,
				bNoise ? *FString::Printf(TEXT("   envelope %.3f"), State.Envelope.Value) : *FString::Printf(TEXT("   impulses %d"), State.ImpulseCount)),
				Effect && Effect->bEnabled ? FColor::White : FColor(150, 150, 150));
		}
		const UWorld* World = Camera.GetWorld();
		FString Events;
		for (const FGinkgoCameraEvent& Event : Frame.Events)
		{
			Events += FString::Printf(TEXT("  %s at %+.4f s (%.0f)"), *Event.Tag.ToString(), Event.Time - (World->GetTimeSeconds() - World->GetDeltaSeconds()), Event.Magnitude);
		}
		AddLine(FString::Printf(TEXT("events this frame:%s"), Events.IsEmpty() ? TEXT(" none") : *Events), FColor::Cyan);
	}

	/**
	 * @brief ginkgo.showCamera：世界里画出跟随点、前馈的目标与两段扫掠，屏幕上写出滞后与压缩；2 时把画面换到侧面的观察位置
	 * @param OutVT - 这一帧的输出，观察位置时改写它的位置与朝向
	 */
	void DrawPipeline(const AGinkgoPlayerCameraManager& Camera, FTViewTarget& OutVT, const int32 Mode)
	{
		const UWorld* World = Camera.GetWorld();
		const AGinkgoPlayerCameraManager::FDebugFrame& Frame = Camera.GetDebugFrame();
		const FColor Gray(170, 170, 170);
		// 跟随点画在前景：在角色身体里面也看得见
		constexpr uint8 Foreground = SDPG_Foreground;

		// 跟随点：平滑的是胶囊中心，跟随点偏移在平滑之后加，所以三个点都加同一个偏移
		const FVector CenterToPivot = Frame.Pivot - Frame.FollowCenter;
		const FVector RawPivot = Frame.CapsuleCenter + CenterToPivot;
		const FVector FeedforwardPivot = Frame.FeedforwardTarget + CenterToPivot;
		DrawDebugPoint(World, RawPivot, 10.f, FColor::White, false, -1.f, Foreground);
		DrawDebugPoint(World, FeedforwardPivot, 10.f, FColor::Yellow, false, -1.f, Foreground);
		DrawDebugLine(World, RawPivot, FeedforwardPivot, FColor::Yellow, false, -1.f, Foreground, 1.f);
		DrawDebugSphere(World, Frame.Pivot, 6.f, 8, FColor::Green, false, -1.f, Foreground, 1.f);
		DrawDebugLine(World, RawPivot, Frame.Pivot, FColor::Green, false, -1.f, Foreground, 1.f);
		const float MaxLag = Camera.GetConfig().Follow.MaxLag;
		if (MaxLag > 0.f)
		{
			DrawDebugCircle(World, RawPivot, MaxLag, 48, FColor::White, false, -1.f, 0, 1.f, FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
		}

		// 碰撞：第一段从胶囊中心到跟随点，第二段从安全的跟随点到期望位置。第二段穿过相机本身，只在观察位置画
		DrawDebugLine(World, Frame.CapsuleCenter, Frame.Pivot, Frame.bPivotBlocked ? FColor::Red : Gray, false, -1.f, 0, 1.f);
		if (Frame.bPivotBlocked)
		{
			DrawDebugSphere(World, Frame.SafePivot, Frame.ProbeRadius, SphereSegments, FColor::Red, false, -1.f, 0, 1.f);
		}
		if (Mode >= 2)
		{
			const FVector BoomEnd = Frame.SafePivot + Frame.Boom;
			DrawDebugLine(World, Frame.SafePivot, BoomEnd, Gray, false, -1.f, 0, 1.f);
			DrawDebugSphere(World, BoomEnd, Frame.ProbeRadius, SphereSegments, Gray, false, -1.f, 0, 1.f);
			if (Frame.bBoomBlocked)
			{
				DrawDebugSphere(World, Ginkgo::Camera::ApplyCompression(Frame.SafePivot, Frame.Boom, Frame.RequiredCompression), Frame.ProbeRadius, SphereSegments,
					FColor::Red, false, -1.f, 0, 1.f);
			}
			DrawDebugSphere(World, Frame.Location, Frame.ProbeRadius, SphereSegments, FColor::Cyan, false, -1.f, 0, 1.5f);
		}

		// 屏幕文字用英文：引擎的屏幕调试字体没有中文字形
		if (GEngine)
		{
			const FVector Lag = Frame.FollowCenter - Frame.CapsuleCenter;
			const FString Text = FString::Printf(TEXT("camera   lag %.1f cm (horizontal %.1f, vertical %+.1f)%s   feedforward %.1f cm   boom %.1f cm   ")
				TEXT("compression %.1f cm (required %.1f, %+.0f cm/s)%s%s"), Lag.Size(), Lag.Size2D(), Lag.Z, Frame.bFollowClamped ? TEXT(" clamped") : TEXT(""),
				(Frame.FeedforwardTarget - Frame.CapsuleCenter).Size(), Frame.Boom.Size(), Frame.Compression, Frame.RequiredCompression, Frame.CompressionVelocity,
				Frame.bPivotBlocked ? TEXT("   pivot blocked") : TEXT(""), Frame.bCut ? TEXT("   cut") : TEXT(""));
			GEngine->AddOnScreenDebugMessage(static_cast<uint64>(Camera.GetUniqueID()), 0.f, FColor::Cyan, Text);
		}

		if (Mode >= 2)
		{
			// 从侧面看：画出相机本身，画面换到观察位置
			DrawDebugCamera(World, Frame.Location, Frame.Rotation, Frame.FieldOfView, 4.f, FColor::Cyan);
			const FVector Focus = (Frame.Pivot + Frame.Location) * 0.5;
			const FVector Observer = Focus + FRotator(0.0, Frame.Rotation.Yaw, 0.0).RotateVector(ObserverOffset);
			OutVT.POV.Location = Observer;
			OutVT.POV.Rotation = (Focus - Observer).Rotation();
		}
	}

	/** @brief 只画行为通道的真实采样；与渲染帧率无关，不在冷却或间隔中补发查询 */
	void DrawShoulder(const AGinkgoPlayerCameraManager& Camera, FTViewTarget& OutVT, const int32 Mode)
	{
		const FGinkgoCameraShoulder& Shoulder = Camera.GetShoulder();
		const FGinkgoCameraShoulder::FDebugSample& Sample = Shoulder.GetDebugSample();
		const UWorld* World = Camera.GetWorld();
		const double Now = World->GetTimeSeconds();
		const double Cooldown = Shoulder.GetCooldownRemaining(Now);
		const bool bHasSample = !Sample.Rays.IsEmpty();
		const FVector2D Scores = Shoulder.GetScores();
		if (GEngine)
		{
			// 分数属于快照里的两个视点，单列采样前与决策后的肩位，便于查看冷却中冻结的换肩依据。
			const FString Status = !Shoulder.IsAdsActive() ? TEXT("WAIT ADS") : Cooldown > 0 ? TEXT("COOLDOWN / FROZEN")
				: !bHasSample ? TEXT("NO SAMPLE") : TEXT("SAMPLING");
			const FString Text = FString::Printf(TEXT("shoulder %s | L %.2f : R %.2f | sampled from %s | %s | CD %.2fs | sample age %.2fs\n")
				TEXT("L origin cyan / R origin orange / HIT red / remaining gray | showShoulder 2: observer"),
				Shoulder.IsRightShoulder() ? TEXT("R") : TEXT("L"), Scores.X, Scores.Y,
				bHasSample ? (Sample.bOnRight ? TEXT("R") : TEXT("L")) : TEXT("-"), *Status, Cooldown, bHasSample ? Now - Sample.Time : 0.0);
			GEngine->AddOnScreenDebugMessage((static_cast<uint64>(Camera.GetUniqueID()) << 8) + 0x70, 0.f, FColor::White, Text);
		}
		if (!bHasSample)
		{
			return;
		}
		for (const FGinkgoCameraShoulder::FDebugRay& Ray : Sample.Rays)
		{
			const FColor SideColor = Ray.bRightOrigin ? FColor::Orange : FColor::Cyan;
			DrawDebugLine(World, Ray.bRightOrigin ? Sample.RightOrigin : Sample.LeftOrigin, Ray.HitPoint, SideColor, false, -1.f, SDPG_Foreground, 1.5f);
			if (Ray.bBlocked)
			{
				DrawDebugLine(World, Ray.HitPoint, Ray.End, FColor(100, 100, 100), false, -1.f, SDPG_Foreground, 0.5f);
				DrawDebugSphere(World, Ray.HitPoint, 2.5f, 8, FColor::Red, false, -1.f, SDPG_Foreground);
			}
			DrawDebugPoint(World, Ray.End, 6.f, SideColor, false, -1.f, SDPG_Foreground);
			const FString Label = FString::Printf(TEXT("%s#%d %+.1f +%.2f"), Ray.bRightOrigin ? TEXT("R") : TEXT("L"), Ray.Index + 1, Ray.YawOffset, Ray.Score);
			DrawDebugString(World, Ray.End + FVector(0, 0, 4), Label, nullptr, SideColor, 0.f, true);
		}
		DrawDebugPoint(World, Sample.LeftOrigin, 10.f, FColor::Cyan, false, -1.f, SDPG_Foreground);
		DrawDebugPoint(World, Sample.RightOrigin, 10.f, FColor::Orange, false, -1.f, SDPG_Foreground);
		if (Mode >= 2)
		{
			// 射线从镜头发出，在原视角下几乎投成点；观察位置专用于查看采样扇面，不回写行为通道。
			const FVector Focus = (Sample.LeftOrigin + Sample.RightOrigin) * 0.5 + Sample.Rotation.Vector() * 100.0;
			const FVector Observer = Focus + FRotator(0, Sample.Rotation.Yaw, 0).RotateVector(FVector(-160, 340, 300));
			OutVT.POV.Location = Observer;
			OutVT.POV.Rotation = (Focus - Observer).Rotation();
			OutVT.POV.FOV = 90.f;
			// 观察镜头不贴瞄具；只在调试输出中去掉倍镜合成与景深，玩家相机和检测保持原样。
			OutVT.POV.PostProcessBlendWeight = 0.f;
		}
	}

	/**
	 * @brief ginkgo.camera.poseView：画面换到角色周围的近景（截图看姿势），相机本身照常计算
	 * @param OutVT - 这一帧的输出，开着时改写它的位置与朝向
	 */
	void ApplyPoseView(FTViewTarget& OutVT)
	{
		TArray<FString> Parts;
		CVarPoseView.GetValueOnGameThread().ParseIntoArrayWS(Parts);
		const APawn* Pawn = Cast<APawn>(OutVT.Target);
		if (Parts.Num() < 3 || !Pawn)
		{
			return;
		}
		const double Yaw = FCString::Atod(*Parts[0]);
		const double Pitch = FCString::Atod(*Parts[1]);
		const double Distance = FCString::Atod(*Parts[2]);
		const double Height = Parts.Num() >= 4 ? FCString::Atod(*Parts[3]) : 0.0;
		const double Forward = Parts.Num() >= 5 ? FCString::Atod(*Parts[4]) : 0.0;
		const FVector Focus = Pawn->GetActorLocation() + FVector(0.0, 0.0, Height) + FRotator(0.0, Pawn->GetActorRotation().Yaw, 0.0).Vector() * Forward;
		// 从注视点往相机的方向：偏航绕角色的朝向转，俯角为正时相机在上方
		const FVector Location = Focus + FRotator(Pitch, Pawn->GetActorRotation().Yaw + Yaw, 0.0).Vector() * Distance;
		OutVT.POV.Location = Location;
		OutVT.POV.Rotation = (Focus - Location).Rotation();
	}

	/** @brief ginkgo.camera.curves：采样叠加层的 x 与 v（每帧相机更新后） */
	void SampleCurves(const AGinkgoPlayerCameraManager& Camera)
	{
		if (Curves.Camera.Get() != &Camera)
		{
			Curves = FOverlayCurves();
			Curves.Camera = &Camera;
		}
		const double Now = Camera.GetWorld()->GetTimeSeconds();
		Curves.OverlayPitch.AddSample(Now, Camera.GetDebugFrame().Overlay.Rotation.Pitch, FColor::White);
		double Velocity = 0.0;
		const UGinkgoCameraConfig& Config = Camera.GetConfig();
		const TConstArrayView<FGinkgoCameraOverlayState> Effects = Camera.GetOverlayLayer().GetStates();
		for (int32 Index = 0; Index < Effects.Num() && Index < Config.OverlayEffects.Num(); ++Index)
		{
			if (Config.OverlayEffects[Index] && Config.OverlayEffects[Index]->IsA<UGinkgoCameraImpulseShake>())
			{
				Velocity = Effects[Index].Rotation.Velocity.X;
				break;
			}
		}
		Curves.ShakeVelocity.AddSample(Now, static_cast<float>(Velocity), FColor::Yellow);
		Curves.FieldOfView.AddSample(Now, Camera.GetParams().FieldOfView - Config.BaseParams.FieldOfView, FColor::Cyan);
	}

	/** @brief 画布上画出 ginkgo.camera.curves 的三条滚动曲线（本地相机的） */
	void DrawCurves(UCanvas* Canvas, APlayerController* PlayerController)
	{
		if (!Canvas || !PlayerController || CVarCurves.GetValueOnGameThread() == 0 || !Curves.Camera.IsValid()
			|| PlayerController->PlayerCameraManager != Curves.Camera.Get())
		{
			return;
		}
		const FVector2D Size(FMath::Min(520.f, Canvas->ClipX * 0.4f), 110.f);
		const float X = Canvas->ClipX - Size.X - 40.f;
		float Y = Canvas->ClipY - 3.f * (Size.Y + 12.f) - 40.f;
		Curves.OverlayPitch.Draw(*Canvas, FVector2D(X, Y), Size, TEXT("overlay pitch x (deg)"), {}, true);
		Y += Size.Y + 12.f;
		Curves.ShakeVelocity.Draw(*Canvas, FVector2D(X, Y), Size, TEXT("slide shake pitch v (deg/s)"), {}, true);
		Y += Size.Y + 12.f;
		Curves.FieldOfView.Draw(*Canvas, FVector2D(X, Y), Size, TEXT("param fov - base (deg)"), {}, true);
	}

	/** 相机这一帧算完以后：记下默认值、套用本地的调参文件，画调试，换画面 */
	void OnDebugView(AGinkgoPlayerCameraManager& Camera, FTViewTarget& OutVT)
	{
		Ginkgo::Debug::Camera::OnCameraSeen(Camera);
		if (CVarCurves.GetValueOnGameThread() != 0)
		{
			SampleCurves(Camera);
		}
		if (CVarShowLayers.GetValueOnGameThread() != 0 && GEngine)
		{
			DrawLayers(Camera);
		}
		if (const int32 Mode = CVarShowCamera.GetValueOnGameThread(); Mode > 0)
		{
			DrawPipeline(Camera, OutVT, Mode);
		}
		if (const int32 Mode = CVarShowShoulder.GetValueOnGameThread(); Mode > 0)
		{
			DrawShoulder(Camera, OutVT, Mode);
		}
		ApplyPoseView(OutVT);
	}
}

void Ginkgo::Debug::Camera::Startup()
{
	DebugViewHandle = AGinkgoPlayerCameraManager::OnDebugView.AddStatic(&OnDebugView);
	DebugDrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&DrawCurves));
	StartupTuning();
}

void Ginkgo::Debug::Camera::Shutdown()
{
	ShutdownTuning();
	UDebugDrawService::Unregister(DebugDrawHandle);
	AGinkgoPlayerCameraManager::OnDebugView.Remove(DebugViewHandle);
	Curves = FOverlayCurves();
}

AGinkgoPlayerCameraManager* Ginkgo::Debug::Camera::FindLocalCamera(const UWorld* World)
{
	const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	return Controller ? Cast<AGinkgoPlayerCameraManager>(Controller->PlayerCameraManager) : nullptr;
}

UGinkgoCameraConfig& Ginkgo::Debug::Camera::GetMutableConfig(const AGinkgoPlayerCameraManager& Camera)
{
	// 相机只读它：配置是资产或类默认值，调试时直接改，相机下一帧就读到
	return const_cast<UGinkgoCameraConfig&>(Camera.GetConfig());
}
