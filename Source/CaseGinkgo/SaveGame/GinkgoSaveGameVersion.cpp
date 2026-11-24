#include "SaveGame/GinkgoSaveGameVersion.h"

#include "UObject/DevObjectVersion.h"   // FDevVersionRegistration

// 不沿用 CaseRaccoon 的 GUID：别的项目的存档要在版本检查这里被拒绝
const FGuid FGinkgoSaveGameVersion::GUID(0xF357AA61, 0xE7A846BE, 0x99F75356, 0x959FE665);

static FDevVersionRegistration GRegisterGinkgoSaveGameVersion(FGinkgoSaveGameVersion::GUID, FGinkgoSaveGameVersion::LatestVersion, TEXT("GinkgoSaveGame"));
