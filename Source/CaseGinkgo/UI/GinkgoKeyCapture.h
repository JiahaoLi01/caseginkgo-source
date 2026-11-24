#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"
#include "InputCoreTypes.h"

/**
 * @class FGinkgoKeyCapture : 改键时等玩家按下一个键
 * Slate 的输入预处理器，注册在最前面（weft 的手柄输入也是一个预处理器）：开着时键盘、鼠标键、滚轮与手柄的键按下都到这里，页面收不到，也就不会既绑键又操作页面。
 * 给的是引擎的 FKey，不用把页面的键名换算回去；手柄的键、鼠标的侧键与滚轮也一样。松开、指针移动与摇杆照常放过去（页面的手柄状态要跟着松开）。
 */
class FGinkgoKeyCapture final : public IInputProcessor
{
public:
	DECLARE_DELEGATE_OneParam(FOnKey, const FKey&);

	explicit FGinkgoKeyCapture(FOnKey InOnKey);

	//~ Begin IInputProcessor
	virtual void Tick(float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InWheelEvent, const FPointerEvent* InGestureEvent) override;
	virtual const TCHAR* GetDebugName() const override { return TEXT("FGinkgoKeyCapture"); }
	//~ End IInputProcessor

private:
	bool MouseKey(const FKey& Key);

	FOnKey OnKey;

	/** 开始等键的时候：刚开始的一小会儿不认鼠标键，双击键位时第二下不该绑上去 */
	double StartSeconds = 0.0;
};
