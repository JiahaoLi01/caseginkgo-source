#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Movement/Traversal/GinkgoTraversalNetworking.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalNetStateTest, "CaseGinkgo.Movement.Traversal.Net.StateRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoTraversalNetStateTest::RunTest(const FString& Parameters)
{
	FGinkgoTraversalInput Input;
	Input.Sequence = 65535;
	Input.Yaw = FRotator::CompressAxisToShort(123.45f);
	Input.WeaponSlot = 1;
	Input.bMovingExit = true;
	FBitWriter InputWriter(256, true);
	Input.Serialize(InputWriter);
	FBitReader InputReader(InputWriter.GetData(), InputWriter.GetNumBits());
	FGinkgoTraversalInput ReadInput;
	ReadInput.Serialize(InputReader);
	TestEqual(TEXT("持久请求计数不丢失"), ReadInput.Sequence, Input.Sequence);
	TestEqual(TEXT("检测偏航两端使用相同量化值"), ReadInput.Yaw, Input.Yaw);
	TestTrue(TEXT("出口与持枪表现输入保留"), ReadInput.bMovingExit && ReadInput.WeaponSlot == 1);

	FGinkgoTraversalSyncState Active;
	Active.ActionSequence = 65535;
	Active.HandledSequence = 65535;
	Active.Phase = EGinkgoTraversalPhase::Moving;
	Active.Time = .4851f;
	Active.Result.Kind = EGinkgoTraversalKind::Mantle;
	Active.Result.Height = 100.f;
	Active.Result.Landing.Position = FVector(100., 2., 100.);
	Active.Origin = FTransform(FRotator(0., 42., 0.), FVector(12., 34., 56.));
	bool bSuccess = false;
	FBitWriter Writer(4096, true);
	Active.NetSerialize(Writer, nullptr, bSuccess);
	TestTrue(TEXT("执行状态写入有效"), bSuccess);
	FGinkgoTraversalSyncState Read;
	FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
	Read.NetSerialize(Reader, nullptr, bSuccess);
	TestTrue(TEXT("原起点、锚点与阶段时钟完整往返"), bSuccess && Read.Matches(Active));
	TestEqual(TEXT("时钟不量化，避免交接落在不同移动"), Read.Time, Active.Time);
	TestTrue(TEXT("攀爬快照为 CMC 回包保留至少一半位预算"), Writer.GetNumBits() < 2048);
	auto Changed = Active;
	Changed.Time += .0001f;
	TestFalse(TEXT("复制比较能识别进度变化"), Active.Identical(&Changed, 0));
	Changed = Active;
	Changed.Phase = EGinkgoTraversalPhase::Recovering;
	TestFalse(TEXT("复制比较能识别支撑交接"), Active.Identical(&Changed, 0));
	TestTrue(TEXT("未变化的状态不重复发包"), Active.Identical(&Active, 0));

	FGinkgoTraversalSyncState Idle;
	Idle.HandledSequence = 0;
	Idle.ActionSequence = 65535;
	FBitWriter IdleWriter(256, true);
	Idle.NetSerialize(IdleWriter, nullptr, bSuccess);
	FBitReader IdleReader(IdleWriter.GetData(), IdleWriter.GetNumBits());
	Read.NetSerialize(IdleReader, nullptr, bSuccess);
	TestTrue(TEXT("序号回绕后拒绝仍被确认，旧锚点不会残留"), bSuccess && Read.Matches(Idle)
		&& Read.Result.Kind == EGinkgoTraversalKind::None && Read.Time == 0.f);
	TestEqual(TEXT("非攀爬状态仅两个序号加阶段，共 34 位"), IdleWriter.GetNumBits(), int64(34));
	return true;
}

#endif
