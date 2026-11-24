#pragma once

#include "CoreMinimal.h"

class FJsonValue;
class UGameUserSettings;
struct FGinkgoUpscaling;

/** 一次打开设置页的画质草稿。候选值取自本机，确认前不修改渲染器、窗口或存档。 */
class CASEGINKGO_API FGinkgoVideoSettingsEdit
{
public:
	void Open(UGameUserSettings& Settings, const FGinkgoUpscaling& Support, bool bEditorWindow);
	bool Set(const FString& Id, const FString& Value);
	void Reset(const FString& Id);
	void ResetAll();
	void Revert();
	int32 CountChanges() const;
	void AppendRows(TArray<TSharedPtr<FJsonValue>>& Out, bool bIncludeDefinition) const;
	void Apply(UGameUserSettings& Settings, const UWorld* World) const;

private:
	/** 一行选项在本次打开期间的快照，与输入设置页使用同一套确认、撤销语义。 */
	struct FRow
	{
		/** 页面目录中的稳定键。 */
		FString Id;

		/** 当前窗口与硬件接受的值。 */
		TArray<FString> Options;

		/** 打开页面时读到的生效值；撤销时回到这里。 */
		FString Saved;

		/** 页面暂存的选择。 */
		FString Value;

		/** 恢复默认按钮的目标。 */
		FString Default;

		/** 硬件或窗口限制。依赖其他草稿项的限制由 DisabledReason 即时计算。 */
		FString Unavailable;
	};

	/** 页面协议中的行，打开时重建。 */
	TArray<FRow> Rows;

	/** 系统提供的显示模式，键是页面显示的像素尺寸。 */
	TMap<FString, FIntPoint> Resolutions;

	/** 驱动允许同时启用垂直同步与帧生成。 */
	bool bCanSyncGeneratedFrames = true;

	const FRow& Row(const TCHAR* Id) const;
	FString DisabledReason(const FRow& Entry) const;
};
