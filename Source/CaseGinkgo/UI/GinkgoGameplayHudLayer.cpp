#include "UI/GinkgoGameplayHudLayer.h"

#include "Engine/GameViewportClient.h"
#include "Widgets/SOverlay.h"

FGinkgoGameplayHudLayer::FGinkgoGameplayHudLayer(UGameViewportClient& InViewport, const bool bSuppressed)
	: Viewport(&InViewport)
{
	Root = SNew(SOverlay);
	SetSuppressed(bSuppressed);
	InViewport.AddViewportWidgetContent(Root.ToSharedRef(), 5);
}

FGinkgoGameplayHudLayer::~FGinkgoGameplayHudLayer()
{
	if (auto* Owner = Viewport.Get()) { Owner->RemoveViewportWidgetContent(Root.ToSharedRef()); }
}

void FGinkgoGameplayHudLayer::Add(const TSharedRef<SWidget>& Widget, const int32 ZOrder)
{
	Root->AddSlot(ZOrder)[Widget];
}

void FGinkgoGameplayHudLayer::Remove(const TSharedRef<SWidget>& Widget)
{
	Root->RemoveSlot(Widget);
}

void FGinkgoGameplayHudLayer::SetSuppressed(const bool bSuppressed)
{
	Root->SetVisibility(bSuppressed ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible);
}
