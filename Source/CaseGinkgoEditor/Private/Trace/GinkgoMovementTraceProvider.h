#pragma once

#include "CoreMinimal.h"
#include "GinkgoMovementTraceData.h"
#include "TraceServices/Model/AnalysisSession.h"

/**
 * @class FGinkgoMovementTraceProvider : 分析会话里的移动追踪数据
 * 分析器在会话的写锁里写（Edit），Rewind Debugger 的轨道在会话的读锁里读（Read）。
 */
class FGinkgoMovementTraceProvider : public TraceServices::IProvider
{
public:
	static const FName ProviderName;

	explicit FGinkgoMovementTraceProvider(TraceServices::IAnalysisSession& InSession)
		: Session(InSession)
	{
	}

	//~ Begin TraceServices::IProvider
	virtual void BeginRead() const override { Session.BeginRead(); }
	virtual void EndRead() const override { Session.EndRead(); }
	virtual void ReadAccessCheck() const override { Session.ReadAccessCheck(); }
	//~ End TraceServices::IProvider

	FGinkgoMovementTraceData& Edit()
	{
		Session.WriteAccessCheck();
		return Data;
	}

	const FGinkgoMovementTraceData& Read() const
	{
		Session.ReadAccessCheck();
		return Data;
	}

private:
	TraceServices::IAnalysisSession& Session;

	FGinkgoMovementTraceData Data;
};
