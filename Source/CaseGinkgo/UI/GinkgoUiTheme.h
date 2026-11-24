#pragma once

#include "CoreMinimal.h"

class UWeftView;

/**
 * 纸页的主题（Docs/Design/UIArt.md「在 weft 里落地」）：Content/UI/Theme 里的主题样式表 theme.css（令牌与纸页的组件）、组件脚本 paper.js
 * （侧栏、选项条、滑块、开关、按钮、描述区、公式、图标、预览的画图）与烘好的洗染图 wash/。相机的调参面板与设置界面共用。
 *
 * weft 的 page.json 只认一个样式表、不认 @import，所以照 CaseKargaroo 由这里接进页面：主题样式表接在页面的样式表前面，
 * 组件脚本排在页面的脚本前面。页面另外要的样式表写在 page.json 的 "styles" 里（如 KaTeX 的 katex.css），接在最前面。
 * url() 同浏览器，相对写它的那个样式表：页面的相对页面的样式表（weft 的基准），主题的（wash/grain.png）相对主题的目录、
 * "styles" 里的相对它自己，接进来时补上从页面的样式表到它们的相对路径，文件本身不用改
 */
namespace Ginkgo::UI
{
	/** @brief 主题的目录：Content/UI/Theme（随包，DefaultGame.ini 的 DirectoriesToAlwaysStageAsUFS） */
	CASEGINKGO_API FString GetThemeDirectory();

	/**
	 * @brief 载入一个用主题的页面
	 * @param Directory - 页面的目录：page.json 与它列的脚本、样式表
	 * @return 载入了没有；读不到文件、weft 不收时为假，原因在日志里（weft 的诊断在 LogWeft）
	 */
	CASEGINKGO_API bool LoadThemedPage(UWeftView& View, const FString& Directory);

	/**
	 * @brief 载入一个不用纸页主题、但要键帽的页面（武器的界面）：Content/UI/Theme 的 keycap.css 接在页面的样式表前面，keycap.js 排在页面的脚本前面
	 * @note LoadThemedPage 的页面（设置界面）也有键帽。键帽：globalThis.GinkgoKeycap
	 */
	CASEGINKGO_API bool LoadPageWithKeycaps(UWeftView& View, const FString& Directory);
}
