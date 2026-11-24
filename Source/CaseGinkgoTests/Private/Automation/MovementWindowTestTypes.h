#pragma once

#include "CoreMinimal.h"
#include "Movement/Windows/GinkgoMovementWindow.h"
#include "MovementWindowTestTypes.generated.h"

/**
 * @class UGinkgoTestBehaviorWindow : 测试用的带行为的窗口
 * 只处理一种事件：速度 X 加上一个量，按设置消费事件、用掉自己；处理时记下名字与看到的状态，测处理顺序与上下文。
 * UHT 不认 WITH_DEV_AUTOMATION_TESTS，这个类不放在它里面；测试模块本来就不进发行版
 */
UCLASS(HideDropdown, NotBlueprintable)
class UGinkgoTestBehaviorWindow : public UGinkgoMovementWindow
{
	GENERATED_BODY()

public:
	EGinkgoWindowEvent HandledEvent = EGinkgoWindowEvent::Jumped;

	bool bConsume = false;

	bool bEndSelf = false;

	float VelocityDeltaX = 0.f;

	/** 处理事件时记进调用记录的名字（对象名由 NewObject 自动生成，重复跑测试时不会撞名） */
	FString LogName;

	/** 调用记录；空着不记 */
	TArray<FString>* CallLog = nullptr;

	/** 最近一次处理事件时看到的汇总标签、阶段已过时间与参数 */
	mutable FGameplayTagContainer SeenTags;

	mutable float SeenPhaseTime = -1.f;

	mutable float SeenParam = -1.f;

	virtual FGinkgoWindowBehaviorResult ApplyBehavior(const FGinkgoWindowEvent& Event, const float PhaseTime, const float Param, FGinkgoWindowEventContext& Context) const override
	{
		if (Event.Type != HandledEvent)
		{
			return {};
		}
		if (CallLog)
		{
			CallLog->Add(LogName);
		}
		SeenTags = Context.Tags ? *Context.Tags : FGameplayTagContainer();
		SeenPhaseTime = PhaseTime;
		SeenParam = Param;
		Context.Velocity.X += VelocityDeltaX;
		return {bConsume, bEndSelf};
	}
};
