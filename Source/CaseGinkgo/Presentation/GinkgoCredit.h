#pragma once

#include "CoreMinimal.h"
#include "GinkgoCredit.generated.h"

/** 同一职务下的一位合作者或一部引用作品，在当前署名中单独占一行。 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoCreditContributor
{
	GENERATED_BODY()

	/** 显示的姓名或作品标题；留空时跳过该行。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Credit")
	FText Name;

	/** 为本行选择斜体字面，例如在正体作者名下方列出引用作品。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Credit")
	bool bItalic = false;
};

/** 同一职务下同时展示的一组署名；Volume 持有内容，服务器发送给进入者，共用一次显现、停留和淡出。 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoCredit
{
	GENERATED_BODY()

	/** 第一行的职务或署名类别，例如 AUTHOR、ART DIRECTION；空值保留该行的排版高度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Credit")
	FText Role = FText::FromString(TEXT("AUTHOR"));

	/** 首位作者或作品名；沿用已有 Volume 的 Name 字段，留空时从后续非空条目开始显示。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Credit")
	FText Name = FText::FromString(TEXT("JIAHAO LI"));

	/** 首个 Name 使用斜体，适合只展示一部作品的引用署名。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Credit")
	bool bItalic = false;

	/** 同屏的其他作者或作品，按数组顺序逐行排列，每个条目可独立设置斜体。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Credit", meta = (TitleProperty = "Name"))
	TArray<FGinkgoCreditContributor> AdditionalContributors;

	/** 至少有一个可显示的姓名或作品时才接受本组，触发端和本地队列共用该条件。 */
	bool HasContent() const
	{
		return !Name.IsEmptyOrWhitespace() || AdditionalContributors.ContainsByPredicate(
			[](const FGinkgoCreditContributor& Contributor) { return !Contributor.Name.IsEmptyOrWhitespace(); });
	}
};
