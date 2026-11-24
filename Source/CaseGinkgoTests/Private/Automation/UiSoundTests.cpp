#include "Framework/GinkgoDeveloperSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "UI/GinkgoUiSounds.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoUiSoundsTest, "CaseGinkgo.UI.Sounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoUiSoundsTest::RunTest(const FString& Parameters)
{
	using Ginkgo::UI::ParseUiSound;
	TestEqual(TEXT("页面发的名字：Focus"), ParseUiSound(TEXT("Focus")), EGinkgoUiSound::Focus);
	TestEqual(TEXT("页面发的名字：Select"), ParseUiSound(TEXT("Select")), EGinkgoUiSound::Select);
	TestEqual(TEXT("页面发的名字：Confirm"), ParseUiSound(TEXT("Confirm")), EGinkgoUiSound::Confirm);
	TestEqual(TEXT("页面发的名字：Deny"), ParseUiSound(TEXT("Deny")), EGinkgoUiSound::Deny);
	TestEqual(TEXT("页面发的名字：Invalid"), ParseUiSound(TEXT("Invalid")), EGinkgoUiSound::Invalid);
	TestEqual(TEXT("名字：Notification"), ParseUiSound(TEXT("Notification")), EGinkgoUiSound::Notification);
	TestEqual(TEXT("认不出的名字：不放"), ParseUiSound(TEXT("NoSuchSound")), EGinkgoUiSound::None);

	// 所有提示音都有可加载的默认资产，包括工作台独立的安装确认声。
	const FGinkgoUiSounds& Sounds = UGinkgoDeveloperSettings::Get()->UiSounds;
	TestNull(TEXT("None 没有声音"), Sounds.Get(EGinkgoUiSound::None));
	TestTrue(TEXT("工作台安装使用独立的短确认声"), Sounds.WorkbenchConfirm.Sound.ToSoftObjectPath() != Sounds.Confirm.Sound.ToSoftObjectPath());
	for (const EGinkgoUiSound Kind : {EGinkgoUiSound::Open, EGinkgoUiSound::Close, EGinkgoUiSound::Focus, EGinkgoUiSound::Select, EGinkgoUiSound::Confirm, EGinkgoUiSound::Deny, EGinkgoUiSound::Invalid, EGinkgoUiSound::Notification, EGinkgoUiSound::WorkbenchConfirm})
	{
		const FGinkgoUiSound* Item = Sounds.Get(Kind);
		const FString Name = StaticEnum<EGinkgoUiSound>()->GetNameStringByValue(static_cast<int64>(Kind));
		if (!TestNotNull(*FString::Printf(TEXT("%s：配了"), *Name), Item))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s：资产在（%s）"), *Name, *Item->Sound.ToString()),
			!Item->Sound.IsNull() && FPackageName::DoesPackageExist(Item->Sound.ToSoftObjectPath().GetLongPackageName()));
		TestTrue(*FString::Printf(TEXT("%s：音量 %.2f"), *Name, Item->Volume), Item->Volume > 0.f);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
