#include "GinkgoUiTheme.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Utilities/Debug/DebugCategory.h"
#include "WeftTypes.h"  // FWeftPage：读页面的目录，接上主题再交给 View
#include "WeftView.h"

namespace
{
	/** url() 里的路径是不是相对的：不带协议（data:、http: 这些）、不从根或 # 开始 */
	bool IsRelativeUrl(const FString& Url)
	{
		return !Url.IsEmpty() && !Url.StartsWith(TEXT("/")) && !Url.StartsWith(TEXT("#")) && !Url.Contains(TEXT(":"));
	}

	/**
	 * @brief 样式表里相对的 url() 换一个基准：接到页面的样式表前面以后 weft 按页面的样式表解析，所以前面补上
	 *        "页面的样式表所在的目录 → 这个样式表的目录"的相对路径
	 * @param Prefix - 补在前面的路径，以 '/' 结尾；空的时候不变
	 */
	FString RebaseUrls(const FString& Stylesheet, const FString& Prefix)
	{
		if (Prefix.IsEmpty())
		{
			return Stylesheet;
		}
		FString Out;
		Out.Reserve(Stylesheet.Len() + 64);
		int32 Index = 0;
		while (true)
		{
			const int32 At = Stylesheet.Find(TEXT("url("), ESearchCase::IgnoreCase, ESearchDir::FromStart, Index);
			const int32 Close = At == INDEX_NONE ? INDEX_NONE : Stylesheet.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At);
			if (Close == INDEX_NONE)
			{
				Out += Stylesheet.Mid(Index);
				return Out;
			}
			const int32 Start = At + 4;
			FString Url = Stylesheet.Mid(Start, Close - Start).TrimStartAndEnd();
			FString Quote;
			if (Url.Len() >= 2 && (Url[0] == TEXT('"') || Url[0] == TEXT('\'')) && Url[Url.Len() - 1] == Url[0])
			{
				Quote = Url.Left(1);
				Url = Url.Mid(1, Url.Len() - 2);
			}
			Out += Stylesheet.Mid(Index, Start - Index);
			Out += Quote + (IsRelativeUrl(Url) ? Prefix + Url : Url) + Quote;
			Index = Close;
		}
	}

	/**
	 * @brief 读一个要接进页面的样式表，里面相对的 url() 换成相对页面的样式表（同浏览器：url() 相对写它的那个样式表）
	 * @param PageStylesheet - 页面的样式表（weft 解析 url() 的基准，FWeftPage::Base）
	 */
	bool LoadRebased(const FString& File, const FString& PageStylesheet, FString& Out)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			return false;
		}
		FString Prefix = FPaths::ConvertRelativePathToFull(FPaths::GetPath(File)) + TEXT("/");
		FPaths::MakePathRelativeTo(Prefix, *FPaths::ConvertRelativePathToFull(PageStylesheet));
		if (Prefix == TEXT("./"))
		{
			Prefix.Reset();
		}
		if (!Prefix.IsEmpty() && !Prefix.EndsWith(TEXT("/")))
		{
			Prefix += TEXT("/");
		}
		Out = RebaseUrls(Text, Prefix);
		return true;
	}

	/** 键帽（keycap.css、keycap.js）接进页面：样式表在 Prefix 之后、页面的前面，脚本排在 ScriptIndex */
	bool AddKeycaps(FWeftPage& Page, FString& OutStylesheet, const int32 ScriptIndex)
	{
		const FString Theme = Ginkgo::UI::GetThemeDirectory();
		FString Script;
		if (!LoadRebased(Theme / TEXT("keycap.css"), Page.Base, OutStylesheet) || !FFileHelper::LoadFileToString(Script, *(Theme / TEXT("keycap.js"))))
		{
			UE_LOG(LogGinkgoUI, Warning, TEXT("[Theme] 键帽读不到：%s 里要有 keycap.css 与 keycap.js"), *Theme);
			return false;
		}
		Page.Scripts.Insert(TPair<FString, FString>(TEXT("Theme/keycap.js"), MoveTemp(Script)), ScriptIndex);
		return true;
	}

	/** page.json 里页面另外要的样式表（"styles"，如 KaTeX 的 katex.css）：weft-unreal 的 FWeftPage 不读这一项 */
	TArray<FString> ReadExtraStyles(const FString& Directory)
	{
		TArray<FString> Styles;
		FString Manifest;
		TSharedPtr<FJsonObject> Json;
		if (FFileHelper::LoadFileToString(Manifest, *(Directory / TEXT("page.json"))) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Manifest), Json) && Json)
		{
			Json->TryGetStringArrayField(TEXT("styles"), Styles);
		}
		return Styles;
	}
}

FString Ginkgo::UI::GetThemeDirectory()
{
	return FPaths::ProjectContentDir() / TEXT("UI/Theme");
}

bool Ginkgo::UI::LoadThemedPage(UWeftView& View, const FString& Directory)
{
	FWeftPage Page;
	FString Error;
	if (!FWeftPage::FromDirectory(Directory, Page, &Error))
	{
		UE_LOG(LogGinkgoUI, Warning, TEXT("[Theme] 页面读不到：%s"), *Error);
		return false;
	}
	// 样式表的顺序：页面另外要的（page.json 的 "styles"）、主题、页面自己的
	FString Stylesheet;
	for (const FString& Style : ReadExtraStyles(Directory))
	{
		FString Text;
		if (!LoadRebased(Directory / Style, Page.Base, Text))
		{
			UE_LOG(LogGinkgoUI, Warning, TEXT("[Theme] 页面要的样式表读不到：%s"), *(Directory / Style));
			return false;
		}
		Stylesheet += Text + TEXT("\n");
	}
	const FString Theme = GetThemeDirectory();
	FString ThemeStylesheet;
	FString ThemeScript;
	if (!LoadRebased(Theme / TEXT("theme.css"), Page.Base, ThemeStylesheet) || !FFileHelper::LoadFileToString(ThemeScript, *(Theme / TEXT("paper.js"))))
	{
		UE_LOG(LogGinkgoUI, Warning, TEXT("[Theme] 主题读不到：%s 里要有 theme.css 与 paper.js"), *Theme);
		return false;
	}
	// 键帽排在主题的后面、页面的前面：catalog.js 要用它的键名表
	FString KeycapStylesheet;
	if (!AddKeycaps(Page, KeycapStylesheet, 0))
	{
		return false;
	}
	Page.Stylesheet = Stylesheet + ThemeStylesheet + TEXT("\n") + KeycapStylesheet + TEXT("\n") + Page.Stylesheet;
	Page.Scripts.Insert(TPair<FString, FString>(TEXT("Theme/paper.js"), MoveTemp(ThemeScript)), 0);
	return View.Load(Page);
}

bool Ginkgo::UI::LoadPageWithKeycaps(UWeftView& View, const FString& Directory)
{
	FWeftPage Page;
	FString Error;
	if (!FWeftPage::FromDirectory(Directory, Page, &Error))
	{
		UE_LOG(LogGinkgoUI, Warning, TEXT("[Theme] 页面读不到：%s"), *Error);
		return false;
	}
	FString KeycapStylesheet;
	if (!AddKeycaps(Page, KeycapStylesheet, 0))
	{
		return false;
	}
	Page.Stylesheet = KeycapStylesheet + TEXT("\n") + Page.Stylesheet;
	return View.Load(Page);
}
