#pragma once

#include "CoreMinimal.h"
#include "GinkgoInputSettings.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GinkgoInputSettingsSubsystem.generated.h"

class APlayerController;

/**
 * @class UGinkgoInputSettingsSubsystem : 本地玩家的输入设置
 * 放在本地玩家上，不放控制器上：控制器换关卡时会重建，设置要跟着玩家走；专用服务器上没有本地玩家，也就没有这份数据。
 * 两份数：Current 是正在生效的（Enhanced Input 的修改器每帧读），Saved 是文件里的。设置界面改的是 Current，确认时写文件，撤销修改时回到 Saved。
 * 键位也在这两份数里，由 ApplyKeys 交给 Enhanced Input 的改键系统（UEnhancedInputUserSettings）。键位不走它的存盘（.sav）：同一次确认要和别的设置一起写进一个文件，
 * 它的 SaveSettings 也不告诉写成了没有（Docs/Design/Control.md「改键」）。
 *
 * 存在 Saved/Ginkgo/InputSettings.json（Docs/Design/Control.md「存盘」说了为什么是它）：只记和默认值不同的项；发行版的 Saved 在
 * %LOCALAPPDATA%/CaseGinkgo/Saved（FPaths::ShouldSaveToUserDir），开发版在包的目录里。
 * 自动化的运行（-unattended）不读不写这个文件，测量照旧是默认值；非发行版可以用 -GinkgoInputSettingsFile=<路径> 指定一个文件，这时读写都用它。
 */
UCLASS()
class CASEGINKGO_API UGinkgoInputSettingsSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	/** @brief 这个控制器的本地玩家的；不是本地玩家时为空 */
	static UGinkgoInputSettingsSubsystem* Get(const APlayerController* Controller);

	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	//~ End USubsystem

	/** 正在生效的设置 */
	const FGinkgoInputSettings& GetCurrent() const { return Current; }

	/** 文件里的设置（上一次确认的） */
	const FGinkgoInputSettings& GetSaved() const { return Saved; }

	/**
	 * @brief 改一项，当场生效，不写文件
	 * @param Value - 数夹到范围里；开关 0 或 1；选择是下标
	 * @return 有没有这一项
	 */
	bool SetValue(const FString& Id, double Value);

	/** @brief 一项恢复默认，当场生效，不写文件 */
	bool ResetValue(const FString& Id);

	/**
	 * @brief 改一个键位的键，当场生效，不写文件。别的键位在用这个键时互换
	 * @param Key - EKeys::Invalid 是清空
	 * @return 有没有这个键位
	 */
	bool SetKey(int32 Index, const FKey& Key);

	/** @brief 改一个键位的方式（只有冲刺、蹲、开镜有），当场生效，不写文件 */
	bool SetKeyMode(int32 Index, EGinkgoHoldMode Mode);

	/** @brief 一行（一个动作的两个键位）恢复默认，当场生效，不写文件 */
	bool ResetKeyRow(const FString& RowId);

	/**
	 * @brief 把现在的键位交给 Enhanced Input 的改键系统（只改不一样的，一样时不重建映射）
	 * @note 映射注册过以后才有东西可改：控制器注册完调一次，设置变了再调
	 */
	void ApplyKeys() const;

	/** @brief 一个目录恢复默认，当场生效，不写文件：撤销修改还能回到确认过的 */
	void ResetDirectory(EGinkgoInputDirectory Directory);

	/** @brief 回到文件里的设置（撤销修改，或放弃修改退出） */
	void RevertToSaved();

	/**
	 * @brief 确认：写文件。写不进去时 Saved 不变，改动还在，界面留着让玩家决定
	 * @param OutError - 失败的原因
	 */
	bool Confirm(FString& OutError);

	/** 文件的路径；不用文件时（-unattended）也是它该在的地方 */
	const FString& GetFilePath() const { return FilePath; }

	/** Current 变了以后（控制器要清掉切换留下的状态、改映射里的键） */
	FSimpleMulticastDelegate OnChanged;

private:
	void SetCurrent(const FGinkgoInputSettings& Settings);

	FGinkgoInputSettings Current;
	FGinkgoInputSettings Saved;
	FString FilePath;
	/** -unattended 时不碰文件 */
	bool bUseFile = true;
};
