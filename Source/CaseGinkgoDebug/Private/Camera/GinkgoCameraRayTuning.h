#pragma once

#include "CoreMinimal.h"
#include "Camera/GinkgoCameraShoulder.h"

class FJsonValue;
class FProperty;

namespace Ginkgo::Debug::Camera
{
	/** 射线列表作为一个调参项读写；不展开成固定下标，增删后仍能整组保存、恢复默认 */
	bool IsRayList(const FProperty& Property);

	/** 页面与本地文件共用的 JSON 数组：每个元素含 angle（度）、length（cm）、score（分） */
	TSharedRef<FJsonValue> RaysToJson(const TArray<FGinkgoCameraShoulderRay>& Rays);

	/** 校验全部元素及数量后一次性替换 Out；失败保留原值，空数组合法 */
	bool RaysFromJson(const FJsonValue& Value, TArray<FGinkgoCameraShoulderRay>& Out);

	/** 确定性的紧凑 JSON 文本；调参面板据此比较当前、默认和已保存值 */
	FString ExportRays(const TArray<FGinkgoCameraShoulderRay>& Rays);

	/** 解析本地文件或控制台给出的完整列表，沿用 RaysFromJson 的原子校验 */
	bool ImportRays(const FString& Text, TArray<FGinkgoCameraShoulderRay>& Out);

	/** 旧版统一射程迁移到默认射线列表；显式的新列表优先，其他调参项保持原样 */
	void MigrateRayTuning(TMap<FString, FString>& Values, const TArray<FGinkgoCameraShoulderRay>& DefaultRays);
}
