#include "Control/GinkgoDirectionKeys.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GinkgoDirectionKeysTests
{
	/** 四个键的顺序同 FGinkgoDirectionKeys：右、左、前、后 */
	enum EKey : int32 { KeyRight, KeyLeft, KeyForward, KeyBack };

	/** 一帧：这一帧按下的键、这一帧之后按着的键 */
	struct FFrame
	{
		TArray<EKey> Pressed;
		TArray<EKey> Held;
	};

	FIntPoint Step(FGinkgoDirectionKeys& Keys, const FFrame& Frame)
	{
		bool bPressed[FGinkgoDirectionKeys::Count] = {};
		bool bHeld[FGinkgoDirectionKeys::Count] = {};
		for (const EKey Key : Frame.Pressed)
		{
			bPressed[Key] = true;
		}
		for (const EKey Key : Frame.Held)
		{
			bHeld[Key] = true;
		}
		Keys.Update(bPressed, bHeld);
		return Keys.Resolve();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoDirectionKeysTest, "CaseGinkgo.Control.DirectionKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoDirectionKeysTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoDirectionKeysTests;

	// 换方向时重叠：按住 D，按下 A（D 还没松开），A 后按、赢；松开 D 还是 A；两个方向都一样
	{
		FGinkgoDirectionKeys Keys;
		TestEqual(TEXT("按住 D 向右"), Step(Keys, {{KeyRight}, {KeyRight}}), FIntPoint(1, 0));
		TestEqual(TEXT("D 还按着时按下 A：后按的 A 赢"), Step(Keys, {{KeyLeft}, {KeyRight, KeyLeft}}), FIntPoint(-1, 0));
		TestEqual(TEXT("之后两个都按着，还是 A"), Step(Keys, {{}, {KeyRight, KeyLeft}}), FIntPoint(-1, 0));
		TestEqual(TEXT("松开 D 还是 A"), Step(Keys, {{}, {KeyLeft}}), FIntPoint(-1, 0));
		TestEqual(TEXT("A 还按着时按下 D：D 赢"), Step(Keys, {{KeyRight}, {KeyRight, KeyLeft}}), FIntPoint(1, 0));
		TestEqual(TEXT("松开后按的 D，回到还按着的 A"), Step(Keys, {{}, {KeyLeft}}), FIntPoint(-1, 0));
	}

	// 两条轴各算各的；同一帧按下反方向的两个键谁也不比谁后，抵消；之后松开一个，剩下的就是它
	{
		FGinkgoDirectionKeys Keys;
		TestEqual(TEXT("W + D 斜着"), Step(Keys, {{KeyForward, KeyRight}, {KeyForward, KeyRight}}), FIntPoint(1, 1));
		TestEqual(TEXT("D 还按着时按下 A，前后不受影响"), Step(Keys, {{KeyLeft}, {KeyForward, KeyRight, KeyLeft}}), FIntPoint(-1, 1));
		TestEqual(TEXT("同一帧按下 W 与 S：抵消"), Step(Keys, {{KeyForward, KeyBack}, {KeyForward, KeyBack}}).Y, 0);
		TestEqual(TEXT("松开 S，剩下 W"), Step(Keys, {{}, {KeyForward}}).Y, 1);
	}

	// 一帧里按下又松开（点按）也算按着：这一帧的朝向是它
	{
		FGinkgoDirectionKeys Keys;
		Step(Keys, {{KeyRight}, {KeyRight}});
		TestEqual(TEXT("D 按着时一帧里点按 A：这一帧是 A"), Step(Keys, {{KeyLeft}, {KeyRight, KeyLeft}}), FIntPoint(-1, 0));
		TestEqual(TEXT("下一帧 A 已松开：回到 D"), Step(Keys, {{}, {KeyRight}}), FIntPoint(1, 0));
	}

	// 一直按着、没记下按下的两个键（例如开始记录之前就按着）谁也不比谁后，抵消
	{
		FGinkgoDirectionKeys Keys;
		TestEqual(TEXT("没记下按下的两个反方向的键抵消"), Step(Keys, {{}, {KeyRight, KeyLeft}}), FIntPoint::ZeroValue);
		TestEqual(TEXT("什么也没按是零"), Step(Keys, {{}, {}}), FIntPoint::ZeroValue);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
