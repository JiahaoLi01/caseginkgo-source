#pragma once

#include "CoreMinimal.h"
#include "Trace/Analyzer.h"

class FGinkgoMovementTraceProvider;
namespace TraceServices { class IAnalysisSession; }

/**
 * @class FGinkgoMovementTraceAnalyzer : 移动追踪的分析器
 * 把运行时 FGinkgoMovementTrace 写的事件读进 FGinkgoMovementTraceProvider。事件与字段的名字要与运行时一致（GinkgoMovementTrace.cpp）。
 */
class FGinkgoMovementTraceAnalyzer : public UE::Trace::IAnalyzer
{
public:
	FGinkgoMovementTraceAnalyzer(TraceServices::IAnalysisSession& InSession, FGinkgoMovementTraceProvider& InProvider);

	//~ Begin UE::Trace::IAnalyzer
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnAnalysisEnd() override {}
	virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;
	//~ End UE::Trace::IAnalyzer

private:
	enum : uint16
	{
		RouteId_StringId,
		RouteId_WindowState,
		RouteId_WindowEvent,
		RouteId_WindowChange,
		RouteId_Correction,
	};

	TraceServices::IAnalysisSession& Session;

	FGinkgoMovementTraceProvider& Provider;
};
