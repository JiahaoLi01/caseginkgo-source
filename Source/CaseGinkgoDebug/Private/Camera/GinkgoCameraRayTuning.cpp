#include "GinkgoCameraRayTuning.h"

#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"

bool Ginkgo::Debug::Camera::IsRayList(const FProperty& Property)
{
	const FArrayProperty* Array = CastField<FArrayProperty>(&Property);
	const FStructProperty* Element = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
	return Element && Element->Struct == FGinkgoCameraShoulderRay::StaticStruct();
}

TSharedRef<FJsonValue> Ginkgo::Debug::Camera::RaysToJson(const TArray<FGinkgoCameraShoulderRay>& Rays)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FGinkgoCameraShoulderRay& Ray : Rays)
	{
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("angle"), Ray.Angle);
		Object->SetNumberField(TEXT("length"), Ray.Length);
		Object->SetNumberField(TEXT("score"), Ray.Score);
		Values.Add(MakeShared<FJsonValueObject>(Object));
	}
	return MakeShared<FJsonValueArray>(Values);
}

bool Ginkgo::Debug::Camera::RaysFromJson(const FJsonValue& Value, TArray<FGinkgoCameraShoulderRay>& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Value.TryGetArray(Values) || Values->Num() > FGinkgoCameraShoulderSettings::MaxRays)
	{
		return false;
	}
	TArray<FGinkgoCameraShoulderRay> Parsed;
	for (const TSharedPtr<FJsonValue>& Entry : *Values)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		double Angle = 0.0;
		double Length = 0.0;
		double Score = 0.0;
		if (!Entry || !Entry->TryGetObject(Object) || !(*Object)->TryGetNumberField(TEXT("angle"), Angle)
			|| !(*Object)->TryGetNumberField(TEXT("length"), Length) || !(*Object)->TryGetNumberField(TEXT("score"), Score))
		{
			return false;
		}
		const FGinkgoCameraShoulderRay Ray(Angle, Length, Score);
		if (!Ray.IsValid())
		{
			return false;
		}
		Parsed.Add(Ray);
	}
	Out = MoveTemp(Parsed);
	return true;
}

FString Ginkgo::Debug::Camera::ExportRays(const TArray<FGinkgoCameraShoulderRay>& Rays)
{
	FString Text;
	FJsonSerializer::Serialize(RaysToJson(Rays), TEXT(""), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
	return Text;
}

bool Ginkgo::Debug::Camera::ImportRays(const FString& Text, TArray<FGinkgoCameraShoulderRay>& Out)
{
	TSharedPtr<FJsonValue> Value;
	return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Value) && Value && RaysFromJson(*Value, Out);
}

void Ginkgo::Debug::Camera::MigrateRayTuning(TMap<FString, FString>& Values, const TArray<FGinkgoCameraShoulderRay>& DefaultRays)
{
	FString LegacyDistance;
	if (!Values.RemoveAndCopyValue(TEXT("Shoulder.TraceDistance"), LegacyDistance) || Values.Contains(TEXT("Shoulder.Rays")))
	{
		return;
	}
	float Distance = 0.f;
	if (!LexTryParseString(Distance, *LegacyDistance) || !FGinkgoCameraShoulderRay(0.f, Distance).IsValid())
	{
		return;
	}
	TArray<FGinkgoCameraShoulderRay> Rays = DefaultRays;
	for (FGinkgoCameraShoulderRay& Ray : Rays)
	{
		Ray.Length = Distance;
	}
	Values.Add(TEXT("Shoulder.Rays"), ExportRays(Rays));
}
