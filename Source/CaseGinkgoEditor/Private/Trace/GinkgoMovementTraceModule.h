#pragma once

#include "CoreMinimal.h"
#include "TraceServices/ModuleService.h"

/**
 * @class FGinkgoMovementTraceModule : 移动追踪的分析模块
 * 每个分析会话开始时（Rewind Debugger 开始录制、Insights 打开追踪文件）加上数据提供者与分析器。
 * 作为模块化功能注册（CaseGinkgoEditor 模块启动时），要在第一个分析会话之前：TraceServices 第一次用到时就缓存了模块列表。
 */
class FGinkgoMovementTraceModule : public TraceServices::IModule
{
public:
	//~ Begin TraceServices::IModule
	virtual void GetModuleInfo(TraceServices::FModuleInfo& OutModuleInfo) override;
	virtual void OnAnalysisBegin(TraceServices::IAnalysisSession& Session) override;
	virtual void GetLoggers(TArray<const TCHAR*>& OutLoggers) override;
	virtual void GenerateReports(const TraceServices::IAnalysisSession& Session, const TCHAR* CmdLine, const TCHAR* OutputDirectory) override {}
	virtual const TCHAR* GetCommandLineArgument() override { return TEXT("ginkgomovement"); }
	//~ End TraceServices::IModule
};
