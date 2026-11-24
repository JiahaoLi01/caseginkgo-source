#include "GinkgoCameraParams.h"

FGinkgoCameraParamField FGinkgoCameraParamField::FromProperty(const FProperty* Property)
{
	return FGinkgoCameraParamField(FName(*FString::Printf(TEXT("%s.%s"), *Property->GetOwnerStruct()->GetName(), *Property->GetName())));
}

FGinkgoCameraParamField FGinkgoCameraParamField::FromMember(const FName MemberName)
{
	return FGinkgoCameraParamField(FName(*FString::Printf(TEXT("%s.%s"), *FGinkgoCameraParams::StaticStruct()->GetName(), *MemberName.ToString())));
}

const TArray<FGinkgoCameraParamFieldInfo>& Ginkgo::Camera::GetParamFieldInfoList(const UStruct* Struct)
{
	// 每个结构一份，第一次访问时建：遍历整条继承链（不加 ExcludeSuper），只收 float
	static TMap<const UStruct*, TArray<FGinkgoCameraParamFieldInfo>> Cache;
	if (const TArray<FGinkgoCameraParamFieldInfo>* Found = Cache.Find(Struct))
	{
		return *Found;
	}
	TArray<FGinkgoCameraParamFieldInfo>& List = Cache.Add(Struct);
	for (TFieldIterator<FFloatProperty> It(Struct); It; ++It)
	{
		List.Add({*It, FGinkgoCameraParamField::FromProperty(*It)});
	}
	return List;
}

const FFloatProperty* Ginkgo::Camera::FindParamProperty(const FGinkgoCameraParamField& Field)
{
	const FGinkgoCameraParamFieldInfo* Info = GetParamFieldInfoList(FGinkgoCameraParams::StaticStruct()).FindByPredicate(
		[&Field](const FGinkgoCameraParamFieldInfo& Item) { return Item.Field == Field; });
	return Info ? Info->Property : nullptr;
}

float Ginkgo::Camera::GetParamValue(const FGinkgoCameraParams& Params, const FFloatProperty& Property)
{
	return *Property.ContainerPtrToValuePtr<float>(&Params);
}

void Ginkgo::Camera::SetParamValue(FGinkgoCameraParams& Params, const FFloatProperty& Property, const float Value)
{
	*Property.ContainerPtrToValuePtr<float>(&Params) = Value;
}
