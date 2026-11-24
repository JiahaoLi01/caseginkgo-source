#include "Checkpoint/GinkgoCheckpointSaveNode.h"

#include "Checkpoint/GinkgoCheckpointSubsystem.h"
#include "SaveGame/SaveNode/GinkgoSaveNodeContext.h"
#include "SaveGame/SaveNode/Core/GinkgoCoreDestroyedActorsSaveNode.h"
#include "NativeGameplayTags.h"
#include "UObject/DevObjectVersion.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SaveNode_Checkpoint, "SaveNode.Checkpoint.Progress");
const FGuid FGinkgoCheckpointSaveNode::NodeGuid(0xD7B04126, 0x4F304CBE, 0xB210E62F, 0x17E09A15);
static FDevVersionRegistration GRegisterCheckpointSaveNode(FGinkgoCheckpointSaveNode::NodeGuid, 0, TEXT("GinkgoCheckpoint"));

FGameplayTag FGinkgoCheckpointSaveNode::GetClassificationTag() const { return TAG_SaveNode_Checkpoint; }

void FGinkgoCheckpointSaveNode::GetDependencies(TArray<FGuid>& Out) const
{
	Out.Add(FGinkgoCoreDestroyedActorsSaveNode::NodeGuid);
}

void FGinkgoCheckpointSaveNode::Serialize(FGinkgoSaveNodeContext& Ctx)
{
	if (UGinkgoCheckpointSubsystem* System = UGinkgoCheckpointSubsystem::Get(Ctx.GetWorld()))
	{
		Ctx.Field(TEXT("Progress"), System->State);
	}
	else { Ctx.GetUnderlyingArchive().SetError(); }
}

void FGinkgoCheckpointSaveNode::OnMissingFromSave(FGinkgoSaveNodeContext& Ctx)
{
	if (auto* System = UGinkgoCheckpointSubsystem::Get(Ctx.GetWorld())) { System->State = {}; }
}

void FGinkgoCheckpointSaveNode::PostLoadGame(FGinkgoSaveNodeContext& Ctx)
{
	if (auto* System = UGinkgoCheckpointSubsystem::Get(Ctx.GetWorld())) { System->ApplyLoadedState(); }
}
