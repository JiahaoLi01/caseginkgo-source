#include "Algo/BinarySearch.h"
#include "Algo/Transform.h"
#include "Camera/GinkgoCameraConfig.h"
#include "Camera/GinkgoPlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Control/GinkgoPlayerController.h"
#include "Containers/Ticker.h"  // FTSTicker：测试输入按世界时间排着
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"  // 视口的 DPI 缩放：页面第一次布局用的大小
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "GinkgoCameraConfigPath.h"
#include "GinkgoCameraDebug.h"
#include "GinkgoCameraRayTuning.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Movement/GinkgoCharacterMovementComponent.h"  // 描述区的预览按移动配置的速度画（滑铲的减速、跑速）
#include "Movement/GinkgoMovementConfig.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UI/GinkgoUiSounds.h"
#include "SWeftViewport.h"
#include "UI/GinkgoUiTheme.h"  // 纸页的主题：与以后的设置界面共用的样式与组件
#include "UnrealClient.h"  // FScreenshotRequest
#include "UObject/StrongObjectPtr.h"  // 持有 weft 的 View：weft 的子系统只记弱引用
#include "Utilities/Debug/DebugCategory.h"
#include "WeftSubsystem.h"
#include "WeftView.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/SOverlay.h"

/**
 * 相机的调参面板（weft 页面 Source/CaseGinkgoDebug/UI/CameraTuning）与本地的调参文件 Saved/Ginkgo/CameraTuning.json。
 * 面板列出本地相机正在用的配置里所有能调的项（GinkgoCameraConfigPath 的 CollectTunables），改了立即生效；"保存到本地"把和默认值不同的项写进文件，
 * 之后每次启动看到相机时自动套用（自动化的运行 -unattended、或带 -GinkgoNoCameraTuning 时不套用，测量照旧是类默认值）。
 * 默认值是第一次看到这份配置时记下的：类默认值在这之前没人改过；编辑器里多次 PIE 共用类默认值，记下的仍是第一次的。
 * 开关：F7 或 ginkgo.camera.tune。开着时鼠标交给面板（显示指针），键盘照常走游戏；滚轮在面板上滚面板。
 *
 * 页面是纸页（主题见 UI/GinkgoUiTheme.h）：左边是目录，中间是这一类的选项，右边是描述区（原理、公式与按当前值画的预览）。
 * 目录怎么分、每一项的说明与预览都在页面的 catalog.js 里；这里给的是配置本身：每一行路径上每一段的字段、所属的结构与显示名（参数的
 * DisplayName），每一组（修改器、叠加效果、结构）的类型、名字、作用的参数与激活条件，再加上移动配置的速度（预览按它们画滑铲、冲刺）
 */
namespace
{
	using namespace Ginkgo::Debug::Camera;

	const TCHAR* CommandName = TEXT("ginkgo.camera.tune");

	/** 面板的热键：F1–F5、F9、F11 是引擎在开发版里的调试键，F6 是窗口时间轴，F8 是编辑器里 PIE 的弹出 */
	const FKey ToggleKey = EKeys::F7;

	/** 在别的界面之上（武器栏 5、交互的提示 10、窗口时间轴 20） */
	constexpr int32 WidgetZOrder = 30;

	/** 调参面板底层高斯模糊强度，与设置界面保持一致 */
	constexpr float TuningBlurStrength = 9.0f;

	FString GetTuningFilePath()
	{
		// 自动化用独立文件验证保存/重启，不触碰玩家已有的调参文件。
		FString Override;
		return FParse::Value(FCommandLine::Get(), TEXT("GinkgoCameraTuningFile="), Override)
			? Override : FPaths::ProjectSavedDir() / TEXT("Ginkgo/CameraTuning.json");
	}

	FString GetPageDirectory()
	{
		return FPaths::ProjectDir() / TEXT("Source/CaseGinkgoDebug/UI/CameraTuning");
	}

	bool ShouldApplyLocalFile()
	{
		FString Override;
		const bool bExplicitFile = FParse::Value(FCommandLine::Get(), TEXT("GinkgoCameraTuningFile="), Override);
		return (!FApp::IsUnattended() || bExplicitFile) && !FParse::Param(FCommandLine::Get(), TEXT("GinkgoNoCameraTuning"));
	}

	/** 本地的调参文件：路径 → 值的文本；没有文件、读不懂时是空的 */
	TMap<FString, FString> ReadTuningFile(const TArray<FGinkgoCameraShoulderRay>& DefaultRays)
	{
		TMap<FString, FString> Values;
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *GetTuningFilePath()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
		{
			return Values;
		}
		const TSharedPtr<FJsonObject>* Items = nullptr;
		if (Root->TryGetObjectField(TEXT("values"), Items))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Item : (*Items)->Values)
			{
				FString Value;
				if (Item.Value && Item.Value->TryGetString(Value))
				{
					Values.Add(Item.Key, Value);
				}
			}
		}
		MigrateRayTuning(Values, DefaultRays);
		return Values;
	}

	/** 一份配置第一次被看到时的值（默认值） */
	struct FConfigRecord
	{
		/** 首次观察到的配置对象；弱引用不延长资产或 PIE 实例的生命周期 */
		TWeakObjectPtr<UGinkgoCameraConfig> Config;

		/** 应用本地覆盖前的完整调参快照；列表作为一个值记录，支持恢复增删 */
		TMap<FString, FString> Defaults;
	};
	TArray<FConfigRecord> Records;

	const FConfigRecord* FindRecord(const UGinkgoCameraConfig& Config)
	{
		return Records.FindByPredicate([&Config](const FConfigRecord& Record) { return Record.Config.Get() == &Config; });
	}

	/** 一项的值写成 JSON：数是数，开关是 true、false，枚举是名字，射线为对象数组 */
	TSharedRef<FJsonValue> ToJson(const FConfigItem& Item)
	{
		if (IsRayList(*Item.Property))
		{
			return RaysToJson(*static_cast<const TArray<FGinkgoCameraShoulderRay>*>(Item.Address));
		}
		if (const FFloatProperty* Float = CastField<FFloatProperty>(Item.Property))
		{
			return MakeShared<FJsonValueNumber>(Float->GetPropertyValue(Item.Address));
		}
		if (const FDoubleProperty* Double = CastField<FDoubleProperty>(Item.Property))
		{
			return MakeShared<FJsonValueNumber>(Double->GetPropertyValue(Item.Address));
		}
		if (const FIntProperty* Int = CastField<FIntProperty>(Item.Property))
		{
			return MakeShared<FJsonValueNumber>(Int->GetPropertyValue(Item.Address));
		}
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Item.Property))
		{
			return MakeShared<FJsonValueBoolean>(Bool->GetPropertyValue(Item.Address));
		}
		return MakeShared<FJsonValueString>(ExportItem(Item));
	}

	/** 页面给的值换成属性的文本 */
	FString FromJson(const FConfigItem& Item, const FJsonValue& Value)
	{
		if (IsRayList(*Item.Property))
		{
			TArray<FGinkgoCameraShoulderRay> Rays;
			return RaysFromJson(Value, Rays) ? ExportRays(Rays) : FString();
		}
		if (Item.Property->IsA<FBoolProperty>())
		{
			return Value.AsBool() ? TEXT("True") : TEXT("False");
		}
		if (Item.Property->IsA<FIntProperty>())
		{
			return FString::FromInt(FMath::RoundToInt32(Value.AsNumber()));
		}
		if (Item.Property->IsA<FFloatProperty>() || Item.Property->IsA<FDoubleProperty>())
		{
			return FString::SanitizeFloat(Value.AsNumber());
		}
		return Value.AsString();
	}

	FString KindOf(const FProperty& Property)
	{
		if (IsRayList(Property))
		{
			return TEXT("rays");
		}
		if (Property.IsA<FBoolProperty>())
		{
			return TEXT("bool");
		}
		if (Property.IsA<FIntProperty>())
		{
			return TEXT("int");
		}
		return Property.IsA<FFloatProperty>() || Property.IsA<FDoubleProperty>() ? TEXT("number") : TEXT("enum");
	}

	/** 页面上次的状态（选中的目录与行、折起的类别）：页面发来的 JSON 原样存着，下次打开面板时还给它，关了再开还在原处。进程内有效 */
	FString PageState;

	/**
	 * @brief 字段、结构与类的显示名。编辑器版本读 UPROPERTY 的 DisplayName（中文，给策划看的）；不带编辑器数据的版本没有元数据，
	 *        GetDisplayNameText 也不在，用字段名。页面随这个模块打进 Development 包（Build.cs 的 RuntimeDependencies），所以那里的面板
	 *        标签是英文的字段名，单位、范围与 ToolTip 也没有（WITH_METADATA）；catalog.js 里写了的标签、单位、范围与说明不受影响
	 */
	FString DisplayNameOf(const FField& Field)
	{
#if WITH_EDITORONLY_DATA
		return Field.GetDisplayNameText().ToString();
#else
		return Field.GetName();
#endif
	}

	FString DisplayNameOf(const UField& Field)
	{
#if WITH_EDITORONLY_DATA
		return Field.GetDisplayNameText().ToString();
#else
		return Field.GetName();
#endif
	}

	/** 按路径读一个标签；不是标签、空着或找不到时为空 */
	FString ReadTag(UObject& Root, const FString& Path)
	{
		FConfigItem Item;
		FString Error;
		if (!ResolveConfigPath(Root, Path, Item, Error))
		{
			return FString();
		}
		const FStructProperty* Struct = CastField<FStructProperty>(Item.Property);
		const FGameplayTag* Tag = Struct && Struct->Struct == FGameplayTag::StaticStruct() ? static_cast<const FGameplayTag*>(Item.Address) : nullptr;
		return Tag && Tag->IsValid() ? Tag->ToString() : FString();
	}

	/** 按路径找一个结构的地址；类型不对或找不到时为空 */
	template <typename T>
	const T* ReadStruct(UObject& Root, const FString& Path)
	{
		FConfigItem Item;
		FString Error;
		if (!ResolveConfigPath(Root, Path, Item, Error))
		{
			return nullptr;
		}
		const FStructProperty* Struct = CastField<FStructProperty>(Item.Property);
		return Struct && Struct->Struct == T::StaticStruct() ? static_cast<const T*>(Item.Address) : nullptr;
	}

	/** 标签查询写成表达式的树：{op: any | all | none（这几个标签）| anyOf | allOf | noneOf（这几个子表达式）, tags | exprs}，页面按目录把标签换成中文 */
	TSharedRef<FJsonObject> DescribeQueryExpr(const FGameplayTagQueryExpression& Expr)
	{
		const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		const TCHAR* Op = TEXT("");
		switch (Expr.ExprType)
		{
		case EGameplayTagQueryExprType::AnyTagsMatch:		Op = TEXT("any"); break;
		case EGameplayTagQueryExprType::AllTagsMatch:		Op = TEXT("all"); break;
		case EGameplayTagQueryExprType::NoTagsMatch:		Op = TEXT("none"); break;
		case EGameplayTagQueryExprType::AnyTagsExactMatch:	Op = TEXT("anyExact"); break;
		case EGameplayTagQueryExprType::AllTagsExactMatch:	Op = TEXT("allExact"); break;
		case EGameplayTagQueryExprType::AnyExprMatch:		Op = TEXT("anyOf"); break;
		case EGameplayTagQueryExprType::AllExprMatch:		Op = TEXT("allOf"); break;
		case EGameplayTagQueryExprType::NoExprMatch:		Op = TEXT("noneOf"); break;
		default: break;
		}
		Out->SetStringField(TEXT("op"), Op);
		if (Expr.UsesTagSet())
		{
			TArray<TSharedPtr<FJsonValue>> Tags;
			for (const FGameplayTag& Tag : Expr.TagSet)
			{
				Tags.Add(MakeShared<FJsonValueString>(Tag.ToString()));
			}
			Out->SetArrayField(TEXT("tags"), Tags);
		}
		else
		{
			TArray<TSharedPtr<FJsonValue>> Exprs;
			for (const FGameplayTagQueryExpression& Child : Expr.ExprSet)
			{
				Exprs.Add(MakeShared<FJsonValueObject>(DescribeQueryExpr(Child)));
			}
			Out->SetArrayField(TEXT("exprs"), Exprs);
		}
		return Out;
	}

	/**
	 * @brief 一行路径上 Group 之后的几段：字段名、所属的结构或类（声明它的那个）、显示名（参数的 DisplayName）、数组下标。
	 *        页面按"所属.字段"在目录里找说明，按显示名写标签（Impulse.Pitch 写成"冲量 · 俯仰"）
	 */
	TArray<TSharedPtr<FJsonValue>> DescribeChain(UObject& Root, const FTunable& Row)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		TArray<FPathSegment> Chain;
		FConfigItem Item;
		FString Error;
		if (!ResolveConfigPath(Root, Row.Path, Item, Error, &Chain))
		{
			return Out;
		}
		TArray<FString> GroupTokens;
		Row.Group.ParseIntoArray(GroupTokens, TEXT("."));
		for (int32 Index = GroupTokens.Num(); Index < Chain.Num(); ++Index)
		{
			const FProperty& Property = *Chain[Index].Property;
			const TSharedRef<FJsonObject> Segment = MakeShared<FJsonObject>();
			Segment->SetStringField(TEXT("field"), Property.GetName());
			Segment->SetStringField(TEXT("owner"), Property.GetOwnerStruct() ? Property.GetOwnerStruct()->GetName() : FString());
			Segment->SetStringField(TEXT("name"), DisplayNameOf(Property));
			if (Chain[Index].Index != INDEX_NONE)
			{
				Segment->SetNumberField(TEXT("index"), Chain[Index].Index);
			}
			Out.Add(MakeShared<FJsonValueObject>(Segment));
		}
		return Out;
	}

	/**
	 * @brief 一组：配置的第一层（结构、数组的元素）。类型与显示名、元素的名字、作用的参数（修改器、参数弹簧）、
	 *        事件（冲量弹簧）、包络的连续量（噪声）、激活条件（标签查询的表达式、连续量条件读的标签）
	 */
	TSharedRef<FJsonObject> DescribeGroup(UObject& Root, const FString& Group)
	{
		const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("path"), Group);
		if (Group.IsEmpty())
		{
			Out->SetStringField(TEXT("kind"), TEXT("config"));
			Out->SetStringField(TEXT("type"), Root.GetClass()->GetName());
			Out->SetStringField(TEXT("typeName"), DisplayNameOf(*Root.GetClass()));
			return Out;
		}
		TArray<FPathSegment> Chain;
		FConfigItem Item;
		FString Error;
		if (!ResolveConfigPath(Root, Group, Item, Error, &Chain) || Chain.IsEmpty())
		{
			return Out;
		}
		Out->SetStringField(TEXT("property"), Chain[0].Property->GetName());
		Out->SetStringField(TEXT("propertyName"), DisplayNameOf(*Chain[0].Property));
		Out->SetStringField(TEXT("kind"), Chain.Last().Index != INDEX_NONE ? TEXT("element") : TEXT("struct"));
		if (Chain.Last().Index != INDEX_NONE)
		{
			Out->SetNumberField(TEXT("index"), Chain.Last().Index);
		}
		const UStruct* Type = nullptr;
		if (const FStructProperty* Struct = CastField<FStructProperty>(Item.Property))
		{
			Type = Struct->Struct;
		}
		else if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Item.Property))
		{
			const UObject* Value = Object->GetObjectPropertyValue(Item.Address);
			Type = Value ? Value->GetClass() : nullptr;
		}
		if (Type)
		{
			Out->SetStringField(TEXT("type"), Type->GetName());
			Out->SetStringField(TEXT("typeName"), DisplayNameOf(*Type));
		}
		FConfigItem NameItem;
		if (ResolveConfigPath(Root, Group + TEXT(".Name"), NameItem, Error) && NameItem.Property->IsA<FNameProperty>())
		{
			const FName Name = CastField<FNameProperty>(NameItem.Property)->GetPropertyValue(NameItem.Address);
			Out->SetStringField(TEXT("element"), Name.IsNone() ? FString() : Name.ToString());
		}
		// 作用的参数：GinkgoCameraParams.FieldOfView → FieldOfView，再带上它的显示名与单位
		FConfigItem ParamItem;
		if (ResolveConfigPath(Root, Group + TEXT(".Param.Name"), ParamItem, Error) && ParamItem.Property->IsA<FNameProperty>())
		{
			FString Param = CastField<FNameProperty>(ParamItem.Property)->GetPropertyValue(ParamItem.Address).ToString();
			Param.Split(TEXT("."), nullptr, &Param);
			Out->SetStringField(TEXT("param"), Param);
			if (const FProperty* ParamProperty = FindFProperty<FProperty>(FGinkgoCameraParams::StaticStruct(), *Param))
			{
				Out->SetStringField(TEXT("paramName"), DisplayNameOf(*ParamProperty));
#if WITH_METADATA
				Out->SetStringField(TEXT("paramUnits"), ParamProperty->GetMetaData(TEXT("Units")));
#endif
			}
		}
		if (const FString Event = ReadTag(Root, Group + TEXT(".Event")); !Event.IsEmpty())
		{
			Out->SetStringField(TEXT("event"), Event);
		}
		if (const FString Envelope = ReadTag(Root, Group + TEXT(".EnvelopeScalar")); !Envelope.IsEmpty())
		{
			Out->SetStringField(TEXT("envelopeScalar"), Envelope);
		}
		if (const FGinkgoCameraActivation* Activation = ReadStruct<FGinkgoCameraActivation>(Root, Group + TEXT(".Activation")))
		{
			if (!Activation->Query.IsEmpty())
			{
				FGameplayTagQueryExpression Expr;
				Activation->Query.GetQueryExpr(Expr);
				Out->SetObjectField(TEXT("query"), DescribeQueryExpr(Expr));
			}
			TArray<TSharedPtr<FJsonValue>> Conditions;
			for (const FGinkgoCameraScalarCondition& Condition : Activation->Conditions)
			{
				const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("scalar"), Condition.Scalar.IsValid() ? Condition.Scalar.ToString() : FString());
				Entry->SetStringField(TEXT("thresholdScalar"), Condition.ThresholdScalar.IsValid() ? Condition.ThresholdScalar.ToString() : FString());
				Conditions.Add(MakeShared<FJsonValueObject>(Entry));
			}
			Out->SetArrayField(TEXT("conditions"), Conditions);
		}
		return Out;
	}

	/** 枚举：各个值的名字（不带 _MAX）与显示名 */
	const UEnum* FindEnum(const FProperty& Property)
	{
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(&Property))
		{
			return EnumProperty->GetEnum();
		}
		const FByteProperty* Byte = CastField<FByteProperty>(&Property);
		return Byte ? Byte->Enum.Get() : nullptr;
	}

	/**
	 * @class FCameraTuningPanel : 调参面板
	 * 视图铺满视口，纸页盖住整个画面（游戏照常在后面跑）；页面载入以后发 "ready"，这里回整张表 "tuning"，
	 * 之后页面发 set、reset、resetAll、save、close，这里改配置、回变了的行 "values"；页面换了目录、选中的行时发 "state"，这里记着
	 */
	class FCameraTuningPanel
	{
	public:
		bool IsShown() const { return View.IsValid(); }

		void Toggle(UWorld* World)
		{
			if (IsShown())
			{
				Hide();
			}
			else
			{
				Show(World);
			}
		}

		void Show(UWorld* World)
		{
			if (IsShown())
			{
				return;
			}
			AGinkgoPlayerCameraManager* Camera = FindLocalCamera(World);
			APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
			UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
			UWeftSubsystem* Weft = UWeftSubsystem::Get();
			if (!Camera || !PlayerController || !Viewport || !Weft || !Weft->IsAvailable())
			{
				UE_LOG(LogGinkgoCamera, Warning, TEXT("[Tuning] 打不开调参面板：%s"), !Camera ? TEXT("没有本地的 Ginkgo 相机")
					: !Viewport ? TEXT("没有游戏视口（无界面运行）") : TEXT("weft 在这里不运行（-nullrhi，或没有编出 weft 的运行时库）"));
				return;
			}
			UWeftView* NewView = Weft->CreateView();
			if (!NewView)
			{
				UE_LOG(LogGinkgoCamera, Warning, TEXT("[Tuning] 打不开调参面板：weft 没有给出 View"));
				return;
			}
			OnCameraSeen(*Camera);
			View.Reset(NewView);
			ViewportClient = Viewport;
			Controller = PlayerController;
			Config = &GetMutableConfig(*Camera);
			Rows = CollectTunables(*Config);
			// 旧文件的统一射程应覆盖原始布局，不能拿本次 PIE 已增删的列表作为迁移基准。
			TArray<FGinkgoCameraShoulderRay> DefaultRays = Config->Shoulder.Rays;
			ImportRays(DefaultText(TEXT("Shoulder.Rays")), DefaultRays);
			Saved = ReadTuningFile(DefaultRays);
			MessageHandle = NewView->OnMessage.AddRaw(this, &FCameraTuningPanel::OnMessage);
			Widget = SNew(SWeftViewport).View(NewView);
			RootWidget = SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SBackgroundBlur)
					.Visibility(EVisibility::HitTestInvisible)
					.Padding(FMargin(0.f))
					.BlurStrength(TuningBlurStrength)
					.bApplyAlphaToBlur(true)
				]
				+ SOverlay::Slot()
				[
					Widget.ToSharedRef()
				];
			Viewport->AddViewportWidgetContent(RootWidget.ToSharedRef(), WidgetZOrder);
			PostActorTickHandle = FWorldDelegates::OnWorldPostActorTick.AddRaw(this, &FCameraTuningPanel::OnWorldPostActorTick);
			WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddRaw(this, &FCameraTuningPanel::OnWorldCleanup);

			// 鼠标交给面板：显示指针，只在按下时抓鼠标（按住左键拖过游戏画面照样转视角）；键盘照常走游戏
			bPreviousCursor = PlayerController->ShouldShowMouseCursor();
			PreviousFocus = FSlateApplication::Get().GetKeyboardFocusedWidget();
			FInputModeGameAndUI InputMode;
			InputMode.SetWidgetToFocus(Widget);
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			InputMode.SetHideCursorDuringCapture(false);
			PlayerController->SetInputMode(InputMode);
			PlayerController->SetShowMouseCursor(true);
			Widget->FocusView(View.Get());
			UE_LOG(LogGinkgoCamera, Log, TEXT("[Tuning] 打开调参面板：%d 项（%s 或 %s 关闭），本地的调参文件 %s"), Rows.Num(), *ToggleKey.GetDisplayName().ToString(),
				CommandName, *GetTuningFilePath());
			UGinkgoUiSoundSubsystem::Play(PlayerController, EGinkgoUiSound::Open);
		}

		/**
		 * @brief 测试用的输入（ginkgo.camera.tune.input）：直接给页面的指针、按键与文字，不经过系统的鼠标与键盘；截图、存表、退出
		 * @param Args - click | down | up | move | drag <x> <y>（逻辑 px；drag 是按着左键的移动）、wheel <x> <y> <dy>、type <文字>（全选后输入、回车）、shot <文件>（带界面）、
		 *               dump <文件>（发给页面的整张表存成 JSON：浏览器里预览页面用的样本）、quit
		 */
		void Inject(const TArray<FString>& Args)
		{
			const FString Action = Args.Num() > 0 ? Args[0] : FString();
			UE_LOG(LogGinkgoCamera, Log, TEXT("[Tuning] 测试输入：%s"), *FString::Join(Args, TEXT(" ")));
			if (Action == TEXT("shot") && Args.Num() >= 2)
			{
				FScreenshotRequest::RequestScreenshot(Args[1], true, false);
			}
			else if (Action == TEXT("dump") && Args.Num() >= 2 && Config.IsValid())
			{
				FString Json;
				FJsonSerializer::Serialize(BuildTable(), TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Json));
				const bool bSaved = FFileHelper::SaveStringToFile(Json, *Args[1], FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
				UE_LOG(LogGinkgoCamera, Log, TEXT("[Tuning] 整张表%s %s"), bSaved ? TEXT("存到") : TEXT("存不进"), *Args[1]);
			}
			else if (Action == TEXT("quit"))
			{
				FPlatformMisc::RequestExit(false, TEXT("ginkgo.camera.tune.input"));
			}
			else if (!View)
			{
				UE_LOG(LogGinkgoCamera, Warning, TEXT("[Tuning] 测试输入 %s：面板没开着"), *Action);
				return;
			}
			// 测试输入一来，面板就不再接真的鼠标：Slate 会按真的指针位置给页面补发移动，指针停在窗口里时会改掉悬停、打断拖动
			if (Widget && Widget->GetVisibility() != EVisibility::HitTestInvisible)
			{
				Widget->SetVisibility(EVisibility::HitTestInvisible);
				UE_LOG(LogGinkgoCamera, Log, TEXT("[Tuning] 测试输入：面板不再接真的鼠标，只收注入的"));
			}
			if (Action == TEXT("shot") || Action == TEXT("quit") || Action == TEXT("dump"))
			{
				return;
			}
			else if (Action == TEXT("type") && Args.Num() >= 2)
			{
				View->Key(true, TEXT("KeyA"), TEXT("a"), EWeftModifier::Control);
				View->Key(false, TEXT("KeyA"), TEXT("a"), EWeftModifier::Control);
				View->CommitText(FString::Join(TArrayView<const FString>(Args).RightChop(1), TEXT(" ")));
				View->Key(true, TEXT("Enter"), TEXT("Enter"));
				View->Key(false, TEXT("Enter"), TEXT("Enter"));
			}
			else if (Args.Num() >= 3)
			{
				const FVector2D Position(FCString::Atod(*Args[1]), FCString::Atod(*Args[2]));
				if (Action == TEXT("click") || Action == TEXT("move"))
				{
					View->Pointer(EWeftPointer::Move, Position, 0);
				}
				if (Action == TEXT("drag"))
				{
					View->Pointer(EWeftPointer::Move, Position, 1);
				}
				if (Action == TEXT("click") || Action == TEXT("down"))
				{
					View->Pointer(EWeftPointer::Down, Position, 1);
				}
				if (Action == TEXT("click") || Action == TEXT("up"))
				{
					View->Pointer(EWeftPointer::Up, Position, 0);
				}
				if (Action == TEXT("wheel") && Args.Num() >= 4)
				{
					View->Pointer(EWeftPointer::Move, Position, 0);
					View->Pointer(EWeftPointer::Wheel, Position, 0, FVector2D(0.0, FCString::Atod(*Args[3])));
				}
			}
		}

		void Hide()
		{
			if (!IsShown())
			{
				return;
			}
			if (const APlayerController* Owner = Controller.Get(); Owner && Owner->GetWorld() && !Owner->GetWorld()->bIsTearingDown)
			{
				UGinkgoUiSoundSubsystem::Play(Owner, EGinkgoUiSound::Close);
			}
			FWorldDelegates::OnWorldPostActorTick.Remove(PostActorTickHandle);
			FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
			View->OnMessage.Remove(MessageHandle);
			if (UGameViewportClient* Viewport = ViewportClient.Get(); Viewport && RootWidget)
			{
				Viewport->RemoveViewportWidgetContent(RootWidget.ToSharedRef());
			}
			if (APlayerController* PlayerController = Controller.Get(); PlayerController && PlayerController->GetWorld()
				&& PlayerController->GetWorld()->GetGameViewport() && !PlayerController->GetWorld()->bIsTearingDown)
			{
				const auto* GinkgoPC = Cast<AGinkgoPlayerController>(PlayerController);
				// 暂停菜单或设置页在更上层时由它们保有输入；否则还给打开 F7 前的工作台或游戏视口。
				if (!GinkgoPC || !GinkgoPC->IsMenuOpen())
				{
					if (bPreviousCursor)
					{
						FInputModeGameAndUI InputMode;
						InputMode.SetWidgetToFocus(PreviousFocus.Pin());
						InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
						InputMode.SetHideCursorDuringCapture(false);
						PlayerController->SetInputMode(InputMode);
						// SetInputMode 的 Slate 操作由视口延后处理；关闭面板的当下也要交回焦点。
						if (const auto Focus = PreviousFocus.Pin(); Focus && Focus->SupportsKeyboardFocus())
						{
							FSlateApplication::Get().SetAllUserFocus(Focus, EFocusCause::SetDirectly);
						}
					}
					else { PlayerController->SetInputMode(FInputModeGameOnly()); }
					PlayerController->SetShowMouseCursor(bPreviousCursor);
				}
			}
			PreviousFocus.Reset();
			RootWidget.Reset();
			Widget.Reset();
			View.Reset();
			ViewportClient.Reset();
			Controller.Reset();
			Config.Reset();
			Rows.Reset();
			bLoaded = false;
			bHideRequested = false;
			UE_LOG(LogGinkgoCamera, Log, TEXT("[Tuning] 关闭调参面板"));
		}

	private:
		void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
		{
			const APlayerController* PlayerController = Controller.Get();
			if (!PlayerController || PlayerController->GetWorld() == World)
			{
				Hide();
			}
		}

		void OnWorldPostActorTick(UWorld* World, const ELevelTick TickType, const float DeltaSeconds)
		{
			const APlayerController* PlayerController = Controller.Get();
			if (!PlayerController || !Config.IsValid() || bHideRequested)
			{
				Hide();
				return;
			}
			// 全局委托也会收到编辑器预览和其它 PIE World；只有所属游戏世界能推进此面板。
			// 不属于自己的更新不能被当成关闭条件，否则 F7 刚打开就会在同一帧被关掉。
			if (World != PlayerController->GetWorld() || TickType == LEVELTICK_TimeOnly) { return; }
			// 视口有了大小才载入：View 一开始是 1 × 1，页面第一次布局在那么窄的宽度上，文字会折成很长一列
			FVector2D Pixels = FVector2D::ZeroVector;
			if (const UGameViewportClient* Viewport = ViewportClient.Get())
			{
				Viewport->GetViewportSize(Pixels);
			}
			if (bLoaded || Pixels.X < 2.0 || Pixels.Y < 2.0)
			{
				return;
			}
			const float DPIScale = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(FMath::RoundToInt(Pixels.X), FMath::RoundToInt(Pixels.Y)));
			View->Resize(Pixels / DPIScale, DPIScale);
			if (!Ginkgo::UI::LoadThemedPage(*View, GetPageDirectory()))
			{
				UE_LOG(LogGinkgoCamera, Warning, TEXT("[Tuning] 页面载入失败：%s（原因见 LogGinkgoUI、LogWeft）"), *GetPageDirectory());
				Hide();
				return;
			}
			bLoaded = true;
		}

		/** 页面的消息：ready（载入好了）、set {path, value}、reset {path}、resetAll、save、close、state {…}（页面自己的，原样存着） */
		void OnMessage(const FString& Name, const FString& JsonValue, uint64 CallId)
		{
			TSharedPtr<FJsonValue> Value;
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonValue), Value);
			const TSharedPtr<FJsonObject>* Object = nullptr;
			const bool bObject = Value.IsValid() && Value->TryGetObject(Object);
			if (Name == TEXT("ready"))
			{
				SendTable();
			}
			else if (Name == TEXT("state"))
			{
				PageState = JsonValue;
			}
			else if (Name == TEXT("sound"))
			{
				UGinkgoUiSoundSubsystem::PlayFromPage(Controller.Get(), JsonValue);
			}
			else if (Name == TEXT("log"))
			{
				// 页面自己的诊断（发行版里的页面没有 console）
				UE_LOG(LogGinkgoCamera, Log, TEXT("[Tuning] 页面：%s"), *JsonValue);
			}
			else if (Name == TEXT("set") && bObject)
			{
				const FString Path = (*Object)->GetStringField(TEXT("path"));
				const TSharedPtr<FJsonValue> NewValue = (*Object)->TryGetField(TEXT("value"));
				FConfigItem Item;
				FString Error;
				FString Status;
				if (NewValue && ResolveConfigPath(*Config, Path, Item, Error))
				{
					const bool bImported = ImportItem(Item, FromJson(Item, *NewValue));
					Status = bImported ? FString::Printf(TEXT("%s = %s（还没存）"), *Path, *ExportItem(Item)) : FString::Printf(TEXT("%s：读不懂这个值"), *Path);
					if (bImported && IsRayList(*Item.Property))
					{
						Status = FString::Printf(TEXT("检测射线：%d 条（还没存）"), static_cast<const TArray<FGinkgoCameraShoulderRay>*>(Item.Address)->Num());
					}
					UE_CLOG(!bImported, LogGinkgoCamera, Warning, TEXT("[Tuning] %s：读不懂 %s"), *Path, *JsonValue);
				}
				SendValues({Path}, Status);
			}
			else if (Name == TEXT("reset") && bObject)
			{
				const FString Path = (*Object)->GetStringField(TEXT("path"));
				ResetToDefault(Path);
				SendValues({Path}, FString::Printf(TEXT("%s 恢复成默认值（还没存）"), *Path));
			}
			else if (Name == TEXT("resetAll"))
			{
				TArray<FString> Paths;
				for (const FTunable& Row : Rows)
				{
					ResetToDefault(Row.Path);
					Paths.Add(Row.Path);
				}
				SendValues(Paths, TEXT("都恢复成默认值了（还没存）"));
			}
			else if (Name == TEXT("save"))
			{
				Save();
			}
			else if (Name == TEXT("close"))
			{
				// 不在这里关：消息是页面在 SWeftViewport::Tick 里发的，而 Slate 是在画控件的那一趟里 Tick 它的。这时拿掉控件，它的 OnPaint
				// 已经把 drawer 交给了这一帧的批次（只存裸指针），控件析构时排的"在渲染线程释放 drawer"跑在这一帧的 Slate 绘制之前，
				// 渲染线程就拿着释放了的 drawer 画（2026-11-01 点关闭按钮时崩过）。所以只记下来，下一帧世界 tick 时（Slate 绘制之前）再关
				bHideRequested = true;
			}
		}

		/** 默认值的文本（第一次看到这份配置时记下的） */
		FString DefaultText(const FString& Path) const
		{
			const FConfigRecord* Record = Config.IsValid() ? FindRecord(*Config) : nullptr;
			const FString* Text = Record ? Record->Defaults.Find(Path) : nullptr;
			return Text ? *Text : FString();
		}

		/** 文件里存的值：没存过的项是默认值 */
		FString SavedText(const FString& Path) const
		{
			const FString* Text = Saved.Find(Path);
			return Text ? *Text : DefaultText(Path);
		}

		void ResetToDefault(const FString& Path)
		{
			FConfigItem Item;
			FString Error;
			const FString Default = DefaultText(Path);
			if (!Default.IsEmpty() && ResolveConfigPath(*Config, Path, Item, Error))
			{
				ImportItem(Item, Default);
			}
		}

		/** 和默认值不同的项写进本地的文件，按面板的顺序 */
		void Save()
		{
			FString Text;
			const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text);
			Writer->WriteObjectStart();
			Writer->WriteValue(TEXT("note"), TEXT("相机调参（F7 的面板存的）：和类默认值不同的项，路径 → 值的文本。启动时自动套用（-unattended、-GinkgoNoCameraTuning 时不套用）；删掉这个文件就回到默认"));
			Writer->WriteValue(TEXT("config"), Config->GetPathName());
			Writer->WriteObjectStart(TEXT("values"));
			TMap<FString, FString> NewSaved;
			for (const FTunable& Row : Rows)
			{
				FConfigItem Item;
				FString Error;
				if (ResolveConfigPath(*Config, Row.Path, Item, Error) && ExportItem(Item) != DefaultText(Row.Path))
				{
					NewSaved.Add(Row.Path, ExportItem(Item));
					Writer->WriteValue(Row.Path, ExportItem(Item));
				}
			}
			Writer->WriteObjectEnd();
			Writer->WriteObjectEnd();
			Writer->Close();
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(GetTuningFilePath()), true);
			const bool bSaved = FFileHelper::SaveStringToFile(Text, *GetTuningFilePath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			if (bSaved)
			{
				Saved = MoveTemp(NewSaved);
			}
			UE_LOG(LogGinkgoCamera, Log, TEXT("[Tuning] %s %s：%d 项和默认值不同"), bSaved ? TEXT("存到") : TEXT("存不进"), *GetTuningFilePath(), Saved.Num());
			UGinkgoUiSoundSubsystem::Play(Controller.Get(), bSaved ? EGinkgoUiSound::Confirm : EGinkgoUiSound::Invalid);
			TArray<FString> Paths;
			Algo::Transform(Rows, Paths, &FTunable::Path);
			SendValues(Paths, bSaved ? FString::Printf(TEXT("存好了：%d 项和默认值不同"), Saved.Num()) : TEXT("存不进本地的文件，见日志"));
		}

		/** 一行的值、和默认值比、和存过的比 */
		TSharedRef<FJsonObject> RowValue(const FString& Path) const
		{
			const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("path"), Path);
			FConfigItem Item;
			FString Error;
			if (ResolveConfigPath(*Config, Path, Item, Error))
			{
				const FString Text = ExportItem(Item);
				Out->SetField(TEXT("value"), ToJson(Item));
				Out->SetBoolField(TEXT("changed"), Text != DefaultText(Path));
				Out->SetBoolField(TEXT("unsaved"), Text != SavedText(Path));
			}
			return Out;
		}

		void WriteSummary(FJsonObject& Out, const FString& Status) const
		{
			int32 Changed = 0;
			int32 Unsaved = 0;
			for (const FTunable& Row : Rows)
			{
				FConfigItem Item;
				FString Error;
				if (ResolveConfigPath(*Config, Row.Path, Item, Error))
				{
					const FString Text = ExportItem(Item);
					Changed += Text != DefaultText(Row.Path) ? 1 : 0;
					Unsaved += Text != SavedText(Row.Path) ? 1 : 0;
				}
			}
			Out.SetNumberField(TEXT("changed"), Changed);
			Out.SetNumberField(TEXT("unsaved"), Unsaved);
			Out.SetStringField(TEXT("status"), Status);
		}

		bool Emit(const TCHAR* Name, const TSharedRef<FJsonObject>& Payload) const
		{
			FString Json;
			FJsonSerializer::Serialize(Payload, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
			return View && View->Emit(Name, Json);
		}

		/**
		 * @brief 整张表：行按配置里声明的顺序，每行带路径上的几段、种类、值、默认值、范围、单位与完整的说明（ToolTip）；
		 *        组按第一次出现的顺序；再带上移动的数（预览用）与页面上次的状态。目录怎么分由页面的 catalog.js 定
		 */
		void SendTable() const
		{
			Emit(TEXT("tuning"), BuildTable());
		}

		TSharedRef<FJsonObject> BuildTable() const
		{
			const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("file"), GetTuningFilePath());
			Payload->SetStringField(TEXT("key"), ToggleKey.GetDisplayName().ToString());
			Payload->SetStringField(TEXT("config"), Config->GetPathName());
			Payload->SetStringField(TEXT("state"), PageState);
			Payload->SetObjectField(TEXT("context"), DescribeContext());
			TArray<TSharedPtr<FJsonValue>> Groups;
			TArray<TSharedPtr<FJsonValue>> RowValues;
			TSet<FString> SeenGroups;
			for (const FTunable& Row : Rows)
			{
				if (!SeenGroups.Contains(Row.Group))
				{
					SeenGroups.Add(Row.Group);
					Groups.Add(MakeShared<FJsonValueObject>(DescribeGroup(*Config, Row.Group)));
				}
				RowValues.Add(MakeShared<FJsonValueObject>(DescribeRow(Row)));
			}
			Payload->SetArrayField(TEXT("groups"), Groups);
			Payload->SetArrayField(TEXT("rows"), RowValues);
			WriteSummary(*Payload, Saved.IsEmpty() ? TEXT("还没有存过（都是默认值）") : FString::Printf(TEXT("本地的文件里有 %d 项"), Saved.Num()));
			return Payload;
		}

		TSharedRef<FJsonObject> DescribeRow(const FTunable& Row) const
		{
			const TSharedRef<FJsonObject> Out = RowValue(Row.Path);
			Out->SetStringField(TEXT("group"), Row.Group);
			Out->SetStringField(TEXT("kind"), KindOf(*Row.Property));
			Out->SetArrayField(TEXT("chain"), DescribeChain(*Config, Row));
			// 默认值按值的写法给（数、开关、名字），从记下的文本换
			const FString Default = DefaultText(Row.Path);
			if (IsRayList(*Row.Property))
			{
				TArray<FGinkgoCameraShoulderRay> Rays;
				ImportRays(Default, Rays);
				Out->SetField(TEXT("def"), RaysToJson(Rays));
				Out->SetNumberField(TEXT("maxItems"), FGinkgoCameraShoulderSettings::MaxRays);
			}
			else if (Row.Property->IsA<FBoolProperty>())
			{
				Out->SetBoolField(TEXT("def"), Default == TEXT("True"));
			}
			else if (const UEnum* Enum = FindEnum(*Row.Property))
			{
				Out->SetStringField(TEXT("def"), Default);
				TArray<TSharedPtr<FJsonValue>> Options;
				TArray<TSharedPtr<FJsonValue>> OptionNames;
				for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
				{
					Options.Add(MakeShared<FJsonValueString>(Enum->GetNameStringByIndex(Index)));
					OptionNames.Add(MakeShared<FJsonValueString>(Enum->GetDisplayNameTextByIndex(Index).ToString()));
				}
				Out->SetArrayField(TEXT("options"), Options);
				Out->SetArrayField(TEXT("optionNames"), OptionNames);
			}
			else
			{
				Out->SetNumberField(TEXT("def"), FCString::Atod(*Default));
			}
#if WITH_METADATA
			for (const TCHAR* Key : { TEXT("ClampMin"), TEXT("ClampMax"), TEXT("UIMin"), TEXT("UIMax") })
			{
				if (Row.Property->HasMetaData(Key))
				{
					Out->SetNumberField(FString(Key).ToLower(), FCString::Atod(*Row.Property->GetMetaData(Key)));
				}
			}
			if (Row.Property->HasMetaData(TEXT("Units")))
			{
				Out->SetStringField(TEXT("units"), Row.Property->GetMetaData(TEXT("Units")));
			}
			Out->SetStringField(TEXT("tooltip"), Row.Property->GetMetaData(TEXT("ToolTip")));
#endif
			return Out;
		}

		/** 移动的数：描述区的预览按它们画（滑铲的速度曲线与跑速、冲刺速度、起跳、胶囊的大小） */
		TSharedRef<FJsonObject> DescribeContext() const
		{
			const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
			const APlayerController* PlayerController = Controller.Get();
			const ACharacter* Character = PlayerController ? Cast<ACharacter>(PlayerController->GetPawn()) : nullptr;
			if (const UGinkgoCharacterMovementComponent* Movement = Character ? Cast<UGinkgoCharacterMovementComponent>(Character->GetCharacterMovement()) : nullptr)
			{
				const UGinkgoMovementConfig& Move = Movement->GetConfig();
				Out->SetNumberField(TEXT("runSpeed"), Move.RunSpeed);
				Out->SetNumberField(TEXT("sprintSpeed"), Move.SprintSpeed);
				Out->SetNumberField(TEXT("crouchSpeed"), Move.CrouchSpeed);
				Out->SetNumberField(TEXT("slideStartSpeed"), Move.SlideStartSpeed);
				Out->SetNumberField(TEXT("slideBoost"), Move.SlideBoost);
				Out->SetNumberField(TEXT("slideBoostCap"), Move.SlideBoostCap);
				Out->SetNumberField(TEXT("slideDecel"), Move.SlideDecel);
				Out->SetNumberField(TEXT("slideDecay"), Move.SlideDecay);
				Out->SetNumberField(TEXT("slideEndSpeed"), Move.SlideEndSpeed);
				Out->SetNumberField(TEXT("jumpZVelocity"), Movement->JumpZVelocity);
				Out->SetNumberField(TEXT("gravity"), -Movement->GetGravityZ());
			}
			if (Character && Character->GetCapsuleComponent())
			{
				Out->SetNumberField(TEXT("capsuleRadius"), Character->GetCapsuleComponent()->GetScaledCapsuleRadius());
				Out->SetNumberField(TEXT("capsuleHalfHeight"), Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			}
			return Out;
		}

		void SendValues(const TArray<FString>& Paths, const FString& Status) const
		{
			const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FString& Path : Paths)
			{
				Values.Add(MakeShared<FJsonValueObject>(RowValue(Path)));
			}
			Payload->SetArrayField(TEXT("rows"), Values);
			WriteSummary(*Payload, Status);
			Emit(TEXT("values"), Payload);
		}

		/** 面板打开期间强持有的页面，Hide 解除消息订阅后释放。 */
		TStrongObjectPtr<UWeftView> View;

		/** 挂入游戏视口的顶层 Slate 容器，叠放底层高斯模糊与调参页面；Hide 时从视口移除并释放。 */
		TSharedPtr<SOverlay> RootWidget;

		/** 调参页面的可交互 Slate 容器，保持键盘焦点归属该视口控件；Hide 时随顶层容器一起释放。 */
		TSharedPtr<SWeftViewport> Widget;

		/** 挂载容器和查询尺寸的游戏视口，不持有跨关卡强引用。 */
		TWeakObjectPtr<UGameViewportClient> ViewportClient;

		/** 打开面板的本地控制器，只有其所属 World 的更新能推进页面加载。 */
		TWeakObjectPtr<APlayerController> Controller;

		/** 打开面板前的 Slate 焦点；弱引用避免已关闭的工作台/设置页被保活，Hide 后清空。 */
		TWeakPtr<SWidget> PreviousFocus;

		/** 打开面板前是否显示光标；关闭时决定返回已有 UI 还是纯游戏输入。 */
		bool bPreviousCursor = false;

		/** 当前玩家相机使用的可编辑配置；失效时关闭面板，不引用其它 World 的配置。 */
		TWeakObjectPtr<UGinkgoCameraConfig> Config;

		/** 打开时反射收集的调参行，供页面目录和修改路径校验使用。 */
		TArray<FTunable> Rows;

		/** 当前本地文件已保存的路径到属性文本映射，用于修改计数与保存提示。 */
		TMap<FString, FString> Saved;

		/** 页面是否在有效视口尺寸下加载完成；Hide 清零，下次打开重新加载。 */
		bool bLoaded = false;

		/** 页面要关面板：下一帧世界 tick 时关（见 OnMessage 的 close） */
		bool bHideRequested = false;

		/** 当前页面的消息订阅，Hide 在释放 View 前移除。 */
		FDelegateHandle MessageHandle;

		/** 只在面板打开期间订阅全局 World 更新；非拥有者 World 直接忽略。 */
		FDelegateHandle PostActorTickHandle;

		/** 所属 World 清理时关闭面板；其它 World 清理不影响本面板。 */
		FDelegateHandle WorldCleanupHandle;
	};
	FCameraTuningPanel Panel;

	/** 游戏的世界（-game 的，或编辑器里正在跑的 PIE）；编辑器没在玩时为空 */
	UWorld* GetGameWorld()
	{
		UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
		return World && World->IsGameWorld() ? World : nullptr;
	}

	/** F7：在 Slate 分发之前看按键，游戏里（含 PIE）开关面板；编辑器没在玩时不吃掉这个键 */
	class FTuningHotkey : public IInputProcessor
	{
	public:
		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			UWorld* World = InKeyEvent.GetKey() == ToggleKey && !InKeyEvent.IsRepeat() ? GetGameWorld() : nullptr;
			if (!World)
			{
				return false;
			}
			Panel.Toggle(World);
			return true;
		}

		virtual const TCHAR* GetDebugName() const override { return TEXT("GinkgoCameraTuning"); }
	};
	TSharedPtr<FTuningHotkey> Hotkey;
	FDelegateHandle PostEngineInitHandle;

	void RegisterHotkey()
	{
		if (FSlateApplication::IsInitialized() && !Hotkey)
		{
			Hotkey = MakeShared<FTuningHotkey>();
			FSlateApplication::Get().RegisterInputPreProcessor(Hotkey);
		}
	}

	/** 测试输入：按世界时间排好，到了就交给面板（同一个时刻的按排进来的先后） */
	struct FScheduledInput
	{
		double At = 0.0;
		TArray<FString> Args;
	};
	TArray<FScheduledInput> ScheduledInputs;
	FTSTicker::FDelegateHandle InputTickerHandle;

	/** 一条测试输入：toggle 开关面板（测关了再开回到原处），其余交给面板 */
	void RunTestInput(const TArray<FString>& Args, UWorld* World)
	{
		if (!Args.IsEmpty() && Args[0] == TEXT("toggle"))
		{
			UE_LOG(LogGinkgoCamera, Log, TEXT("[Tuning] 测试输入：toggle"));
			Panel.Toggle(World);
			return;
		}
		Panel.Inject(Args);
	}

	bool TickScheduledInputs(float DeltaTime)
	{
		UWorld* World = GetGameWorld();
		while (World && !ScheduledInputs.IsEmpty() && ScheduledInputs[0].At <= World->GetTimeSeconds())
		{
			const TArray<FString> Args = MoveTemp(ScheduledInputs[0].Args);
			ScheduledInputs.RemoveAt(0);
			RunTestInput(Args, World);
		}
		return true;
	}

	FAutoConsoleCommandWithWorldAndArgs CommandTuneInput(
		TEXT("ginkgo.camera.tune.input"),
		TEXT("测试调参面板用：ginkgo.camera.tune.input [at=<世界时间>] click|down|up|move|drag <x> <y> | wheel <x> <y> <dy> | type <文字> | shot <文件> | dump <文件> | toggle | quit。")
		TEXT("直接给页面的指针（逻辑 px）、按键与文字，不经过系统的鼠标与键盘（第一条之后面板不再接真的鼠标）；带 at= 时排到那个世界时间再做（-ExecCmds 一次排好一串）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& InArgs, UWorld* World)
		{
			TArray<FString> Args = InArgs;
			double At = -1.0;
			if (!Args.IsEmpty() && Args[0].StartsWith(TEXT("at=")))
			{
				At = FCString::Atod(*Args[0].RightChop(3));
				Args.RemoveAt(0);
			}
			if (At < 0.0)
			{
				RunTestInput(Args, World);
				return;
			}
			const int32 Index = Algo::UpperBoundBy(ScheduledInputs, At, &FScheduledInput::At);
			ScheduledInputs.Insert({At, MoveTemp(Args)}, Index);
			if (!InputTickerHandle.IsValid())
			{
				InputTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickScheduledInputs));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CommandTune(
		CommandName,
		TEXT("开关相机的调参面板（同 F7）：ginkgo.camera.tune [1|0]。列出相机配置里所有能调的项，改了立即生效；\"保存到本地\"把和默认值不同的项写进 ")
		TEXT("Saved/Ginkgo/CameraTuning.json，之后启动时自动套用（-unattended、-GinkgoNoCameraTuning 时不套用）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() > 0 && Args[0] == TEXT("0"))
			{
				Panel.Hide();
			}
			else if (Args.Num() > 0 && Args[0] == TEXT("1"))
			{
				Panel.Show(World);
			}
			else
			{
				Panel.Toggle(World);
			}
		}));
}

void Ginkgo::Debug::Camera::StartupTuning()
{
	if (FSlateApplication::IsInitialized())
	{
		RegisterHotkey();
	}
	else
	{
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddStatic(&RegisterHotkey);
	}
}

void Ginkgo::Debug::Camera::ShutdownTuning()
{
	Panel.Hide();
	FTSTicker::GetCoreTicker().RemoveTicker(InputTickerHandle);
	InputTickerHandle.Reset();
	ScheduledInputs.Reset();
	FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
	if (Hotkey && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(Hotkey);
	}
	Hotkey.Reset();
	Records.Reset();
}

void Ginkgo::Debug::Camera::OnCameraSeen(AGinkgoPlayerCameraManager& Camera)
{
	UGinkgoCameraConfig& Config = GetMutableConfig(Camera);
	if (FindRecord(Config))
	{
		return;
	}
	FConfigRecord& Record = Records.AddDefaulted_GetRef();
	Record.Config = &Config;
	for (const FTunable& Tunable : CollectTunables(Config))
	{
		FConfigItem Item;
		FString Error;
		if (ResolveConfigPath(Config, Tunable.Path, Item, Error))
		{
			Record.Defaults.Add(Tunable.Path, ExportItem(Item));
		}
	}
	if (!ShouldApplyLocalFile())
	{
		return;
	}
	const TMap<FString, FString> Values = ReadTuningFile(Config.Shoulder.Rays);
	int32 Applied = 0;
	for (const TPair<FString, FString>& Value : Values)
	{
		FConfigItem Item;
		FString Error;
		if (ResolveConfigPath(Config, Value.Key, Item, Error) && ImportItem(Item, Value.Value))
		{
			++Applied;
		}
		else
		{
			UE_LOG(LogGinkgoCamera, Warning, TEXT("[Tuning] 本地的调参文件里 %s = %s 套不上：%s"), *Value.Key, *Value.Value, Error.IsEmpty() ? TEXT("读不懂这个值") : *Error);
		}
	}
	if (!Values.IsEmpty())
	{
		UE_LOG(LogGinkgoCamera, Log, TEXT("[Tuning] 套用了本地的调参文件 %s：%d / %d 项"), *GetTuningFilePath(), Applied, Values.Num());
	}
}
