#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Checkpoint/GinkgoCheckpoint.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundSource.h"
#include "Sound/SoundWave.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCheckpointAudioTest, "CaseGinkgo.Checkpoint.RandomAudioGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGinkgoCheckpointAudioTest::RunTest(const FString& Parameters)
{
	auto* Sound = Cast<UMetaSoundSource>(GetDefault<AGinkgoCheckpoint>()->Announcement.Sound.LoadSynchronous());
	if (!TestNotNull(TEXT("默认地点音效是真实 MetaSound"), Sound)) { return false; }
	const auto& Document = Sound->GetConstDocument();
	const auto& Graph = Document.RootGraph.GetConstDefaultGraph();
	auto FindNode = [&](const FName Namespace, const FName Name, const FName Variant) -> const FMetasoundFrontendNode*
	{
		const auto* Class = Document.Dependencies.FindByPredicate([&](const auto& Item)
		{
			const auto& Key = Item.Metadata.GetClassName();
			return Key.Namespace == Namespace && Key.Name == Name && Key.Variant == Variant;
		});
		return Class ? Graph.Nodes.FindByPredicate([&](const auto& Node) { return Node.ClassID == Class->ID; }) : nullptr;
	};
	const auto* Picker = FindNode(TEXT("Array"), TEXT("Random Get"), TEXT("WaveAsset:Array"));
	const auto* Player = FindNode(TEXT("UE"), TEXT("Wave Player"), TEXT("Stereo"));
	if (!TestNotNull(TEXT("随机取样节点"), Picker) || !TestNotNull(TEXT("双声道播放器"), Player)) { return false; }
	auto Literal = [&](const FName Name) -> const FMetasoundFrontendLiteral*
	{
		const auto* Input = Picker->Interface.Inputs.FindByPredicate([&](const auto& Vertex) { return Vertex.Name == Name; });
		if (!Input) { return nullptr; }
		const auto* Value = Picker->InputLiterals.FindByPredicate([&](const auto& Item) { return Item.VertexID == Input->VertexID; });
		return Value ? &Value->Value : nullptr;
	};
	const auto* Samples = Literal(TEXT("In Array"));
	TArray<UObject*> Objects;
	if (TestTrue(TEXT("样本数组已落盘"), Samples && Samples->TryGet(Objects)))
	{
		TestEqual(TEXT("包含全部六个 take"), Objects.Num(), 6);
		TSet<UObject*> Unique;
		for (UObject* Object : Objects)
		{
			Unique.Add(Object);
			if (auto* Wave = Cast<USoundWave>(Object))
			{
				TestEqual(TEXT("保持原始双声道"), Wave->NumChannels, 2);
				TestTrue(TEXT("保留自然尾音，不被五秒标题截断"), Wave->Duration > 6.f && Wave->Duration < 8.2f);
				TestFalse(TEXT("每次只播一个 take，不循环"), Wave->bLooping);
			}
			else { AddError(TEXT("数组含非 SoundWave 项")); }
		}
		TestEqual(TEXT("六个独立样本，不重复塞入同一文件"), Unique.Num(), 6);
	}
	int32 NoRepeats = 0;
	int32 Sharing = 0;
	bool bShared = false;
	const auto* Repeats = Literal(TEXT("No Repeats"));
	const auto* Shared = Literal(TEXT("Enable Shared State"));
	const auto* Behavior = Literal(TEXT("Shared State Behavior"));
	TestTrue(TEXT("禁止连续重复"), Repeats && Repeats->TryGet(NoRepeats) && NoRepeats == 1);
	TestTrue(TEXT("跨播放实例共享随机历史"), Shared && Shared->TryGet(bShared) && bShared);
	TestTrue(TEXT("按样本数组共享，与其它 UI 和武器音效隔离"), Behavior && Behavior->TryGet(Sharing) && Sharing == 2);
	auto Connected = [&](const FName From, const FName To)
	{
		const auto* Output = Picker->Interface.Outputs.FindByPredicate([&](const auto& Vertex) { return Vertex.Name == From; });
		const auto* Input = Player->Interface.Inputs.FindByPredicate([&](const auto& Vertex) { return Vertex.Name == To; });
		return Output && Input && Graph.Edges.ContainsByPredicate([&](const auto& Edge)
		{
			return Edge.FromNodeID == Picker->GetID() && Edge.FromVertexID == Output->VertexID
				&& Edge.ToNodeID == Player->GetID() && Edge.ToVertexID == Input->VertexID;
		});
	};
	TestTrue(TEXT("随机选择完成后才播放"), Connected(TEXT("On Next"), TEXT("Play")));
	TestTrue(TEXT("播放器读取随机样本"), Connected(TEXT("Value"), TEXT("Wave Asset")));
	const auto* Next = Picker->Interface.Inputs.FindByPredicate([](const auto& Vertex) { return Vertex.Name == TEXT("Next"); });
	TestTrue(TEXT("随机节点具有播放触发输入"), Next && Graph.Edges.ContainsByPredicate([&](const auto& Edge)
	{
		return Edge.ToNodeID == Picker->GetID() && Edge.ToVertexID == Next->VertexID;
	}));
	return true;
}

#endif
