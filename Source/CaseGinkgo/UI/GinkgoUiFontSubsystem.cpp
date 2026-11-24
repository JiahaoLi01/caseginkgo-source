#include "UI/GinkgoUiFontSubsystem.h"

#include "Misc/Paths.h"
#include "WeftSubsystem.h"

void UGinkgoUiFontSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UWeftSubsystem>();
	if (auto* Weft = UWeftSubsystem::Get(); Weft && Weft->IsAvailable())
	{
		// AddFonts 的原生字体加载器读取磁盘文件，因此 Fonts 目录按 NonUFS 随包，而不是只放在 pak 中。
		RegisteredFaceCount = Weft->AddFonts(FPaths::ProjectContentDir() / TEXT("Fonts/fonts.json"));
	}
}
