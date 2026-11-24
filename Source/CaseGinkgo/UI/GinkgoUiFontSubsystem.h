#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "GinkgoUiFontSubsystem.generated.h"

/** 引擎启动时向 Weft 注册项目随包字体，早于任何游戏页面；避免在首次地点弹出时才注册而被运行时拒绝。 */
UCLASS()
class CASEGINKGO_API UGinkgoUiFontSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 成功注册的字体数，用于启动诊断和打包/自动化检查；无 Weft 运行时为 0。 */
	int32 GetRegisteredFaceCount() const { return RegisteredFaceCount; }

private:
	/** Initialize 写入，本引擎生命周期保持；Weft 子系统拥有注册后字体，不由地图/玩家重复加载。 */
	int32 RegisteredFaceCount = 0;
};
