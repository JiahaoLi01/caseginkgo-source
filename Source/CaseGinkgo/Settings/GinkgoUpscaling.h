#pragma once

#include "CoreMinimal.h"

/** 本次游戏视口可用的超分和插帧模式。分别向渲染插件查询，供设置页与实际应用共用。 */
struct CASEGINKGO_API FGinkgoUpscaling
{
	/** 可用的 DLSS 超分档位，首项始终为 off。 */
	TArray<FString> SuperResolution = {TEXT("off")};

	/** 可用的帧生成倍率，首项始终为 off。 */
	TArray<FString> FrameGeneration = {TEXT("off")};

	/** 超分不可用时向玩家说明原因；空串表示可选。 */
	FString SuperResolutionReason;

	/** 当前交换链无法插帧时的原因；空串表示可选。 */
	FString FrameGenerationReason;

	/** 当前驱动与显示配置能否同时使用帧生成与垂直同步。 */
	bool bFrameGenerationWithVSync = true;

	static FGinkgoUpscaling Query(const UWorld* World);

	/** 从本机 GameUserSettings.ini 读取偏好，在当前视口重新检查后应用；硬件变化时保留原偏好。 */
	static void ApplySaved(const UWorld* World);

	/** 立即应用到当前游戏视口。切出 PIE 时用 off 释放本次游戏的比例覆盖。 */
	static void Apply(const UWorld* World, const FString& Resolution, const FString& FrameGenerationMode);

	/** 读取或仅更新指定的本机偏好。调用方在确认成功后一并写盘。 */
	static FString ReadPreference(const TCHAR* Key);
	static void SetPreference(const TCHAR* Key, const FString& Value);
};
