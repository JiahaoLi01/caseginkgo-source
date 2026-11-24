#include "Presentation/GinkgoDialogueComponent.h"

#include "Control/GinkgoPlayerController.h"
#include "Engine/World.h"
#include "Sound/SoundBase.h"

void UGinkgoDialogueComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!GetOwner()->HasAuthority()) { return; }
	auto* Hub = UGinkgoEventHub::Find(GetOwner());
	ActiveSet = Hub ? DialogueSet.LoadSynchronous() : nullptr;
	if (!ActiveSet) { return; }
	FGinkgoEventListener Listener;
	Listener.BindDynamic(this, &ThisClass::HandleEvent);
	for (const auto& Pair : ActiveSet->Cues) { Hub->Subscribe(Pair.Key, Listener); }
}

void UGinkgoDialogueComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	auto* Hub = UGinkgoEventHub::Find(GetOwner());
	if (Hub && ActiveSet)
	{
		FGinkgoEventListener Listener;
		Listener.BindDynamic(this, &ThisClass::HandleEvent);
		for (const auto& Pair : ActiveSet->Cues) { Hub->Unsubscribe(Pair.Key, Listener); }
	}
	ActiveSet = nullptr;
	Playback.Reset();
	Super::EndPlay(Reason);
}

void UGinkgoDialogueComponent::HandleEvent(const FGinkgoActorEvent& Event)
{
	const auto* Cue = ActiveSet ? ActiveSet->Cues.Find(Event.Tag) : nullptr;
	if (!Cue) { return; }
	FPlayback& State = Playback.FindOrAdd(Event.Tag);
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < State.ReadyAt) { return; }
	TArray<int32, TInlineAllocator<8>> Choices;
	for (int32 Index = 0; Index < Cue->Lines.Num(); ++Index)
	{
		const auto& Line = Cue->Lines[Index];
		if (!Line.Text.IsEmpty() || Line.Voice) { Choices.Add(Index); }
	}
	if (Choices.IsEmpty()) { return; }
	if (Choices.Num() > 1) { Choices.RemoveSingleSwap(State.LastLine); }
	State.LastLine = Choices[FMath::RandHelper(Choices.Num())];
	State.ReadyAt = Now + Cue->Cooldown;
	const FVector Location = GetOwner()->GetActorLocation();
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		auto* PC = Cast<AGinkgoPlayerController>(It->Get());
		const AActor* Listener = PC ? PC->GetViewTarget() : nullptr;
		if (Listener && FVector::DistSquared(Location, Listener->GetActorLocation()) <= FMath::Square(ActiveSet->AudibleRadius))
		{
			PC->ClientHearDialogue(GetOwner()->GetUniqueID(), Location, ActiveSet->Speaker, Cue->Lines[State.LastLine]);
		}
	}
}
