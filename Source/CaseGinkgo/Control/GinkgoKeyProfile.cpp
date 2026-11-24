#include "GinkgoKeyProfile.h"

#include "EnhancedActionKeyMapping.h"

int32 UGinkgoKeyProfile::GetPlayerMappedKeysForRebuildControlMappings(const FEnhancedActionKeyMapping& DefaultMapping, TArray<FKey>& OutKeys) const
{
	OutKeys.Reset();
	if (!DefaultMapping.IsPlayerMappable())
	{
		return 0;
	}
	FPlayerMappableKeyQueryOptions Options;
	Options.MappingName = DefaultMapping.GetMappingName();
	return QueryPlayerMappedKeys(Options, OutKeys);
}
