#include "GinkgoCameraConfigPath.h"

#include "Algo/Find.h"
#include "Camera/GinkgoCameraConfig.h"
#include "Camera/GinkgoPlayerCameraManager.h"
#include "Engine/World.h"
#include "GinkgoCameraDebug.h"
#include "GinkgoCameraRayTuning.h"
#include "Utilities/Debug/DebugCategory.h"

namespace
{
	/** 数组元素的名字：结构或对象上叫 Name 的 FName 成员（修改器、叠加效果都有） */
	FName GetElementName(const FProperty& Inner, void* Element)
	{
		const UStruct* Struct = nullptr;
		const void* Container = nullptr;
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(&Inner))
		{
			Struct = StructProperty->Struct;
			Container = Element;
		}
		else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(&Inner))
		{
			const UObject* Object = ObjectProperty->GetObjectPropertyValue(Element);
			Struct = Object ? Object->GetClass() : nullptr;
			Container = Object;
		}
		const FNameProperty* NameProperty = Struct ? FindFProperty<FNameProperty>(Struct, TEXT("Name")) : nullptr;
		return NameProperty ? NameProperty->GetPropertyValue_InContainer(Container) : NAME_None;
	}

	/** 面板上调的叶子：数、开关、枚举和整组射线；射线由专用组件管理增删 */
	bool IsTunableLeaf(const FProperty& Property)
	{
		if (const FByteProperty* Byte = CastField<FByteProperty>(&Property))
		{
			return Byte->Enum != nullptr;
		}
		return Ginkgo::Debug::Camera::IsRayList(Property) || Property.IsA<FFloatProperty>() || Property.IsA<FDoubleProperty>() || Property.IsA<FIntProperty>() || Property.IsA<FBoolProperty>()
			|| Property.IsA<FEnumProperty>();
	}

	/** 不往里走的结构：标签、标签查询、曲线，修改器与参数弹簧的选参 */
	bool IsSkippedStruct(const UScriptStruct& Struct)
	{
		static const FName Skipped[] = { TEXT("GameplayTag"), TEXT("GameplayTagContainer"), TEXT("GameplayTagQuery"), TEXT("RuntimeFloatCurve"),
			TEXT("GinkgoCameraParamField") };
		return Algo::Find(Skipped, Struct.GetFName()) != nullptr;
	}

	void CollectStruct(const UStruct& Struct, void* Container, const FString& Prefix, const FString& Group, TArray<Ginkgo::Debug::Camera::FTunable>& Out);

	void CollectValue(const FProperty& Property, void* Address, const FString& Path, const FString& Group, TArray<Ginkgo::Debug::Camera::FTunable>& Out)
	{
		if (IsTunableLeaf(Property))
		{
			Out.Add({Path, Group, &Property});
		}
		else if (const FStructProperty* StructProperty = CastField<FStructProperty>(&Property))
		{
			if (!IsSkippedStruct(*StructProperty->Struct))
			{
				CollectStruct(*StructProperty->Struct, Address, Path, Group.IsEmpty() ? Path : Group, Out);
			}
		}
		else if (const FArrayProperty* Array = CastField<FArrayProperty>(&Property))
		{
			FScriptArrayHelper Helper(Array, Address);
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				const FName Name = GetElementName(*Array->Inner, Helper.GetRawPtr(Index));
				const FString Element = FString::Printf(TEXT("%s[%s]"), *Path, Name.IsNone() ? *FString::FromInt(Index) : *Name.ToString());
				CollectValue(*Array->Inner, Helper.GetRawPtr(Index), Element, Group.IsEmpty() ? Element : Group, Out);
			}
		}
		else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(&Property))
		{
			// 只走实例化的子对象（修改器的数值来源、叠加效果）：别的对象引用不是配置的一部分
			UObject* Object = ObjectProperty->HasAnyPropertyFlags(CPF_InstancedReference) ? ObjectProperty->GetObjectPropertyValue(Address) : nullptr;
			if (Object)
			{
				CollectStruct(*Object->GetClass(), Object, Path, Group, Out);
			}
		}
	}

	void CollectStruct(const UStruct& Struct, void* Container, const FString& Prefix, const FString& Group, TArray<Ginkgo::Debug::Camera::FTunable>& Out)
	{
		for (TFieldIterator<FProperty> It(&Struct); It; ++It)
		{
			// 元素的名字是路径的一部分，不是调的数
			if (It->GetFName() == TEXT("Name") || It->HasAnyPropertyFlags(CPF_Transient) || !It->HasAnyPropertyFlags(CPF_Edit))
			{
				continue;
			}
			const FString Path = Prefix.IsEmpty() ? It->GetName() : Prefix + TEXT(".") + It->GetName();
			CollectValue(**It, It->ContainerPtrToValuePtr<void>(Container), Path, Group, Out);
		}
	}
}

bool Ginkgo::Debug::Camera::ResolveConfigPath(UObject& Root, const FString& Path, FConfigItem& Out, FString& OutError, TArray<FPathSegment>* OutChain)
{
	TArray<FString> Tokens;
	Path.ParseIntoArray(Tokens, TEXT("."));
	if (OutChain)
	{
		OutChain->Reset();
	}
	const UStruct* Struct = Root.GetClass();
	void* Container = &Root;
	UObject* Owner = &Root;
	for (int32 Index = 0; Index < Tokens.Num(); ++Index)
	{
		FString Name = Tokens[Index];
		FString Key;
		int32 Bracket = INDEX_NONE;
		if (Name.FindChar(TEXT('['), Bracket) && Name.EndsWith(TEXT("]")))
		{
			Key = Name.Mid(Bracket + 1, Name.Len() - Bracket - 2);
			Name.LeftInline(Bracket);
		}
		const FProperty* Property = FindFProperty<FProperty>(Struct, *Name);
		if (!Property)
		{
			OutError = FString::Printf(TEXT("%s 里没有 %s"), *Struct->GetName(), *Name);
			return false;
		}
		void* Address = Property->ContainerPtrToValuePtr<void>(Container);
		if (!Key.IsEmpty())
		{
			const FArrayProperty* Array = CastField<FArrayProperty>(Property);
			if (!Array)
			{
				OutError = FString::Printf(TEXT("%s 不是数组"), *Name);
				return false;
			}
			FScriptArrayHelper Helper(Array, Address);
			int32 Element = INDEX_NONE;
			if (Key.IsNumeric())
			{
				Element = FCString::Atoi(*Key);
			}
			else
			{
				for (int32 Candidate = 0; Candidate < Helper.Num() && Element == INDEX_NONE; ++Candidate)
				{
					Element = GetElementName(*Array->Inner, Helper.GetRawPtr(Candidate)) == FName(*Key) ? Candidate : INDEX_NONE;
				}
			}
			if (!Helper.IsValidIndex(Element))
			{
				OutError = FString::Printf(TEXT("%s 里没有 [%s]"), *Name, *Key);
				return false;
			}
			if (OutChain)
			{
				OutChain->Add({Array, Element});
			}
			Property = Array->Inner;
			Address = Helper.GetRawPtr(Element);
		}
		else if (OutChain)
		{
			OutChain->Add({Property, INDEX_NONE});
		}
		if (Index == Tokens.Num() - 1)
		{
			Out = {Property, Address, Owner};
			return true;
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			Struct = StructProperty->Struct;
			Container = Address;
		}
		else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			UObject* Object = ObjectProperty->GetObjectPropertyValue(Address);
			if (!Object)
			{
				OutError = FString::Printf(TEXT("%s 是空的"), *Tokens[Index]);
				return false;
			}
			Struct = Object->GetClass();
			Container = Object;
			Owner = Object;
		}
		else
		{
			OutError = FString::Printf(TEXT("%s 不是结构或对象，不能往里走"), *Tokens[Index]);
			return false;
		}
	}
	OutError = TEXT("路径是空的");
	return false;
}

FString Ginkgo::Debug::Camera::ExportItem(const FConfigItem& Item)
{
	if (IsRayList(*Item.Property))
	{
		return ExportRays(*static_cast<const TArray<FGinkgoCameraShoulderRay>*>(Item.Address));
	}
	FString Text;
	Item.Property->ExportTextItem_Direct(Text, Item.Address, nullptr, Item.Owner, PPF_None);
	return Text;
}

bool Ginkgo::Debug::Camera::ImportItem(const FConfigItem& Item, const FString& Text)
{
	if (IsRayList(*Item.Property))
	{
		return ImportRays(Text, *static_cast<TArray<FGinkgoCameraShoulderRay>*>(Item.Address));
	}
	return Item.Property->ImportText_Direct(*Text, Item.Address, Item.Owner, PPF_None) != nullptr;
}

TArray<Ginkgo::Debug::Camera::FTunable> Ginkgo::Debug::Camera::CollectTunables(UObject& Root)
{
	TArray<FTunable> Out;
	CollectStruct(*Root.GetClass(), &Root, FString(), FString(), Out);
	return Out;
}

// ===== ginkgo.camera.set / get / list：运行时改、读相机配置里的任何一项 =====

namespace
{
	using namespace Ginkgo::Debug::Camera;

	FAutoConsoleCommandWithWorldAndArgs CommandSet(
		TEXT("ginkgo.camera.set"),
		TEXT("改本地相机正在用的配置里的一项（资产或类默认值；PIE 里也生效，不存盘，存到本地用调参面板 F7）：ginkgo.camera.set <路径> <值>。")
		TEXT("路径：结构的成员用 . 隔开，数组用 [下标] 或 [名字]，如 BaseParams.ArmLength 180、Modifiers[SlideFov].bEnabled false、")
		TEXT("OverlayEffects[AdsSway].Pitch.Amplitude 0.5、OverlayEffects[SlideShake].Impulse (Pitch=-150,Yaw=0,Roll=0)、OverlayPivot Pivot。值的写法同属性的文本导入。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, const UWorld* World)
		{
			AGinkgoPlayerCameraManager* Camera = FindLocalCamera(World);
			if (!Camera || Args.Num() < 2)
			{
				UE_LOG(LogGinkgoCamera, Warning, TEXT("[Camera] ginkgo.camera.set <路径> <值>：%s"), Camera ? TEXT("参数不够") : TEXT("没有本地的 Ginkgo 相机"));
				return;
			}
			FConfigItem Item;
			FString Error;
			if (!ResolveConfigPath(GetMutableConfig(*Camera), Args[0], Item, Error))
			{
				UE_LOG(LogGinkgoCamera, Warning, TEXT("[Camera] ginkgo.camera.set %s：%s"), *Args[0], *Error);
				return;
			}
			FString Value = Args[1];
			for (int32 Index = 2; Index < Args.Num(); ++Index)
			{
				Value += TEXT(" ") + Args[Index];
			}
			const FString Before = ExportItem(Item);
			if (!ImportItem(Item, Value))
			{
				UE_LOG(LogGinkgoCamera, Warning, TEXT("[Camera] ginkgo.camera.set %s：读不懂 %s"), *Args[0], *Value);
				return;
			}
			UE_LOG(LogGinkgoCamera, Log, TEXT("[Camera] %s：%s → %s"), *Args[0], *Before, *ExportItem(Item));
		}));

	FAutoConsoleCommandWithWorldAndArgs CommandGet(
		TEXT("ginkgo.camera.get"),
		TEXT("读本地相机正在用的配置里的一项：ginkgo.camera.get <路径>（写法同 ginkgo.camera.set）。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, const UWorld* World)
		{
			const AGinkgoPlayerCameraManager* Camera = FindLocalCamera(World);
			if (!Camera || Args.Num() < 1)
			{
				UE_LOG(LogGinkgoCamera, Warning, TEXT("[Camera] ginkgo.camera.get <路径>：%s"), Camera ? TEXT("参数不够") : TEXT("没有本地的 Ginkgo 相机"));
				return;
			}
			FConfigItem Item;
			FString Error;
			if (!ResolveConfigPath(GetMutableConfig(*Camera), Args[0], Item, Error))
			{
				UE_LOG(LogGinkgoCamera, Warning, TEXT("[Camera] ginkgo.camera.get %s：%s"), *Args[0], *Error);
				return;
			}
			UE_LOG(LogGinkgoCamera, Display, TEXT("[Camera] %s = %s"), *Args[0], *ExportItem(Item));
		}));

	FAutoConsoleCommandWithWorldAndArgs CommandList(
		TEXT("ginkgo.camera.list"),
		TEXT("列出本地相机正在用的配置里所有能调的项（路径 = 值），路径可以直接给 ginkgo.camera.set 用。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, const UWorld* World)
		{
			const AGinkgoPlayerCameraManager* Camera = FindLocalCamera(World);
			if (!Camera)
			{
				UE_LOG(LogGinkgoCamera, Warning, TEXT("[Camera] ginkgo.camera.list：没有本地的 Ginkgo 相机"));
				return;
			}
			UGinkgoCameraConfig& Config = GetMutableConfig(*Camera);
			UE_LOG(LogGinkgoCamera, Display, TEXT("[Camera] 相机配置 %s"), *Config.GetPathName());
			for (const FTunable& Tunable : CollectTunables(Config))
			{
				FConfigItem Item;
				FString Error;
				UE_LOG(LogGinkgoCamera, Display, TEXT("[Camera]   %s = %s"), *Tunable.Path, ResolveConfigPath(Config, Tunable.Path, Item, Error) ? *ExportItem(Item) : *Error);
			}
		}));
}
