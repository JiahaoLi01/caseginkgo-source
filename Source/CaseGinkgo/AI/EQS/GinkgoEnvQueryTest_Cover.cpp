#include "AI/EQS/GinkgoEnvQueryTest_Cover.h"

#include "AI/GinkgoAIController.h"
#include "AI/GinkgoAITacticsComponent.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_VectorBase.h"
#include "GameFramework/Pawn.h"

UGinkgoEnvQueryTest_Cover::UGinkgoEnvQueryTest_Cover()
{
	Cost = EEnvTestCost::High;
	ValidItemType = UEnvQueryItemType_VectorBase::StaticClass();
	SetWorkOnFloatValues(false);
	TestPurpose = EEnvTestPurpose::Filter;
	FilterType = EEnvTestFilterType::Match;
	BoolValue.DefaultValue = true;
}

void UGinkgoEnvQueryTest_Cover::RunTest(FEnvQueryInstance& QueryInstance) const
{
	const APawn* Querier = Cast<APawn>(QueryInstance.Owner.Get());
	const AGinkgoAIController* Controller = Querier ? Cast<AGinkgoAIController>(Querier->GetController()) : nullptr;
	for (FEnvQueryInstance::ItemIterator It(this, QueryInstance); It; ++It)
	{
		const UGinkgoAITacticsComponent* Tactics = Controller ? Controller->GetTactics() : nullptr;
		FGinkgoAICoverPose Pose;
		const bool bUsable = Tactics && Tactics->EvaluateCover(GetItemLocation(QueryInstance, It.GetIndex()), Tactics->GetQueryThreat(), Pose);
		It.SetScore(TestPurpose, FilterType, bUsable, true);
	}
}
