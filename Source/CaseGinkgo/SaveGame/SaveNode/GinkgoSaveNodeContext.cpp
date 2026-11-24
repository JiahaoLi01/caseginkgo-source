#include "SaveGame/SaveNode/GinkgoSaveNodeContext.h"

#include "Engine/World.h"
#include "SaveGame/GinkgoSaveGameSubsystem.h"
#include "Serialization/Archive.h"
#include "Utilities/Debug/DebugCategory.h"

FGinkgoSaveNodeContext::FGinkgoSaveNodeContext(
	FStructuredArchive::FRecord InRecord,
	int32 InSavedVersion,
	bool bInIsLoading,
	UGinkgoSaveGameSubsystem* InSubsystem,
	FAddSoftPathRedirectFn InAddRedirect)
	: Record(InRecord)
	, SavedVersion(InSavedVersion)
	, bIsLoading(bInIsLoading)
	, Subsystem(InSubsystem)
	, AddRedirectFn(MoveTemp(InAddRedirect))
{
}

FGinkgoSaveNodeContext::FGinkgoSaveNodeContext(UGinkgoSaveGameSubsystem* InSubsystem)
	: Subsystem(InSubsystem)
{
}

UWorld* FGinkgoSaveNodeContext::GetWorld() const
{
	return Subsystem ? Subsystem->GetWorld() : nullptr;
}

FStructuredArchive::FRecord& FGinkgoSaveNodeContext::GetRecord()
{
	checkf(HasArchive(), TEXT("FGinkgoSaveNodeContext::GetRecord：这个上下文没有存档"));
	return Record.GetValue();
}

FArchive& FGinkgoSaveNodeContext::GetUnderlyingArchive()
{
	checkf(HasArchive(), TEXT("FGinkgoSaveNodeContext::GetUnderlyingArchive：这个上下文没有存档"));
	return Record->GetUnderlyingArchive();
}

void FGinkgoSaveNodeContext::AddSoftPathRedirect(const FSoftObjectPath& From, const FSoftObjectPath& To)
{
	if (AddRedirectFn)
	{
		AddRedirectFn(From, To);
	}
}

FGinkgoSaveGameArchive& FGinkgoSaveNodeContext::GetFields()
{
	if (!Fields)
	{
		Fields = MakeUnique<FGinkgoSaveGameArchive>(Record.GetValue(), Subsystem);
	}
	return *Fields;
}

void FGinkgoSaveNodeContext::MarkFieldMissing(const TCHAR* Name)
{
	UE_LOG(LogGinkgoSave, Error, TEXT("[Save] 必填字段 %s 不在存档里，存档损坏，中止读档"), Name);
	GetUnderlyingArchive().SetError();
}
