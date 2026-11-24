#include "GinkgoMovementTraceModule.h"

#include "GinkgoMovementTraceAnalyzer.h"
#include "GinkgoMovementTraceProvider.h"
#include "TraceServices/Model/AnalysisSession.h"

const FName FGinkgoMovementTraceProvider::ProviderName(TEXT("GinkgoMovementProvider"));

void FGinkgoMovementTraceModule::GetModuleInfo(TraceServices::FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = TEXT("GinkgoMovement");
	OutModuleInfo.DisplayName = TEXT("Ginkgo Movement");
}

void FGinkgoMovementTraceModule::OnAnalysisBegin(TraceServices::IAnalysisSession& Session)
{
	const TSharedPtr<FGinkgoMovementTraceProvider> Provider = MakeShared<FGinkgoMovementTraceProvider>(Session);
	Session.AddProvider(FGinkgoMovementTraceProvider::ProviderName, Provider);
	Session.AddAnalyzer(new FGinkgoMovementTraceAnalyzer(Session, *Provider));
}

void FGinkgoMovementTraceModule::GetLoggers(TArray<const TCHAR*>& OutLoggers)
{
	OutLoggers.Add(TEXT("GinkgoMovement"));
}
