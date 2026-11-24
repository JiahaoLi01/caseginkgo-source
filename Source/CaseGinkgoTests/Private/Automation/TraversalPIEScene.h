#pragma once

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "Settings/LevelEditorPlaySettings.h"

namespace GinkgoTraversalTests
{
	/** 单机轨迹用例自行拥有 PIE 配置，避免用户最近一次联机手测设置把直接放置角色变成客户端作弊传送。 */
	class FStartStandalonePIE : public IAutomationLatentCommand
	{
	public:
		virtual bool Update() override
		{
			FRequestPlaySessionParams Params;
			Params.EditorPlaySettings = DuplicateObject<ULevelEditorPlaySettings>(GetMutableDefault<ULevelEditorPlaySettings>(), GetTransientPackage());
			Params.EditorPlaySettings->SetPlayNetMode(PIE_Standalone);
			Params.EditorPlaySettings->SetPlayNumberOfClients(1);
			GEditor->RequestPlaySession(Params);
			return true;
		}
	};
}

#endif
