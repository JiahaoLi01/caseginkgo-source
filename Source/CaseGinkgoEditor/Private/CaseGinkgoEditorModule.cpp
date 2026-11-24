#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"
#include "Trace/GinkgoMovementRewindDebugger.h"
#include "Trace/GinkgoMovementTraceModule.h"
#include "Weapon/GinkgoRecoilPatternWatcher.h"

/**
 * @class FCaseGinkgoEditorModule : 编辑器模块
 * 盯着网页工具存的后坐模板（FGinkgoRecoilPatternWatcher）；注册三样模块化功能：移动追踪的分析模块（TraceServices，每个分析会话加上数据提供者与分析器）、
 * Rewind Debugger 的轨道、Rewind Debugger 开始录制时打开移动追踪通道的扩展。
 */
class FCaseGinkgoEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		IModularFeatures& Features = IModularFeatures::Get();
		Features.RegisterModularFeature(TraceServices::ModuleFeatureName, &TraceModule);
		Features.RegisterModularFeature(RewindDebugger::IRewindDebuggerTrackCreator::ModularFeatureName, &TrackCreator);
		Features.RegisterModularFeature(IRewindDebuggerRuntimeExtension::ModularFeatureName, &RuntimeExtension);
		RecoilPatternWatcher.Start();
	}

	virtual void ShutdownModule() override
	{
		RecoilPatternWatcher.Stop();
		IModularFeatures& Features = IModularFeatures::Get();
		Features.UnregisterModularFeature(IRewindDebuggerRuntimeExtension::ModularFeatureName, &RuntimeExtension);
		Features.UnregisterModularFeature(RewindDebugger::IRewindDebuggerTrackCreator::ModularFeatureName, &TrackCreator);
		Features.UnregisterModularFeature(TraceServices::ModuleFeatureName, &TraceModule);
	}

private:
	FGinkgoMovementTraceModule TraceModule;

	FGinkgoMovementTrackCreator TrackCreator;

	FGinkgoMovementRewindDebuggerRuntimeExtension RuntimeExtension;

	FGinkgoRecoilPatternWatcher RecoilPatternWatcher;
};

IMPLEMENT_MODULE(FCaseGinkgoEditorModule, CaseGinkgoEditor)
