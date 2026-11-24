#include "SaveGame/GinkgoSaveGameSettings.h"

FGuid UGinkgoSaveGameSettings::GetVersionId(const UEnum* VersionEnum) const
{
	if (CachedVersions.IsEmpty())
	{
		CachedVersions.Reserve(Versions.Num());

		for (const FGinkgoSaveGameVersionInfo& VersionInfo : Versions)
		{
			if (VersionInfo.Id.IsValid() && VersionInfo.Enum != nullptr)
			{
				CachedVersions.FindOrAdd(VersionInfo.Enum) = VersionInfo.Id;
			}
		}
	}

	const FGuid* Found = CachedVersions.Find(VersionEnum);
	return Found ? *Found : FGuid();
}

#if WITH_EDITOR
void UGinkgoSaveGameSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UGinkgoSaveGameSettings, Versions))
	{
		CachedVersions.Reset();
	}
}
#endif
