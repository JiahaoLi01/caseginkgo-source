#include "SaveGame/GinkgoSaveGameLibrary.h"

#include "GameFramework/Actor.h"
#include "SaveGame/GinkgoSaveGameSettings.h"

#if WITH_EDITOR
#include "Blueprint/BlueprintExceptionInfo.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"

namespace
{
	/** 蓝图连错时：在 PIE 的消息日志里报错，点一下跳到那个节点 */
	void BreakPointWithError(FFrame& Stack, const FText& Text)
	{
		const FBlueprintExceptionInfo ExceptionInfo{EBlueprintExceptionType::Breakpoint, Text};
		const int32 BreakPointOpCodeOffset = Stack.Code - Stack.Node->Script.GetData() - 1;
		const UEdGraphNode* Node = FKismetDebugUtilities::FindSourceNodeForCodeLocation(Stack.Object, Stack.Node, BreakPointOpCodeOffset, true);

		struct FLocal
		{
			static void OnMessageLogLinkActivated(const TSharedRef<IMessageToken>& Token)
			{
				if (Token->GetType() == EMessageToken::Object)
				{
					const TSharedRef<FUObjectToken> ObjectToken = StaticCastSharedRef<FUObjectToken>(Token);
					if (ObjectToken->GetObject().IsValid())
					{
						FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(ObjectToken->GetObject().Get());
					}
				}
			}
		};

		FMessageLog MessageLog{"PIE"};
		MessageLog.Error()
			->AddToken(FUObjectToken::Create(Node, Node->GetNodeTitle(ENodeTitleType::Type::ListView))
				->OnMessageTokenActivated(FOnMessageTokenActivated::CreateStatic(&FLocal::OnMessageLogLinkActivated)))
			->AddToken(FTextToken::Create(Text));
		MessageLog.Open(EMessageSeverity::Type::Error);

		FBlueprintCoreDelegates::ThrowScriptException(Stack.Object, Stack, ExceptionInfo);
	}
}
#endif

bool UGinkgoSaveGameLibrary::WasObjectLoaded(const UObject* Object)
{
	return Object != nullptr && Object->HasAnyFlags(RF_WasLoaded | RF_LoadCompleted);
}

bool UGinkgoSaveGameLibrary::IsLoading(const FGinkgoSaveGameArchive& Archive)
{
	return Archive.IsValid() && Archive.GetRecord().GetUnderlyingArchive().IsLoading();
}

bool UGinkgoSaveGameLibrary::SerializeActorTransform(FGinkgoSaveGameArchive& Archive, AActor* Actor)
{
	if (!Archive.IsValid() || !IsValid(Actor))
	{
		return false;
	}

	const bool bIsMovable = Actor->IsRootComponentMovable();
	const bool bIsLoading = IsLoading(Archive);

	return (bIsLoading || bIsMovable) && Archive.SerializeField(TEXT("ActorTransform"), [&](FStructuredArchive::FSlot Slot)
	{
		FTransform ActorTransform;
		if (!bIsLoading)
		{
			ActorTransform = Actor->GetActorTransform();
		}

		Slot << ActorTransform;

		if (bIsLoading && bIsMovable)
		{
			Actor->SetActorTransform(ActorTransform, false, nullptr, ETeleportType::TeleportPhysics);
		}
	});
}

bool UGinkgoSaveGameLibrary::SerializeItem(FGinkgoSaveGameArchive& Archive, int32& Value, bool bSave)
{
	checkf(false, TEXT("这个函数只给蓝图用（CustomThunk）"));
	return false;
}

DEFINE_FUNCTION(UGinkgoSaveGameLibrary::execSerializeItem)
{
	P_GET_STRUCT_REF(FGinkgoSaveGameArchive, Archive);

	Stack.StepCompiledIn<FStructProperty>(nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	uint8* ValueAddress = Stack.MostRecentPropertyAddress;

	P_GET_UBOOL(bSave);
	P_FINISH;

	P_NATIVE_BEGIN

	*StaticCast<bool*>(RESULT_PARAM) = false;

#if WITH_EDITOR
	if (ValueProperty && (!ValueProperty->HasAnyPropertyFlags(CPF_Edit) || ValueProperty->HasAnyPropertyFlags(CPF_BlueprintReadOnly)))
	{
		BreakPointWithError(Stack, FText::Format(NSLOCTEXT("GinkgoSaveGame", "SerializeItem_NotVariableException", "连到 Value 的 '{0}' 不是可编辑的变量"), ValueProperty->GetDisplayNameText()));
	}
	else
#endif
	if (ValueProperty && Archive.IsValid() && (IsLoading(Archive) || bSave))
	{
		Archive.SerializeField(ValueProperty->GetFName(), [&](FStructuredArchive::FSlot Slot)
		{
			ValueProperty->SerializeItem(Slot, ValueAddress, nullptr);
			*StaticCast<bool*>(RESULT_PARAM) = true;
		});
	}
	P_NATIVE_END;
}

void UGinkgoSaveGameLibrary::SerializeGameplayTagContainer(FStructuredArchive::FSlot Slot, FGameplayTagContainer& TagContainer, bool bIsLoading)
{
	TArray<FGameplayTag> Tags;
	if (!bIsLoading)
	{
		TagContainer.GetGameplayTagArray(Tags);
	}

	int32 Count = Tags.Num();
	FStructuredArchive::FArray Array = Slot.EnterArray(Count);

	if (bIsLoading)
	{
		TagContainer.Reset();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FString TagString;
			Array.EnterElement() << TagString;

			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TagString), false);
			if (Tag.IsValid())
			{
				TagContainer.AddTag(Tag);
			}
		}
	}
	else
	{
		for (const FGameplayTag& Tag : Tags)
		{
			FString TagString = Tag.ToString();
			Array.EnterElement() << TagString;
		}
	}
}

int32 UGinkgoSaveGameLibrary::UseCustomVersion(FGinkgoSaveGameArchive& Archive, const UEnum* VersionEnum)
{
	if (Archive.IsValid() && ::IsValid(VersionEnum))
	{
		FArchive& UnderlyingArchive = Archive.GetRecord().GetUnderlyingArchive();
		const FGuid VersionGUID = GetDefault<UGinkgoSaveGameSettings>()->GetVersionId(VersionEnum);

		if (VersionGUID.IsValid())
		{
			if (UnderlyingArchive.IsLoading())
			{
				const FCustomVersion* CustomVersion = UnderlyingArchive.GetCustomVersions().GetVersion(VersionGUID);
				return CustomVersion ? CustomVersion->Version : INDEX_NONE;
			}

			const int32 Version = VersionEnum->GetMaxEnumValue() - 1;
			UnderlyingArchive.SetCustomVersion(VersionGUID, Version, VersionEnum->GetFName());
			return Version;
		}
	}

	return INDEX_NONE;
}
