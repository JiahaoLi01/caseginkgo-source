#pragma once

#include "CoreMinimal.h"
#include "Presentation/GinkgoCredit.h"
#include "UI/GinkgoCreditsSettings.h"

/** 一个完整字素簇，不拆开组合音标或代理对；只改变显现顺序，不改变它在原文中的位置。 */
struct FGinkgoCreditGlyph
{
	/** 最终显示的真实文字，绝不以随机乱码替换。 */
	FString Text;

	/** 相对于本条开始的 UI 秒数；空白字符不参与随机排序。 */
	float Delay = 0.f;

	/** 所属显示行，0 为职务，1 起依次为非空署名。整条播放期间行号固定。 */
	int32 Row = 0;

	/** 所属行的字面样式，由关卡条目提供；职务使用正体。 */
	bool bItalic = false;
};

/** 署名的排队与逐字时序；渲染层只展示采样结果，不自行运行 CSS/JS 计时器。 */
class CASEGINKGO_API FGinkgoCreditsQueue
{
public:
	enum class EStep : uint8 { None, Begin, Finish };

	/** 拷贝内容/配置并为每条生成固定显现顺序；空姓名跳过，返回实际加入数量。 */
	int32 Push(const TArray<FGinkgoCredit>& Credits, const FGinkgoCreditsSettings& Settings, int32 Seed);

	/** 仅在允许展示时推进；队列最后一条 Finish 后调用者释放页面和 Tick。 */
	EStep Advance(float DeltaSeconds);

	bool IsActive() const { return Current.IsSet() || !Pending.IsEmpty(); }
	bool HasCurrent() const { return Current.IsSet(); }
	const TArray<FGinkgoCreditGlyph>& GetGlyphs() const;
	const FGinkgoCreditsSettings* GetSettings() const;
	/** 字符显现的采样时刻（UI 秒）；最后一个字符清晰后固定，等待中的条目返回 0。 */
	float GetRevealTime() const { return Current ? FMath::Min(Elapsed, Current->RevealEnd) : 0.f; }
	float GetGlyphAlpha(int32 Index) const;
	float GetOpacity() const;

private:
	/** 一条署名在触发时固定的表现数据。 */
	struct FEntry
	{
		/** 保持组内行号和原文排布的字素簇，Delay 独立决定其显现时刻。 */
		TArray<FGinkgoCreditGlyph> Glyphs;

		/** 入队时的共享项目设置快照，显示中途不重读。 */
		FGinkgoCreditsSettings Settings;

		/** 最后一个可见字符完成显现的 UI 秒数，之后才开始计算 Hold。 */
		float RevealEnd = 0.f;
	};

	/** 等待显示的条目，按 Volume 内容和触发先后顺序播放。 */
	TArray<FEntry> Pending;

	/** 当前条目，Advance 返回 Begin 时建立，Finish 时清空。 */
	TOptional<FEntry> Current;

	/** 当前条目已推进的 UI 秒数，暂停时不更新。 */
	float Elapsed = 0.f;
};
