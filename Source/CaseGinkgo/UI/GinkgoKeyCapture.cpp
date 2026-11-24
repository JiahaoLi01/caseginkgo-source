#include "UI/GinkgoKeyCapture.h"

#include "Input/Events.h"

namespace
{
	/** 开始等键以后这么久（秒）之内的鼠标键不算：够双击的第二下 */
	constexpr double MouseGraceSeconds = 0.25;
}

FGinkgoKeyCapture::FGinkgoKeyCapture(FOnKey InOnKey)
	: OnKey(MoveTemp(InOnKey))
	, StartSeconds(FPlatformTime::Seconds())
{
}

bool FGinkgoKeyCapture::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	// 按键重复不算：打开等键用的那个键可能还按着
	if (!InKeyEvent.IsRepeat())
	{
		OnKey.ExecuteIfBound(InKeyEvent.GetKey());
	}
	return true;
}

bool FGinkgoKeyCapture::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	return MouseKey(MouseEvent.GetEffectingButton());
}

bool FGinkgoKeyCapture::HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	return MouseKey(MouseEvent.GetEffectingButton());
}

bool FGinkgoKeyCapture::HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InWheelEvent, const FPointerEvent* InGestureEvent)
{
	if (!InGestureEvent || InGestureEvent->GetGestureType() == EGestureEvent::None)
	{
		MouseKey(InWheelEvent.GetWheelDelta() < 0.f ? EKeys::MouseScrollDown : EKeys::MouseScrollUp);
	}
	return true;
}

bool FGinkgoKeyCapture::MouseKey(const FKey& Key)
{
	if (FPlatformTime::Seconds() - StartSeconds >= MouseGraceSeconds)
	{
		OnKey.ExecuteIfBound(Key);
	}
	return true;
}
