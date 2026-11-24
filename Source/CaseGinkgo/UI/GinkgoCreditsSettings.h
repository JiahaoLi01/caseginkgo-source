#pragma once

#include "CoreMinimal.h"
#include "GinkgoCreditsSettings.generated.h"

/** 所有署名共用的排版和动画，入队时复制快照；Volume 只填写内容和重复触发规则。 */
USTRUCT()
struct CASEGINKGO_API FGinkgoCreditsSettings
{
	GENERATED_BODY()

	/** 已注册的 Weft 无衬线字体家族；默认复用项目自带的 Barlow Condensed。 */
	UPROPERTY(EditAnywhere, Category = "Typography")
	FString FontFamily = TEXT("Barlow Condensed");

	/** 职务行字号，DPI 缩放前的逻辑像素；长文本按可用宽度整体缩小，始终只占一行。 */
	UPROPERTY(EditAnywhere, Category = "Typography", meta = (ClampMin = "1"))
	float RoleSize = 20.f;

	/** 姓名行字号，DPI 缩放前的逻辑像素。 */
	UPROPERTY(EditAnywhere, Category = "Typography", meta = (ClampMin = "1"))
	float NameSize = 44.f;

	/** 职务、作者和作品共用的字重（100–900），默认 600；每一行可按内容选择对应字面。 */
	UPROPERTY(EditAnywhere, Category = "Typography", meta = (ClampMin = "100", ClampMax = "900"))
	int32 FontWeight = 600;

	/** 字符之间的额外间距，相对于各行字号（em）；0 不增加字距。 */
	UPROPERTY(EditAnywhere, Category = "Typography", meta = (ClampMin = "0", ClampMax = "1"))
	float LetterSpacing = .1f;

	/** 职务与首行署名的间距，DPI 缩放前的逻辑像素。 */
	UPROPERTY(EditAnywhere, Category = "Typography", meta = (ClampMin = "0"))
	float LineGap = 5.f;

	/** 暖白文字颜色（sRGB）；每个字符的显现透明度及最终整体淡出另外计算。 */
	UPROPERTY(EditAnywhere, Category = "Typography")
	FColor TextColor = FColor(246, 243, 235);

	/** 展示时转为大写，原始内容不被修改。 */
	UPROPERTY(EditAnywhere, Category = "Typography")
	bool bUppercase = true;

	/** 左边距，占当前玩家视口宽度的比例（0–0.8）。 */
	UPROPERTY(EditAnywhere, Category = "Layout", meta = (ClampMin = "0", ClampMax = "0.8"))
	float Left = .085f;

	/** 下边距，占当前玩家视口高度的比例（0–0.8）；默认放在左下区域，避开屏幕边缘。 */
	UPROPERTY(EditAnywhere, Category = "Layout", meta = (ClampMin = "0", ClampMax = "0.8"))
	float Bottom = .18f;

	/** 每行最多占视口宽度的比例（0.1–0.8），同组作者按最长姓名或作品名缩放字号。 */
	UPROPERTY(EditAnywhere, Category = "Layout", meta = (ClampMin = "0.1", ClampMax = "0.8"))
	float MaxWidth = .28f;

	/** 最早与最晚字符开始显现的间隔（UI 秒）；字符顺序每次随机，0 表示全部同时开始。 */
	UPROPERTY(EditAnywhere, Category = "Timing", meta = (ClampMin = "0", Units = "s"))
	float Stagger = .55f;

	/** 单个字符从模糊/透明到清晰/不透明的时间（UI 秒）；0 表示在其开始时刻立即显现。 */
	UPROPERTY(EditAnywhere, Category = "Timing", meta = (ClampMin = "0", Units = "s"))
	float Reveal = .6f;

	/** 全部字符清晰以后完整停留的 UI 秒数；暂停、设置页和地点标注期间不计时。 */
	UPROPERTY(EditAnywhere, Category = "Timing", meta = (ClampMin = "0", Units = "s"))
	float Hold = 5.f;

	/** 整组同时淡出的 UI 秒数；0 表示停留结束直接消失。 */
	UPROPERTY(EditAnywhere, Category = "Timing", meta = (ClampMin = "0", Units = "s"))
	float FadeOut = .7f;

	/** 字符开始显现时的高斯模糊标准差，单位为逻辑像素；0 只按随机顺序淡入。 */
	UPROPERTY(EditAnywhere, Category = "Timing", meta = (ClampMin = "0", ClampMax = "32"))
	float Blur = 10.f;
};
