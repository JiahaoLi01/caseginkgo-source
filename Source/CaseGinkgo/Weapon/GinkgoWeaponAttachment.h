#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Weapon/GinkgoAttachmentEffects.h"
#include "GinkgoWeaponAttachment.generated.h"

class UGinkgoWeaponDefinition;
class USceneComponent;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;
class USkeletalMesh;
class UGinkgoModularWeaponVisual;

/** 配件效果说明的表现语义；界面据此选择颜色，不参与实际属性计算，也不从文案正负号推断效果 */
UENUM(BlueprintType)
enum class EGinkgoAttachmentEffectTone : uint8
{
	Neutral UMETA(DisplayName = "说明"),
	Positive UMETA(DisplayName = "增益"),
	Negative UMETA(DisplayName = "减益")
};

/**
 * 桌前拆装配件的动作风格，由配件数据资产指定；决定施力手、拆装轴向、身体/固定相机遮挡取物点与节奏时长。
 * Default 完整保留既有 AK-47 消音器节奏与路径；其余条目为 M4A1 各模块化部位提供利索、贴合结构的专属动作。
 */
UENUM(BlueprintType)
enum class EGinkgoAttachmentInstallStyle : uint8
{
	/** 经典左手取物与绕枪管轴两拍拧装（AK-47 消音器沿用此项，不改变原有节奏） */
	Default UMETA(DisplayName = "默认拧装"),

	/** 枪托轴向推卡：左手稳枪，右手沿缓冲管向右拔出旧托入右腰盲区，再对准推入新托并轻拍锁止 */
	StockSlide UMETA(DisplayName = "枪托轴向推卡"),

	/** 主握把拆装：左手压稳护木，右手沿握把倾斜轴向下拔出旧握把入右腰盲区，推入新握把并单拍拧紧 */
	PistolGrip UMETA(DisplayName = "主握把拆装"),

	/** 前握把导轨快装：右手稳枪，左手沿下导轨向近侧滑脱解锁入躯干左侧盲区，滑入新件并扣紧 */
	ForegripRail UMETA(DisplayName = "前握把导轨快装"),

	/** 扳机精准落位：左手压稳前机匣，右手微抬拔出旧扳机藏入右腰盲区，快拿新扳机直落槽位并按销锁紧 */
	TriggerDropIn UMETA(DisplayName = "扳机精准落位"),

	/** 前端护木总成滑换：右手稳住机匣，左手沿枪管轴向左抽离旧前端入躯干深盲区，再沿轴推套新前端卡紧 */
	HandguardSlide UMETA(DisplayName = "前端轴向滑换"),

	/** 枪口件利索旋换：右手稳枪，左手反向快拧并沿枪管轴抽离旧枪口，再从左侧盲区快取新枪口旋紧 */
	MuzzleQuickThread UMETA(DisplayName = "枪口利索旋换")
};

/**
 * @struct FGinkgoAttachmentEffectDescription : 配件卡片效果区的一项说明
 * 配件资产按展示顺序持有，界面逐项绘制并自动换行。文字可以概括效果，也可以描述条件；实际数值仍由配件的 Effects 属性定义。
 * 空文案不显示；不持有安装状态，不随某把枪的装卸修改。
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoAttachmentEffectDescription
{
	GENERATED_BODY()

	/** 一项效果的完整展示文字，保留设计者写入的正负号和换行；空值跳过，不生成占位行 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Display", meta = (DisplayName = "效果文案", MultiLine = true))
	FText Text;

	/** 展示语义：增益为绿色、减益为红色、普通说明沿用浅色；仅决定样式，不改变属性效果 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Display", meta = (DisplayName = "效果类型"))
	EGinkgoAttachmentEffectTone Tone = EGinkgoAttachmentEffectTone::Neutral;
};

/**
 * @class UGinkgoWeaponAttachment : 工作台上可以安装的一种配件
 * 内容分两处配置。配件资产给模型、说明、图名和效果；武器定义的 Attachments 给适配关系、挂点名和相对变换。
 * 工作台读这份资产生成卡片，安装动作借用模型；成功后武器槽记住配件资产，持枪、地上的拾取物和 HUD 从同一份状态恢复外观。
 * 同一资产可以由多把枪引用。安装改变的是那把枪的 FGinkgoInstalledAttachments，资产里的默认值供各实例共用。
 */
UCLASS(BlueprintType)
class CASEGINKGO_API UGinkgoWeaponAttachment : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 工作台配件卡片显示的名称 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Display")
	FText DisplayName;

	/** 配件的简短介绍，显示在名称与效果区之间；实际增益、减益的展示文字放入 EffectDescriptions，空值不占行 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Display", meta = (MultiLine = true))
	FText Description;

	/** 独立效果区的有序条目，支持多项和长文案；为空时隐藏整个效果区，运行时只读，不从游戏数值自动拼接 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Display", meta = (DisplayName = "效果说明", TitleProperty = "Text"))
	TArray<FGinkgoAttachmentEffectDescription> EffectDescriptions;

	/** 工作台与武器栏共用的图名，两个页面都读 Content/UI/Interaction/<图名>_Part.png；武器栏按挂点对齐 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Display")
	FName HudIcon;

	/** 模块拼图的裁切尺寸（172×38 枪图坐标中的逻辑 px）；零表示沿用旧的 CSS 图标布局 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Display")
	FVector2D HudSize = FVector2D::ZeroVector;

	/** 连接端在裁切图内的位置（逻辑 px，左上角为原点），拼接时与武器挂点对齐 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Display")
	FVector2D HudPivot = FVector2D::ZeroVector;

	/** 安装动作的临时模型与装好后的外观共用，在创建对应组件时同步载入 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mesh")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** 模块安装时使用的骨骼模型；与 Mutable 输入共用枪空间和骨架。空值沿用静态模型 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mesh")
	TSoftObjectPtr<USkeletalMesh> ModuleMesh;

	/** Mutable 中该模块的选项名；普通附加配件留空，不参与组合网格生成 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mesh")
	FName ModularOption;

	/** 空模块选项（如不装前握把）；参与插槽互斥和保存，但没有外观或效果 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mesh")
	bool bEmptyModule = false;

	/** 开镜、换弹、散布与容量的独立修正；只由实例汇总，展示文案仍由 EffectDescriptions 定义 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects")
	FGinkgoAttachmentEffects HandlingEffects;

	/** 桌前落位后，第一下转腕的表演幅度（度，绕模型局部 X）。为零时落位直接松手，适合直接放入的配件 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation", meta = (ClampMin = "-90", ClampMax = "90", Units = "Degrees"))
	float InstallWristTurn = 0.f;

	/** 桌前拆装的动作风格；Default 沿用既有左手拧装，M4A1 各类模块按结构选择对应利索拆装风格 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	EGinkgoAttachmentInstallStyle InstallStyle = EGinkgoAttachmentInstallStyle::Default;

	/** 弹匣模块在工作台上复用战术换弹片段；在 MagIn 提交容量与外观，并按后备余量装填。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation")
	bool bInstallWithReload = false;

	/**
	 * 装上后替换的开火声，音量与上一声的淡出沿用武器的 Fire cue。
	 * 多个配件提供声音时优先用武器定义里靠前的一项，取不到配件声音时依次用弹种和武器自己的声音
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects")
	TSoftObjectPtr<USoundBase> FireSound;

	/** 开火听觉传播半径的倍率；1 保持原枪听距，多个已装配件相乘。只影响听距，不修改音量、后坐或射击事件。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects", meta = (ClampMin = "0.01"))
	float FireNoiseRangeScale = 1.f;

	/** 姿态混合后加到模板偏航、俯仰系数上的值。不同挂点的改变量相加 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects")
	float PatternScaleOffset = 0.f;
};

/** @struct FGinkgoWeaponAttachmentMount : 这把枪接受的配件和它在枪上的挂点 */
USTRUCT(BlueprintType)
struct FGinkgoWeaponAttachmentMount
{
	GENERATED_BODY()

	/** 这个挂点接受的配件定义，工作台据此列出可安装项 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment")
	TObjectPtr<UGinkgoWeaponAttachment> Part;

	/** 同名挂点同一时间只能装一个配件，武器栏用这个名字找到枪图上的锚点 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment")
	FName Mount;

	/** 配件相对枪网格的变换。桌上安装的终点、手上和地上的外观共用 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attachment")
	FTransform RelativeTransform;

	/** 模块武器的新实例默认选择此项；同一挂点最多一个默认项，普通附加配件为假 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modular")
	bool bDefault = false;

	/** 该模块独占的机械骨骼；安装动作临时隐藏原件时使用，空值表示不支持拆旧件 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modular")
	FName ModuleBone;

	/** 换弹轨道中替换弹匣的骨骼名；InstallWithReload 用它驱动新件预览。空值不支持换弹安装，与 ModuleBone 共用参考姿势 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modular")
	FName ReloadSpareBone;

	/** 该插槽在武器 HUD 基准图中的锚点（逻辑 px，左上角为原点），仅模块拼图使用 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modular")
	FVector2D HudAnchor = FVector2D::ZeroVector;

	/** 前端总成提供的枪口子锚点（同一 HUD 坐标）；零表示不覆盖枪口插槽的基础锚点 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modular")
	FVector2D MuzzleHudAnchor = FVector2D::ZeroVector;

	/** 选择此模块后不能占用的其他插槽；空模块不算占用，双方兼容性均在提交前检查 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modular")
	TArray<FName> IncompatibleMounts;

	/** 此模块提供的枪口位置（枪空间 cm）；仅前端总成填写，零向量表示沿用武器原始枪口 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modular")
	FVector MuzzleLocation = FVector::ZeroVector;

	/** 模块改变左手接触时的枪空间偏移（cm），叠加到武器基础握点；零表示保持基础握姿 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Modular")
	FVector SupportHandOffset = FVector::ZeroVector;
};

/** @struct FGinkgoInstalledAttachments : 一把枪已经装上的配件，随武器槽复制，丢枪时原样交给拾取物 */
USTRUCT()
struct FGinkgoInstalledAttachments
{
	GENERATED_BODY()

	/** 这把枪已装的配件定义；普通枪空数组表示原装，模块枪新建时填入各插槽默认项。随实例复制并在丢枪、拾取时传递 */
	UPROPERTY(SaveGame)
	TArray<TObjectPtr<UGinkgoWeaponAttachment>> Parts;
};

/**
 * @struct FGinkgoAttachmentMeshes : 本机用来画已装配件的网格
 * 武器槽和地上的拾取物各持有一份。武器引用、已装配件分别复制，任一到达都按当前武器的挂点重建外观。
 */
USTRUCT()
struct FGinkgoAttachmentMeshes
{
	GENERATED_BODY()

	/**
	 * @brief 按已装配件的顺序复用网格，移除多余的组件，并同步枪的可见性
	 * @param Parent - 这把枪的网格，配件相对它应用武器定义中的挂点变换
	 */
	void Update(const UGinkgoWeaponDefinition* Weapon, const FGinkgoInstalledAttachments& Installed, USceneComponent& Parent);

	/** 动作借用本槽的模块外观对象，移交已落位预览；普通枪或尚未初始化时为空。 */
	UGinkgoModularWeaponVisual* GetModularVisual() const { return ModularVisual; }

	/** 拥有者退出或清槽时主动释放异步外观及其交接网格。 */
	void Reset();

private:
	/** 当前已装配件的外观组件，Update 按已装列表顺序复用并销毁多余项 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Meshes;

	/** 复杂模块武器的本机 Mutable 实例；普通武器为空，枪换走时释放，不参与网络复制 */
	UPROPERTY(Transient)
	TObjectPtr<UGinkgoModularWeaponVisual> ModularVisual;
};
