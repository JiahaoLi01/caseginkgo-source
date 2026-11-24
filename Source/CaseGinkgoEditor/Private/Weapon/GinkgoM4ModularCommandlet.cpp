#include "Weapon/GinkgoM4ModularCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/PackageName.h"
#include "MuCO/CustomizableObject.h"
#include "MuCOE/CustomizableObjectFactory.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "Weapon/GinkgoModularWeaponData.h"
#include "Weapon/GinkgoWeaponDefinition.h"

namespace
{
	template<class T> T* Field(const UStruct* Type, const TCHAR* Name)
	{
		auto* Result = FindFProperty<T>(Type, Name);
		checkf(Result, TEXT("Mutable 字段不存在：%s.%s"), *Type->GetName(), Name);
		return Result;
	}

	/** 反射适配仅限资产生成工具；字段名与 UE 5.8 Mutable 节点对应，缺失时明确失败。 */
	void SetText(UObject& Object, const TCHAR* Name, const FString& Value)
	{
		auto* Property = Field<FStrProperty>(Object.GetClass(), Name);
		Property->SetPropertyValue_InContainer(&Object, Value);
	}

	void SetName(UObject& Object, const TCHAR* Name, FName Value)
	{
		Field<FNameProperty>(Object.GetClass(), Name)->SetPropertyValue_InContainer(&Object, Value);
	}

	UEdGraphNode* Node(UEdGraph& Graph, const TCHAR* ClassName, int32 X, int32 Y)
	{
		UClass* Type = LoadClass<UEdGraphNode>(nullptr, *(FString(TEXT("/Script/CustomizableObjectEditor.")) + ClassName));
		checkf(Type, TEXT("找不到 Mutable 节点 %s"), ClassName);
		auto* Result = NewObject<UEdGraphNode>(&Graph, Type, NAME_None, RF_Transactional);
		Graph.AddNode(Result, false, false);
		Result->CreateNewGuid();
		Result->NodePosX = X;
		Result->NodePosY = Y;
		return Result;
	}

	UEdGraphPin* Pin(UEdGraphNode& Node, EEdGraphPinDirection Direction, const TCHAR* Category)
	{
		for (auto* Item : Node.Pins)
		{
			if (!Item->bOrphanedPin && Item->Direction == Direction && Item->PinType.PinCategory == Category) { return Item; }
		}
		return nullptr;
	}

	void Link(UEdGraphPin* From, UEdGraphPin* To)
	{
		checkf(From && To, TEXT("Mutable 图连接缺少引脚"));
		checkf(From->GetOwningNode()->GetSchema()->TryCreateConnection(From, To), TEXT("Mutable 无法连接 %s -> %s"), *From->PinName.ToString(), *To->PinName.ToString());
	}

	UEdGraphPin* MeshNode(UEdGraph& Graph, const FString& Name, USkeletalMesh* Mesh, int32 X, int32 Y)
	{
		check(Mesh);
		auto* Item = Node(Graph, TEXT("CustomizableObjectNodeSkeletalMeshParameter"), X, Y);
		SetText(*Item, TEXT("ParameterName"), TEXT("Source_") + Name);
		for (const TCHAR* PropertyName : {TEXT("DefaultValue"), TEXT("ReferenceValue")})
		{
			Field<FSoftObjectProperty>(Item->GetClass(), PropertyName)->SetPropertyValue_InContainer(Item, FSoftObjectPtr(Mesh));
		}
		Item->AllocateDefaultPins();
		return Pin(*Item, EGPD_Output, TEXT("skeletalMesh"));
	}

	bool Save(UObject& Asset)
	{
		Asset.MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Asset.GetPackage(), &Asset, *FPackageName::LongPackageNameToFilename(Asset.GetPackage()->GetName(), FPackageName::GetAssetPackageExtension()), Args);
	}
}

UGinkgoM4ModularCommandlet::UGinkgoM4ModularCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UGinkgoM4ModularCommandlet::Main(const FString& Params)
{
	auto* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M4/DA_M4.DA_M4"));
	if (!Weapon || !Weapon->Modularity) { UE_LOG(LogTemp, Error, TEXT("先生成 M4 武器与配件数据")); return 1; }
	const TCHAR* PackageName = TEXT("/Game/Weapons/M4/CO_M4");
	UPackage* Package = CreatePackage(PackageName);
	Package->FullyLoad();
	// 工具可重复运行，重建图但保留资产对象路径，武器和已摆放实例不需要重新引用。
	auto* Factory = NewObject<UCustomizableObjectFactory>();
	auto* Object = Cast<UCustomizableObject>(Factory->FactoryCreateNew(UCustomizableObject::StaticClass(), Package, TEXT("CO_M4"), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
	if (!Object) { return 1; }
	TArray<UObject*> Children;
	GetObjectsWithOuter(Object, Children, EGetObjectsFlags::None);
	UEdGraph* Graph = nullptr;
	for (UObject* Child : Children) { if (auto* Candidate = Cast<UEdGraph>(Child)) { Graph = Candidate; break; } }
	if (!Graph) { UE_LOG(LogTemp, Error, TEXT("Mutable 工厂未创建源图")); return 1; }
	UEdGraphNode* Root = nullptr;
	for (UEdGraphNode* Candidate : Graph->Nodes)
	{
		if (Candidate->GetClass()->GetName() == TEXT("CustomizableObjectNodeObject")) { Root = Candidate; break; }
	}
	check(Root);
	UEdGraphPin* Current = MeshNode(*Graph, TEXT("Core"), LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Weapons/M4/SK_M4_Core.SK_M4_Core")), -2400, 0);
	TArray<FName> Slots;
	for (const auto& Mount : Weapon->Attachments) { Slots.AddUnique(Mount.Mount); }
	int32 Row = 0;
	for (FName Slot : Slots)
	{
		const auto Choices = Weapon->Attachments.FilterByPredicate([Slot](const auto& Mount) { return Mount.Mount == Slot && Mount.Part; });
		auto* Enum = Node(*Graph, TEXT("CustomizableObjectNodeEnumParameter"), -2100 + Row * 500, 600);
		SetText(*Enum, TEXT("ParameterName"), Slot.ToString());
		auto* Values = Field<FArrayProperty>(Enum->GetClass(), TEXT("Values"));
		FScriptArrayHelper Array(Values, Values->ContainerPtrToValuePtr<void>(Enum));
		Array.Resize(Choices.Num());
		auto* Element = CastFieldChecked<FStructProperty>(Values->Inner);
		auto* Name = Field<FStrProperty>(Element->Struct, TEXT("Name"));
		for (int32 Index = 0; Index < Choices.Num(); ++Index)
		{
			Name->SetPropertyValue_InContainer(Array.GetRawPtr(Index), Choices[Index].Part->ModularOption.ToString());
			if (Choices[Index].bDefault) { Field<FIntProperty>(Enum->GetClass(), TEXT("DefaultIndex"))->SetPropertyValue_InContainer(Enum, Index); }
		}
		Enum->AllocateDefaultPins();
		auto* Switch = Node(*Graph, TEXT("CONodeSwitch"), -1700 + Row * 500, 0);
		SetName(*Switch, TEXT("PinType"), TEXT("skeletalMesh"));
		Switch->AllocateDefaultPins();
		Link(Pin(*Enum, EGPD_Output, TEXT("enum")), Switch->FindPin(TEXT("Switch Parameter")));
		// 枚举连接会触发重建，生成每个选项的输入引脚。
		for (int32 Index = 0; Index < Choices.Num(); ++Index)
		{
			const auto* Part = Choices[Index].Part.Get();
			UEdGraphPin* Choice = Current;
			if (!Part->bEmptyModule)
			{
				auto* Mesh = MeshNode(*Graph, Part->ModularOption.ToString(), Part->ModuleMesh.LoadSynchronous(), -2300 + Row * 500, 900 + Index * 250);
				auto* Merge = Node(*Graph, TEXT("CONodeSkeletalMeshMerge"), -1950 + Row * 500, 900 + Index * 250);
				Merge->AllocateDefaultPins();
				Link(Current, Merge->FindPin(TEXT("Base")));
				Link(Mesh, Merge->FindPin(TEXT("Added")));
				Choice = Pin(*Merge, EGPD_Output, TEXT("skeletalMesh"));
			}
			Link(Choice, Switch->FindPin(Part->ModularOption));
		}
		Current = Pin(*Switch, EGPD_Output, TEXT("skeletalMesh"));
		++Row;
	}
	auto* Component = Node(*Graph, TEXT("CONodeComponentSkeletalMesh"), 2000, 0);
	SetName(*Component, TEXT("ComponentName"), Weapon->Modularity->ComponentName);
	Field<FObjectProperty>(Component->GetClass(), TEXT("ReferenceSkeletalMesh"))->SetObjectPropertyValue_InContainer(Component, Weapon->SkeletalMesh.LoadSynchronous());
	Component->AllocateDefaultPins();
	// UE 5.8 将可编辑的 Skeletal Mesh 值与最终 UObject 输出分开，显式经过 Object Make。
	auto* MakeObject = Node(*Graph, TEXT("CONodeSkeletalMeshObjectMake"), 1700, 0);
	SetText(*MakeObject, TEXT("SkeletalMeshName"), TEXT("M4"));
	MakeObject->AllocateDefaultPins();
	Link(Current, Pin(*MakeObject, EGPD_Input, TEXT("skeletalMesh")));
	Link(Pin(*MakeObject, EGPD_Output, TEXT("passThroughSkeletalMesh")), Pin(*Component, EGPD_Input, TEXT("passThroughSkeletalMesh")));
	Link(Pin(*Component, EGPD_Output, TEXT("component")), Root->FindPin(TEXT("Components")));
	Root->NodePosX = 2500;
	FAssetRegistryModule::AssetCreated(Object);
	FCompileParams Compile;
	Compile.bAsync = false;
	Compile.bSkipIfNotOutOfDate = false;
	Compile.bGatherReferences = true;
	Object->Compile(Compile);
	if (!Object->IsCompiled()) { UE_LOG(LogTemp, Error, TEXT("M4 Mutable 图编译失败")); return 1; }
	Weapon->Modularity->Object = Object;
	if (!Save(*Object) || !Save(*Weapon->Modularity)) { return 1; }
	UE_LOG(LogTemp, Display, TEXT("M4_MUTABLE_COMPLETE slots=%d choices=%d"), Slots.Num(), Weapon->Attachments.Num());
	return 0;
}
