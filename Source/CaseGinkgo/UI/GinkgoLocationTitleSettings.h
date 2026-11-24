#pragma once

#include "CoreMinimal.h"
#include "GinkgoLocationTitleSettings.generated.h"

/** 地点标注共用的表现配置。项目设置持有，入队时复制一份，避免显示中途改参数导致时序跳变。 */
USTRUCT()
struct CASEGINKGO_API FGinkgoLocationTitleSettings
{
	GENERATED_BODY()

	/** Weft 字体家族名；项目自带 Barlow Condensed，也可填写已注册的家族。缺字继续使用系统无衬线回退。 */
	UPROPERTY(EditAnywhere, Category = "Typography")
	FString FontFamily = TEXT("Barlow Condensed");

	/** CSS 字重，范围 100–900；自带字体包含 400、600、700，默认使用 600（SemiBold）。 */
	UPROPERTY(EditAnywhere, Category = "Typography", meta = (ClampMin = "100", ClampMax = "900"))
	int32 FontWeight = 600;

	/** 短地点名的最大字号（DPI 缩放前的逻辑像素）；长名称按最大宽度自动缩小，不换行。 */
	UPROPERTY(EditAnywhere, Category = "Typography", meta = (ClampMin = "1", DisplayName = "Font Size (Logical Pixels)"))
	float FontSize = 200.f;

	/** 字符间距，相对于字号的比例（em）；0 不额外扩展字距。 */
	UPROPERTY(EditAnywhere, Category = "Typography", meta = (ClampMin = "0", ClampMax = "1"))
	float LetterSpacing = .06f;

	/** 地点文本最多占视口宽度的比例，范围 0.1–1；两侧留白和长名称缩字使用同一限制。 */
	UPROPERTY(EditAnywhere, Category = "Typography", meta = (ClampMin = "0.1", ClampMax = "1"))
	float MaxWidthFraction = .86f;

	/** 只在展示时把英文转成大写，不修改检查点存储的名称或本地化文本；中文不受影响。 */
	UPROPERTY(EditAnywhere, Category = "Typography")
	bool bUppercase = true;

	/** 扁平文字颜色（sRGB），不附加阴影、描边或背景面板。 */
	UPROPERTY(EditAnywhere, Category = "Typography")
	FColor TextColor = FColor(246, 243, 235);

	/** 所有地点共用的淡入时间（UI 秒）；0 表示第一帧直接完整显示，负值在运行时按 0 处理。 */
	UPROPERTY(EditAnywhere, Category = "Timing", meta = (ClampMin = "0", Units = "s"))
	float FadeIn = 0.f;

	/** 所有地点共用的完整显示时间（UI 秒）；默认 5 s，暂停或打开设置时停表，不由检查点逐个覆盖。 */
	UPROPERTY(EditAnywhere, Category = "Timing", meta = (ClampMin = "0", Units = "s"))
	float Hold = 5.f;

	/** 所有地点共用的淡出时间（UI 秒）；0 表示停留结束立即隐藏并归还 HUD，随后显示存档提示。 */
	UPROPERTY(EditAnywhere, Category = "Timing", meta = (ClampMin = "0", Units = "s"))
	float FadeOut = 0.f;
};
