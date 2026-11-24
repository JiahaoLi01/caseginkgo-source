#include "SaveGame/SaveNode/Core/GinkgoCoreDestroyedActorsSaveNode.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "SaveGame/GinkgoSaveGameSubsystem.h"
#include "SaveGame/SaveNode/Core/GinkgoCoreActorsSaveNode.h"
#include "SaveGame/SaveNode/GinkgoSaveNodeContext.h"
#include "UObject/DevObjectVersion.h"
#include "Utilities/Debug/DebugStatGroup.h"
#include "Utilities/Enumeration/Tags/SaveGameTags.h"

DECLARE_CYCLE_STAT(TEXT("Save core destroyed actors"), STAT_GinkgoSaveCoreDestroyedActors, STATGROUP_Ginkgo3C);

const FGuid FGinkgoCoreDestroyedActorsSaveNode::NodeGuid(0x51294F4C, 0x7AA745A5, 0x88D34321, 0x9EBDC2CB);

// 让 FArchive::UsingCustomVersion(NodeGuid) 查得到这个 GUID
static FDevVersionRegistration GRegisterGinkgoCoreDestroyedActorsSaveNode(
	FGinkgoCoreDestroyedActorsSaveNode::NodeGuid,
	FGinkgoCoreDestroyedActorsSaveNode::LatestVersion,
	TEXT("GinkgoCoreDestroyedActorsSaveNode"));

FGameplayTag FGinkgoCoreDestroyedActorsSaveNode::GetClassificationTag() const
{
	return FSaveNodeTags::Core_DestroyedActors;
}

void FGinkgoCoreDestroyedActorsSaveNode::GetDependencies(TArray<FGuid>& Out) const
{
	Out.Add(FGinkgoCoreActorsSaveNode::NodeGuid);
}

void FGinkgoCoreDestroyedActorsSaveNode::Serialize(FGinkgoSaveNodeContext& Ctx)
{
	SCOPE_CYCLE_COUNTER(STAT_GinkgoSaveCoreDestroyedActors);

	UGinkgoSaveGameSubsystem* SaveSubsystem = Ctx.GetSubsystem();
	if (!ensure(SaveSubsystem))
	{
		return;
	}

	const UWorld* World = Ctx.GetWorld();
	if (!ensure(World))
	{
		return;
	}

	FStructuredArchive::FRecord& Record = Ctx.GetRecord();
	FArchive& Archive = Ctx.GetUnderlyingArchive();
	const bool bIsLoading = Ctx.IsLoading();
	TSet<FSoftObjectPath>& DestroyedActors = SaveSubsystem->GetDestroyedLevelActors();

	const TArray<FSoftObjectPath> SavedPaths = bIsLoading ? TArray<FSoftObjectPath>() : DestroyedActors.Array();
	int32 DestroyedActorsCount = SavedPaths.Num();

	FStructuredArchive::FArray DestroyedActorArray = Record.EnterArray(FArchiveFieldName(TEXT("DestroyedActors")), DestroyedActorsCount);
	if (Archive.IsError() || DestroyedActorsCount < 0)
	{
		Archive.SetError();
		return;
	}

	if (bIsLoading)
	{
		DestroyedActors.Reset();
		DestroyedActors.Reserve(DestroyedActorsCount);
	}

	for (int32 ActorIndex = 0; ActorIndex < DestroyedActorsCount && !Archive.IsError(); ++ActorIndex)
	{
		FName ActorName;

		if (!bIsLoading)
		{
			FString ActorSubPath = SavedPaths[ActorIndex].GetSubPathString();
			ActorSubPath.RemoveFromStart(FGinkgoCoreActorsSaveNode::LevelSubPathPrefix);
			ActorName = *ActorSubPath;
		}

		DestroyedActorArray.EnterElement() << ActorName;

		if (bIsLoading)
		{
			// 不管现在找不找得到都记着：原地读档时它可能早被回收了，下一次存档还要写上它，否则从头载入地图时它又回来了
			DestroyedActors.Add(FGinkgoCoreActorsSaveNode::MakeLevelActorPath(*World->GetCurrentLevel(), ActorName.ToString()));

			if (AActor* DestroyedActor = FindObjectFast<AActor>(World->GetCurrentLevel(), ActorName); IsValid(DestroyedActor))
			{
				DestroyedActor->Destroy();
			}
		}
	}
}
