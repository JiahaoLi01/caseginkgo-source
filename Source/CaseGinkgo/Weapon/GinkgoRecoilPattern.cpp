#include "Weapon/GinkgoRecoilPattern.h"

#if WITH_EDITORONLY_DATA
#include "EditorFramework/AssetImportData.h"
#endif
#if WITH_EDITOR
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#endif

FVector2f UGinkgoRecoilPattern::GetStepAfterShot(const int32 ShotIndex) const
{
	const int32 LastStep = Shots.Num() - 2;
	if (LastStep < 0)
	{
		return FVector2f::ZeroVector;
	}
	int32 Step = FMath::Max(ShotIndex, 0);
	if (Step > LastStep)
	{
		const int32 First = FMath::Clamp(LoopFrom, 0, LastStep);
		Step = First + (Step - LastStep - 1) % (LastStep - First + 1);
	}
	return Shots[Step + 1] - Shots[Step];
}

#if WITH_EDITORONLY_DATA
void UGinkgoRecoilPattern::PostInitProperties()
{
	Super::PostInitProperties();
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
}

void UGinkgoRecoilPattern::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	if (AssetImportData)
	{
		Context.AddTag(FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden));
	}
	Super::GetAssetRegistryTags(Context);
}
#endif

#if WITH_EDITOR
namespace GinkgoRecoilPattern
{
	/** 一对有限的数：[偏航, 俯仰] */
	bool ReadPair(const TSharedPtr<FJsonValue>& Value, FVector2f& OutPair)
	{
		const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
		double X = 0.0;
		double Y = 0.0;
		if (!Value.IsValid() || !Value->TryGetArray(Pair) || Pair->Num() != 2 || !(*Pair)[0]->TryGetNumber(X) || !(*Pair)[1]->TryGetNumber(Y)
			|| !FMath::IsFinite(X) || !FMath::IsFinite(Y))
		{
			return false;
		}
		OutPair = FVector2f(static_cast<float>(X), static_cast<float>(Y));
		return true;
	}
}

bool UGinkgoRecoilPattern::ImportFromJson(const FString& Text, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("不是 JSON：%s"), *Reader->GetErrorMessage());
		return false;
	}
	FString Format;
	int32 Version = 0;
	if (!Root->TryGetStringField(TEXT("format"), Format) || Format != TEXT("ginkgo.recoil") || !Root->TryGetNumberField(TEXT("version"), Version))
	{
		OutError = TEXT("format 不是 ginkgo.recoil，或者没有 version");
		return false;
	}
	if (Version != 1)
	{
		OutError = FString::Printf(TEXT("不认识的版本 %d（这里读 1）"), Version);
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* ShotValues = nullptr;
	if (!Root->TryGetArrayField(TEXT("shots"), ShotValues) || ShotValues->IsEmpty())
	{
		OutError = TEXT("没有 shots，或者是空的");
		return false;
	}
	TArray<FVector2f> NewShots;
	NewShots.Reserve(ShotValues->Num());
	for (int32 Index = 0; Index < ShotValues->Num(); ++Index)
	{
		FVector2f Shot;
		if (!GinkgoRecoilPattern::ReadPair((*ShotValues)[Index], Shot))
		{
			OutError = FString::Printf(TEXT("第 %d 发不是 [偏航, 俯仰] 两个数"), Index + 1);
			return false;
		}
		NewShots.Add(Shot);
	}
	int32 NewLoopFrom = 0;
	Root->TryGetNumberField(TEXT("loopFrom"), NewLoopFrom);
	if (NewLoopFrom < 0 || NewLoopFrom >= NewShots.Num())
	{
		OutError = FString::Printf(TEXT("loopFrom %d 不在 0 到 %d 之间"), NewLoopFrom, NewShots.Num() - 1);
		return false;
	}
	FVector2f NewJitter = FVector2f::ZeroVector;
	if (const TSharedPtr<FJsonValue> JitterValue = Root->TryGetField(TEXT("jitter")))
	{
		if (!GinkgoRecoilPattern::ReadPair(JitterValue, NewJitter) || NewJitter.X < 0.f || NewJitter.Y < 0.f)
		{
			OutError = TEXT("jitter 不是 [偏航, 俯仰] 两个不小于 0 的数");
			return false;
		}
	}
	Shots = MoveTemp(NewShots);
	LoopFrom = NewLoopFrom;
	Jitter = NewJitter;
	return true;
}
#endif
