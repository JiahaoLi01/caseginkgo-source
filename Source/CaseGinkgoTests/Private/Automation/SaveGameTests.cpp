#include "SaveGameTestTypes.h"

#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "PlatformFeatures.h"
#include "SaveGame/GinkgoSaveGameLibrary.h"
#include "SaveGame/GinkgoSaveGameProxyArchive.h"
#include "SaveGame/GinkgoSaveGameSubsystem.h"
#include "SaveGame/GinkgoSaveGameVersion.h"
#include "SaveGame/SaveNode/GinkgoSaveNode.h"
#include "SaveGame/SaveNode/GinkgoSaveNodeContext.h"
#include "SaveGame/SaveNode/GinkgoSaveNodeRegistry.h"
#include "SaveGameSystem.h"
#include "Serialization/Formatters/BinaryArchiveFormatter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/DevObjectVersion.h"
#include "UObject/Package.h"

AGinkgoSaveTestActor::AGinkgoSaveTestActor()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
}

bool AGinkgoSaveTestActor::OnSerialize_Implementation(FGinkgoSaveGameArchive& Archive, const bool bIsLoading)
{
	Archive.SerializeField(TEXT("Custom"), [this](FStructuredArchive::FSlot Slot) { Slot << Custom; });
	return UGinkgoSaveGameLibrary::SerializeActorTransform(Archive, this);
}

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const FGuid NodeA(0x3A0BFA3A, 0xFCB84C2F, 0xBA184B43, 0x6293253B);
	const FGuid NodeB(0x6206E3E9, 0xC88944BD, 0xAC265C4E, 0x0ED13F61);
	const FGuid NodeC(0x119731FB, 0x95374255, 0x9231ED93, 0x092A671E);
	const FGuid NodeD(0xD60FA034, 0xB7CF4D8F, 0xA9E6035F, 0xBE180845);

	FDevVersionRegistration GRegisterSaveTestNodeA(NodeA, 1, TEXT("GinkgoSaveTestNodeA"));
	FDevVersionRegistration GRegisterSaveTestNodeB(NodeB, 1, TEXT("GinkgoSaveTestNodeB"));
	FDevVersionRegistration GRegisterSaveTestNodeC(NodeC, 1, TEXT("GinkgoSaveTestNodeC"));
	FDevVersionRegistration GRegisterSaveTestNodeD(NodeD, 1, TEXT("GinkgoSaveTestNodeD"));

	/** 把调用的先后记下来的节点 */
	class FTestNode : public IGinkgoSaveNode
	{
	public:
		FTestNode(const FGuid& InId, TArray<FGuid> InDependencies, const TCHAR* InName, TArray<FString>& InEvents)
			: Id(InId), Dependencies(MoveTemp(InDependencies)), Name(InName), Events(InEvents)
		{
		}

		virtual FGuid GetNodeId() const override { return Id; }
		virtual FGameplayTag GetClassificationTag() const override { return FGameplayTag(); }
		virtual int32 GetCurrentVersion() const override { return Version; }
		virtual void GetDependencies(TArray<FGuid>& Out) const override { Out.Append(Dependencies); }

		virtual void Serialize(FGinkgoSaveNodeContext& Ctx) override
		{
			Events.Add(Name + (Ctx.IsLoading() ? TEXT(":load") : TEXT(":save")));
			Ctx.Field(TEXT("Value"), Value);
		}

		virtual void OnMissingFromSave(FGinkgoSaveNodeContext& Ctx) override { Events.Add(Name + TEXT(":missing")); }
		virtual void PostLoadGame(FGinkgoSaveNodeContext& Ctx) override { Events.Add(Name + TEXT(":post")); }

		FGuid Id;
		TArray<FGuid> Dependencies;
		FString Name;
		TArray<FString>& Events;
		int32 Value = 0;
		int32 Version = 1;
	};

	ISaveGameSystem& SaveSystem()
	{
		return *IPlatformFeaturesModule::Get().GetSaveGameSystem();
	}

	/** 一个独立的 GameInstance 和世界，不碰编辑器的世界，用完把存档删掉 */
	struct FSaveFixture
	{
		UGameInstance* GameInstance = nullptr;
		UWorld* World = nullptr;
		UGinkgoSaveGameSubsystem* Save = nullptr;
		FString Slot;

		FSaveFixture()
		{
			const FString Id = FGuid::NewGuid().ToString();
			Slot = TEXT("Automation_") + Id;
			GameInstance = NewObject<UGameInstance>(GEngine);
			GameInstance->AddToRoot();
			GameInstance->InitializeStandalone(*(TEXT("GinkgoSaveFixture") + Id), CreatePackage(*(TEXT("/Temp/GinkgoSaveFixture") + Id)));
			World = GameInstance->GetWorld();
			Save = GameInstance->GetSubsystem<UGinkgoSaveGameSubsystem>();

			// 世界的 Actor 没初始化时，AActor::ProcessEvent 不执行，接口的 Execute_ 什么也不做
			World->InitializeActorsForPlay(FURL());
		}

		~FSaveFixture()
		{
			GameInstance->Shutdown();
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			GameInstance->RemoveFromRoot();
			SaveSystem().DeleteGame(false, *Slot, 0);
			SaveSystem().DeleteGame(false, *(Slot + TEXT(".json")), 0);
		}
	};

	AGinkgoSaveTestActor* SpawnNamed(UWorld* World, const TCHAR* Name)
	{
		FActorSpawnParameters Params;
		Params.Name = Name;
		return World->SpawnActor<AGinkgoSaveTestActor>(Params);
	}

	/** 关卡里摆的 Actor：存档系统靠这个标记认它 */
	AGinkgoSaveTestActor* SpawnPlaced(UWorld* World, const TCHAR* Name)
	{
		AGinkgoSaveTestActor* Actor = SpawnNamed(World, Name);
		Actor->SetFlags(RF_WasLoaded);
		return Actor;
	}

	template <typename FActor = AGinkgoSaveTestActor>
	int32 CountActors(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<FActor> It(World); It; ++It)
		{
			++Count;
		}
		return Count;
	}

	/** 按序列化器的方式（代理 + 二进制的结构化存档）写一段节点字段 */
	template <typename FFill>
	TArray<uint8> WriteFields(FFill&& Fill)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		TGinkgoSaveGameProxyArchive<false> Proxy(Writer);
		FBinaryArchiveFormatter Formatter(Proxy);
		FStructuredArchive Structured(Formatter);
		FStructuredArchive::FRecord Record = Structured.Open().EnterRecord();
		{
			FGinkgoSaveNodeContext Ctx(Record, /*SavedVersion*/ 1, /*bIsLoading*/ false, nullptr);
			Fill(Ctx);
		}
		Structured.Close();
		return Bytes;
	}

	/** @return 读的过程没有出错 */
	template <typename FRead>
	bool ReadFields(const TArray<uint8>& Bytes, const int32 SavedVersion, FRead&& Read)
	{
		FMemoryReader Reader(Bytes);
		TGinkgoSaveGameProxyArchive<true> Proxy(Reader);
		FBinaryArchiveFormatter Formatter(Proxy);
		FStructuredArchive Structured(Formatter);
		FStructuredArchive::FRecord Record = Structured.Open().EnterRecord();
		{
			FGinkgoSaveNodeContext Ctx(Record, SavedVersion, /*bIsLoading*/ true, nullptr);
			Read(Ctx);
		}
		Structured.Close();
		return !Reader.IsError();
	}

	/** 存档文件解压后的内容（文件的布局见 TGinkgoSaveGameSerializer） */
	TArray<uint8> ReadRawSave(const FString& Slot)
	{
		TArray<uint8> File, Raw;
		SaveSystem().LoadGame(false, *Slot, 0, File);
		int64 RawSize = 0;
		FMemory::Memcpy(&RawSize, File.GetData(), sizeof(RawSize));
		Raw.SetNumUninitialized(RawSize);
		FCompression::UncompressMemory(NAME_Zlib, Raw.GetData(), RawSize, File.GetData() + sizeof(RawSize), File.Num() - sizeof(RawSize));
		return Raw;
	}

	void WriteRawSave(const FString& Slot, const TArray<uint8>& Raw)
	{
		int64 CompressedSize = 0;
		FCompression::CompressMemoryBound(NAME_Zlib, CompressedSize, Raw.Num());
		TArray<uint8> File;
		File.SetNumUninitialized(sizeof(int64) + CompressedSize);
		const int64 RawSize = Raw.Num();
		FMemory::Memcpy(File.GetData(), &RawSize, sizeof(RawSize));
		FCompression::CompressMemory(NAME_Zlib, File.GetData() + sizeof(RawSize), CompressedSize, Raw.GetData(), RawSize);
		File.SetNum(sizeof(RawSize) + CompressedSize);
		SaveSystem().SaveGame(false, *Slot, 0, File);
	}

	int32 FindBytes(const TArray<uint8>& Bytes, const void* Pattern, const int32 Size)
	{
		for (int32 Index = 0; Index + Size <= Bytes.Num(); ++Index)
		{
			if (FMemory::Memcmp(Bytes.GetData() + Index, Pattern, Size) == 0)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoSaveGameRegistryTest, "CaseGinkgo.SaveGame.Registry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoSaveGameRegistryTest::RunTest(const FString& Parameters)
{
	TArray<FString> Events;
	FGinkgoSaveNodeRegistry Registry;

	// 故意倒着注册：顺序只看依赖
	Registry.Register(MakeShared<FTestNode>(NodeC, TArray<FGuid>{NodeB}, TEXT("C"), Events));
	Registry.Register(MakeShared<FTestNode>(NodeD, TArray<FGuid>{NodeA}, TEXT("D"), Events));
	Registry.Register(MakeShared<FTestNode>(NodeB, TArray<FGuid>{NodeA}, TEXT("B"), Events));
	Registry.Register(MakeShared<FTestNode>(NodeA, TArray<FGuid>{}, TEXT("A"), Events));

	TestEqual(TEXT("四个节点"), Registry.Num(), 4);
	{
		const TArray<FGuid>& Order = Registry.GetSaveOrder();
		TestEqual(TEXT("排序里四个都在"), Order.Num(), 4);
		TestTrue(TEXT("A 在 B 前面"), Order.IndexOfByKey(NodeA) < Order.IndexOfByKey(NodeB));
		TestTrue(TEXT("B 在 C 前面"), Order.IndexOfByKey(NodeB) < Order.IndexOfByKey(NodeC));
		TestTrue(TEXT("A 在 D 前面"), Order.IndexOfByKey(NodeA) < Order.IndexOfByKey(NodeD));
	}
	TestNotNull(TEXT("找得到"), Registry.Find(NodeB));

	Registry.Unregister(NodeC);
	TestNull(TEXT("反注册以后找不到"), Registry.Find(NodeC));
	TestEqual(TEXT("反注册以后重新排"), Registry.GetSaveOrder().Num(), 3);
	Registry.Unregister(NodeC);
	TestEqual(TEXT("反注册两次没事"), Registry.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoSaveGameFieldsTest, "CaseGinkgo.SaveGame.Fields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoSaveGameFieldsTest::RunTest(const FString& Parameters)
{
	const TArray<uint8> Older = WriteFields([](FGinkgoSaveNodeContext& Ctx)
	{
		int32 A = 1;
		FString C = TEXT("三");
		Ctx.Field(TEXT("A"), A);
		Ctx.Field(TEXT("C"), C);
	});

	// 新代码在中间加了一个字段：旧存档读得出来，后面的字段不错位
	{
		int32 A = 0, B = -1;
		FString C;
		const bool bRead = ReadFields(Older, 1, [&](FGinkgoSaveNodeContext& Ctx)
		{
			Ctx.Field(TEXT("A"), A);
			Ctx.OptionalField(TEXT("B"), B, 42);
			Ctx.Field(TEXT("C"), C);
		});
		TestTrue(TEXT("加字段：读得出来"), bRead);
		TestEqual(TEXT("加字段：前面的"), A, 1);
		TestEqual(TEXT("加字段：新的给默认值"), B, 42);
		TestEqual(TEXT("加字段：后面的不错位"), C, FString(TEXT("三")));
	}

	const TArray<uint8> Newer = WriteFields([](FGinkgoSaveNodeContext& Ctx)
	{
		int32 A = 1, B = 2;
		FString C = TEXT("三");
		Ctx.Field(TEXT("A"), A);
		Ctx.OptionalField(TEXT("B"), B, 42);
		Ctx.Field(TEXT("C"), C);
	});

	// 字段删了、顺序换了
	{
		int32 A = 0;
		FString C;
		const bool bRead = ReadFields(Newer, 1, [&](FGinkgoSaveNodeContext& Ctx)
		{
			Ctx.Field(TEXT("C"), C);
			Ctx.Field(TEXT("A"), A);
		});
		TestTrue(TEXT("删字段、换顺序：读得出来"), bRead);
		TestEqual(TEXT("换顺序：A"), A, 1);
		TestEqual(TEXT("换顺序：C"), C, FString(TEXT("三")));
	}

	// 存档里有的可选字段读到的是存的值，不是默认值
	{
		int32 B = -1;
		ReadFields(Newer, 1, [&](FGinkgoSaveNodeContext& Ctx) { Ctx.OptionalField(TEXT("B"), B, 42); });
		TestEqual(TEXT("有的可选字段读存的值"), B, 2);
	}

	// 版本门：存档的版本比字段引入的旧时不读
	{
		int32 B = -1;
		const bool bRead = ReadFields(Newer, 1, [&](FGinkgoSaveNodeContext& Ctx) { Ctx.FieldSince(TEXT("B"), B, 2, 7); });
		TestTrue(TEXT("版本门：不读不算错"), bRead);
		TestEqual(TEXT("版本门：旧版本给默认值"), B, 7);
		ReadFields(Newer, 2, [&](FGinkgoSaveNodeContext& Ctx) { Ctx.FieldSince(TEXT("B"), B, 2, 7); });
		TestEqual(TEXT("版本门：够新就读"), B, 2);
	}

	// 必填的缺了：存档损坏，读档中止
	{
		AddExpectedError(TEXT("必填字段 Missing"), EAutomationExpectedErrorFlags::Contains, 1);
		int32 Value = 0;
		const bool bRead = ReadFields(Older, 1, [&](FGinkgoSaveNodeContext& Ctx) { Ctx.Field(TEXT("Missing"), Value); });
		TestFalse(TEXT("必填的缺了算错"), bRead);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoSaveGameNodesTest, "CaseGinkgo.SaveGame.Nodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoSaveGameNodesTest::RunTest(const FString& Parameters)
{
	TArray<FString> Events;
	FSaveFixture Fixture;
	// 游戏子系统可在初始化时注册自己的节点；此用例只计算自己增加的三个，不能把框架总数写死。
	const int32 InitialNodeCount = Fixture.Save->GetRegistry().Num();
	TestTrue(TEXT("至少包含两个框架节点"), InitialNodeCount >= 2);

	const TSharedRef<FTestNode> Second = MakeShared<FTestNode>(NodeB, TArray<FGuid>{NodeA}, TEXT("B"), Events);
	const TSharedRef<FTestNode> First = MakeShared<FTestNode>(NodeA, TArray<FGuid>{}, TEXT("A"), Events);
	const TSharedRef<FTestNode> Removed = MakeShared<FTestNode>(NodeD, TArray<FGuid>{}, TEXT("D"), Events);
	Fixture.Save->GetRegistry().Register(Second);
	Fixture.Save->GetRegistry().Register(First);
	Fixture.Save->GetRegistry().Register(Removed);

	First->Value = 10;
	Second->Value = 20;
	int32 BeforeSave = 0;
	Fixture.Save->OnBeforeSaveGame.AddLambda([&BeforeSave] { ++BeforeSave; });
	if (!TestTrue(TEXT("存档"), Fixture.Save->Save(Fixture.Slot)))
	{
		AddError(Fixture.Save->GetLastError());
		return false;
	}
	TestEqual(TEXT("存档前广播一次"), BeforeSave, 1);
	TestTrue(TEXT("存档时依赖的在前面"), Events.IndexOfByKey(TEXT("A:save")) < Events.IndexOfByKey(TEXT("B:save")));

	// 存档之后：一个节点去掉了，一个节点是新加的
	Fixture.Save->GetRegistry().Unregister(NodeD);
	const TSharedRef<FTestNode> Added = MakeShared<FTestNode>(NodeC, TArray<FGuid>{NodeB}, TEXT("C"), Events);
	Fixture.Save->GetRegistry().Register(Added);

	First->Value = 0;
	Second->Value = 0;
	Events.Reset();
	Fixture.Save->OnSaveGameLoaded.AddLambda([&Events] { Events.Add(TEXT("loaded")); });

	if (!TestTrue(TEXT("读档"), Fixture.Save->Load(Fixture.Slot)))
	{
		AddError(Fixture.Save->GetLastError());
		return false;
	}
	TestFalse(TEXT("原地读档结束后不在读档状态"), Fixture.Save->IsLoadingSaveGame());
	TestEqual(TEXT("A 的数据回来了"), First->Value, 10);
	TestEqual(TEXT("B 的数据回来了"), Second->Value, 20);

	const TArray<FString> Expected = {TEXT("A:load"), TEXT("B:load"), TEXT("C:missing"), TEXT("A:post"), TEXT("B:post"), TEXT("C:post"), TEXT("loaded")};
	TestEqual(TEXT("先恢复数据，再 PostLoadGame，最后广播；去掉的节点没有调用"), FString::Join(Events, TEXT(" ")), FString::Join(Expected, TEXT(" ")));

	// 存档来自更新的版本：节点的版本比代码新，读档中止
	First->Version = 3;
	TestTrue(TEXT("新版本存档"), Fixture.Save->Save(Fixture.Slot));
	First->Version = 1;
	AddExpectedError(TEXT("比代码的"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("存档比代码新：读不了"), Fixture.Save->Load(Fixture.Slot));
	TestFalse(TEXT("读不了也不卡在读档状态"), Fixture.Save->IsLoadingSaveGame());
	TestFalse(TEXT("读不了有原因"), Fixture.Save->GetLastError().IsEmpty());
	First->Version = 3;
	TestTrue(TEXT("版本够了又能读"), Fixture.Save->Load(Fixture.Slot));

	// 另一个 GameInstance（多开 PIE）有自己的注册表
	{
		FSaveFixture Other;
		TestTrue(TEXT("注册表是每个 GameInstance 一份"), &Other.Save->GetRegistry() != &Fixture.Save->GetRegistry());
	}
	TestEqual(TEXT("关掉别的 GameInstance 不影响这边的节点"), Fixture.Save->GetRegistry().Num(), InitialNodeCount + 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoSaveGameActorsTest, "CaseGinkgo.SaveGame.Actors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoSaveGameActorsTest::RunTest(const FString& Parameters)
{
	FSaveFixture Fixture;
	UWorld* World = Fixture.World;
	UGinkgoSaveGameSubsystem* Save = Fixture.Save;

	AGinkgoSaveTestActor* Placed = SpawnPlaced(World, TEXT("Placed"));
	AGinkgoSaveTestActor* Spawned = SpawnNamed(World, TEXT("Spawned"));
	TestEqual(TEXT("生成时自动追踪"), Save->GetSaveGameActors().Num(), 2);

	Placed->Count = 7;
	Placed->Label = TEXT("存档时的");
	Placed->Numbers = {1, 2, 3};
	Placed->NotSaved = 3;
	Placed->Custom = 11;
	Placed->Friend = Spawned;
	Placed->SoftFriend = Spawned;
	Placed->SetActorLocation(FVector(100.0, 200.0, 300.0));
	Spawned->Count = 21;

	// 失效的追踪项（流送关卡卸载时不走 Destroy）不进存档，也不崩
	Save->GetSaveGameActors().Add(TWeakObjectPtr<AActor>());

	if (!TestTrue(TEXT("存档"), Save->Save(Fixture.Slot)))
	{
		AddError(Save->GetLastError());
		return false;
	}

#if WITH_TEXT_ARCHIVE_SUPPORT
	{
		TArray<uint8> JsonBytes;
		TestTrue(TEXT("开发期多写一份 JSON"), SaveSystem().LoadGame(false, *(Fixture.Slot + TEXT(".json")), 0, JsonBytes));
		FString Json;
		FFileHelper::BufferToString(Json, JsonBytes.GetData(), JsonBytes.Num());
		TestTrue(TEXT("JSON 里节点用分类标签当键"), Json.Contains(TEXT("SaveNode.Core.Actors")));
		TestTrue(TEXT("JSON 里有 Actor 的属性"), Json.Contains(TEXT("WithDefault")));
	}
#endif

	// 原地读档：Actor 都还在，只把数据改回去
	Placed->Count = 0;
	Placed->WithDefault = 99;
	Placed->Label.Reset();
	Placed->Numbers.Reset();
	Placed->NotSaved = 4;
	Placed->Custom = 0;
	Placed->Friend = nullptr;
	Placed->SoftFriend.Reset();
	Placed->SetActorLocation(FVector::ZeroVector);
	Spawned->Count = 0;

	if (!TestTrue(TEXT("原地读档"), Save->Load(Fixture.Slot)))
	{
		AddError(Save->GetLastError());
		return false;
	}
	TestEqual(TEXT("属性"), Placed->Count, 7);
	TestEqual(TEXT("存档时等于默认值的属性也回来"), Placed->WithDefault, 5);
	TestEqual(TEXT("字符串"), Placed->Label, FString(TEXT("存档时的")));
	TestEqual(TEXT("数组"), Placed->Numbers.Num(), 3);
	TestEqual(TEXT("没标 SaveGame 的不动"), Placed->NotSaved, 4);
	TestEqual(TEXT("OnSerialize 写的"), Placed->Custom, 11);
	TestEqual(TEXT("位置"), Placed->GetActorLocation(), FVector(100.0, 200.0, 300.0));
	TestTrue(TEXT("引用接回存档时的 Actor"), Placed->Friend == Spawned);
	TestEqual(TEXT("生成的 Actor 还在：接着用它"), Spawned->Count, 21);
	TestEqual(TEXT("没有多生成"), CountActors(World), 2);
	TestTrue(TEXT("读档没有错误"), Save->GetLastError().IsEmpty());

	// 生成的 Actor 存档后销毁了、还没回收，名字还占着：重新生成，引用接到新的上面
	Spawned->Destroy();
	Placed->Friend = nullptr;
	TestTrue(TEXT("生成的 Actor 没了也能读"), Save->Load(Fixture.Slot));
	AGinkgoSaveTestActor* Respawned = Cast<AGinkgoSaveTestActor>(Placed->Friend);
	if (TestNotNull(TEXT("引用接到重新生成的 Actor"), Respawned))
	{
		TestTrue(TEXT("是新的"), Respawned != Spawned);
		TestEqual(TEXT("新的拿到数据"), Respawned->Count, 21);
		TestTrue(TEXT("软引用也接上"), Placed->SoftFriend.Get() == Respawned);
	}
	TestEqual(TEXT("重新生成了一个，没有多"), CountActors(World), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoSaveGameSpawnIdTest, "CaseGinkgo.SaveGame.SpawnId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoSaveGameSpawnIdTest::RunTest(const FString& Parameters)
{
	FSaveFixture Fixture;
	UWorld* World = Fixture.World;
	UGinkgoSaveGameSubsystem* Save = Fixture.Save;

	const FGuid PlayerId = FGuid::NewGuid();
	AGinkgoSaveTestActor* Placed = SpawnPlaced(World, TEXT("Placed"));
	AGinkgoSaveTestSpawnActor* Player = World->SpawnActor<AGinkgoSaveTestSpawnActor>();
	Player->SpawnId = PlayerId;
	Player->Count = 31;
	Placed->Friend = Player;

	if (!TestTrue(TEXT("存档"), Save->Save(Fixture.Slot)))
	{
		AddError(Save->GetLastError());
		return false;
	}

	// 读档之前别人已经生成好了一个（玩家角色）：数据读到它身上，不再生成
	Player->Destroy();
	AGinkgoSaveTestSpawnActor* NewPlayer = World->SpawnActor<AGinkgoSaveTestSpawnActor>();
	NewPlayer->SpawnId = PlayerId;
	Placed->Friend = nullptr;

	TestTrue(TEXT("读档"), Save->Load(Fixture.Slot));
	TestEqual(TEXT("数据读到已经生成的那个"), NewPlayer->Count, 31);
	TestTrue(TEXT("引用接到已经生成的那个"), Placed->Friend == NewPlayer);
	TestEqual(TEXT("没有多生成"), CountActors<AGinkgoSaveTestSpawnActor>(World), 1);

	// 没有人生成：存档系统生成，并把 ID 给它
	NewPlayer->Destroy();
	Placed->Friend = nullptr;
	TestTrue(TEXT("读档"), Save->Load(Fixture.Slot));
	AGinkgoSaveTestSpawnActor* Restored = Cast<AGinkgoSaveTestSpawnActor>(Placed->Friend);
	if (TestNotNull(TEXT("生成了一个"), Restored))
	{
		TestEqual(TEXT("ID 给它了"), Restored->SpawnId, PlayerId);
		TestEqual(TEXT("数据给它了"), Restored->Count, 31);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoSaveGameMissingActorTest, "CaseGinkgo.SaveGame.MissingActor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoSaveGameMissingActorTest::RunTest(const FString& Parameters)
{
	FSaveFixture Fixture;

	// 先追踪的先写：缺的那个在前面，才能证明后面的不受影响
	AGinkgoSaveTestActor* Gone = SpawnPlaced(Fixture.World, TEXT("Gone"));
	AGinkgoSaveTestActor* After = SpawnPlaced(Fixture.World, TEXT("After"));
	Gone->Count = 1;
	After->Count = 2;

	if (!TestTrue(TEXT("存档"), Fixture.Save->Save(Fixture.Slot)))
	{
		AddError(Fixture.Save->GetLastError());
		return false;
	}

	Gone->Destroy();
	After->Count = 0;

	AddExpectedMessagePlain(TEXT("Actor Gone"), ELogVerbosity::Warning);
	TestTrue(TEXT("关卡里少了一个 Actor 也能读"), Fixture.Save->Load(Fixture.Slot));
	TestEqual(TEXT("后面的 Actor 不受影响"), After->Count, 2);

	// 生成的 Actor 的类没有了（删了、改名了）：改文件里的类名（同样长）
	{
		FSaveFixture ClassFixture;
		AGinkgoSaveTestActor* Spawned = SpawnNamed(ClassFixture.World, TEXT("Spawned"));
		AGinkgoSaveTestActor* Later = SpawnPlaced(ClassFixture.World, TEXT("Later"));
		Spawned->Count = 1;
		Later->Count = 2;
		TestTrue(TEXT("存档"), ClassFixture.Save->Save(ClassFixture.Slot));

		TArray<uint8> Raw = ReadRawSave(ClassFixture.Slot);
		const FString ClassName = Spawned->GetClass()->GetName();
		const auto ClassBytes = StringCast<ANSICHAR>(*ClassName);
		const int32 Found = FindBytes(Raw, ClassBytes.Get(), ClassBytes.Length());
		if (TestTrue(TEXT("文件里有类名"), Found != INDEX_NONE))
		{
			Raw[Found + ClassBytes.Length() - 1] = 'x';
			WriteRawSave(ClassFixture.Slot, Raw);

			Spawned->Destroy();
			Later->Count = 0;

			AddExpectedMessagePlain(TEXT("GinkgoSaveTestActox"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
			TestTrue(TEXT("类没有了也能读"), ClassFixture.Save->Load(ClassFixture.Slot));
			TestEqual(TEXT("类没有了：后面的 Actor 不受影响"), Later->Count, 2);
			TestEqual(TEXT("类没有了：没有生成"), CountActors(ClassFixture.World), 1);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoSaveGameDestroyedActorsTest, "CaseGinkgo.SaveGame.DestroyedActors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoSaveGameDestroyedActorsTest::RunTest(const FString& Parameters)
{
	FSaveFixture Fixture;
	UWorld* World = Fixture.World;
	UGinkgoSaveGameSubsystem* Save = Fixture.Save;

	AGinkgoSaveTestActor* Crate = SpawnPlaced(World, TEXT("Crate"));
	AGinkgoSaveTestActor* Spawned = SpawnNamed(World, TEXT("Spawned"));
	Crate->Destroy();
	Spawned->Destroy();
	TestEqual(TEXT("只记关卡里摆的"), Save->GetDestroyedLevelActors().Num(), 1);

	TestTrue(TEXT("存档"), Save->Save(Fixture.Slot));

	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestNull(TEXT("已经回收了"), FindObjectFast<AActor>(World->GetCurrentLevel(), TEXT("Crate")));

	TestTrue(TEXT("原地读档"), Save->Load(Fixture.Slot));
	TestEqual(TEXT("回收了也记着"), Save->GetDestroyedLevelActors().Num(), 1);

	TestTrue(TEXT("再存一次"), Save->Save(Fixture.Slot));

	// 从头载入地图时它又在那里，读档要把它销毁
	AGinkgoSaveTestActor* Reloaded = SpawnPlaced(World, TEXT("Crate"));
	TestTrue(TEXT("读档"), Save->Load(Fixture.Slot));
	TestFalse(TEXT("重新载入的被销毁了"), IsValid(Reloaded));
	TestEqual(TEXT("还是记着一个"), Save->GetDestroyedLevelActors().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoSaveGameBadFilesTest, "CaseGinkgo.SaveGame.BadFiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoSaveGameBadFilesTest::RunTest(const FString& Parameters)
{
	FSaveFixture Fixture;
	UGinkgoSaveGameSubsystem* Save = Fixture.Save;

	AddExpectedMessagePlain(TEXT("Failed to read file"), ELogVerbosity::Warning);
	TestFalse(TEXT("没有这个存档"), Save->Load(Fixture.Slot));
	TestFalse(TEXT("没有这个存档：有原因"), Save->GetLastError().IsEmpty());

	if (!TestTrue(TEXT("存档"), Save->Save(Fixture.Slot)))
	{
		AddError(Save->GetLastError());
		return false;
	}
	TArray<uint8> Genuine;
	SaveSystem().LoadGame(false, *Fixture.Slot, 0, Genuine);

	const auto ExpectRejected = [&](const FString& What, const TArray<uint8>& File)
	{
		SaveSystem().SaveGame(false, *Fixture.Slot, 0, File);
		TestFalse(What + TEXT("：读不了，不崩"), Save->Load(Fixture.Slot));
		TestFalse(What + TEXT("：不卡在读档状态"), Save->IsLoadingSaveGame());
		TestFalse(What + TEXT("：有原因"), Save->GetLastError().IsEmpty());
	};

	ExpectRejected(TEXT("空文件"), {});
	ExpectRejected(TEXT("比大小的前缀还短"), {1, 2, 3});

	TArray<uint8> HugeSize = Genuine;
	HugeSize[7] = 0x7F;
	ExpectRejected(TEXT("大小离谱"), HugeSize);

	// 下面几种解压时报错，引擎打一条 Error
	AddExpectedError(TEXT("Failed to uncompress memory"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedMessagePlain(TEXT("appUncompressMemoryZLIB failed"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);

	TArray<uint8> Truncated = Genuine;
	Truncated.SetNum(Genuine.Num() / 2);
	ExpectRejected(TEXT("只写了一半"), Truncated);

	TArray<uint8> Flipped = Genuine;
	Flipped[Genuine.Num() / 2] ^= 0xFF;
	ExpectRejected(TEXT("坏了一个字节"), Flipped);

	TArray<uint8> Garbage;
	Garbage.Append(Genuine.GetData(), sizeof(int64));
	Garbage.AddZeroed(256);
	ExpectRejected(TEXT("不是 zlib"), Garbage);

	// 解压得出来，但全局版本的 GUID 不是本项目的：别的项目的存档
	{
		SaveSystem().SaveGame(false, *Fixture.Slot, 0, Genuine);
		TArray<uint8> Raw = ReadRawSave(Fixture.Slot);
		const int32 Found = FindBytes(Raw, &FGinkgoSaveGameVersion::GUID, sizeof(FGuid));
		if (TestTrue(TEXT("版本表里有全局版本"), Found != INDEX_NONE))
		{
			Raw[Found] ^= 0xFF;
			WriteRawSave(Fixture.Slot, Raw);
			TestFalse(TEXT("别的项目的存档：读不了"), Save->Load(Fixture.Slot));
			TestTrue(TEXT("别的项目的存档：原因对"), Save->GetLastError().Contains(TEXT("不属于当前项目")));
			TestFalse(TEXT("别的项目的存档：不卡在读档状态"), Save->IsLoadingSaveGame());
		}
	}

	// 存档所在的地图不在了：改文件头里的地图名（同样长）
	{
		SaveSystem().SaveGame(false, *Fixture.Slot, 0, Genuine);
		TArray<uint8> Raw = ReadRawSave(Fixture.Slot);
		const FString Map = Fixture.World->GetOutermost()->GetName();
		const auto MapBytes = StringCast<ANSICHAR>(*Map);
		const int32 Found = FindBytes(Raw, MapBytes.Get(), MapBytes.Length());
		if (TestTrue(TEXT("文件头里有地图名"), Found != INDEX_NONE))
		{
			uint8& LastChar = Raw[Found + MapBytes.Length() - 1];
			LastChar = LastChar == 'A' ? 'B' : 'A';
			WriteRawSave(Fixture.Slot, Raw);
			TestFalse(TEXT("地图不在了：读不了"), Save->Load(Fixture.Slot));
			TestTrue(TEXT("地图不在了：原因对"), Save->GetLastError().Contains(TEXT("地图不存在")));
			TestFalse(TEXT("地图不在了：不卡在读档状态"), Save->IsLoadingSaveGame());
		}
	}

	// 好的文件放回去，还能读
	SaveSystem().SaveGame(false, *Fixture.Slot, 0, Genuine);
	TestTrue(TEXT("好的文件还能读"), Save->Load(Fixture.Slot));
	return true;
}

#endif
