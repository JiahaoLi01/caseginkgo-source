#include "Import/GinkgoAnimationImportCommandlet.h"

#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/DebugSkelMeshComponent.h"		// 预览用的网格组件：按序列的某个时刻摆姿势
#include "Animation/Skeleton.h"
#include "AnimationBlueprintLibrary.h"		// 同步标记与它的轨道
#include "AnimPose.h"								// 校验锁根时按某个时刻求值姿势
#include "AssetCompilingManager.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"
#include "PreviewScene.h"
#include "TextureResource.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/FunctionLibrary/Range/UEQuery.h"

namespace
{
	// ===== 搬什么 =====

	/** 小白人在 GASP 里的目录，相对 Content；导入后本工程用同一个路径 */
	const TCHAR* const MannequinFolder = TEXT("Characters/UEFN_Mannequin");

	/** 骨架，相对小白人目录 */
	const TCHAR* const SkeletonAsset = TEXT("Meshes/SK_UEFN_Mannequin");

	/**
	 * 网格与它的依赖，相对小白人目录：材质的两张贴图、材质、LOD 设置、物理资产、网格。
	 * 网格还引用一个后处理动画蓝图（里面只有一个停用的 ModifyBone 节点），不搬，导入时清掉
	 */
	const TCHAR* const MeshAssets[] =
	{
		TEXT("Textures/T_UEFN_Mannequin_D"),
		TEXT("Textures/T_UEFN_Mannequin_N"),
		TEXT("Materials/M_UEFN_Mannequin"),
		TEXT("Meshes/Character_LodSettings"),
		TEXT("Rigs/PA_UEFN_Mannequin"),
		TEXT("Meshes/SKM_UEFN_Mannequin"),
	};

	/**
	 * 动画目录里只搬类别正好是 AnimSequence 的包：蒙太奇、混合空间、瞄准偏移、运动匹配的数据库与选择器、重定向器都不是。
	 * 实验状态机目录里那个序列（StrafeOffsetCurveContainer）只是存曲线的容器，属于状态机的逻辑，按目录排除
	 */
	const TCHAR* const SkippedAnimationFolder = TEXT("Animations/ExperimentalStateMachineData");

	/**
	 * GASP 自己的曲线压缩设置（1204 个序列在用，其余用工程默认的 ACL）：编解码器是 CompressedRichCurve，参数都是默认值。
	 * 不搬，引用它的序列改指引擎自带的同一份设置（BaseEngine.ini 里的 CurveCompressionSettingsFallback），编解码器与参数都一样
	 */
	const TCHAR* const SourceCurveCompression = TEXT("/Game/Misc/SandboxAnimCurveCompressionSettings");
	const TCHAR* const RichCurveCompression = TEXT("/Engine/Animation/DefaultAnimCurveCompressionSettings.DefaultAnimCurveCompressionSettings");

	/** 挂载 GASP 的 Content 用的根，只用来按类别挑包 */
	const TCHAR* const AnimationSampleMountRoot = TEXT("/GameAnimationSample/");

	/** 根运动的处理，按动画的第一级子目录 */
	enum class ERootMotionRule : uint8
	{
		/** 移动类：关掉根运动、强制锁根。位移由移动组件算，动画原地播放；放进蒙太奇也不会带着胶囊走 */
		InPlace,

		/** 翻越与交互：保留根运动、强制锁根。按蒙太奇播放时由根运动带着胶囊走（翻越要升高 2.5 m），放进状态机时原地 */
		RootMotion,

		/** 单帧的姿势：原样，根骨骼不动 */
		Unchanged,
	};

	/**
	 * GASP 的序列原来由运动匹配驱动，开着根运动的 1301 个几乎都强制锁根，放进状态机时本来就原地；没锁的那几个
	 * （Avoidance 12 个的根骨骼走 60–100 cm，Walk 1 个、Jump 2 个）会让网格跑出胶囊。锁根都锁在参考姿势：
	 * 很多序列首帧的根骨骼还在动捕的世界坐标里，离原点最远 15.8 m、转了 180°，锁在首帧就离胶囊很远。
	 * 新的 GASP 多出这里没有的目录时，导入在挑包时停下
	 */
	const TMap<FString, ERootMotionRule>& RootMotionRules()
	{
		static const TMap<FString, ERootMotionRule> Rules =
		{
			{ TEXT("Walk"), ERootMotionRule::InPlace },
			{ TEXT("Run"), ERootMotionRule::InPlace },
			{ TEXT("Sprint"), ERootMotionRule::InPlace },
			{ TEXT("Crouch"), ERootMotionRule::InPlace },
			{ TEXT("Idle"), ERootMotionRule::InPlace },
			{ TEXT("Jump"), ERootMotionRule::InPlace },
			{ TEXT("Slide"), ERootMotionRule::InPlace },
			{ TEXT("Avoidance"), ERootMotionRule::InPlace },
			{ TEXT("Traversal"), ERootMotionRule::RootMotion },
			{ TEXT("Interactions"), ERootMotionRule::RootMotion },
			{ TEXT("AimOffset"), ERootMotionRule::Unchanged },
			{ TEXT("Poses"), ERootMotionRule::Unchanged },
		};
		return Rules;
	}

	// ===== 共用 =====

	/** 每处理这么多个包回收一次垃圾：一千多个序列的原始数据一起留在内存里要好几 GB */
	constexpr int32 PackagesPerCollect = 64;

	/** 本工程里的包名，Package 相对 Content */
	FString GamePackageName(const FString& Package)
	{
		return TEXT("/Game/") + Package;
	}

	FString MannequinPackage(const TCHAR* Asset)
	{
		return FString(MannequinFolder) / Asset;
	}

	UObject* LoadMannequinAsset(const TCHAR* Asset)
	{
		const UPackage* Package = LoadPackage(nullptr, *GamePackageName(MannequinPackage(Asset)), LOAD_None);
		return Package ? Package->FindAssetInPackage() : nullptr;
	}

	// ===== 虚拟骨骼 =====

	/**
	 * 骨架上加的虚拟骨骼（Docs/Design/Weapons.md「枪跟着身体」）：拿在手上的枪挂 VB spine_05_ik_hand_gun。
	 * 引擎在每个片段里按源与目标算它（目标相对源的变换），它是 spine_05 的子骨骼：上半身按网格空间的旋转分层混合以后，
	 * ik_hand_gun 还停在层的姿势（站着的）那个高度，虚拟骨骼跟着胸口走
	 */
	struct FVirtualBoneSpec
	{
		const TCHAR* Source;
		const TCHAR* Target;
	};
	const FVirtualBoneSpec VirtualBones[] =
	{
		{ TEXT("spine_05"), TEXT("ik_hand_gun") },
	};

	/** 虚拟骨骼的名字：引擎的前缀加"源_目标"，同编辑器里加的 */
	FName VirtualBoneName(const FVirtualBoneSpec& Spec)
	{
		return FName(*VirtualBoneNameHelpers::AddVirtualBonePrefix(FString::Printf(TEXT("%s_%s"), Spec.Source, Spec.Target)));
	}

	/**
	 * @brief 骨架上补上没有的虚拟骨骼；已经加载的、用这个骨架的网格跟着重建参考骨架（引擎的 HandleVirtualBoneChanges）
	 * @return 补了几根
	 * @note 引擎每加一根就换一个随机的 VirtualBoneGuid，序列存着它、按它决定要不要重新压缩：随机的话每次导入骨架与所有序列都不同，
	 *       导入就不再逐字节相同。补完换成按名字算的确定的 Guid
	 */
	int32 EnsureVirtualBones(USkeleton& Skeleton)
	{
		int32 Added = 0;
		FString Names;
		for (const FVirtualBoneSpec& Spec : VirtualBones)
		{
			const FName Name = VirtualBoneName(Spec);
			Names += Name.ToString();
			const bool bExists = Skeleton.GetVirtualBones().ContainsByPredicate([Name](const FVirtualBone& Bone) { return Bone.VirtualBoneName == Name; });
			if (!bExists && Skeleton.AddNewNamedVirtualBone(Spec.Source, Spec.Target, Name))
			{
				++Added;
			}
		}
		if (Added > 0)
		{
			const FStructProperty* GuidProperty = FindFProperty<FStructProperty>(USkeleton::StaticClass(), TEXT("VirtualBoneGuid"));
			check(GuidProperty);
			*GuidProperty->ContainerPtrToValuePtr<FGuid>(&Skeleton) = FGuid::NewDeterministicGuid(TEXT("CaseGinkgo:") + Names);
		}
		return Added;
	}

	bool SavePackage(UPackage& Package, UObject& Asset);

	/**
	 * @brief -VirtualBones：只给已经导入的骨架与网格补上虚拟骨骼，存这两个包，不重新导入
	 * @note 序列不重存：它们存着旧的 VirtualBoneGuid，加载时引擎发现不同、重新压缩（进 DDC，之后命中）。重新导入时一起换掉
	 */
	bool AddVirtualBonesToExisting()
	{
		// 网格先载进来：补虚拟骨骼时引擎只重建已经加载的网格
		USkeletalMesh* Mesh = Cast<USkeletalMesh>(LoadMannequinAsset(TEXT("Meshes/SKM_UEFN_Mannequin")));
		USkeleton* Skeleton = Cast<USkeleton>(LoadMannequinAsset(SkeletonAsset));
		if (!Skeleton || !Mesh || Mesh->GetSkeleton() != Skeleton)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[Import] 加载不了骨架 %s 或网格，或网格用的不是这个骨架"), SkeletonAsset);
			return false;
		}
		const int32 Added = EnsureVirtualBones(*Skeleton);
		if (Added == 0)
		{
			UE_LOG(LogGinkgoAnimation, Display, TEXT("[Import] 骨架上已经有虚拟骨骼了，不改"));
			return true;
		}
		const bool bSaved = SavePackage(*Skeleton->GetPackage(), *Skeleton) && SavePackage(*Mesh->GetPackage(), *Mesh);
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Import] 骨架补了 %d 根虚拟骨骼，网格的参考骨架 %d 根（含虚拟骨骼）%s"), Added, Mesh->GetRefSkeleton().GetNum(),
			bSaved ? TEXT("，两个包都存了") : TEXT("，存不了"));
		return bSaved;
	}

	/** 包在动画目录下的第一级子目录，如 Run、Traversal；Package 相对 Content。不在动画目录里时为空 */
	FString AnimationFolder(const FString& Package)
	{
		const FString AnimationPrefix = MannequinPackage(TEXT("Animations/"));
		FString Folder;
		if (Package.StartsWith(AnimationPrefix))
		{
			Package.RightChop(AnimationPrefix.Len()).Split(TEXT("/"), &Folder, nullptr);
		}
		return Folder;
	}

	FString AnimationFolder(const UObject& Asset)
	{
		return AnimationFolder(Asset.GetPackage()->GetName().RightChop(GamePackageName(FString()).Len()));
	}

	/**
	 * @class FWarningCapture : 收集一段时间里的警告与错误
	 * 构造时挂到 GLog 上，析构时摘下。日志会来自加载线程与压缩任务，加锁收集；读之前先 Flush，让缓冲着的日志都到齐
	 */
	class FWarningCapture final : public FOutputDevice
	{
	public:
		FWarningCapture()
		{
			GLog->AddOutputDevice(this);
		}

		virtual ~FWarningCapture() override
		{
			GLog->RemoveOutputDevice(this);
		}

		virtual bool CanBeUsedOnAnyThread() const override
		{
			return true;
		}

		virtual void Serialize(const TCHAR* Message, const ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			const ELogVerbosity::Type Level = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
			if (Level != ELogVerbosity::NoLogging && Level <= ELogVerbosity::Warning)
			{
				FScopeLock Lock(&Mutex);
				++Messages.FindOrAdd(FString::Printf(TEXT("%s: %s"), *Category.ToString(), Message));
			}
		}

		/** 收集到的消息与各自出现的次数 */
		TMap<FString, int32> Take()
		{
			GLog->Flush();
			FScopeLock Lock(&Mutex);
			return MoveTemp(Messages);
		}

	private:
		FCriticalSection Mutex;

		TMap<FString, int32> Messages;
	};

	/** 打出次数最多的几条消息 */
	void LogTopMessages(const TCHAR* Prefix, const TMap<FString, int32>& Messages, const int32 Limit)
	{
		using namespace UE::Query;

		const int32 Total = Messages | Values() | Fold(0, [](const int32 Sum, const int32 Count) { return Sum + Count; });
		UE_LOG(LogGinkgoAnimation, Display, TEXT("%s 警告与错误 %d 条（%d 种）"), Prefix, Total, Messages.Num());
		Messages
			| OrderByDescending([](const TPair<FString, int32>& Message) { return Message.Value; })
			| Take(Limit)
			| ForEach([Prefix](const TPair<FString, int32>& Message)
			{
				UE_LOG(LogGinkgoAnimation, Display, TEXT("%s   %5d × %s"), Prefix, Message.Value, *Message.Key);
			});
	}

	// ===== 导入 =====

	struct FSelection
	{
		/** 相对 Content 的包路径：序列在前（按名字排序），其次是网格的依赖，骨架最后 */
		TArray<FString> Packages;

		/** 在 GASP 里用 GASP 自己的曲线压缩设置的序列 */
		TSet<FString> RichCurveSequences;
	};

	/**
	 * @brief 挂载 GASP 的 Content，按类别挑出要搬的包
	 * @param SourceContentDir - GASP 的 Content 目录（绝对路径）
	 * @param OutSelection - 挑出的包；哪些序列用 GASP 自己的曲线压缩设置从资产注册表记下的依赖里读
	 * @return 挂载失败、一个序列也没找到、有动画目录没有根运动的规则、或网格的依赖缺了时返回 false
	 */
	bool SelectPackages(const FString& SourceContentDir, FSelection& OutSelection)
	{
		using namespace UE::Query;

		if (!IFileManager::Get().DirectoryExists(*SourceContentDir))
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[Import] 找不到 %s"), *SourceContentDir);
			return false;
		}

		const TRefCountPtr<UE::PackageName::IMountPoint> MountPoint = FPackageName::RegisterMountPoint(AnimationSampleMountRoot, SourceContentDir);
		if (!MountPoint.IsValid())
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[Import] 挂载不了 %s"), *SourceContentDir);
			return false;
		}
		ON_SCOPE_EXIT
		{
			FPackageName::UnRegisterMountPoint(MountPoint);
		};

		IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
		const FString SourceMannequin = AnimationSampleMountRoot + FString(MannequinFolder);
		AssetRegistry.ScanPathsSynchronous({ SourceMannequin }, /*bForceRescan*/ true);

		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPath(FName(SourceMannequin), Assets, /*bRecursive*/ true);

		const int32 RootLength = FCString::Strlen(AnimationSampleMountRoot);
		const FTopLevelAssetPath SequenceClass = UAnimSequence::StaticClass()->GetClassPathName();
		const FString SkippedPath = SourceMannequin / SkippedAnimationFolder;
		TArray<FString>& Packages = OutSelection.Packages;
		Packages = Assets
			| Where([&](const FAssetData& Asset)
			{
				return Asset.AssetClassPath == SequenceClass && !Asset.PackagePath.ToString().StartsWith(SkippedPath);
			})
			| Select([RootLength](const FAssetData& Asset) { return Asset.PackageName.ToString().RightChop(RootLength); })
			| ToArray();
		Packages.Sort();
		if (Packages.IsEmpty())
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[Import] %s 里没有动画序列"), *SourceMannequin);
			return false;
		}

		const FName SourceCurveCompressionName(SourceCurveCompression);
		OutSelection.RichCurveSequences = Packages
			| Where([&](const FString& Package)
			{
				TArray<FName> Dependencies;
				AssetRegistry.GetDependencies(FName(AnimationSampleMountRoot + Package), Dependencies);
				return Dependencies.Contains(SourceCurveCompressionName);
			})
			| ToSet();

		// 每个动画目录都要有根运动的规则：新的 GASP 多出目录时在这里停下，不按猜的处理
		const TSet<FString> FoldersWithoutRule = Packages
			| Select([](const FString& Package) { return AnimationFolder(Package); })
			| Where([](const FString& Folder) { return !RootMotionRules().Contains(Folder); })
			| ToSet();
		for (const FString& Folder : FoldersWithoutRule)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[Import] 动画目录 \"%s\" 没有根运动的规则（RootMotionRules）"), *Folder);
		}
		if (!FoldersWithoutRule.IsEmpty())
		{
			return false;
		}

		// 网格的依赖照名字找：GASP 换了版本、挪了目录时在这里停下，不复制半套
		const int32 SequenceCount = Packages.Num();
		for (const TCHAR* Asset : MeshAssets)
		{
			Packages.Add(MannequinPackage(Asset));
		}
		Packages.Add(MannequinPackage(SkeletonAsset));

		const TSet<FName> SourcePackages = Assets | Select([](const FAssetData& Asset) { return Asset.PackageName; }) | ToSet();
		const TArray<FString> Missing = Packages
			| Drop(SequenceCount)
			| Where([&](const FString& Package) { return !SourcePackages.Contains(FName(AnimationSampleMountRoot + Package)); })
			| ToArray();
		for (const FString& Package : Missing)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[Import] GASP 里没有 %s"), *Package);
		}
		return Missing.IsEmpty();
	}

	const TCHAR* RootMotionRuleName(const ERootMotionRule Rule)
	{
		switch (Rule)
		{
		case ERootMotionRule::InPlace:
			return TEXT("原地（关掉根运动、强制锁根）");
		case ERootMotionRule::RootMotion:
			return TEXT("保留根运动（强制锁根）");
		case ERootMotionRule::Unchanged:
			break;
		}
		return TEXT("原样");
	}

	/** 按动画的子目录数一数挑出的序列与各自的根运动处理，再列出网格与它的依赖 */
	void LogSelection(const FSelection& Selection)
	{
		TMap<FString, int32> SequencesByFolder;
		for (const FString& Package : Selection.Packages)
		{
			const FString Folder = AnimationFolder(Package);
			if (Folder.IsEmpty())
			{
				UE_LOG(LogGinkgoAnimation, Display, TEXT("[Import] %s"), *Package);
			}
			else
			{
				++SequencesByFolder.FindOrAdd(Folder);
			}
		}
		SequencesByFolder.KeySort(TLess<FString>());
		for (const TPair<FString, int32>& Folder : SequencesByFolder)
		{
			UE_LOG(LogGinkgoAnimation, Display, TEXT("[Import] Animations/%-12s %4d 个序列，根运动：%s"),
				*Folder.Key, Folder.Value, RootMotionRuleName(RootMotionRules().FindChecked(Folder.Key)));
		}
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Import] 一共 %d 个包；%d 个序列在 GASP 里用它自己的曲线压缩设置，导入后改指引擎自带的同一份"),
			Selection.Packages.Num(), Selection.RichCurveSequences.Num());
	}

	/**
	 * @brief 把挑出的包原样复制到本工程 Content 的同一相对路径
	 * @note 已有的文件直接覆盖：重跑就是重新导入，导入后在编辑器里对这些资产的修改会被冲掉
	 */
	bool CopyPackages(const FString& SourceContentDir, const TArray<FString>& Packages)
	{
		IFileManager& FileManager = IFileManager::Get();
		const FString ProjectContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
		const FString Extension = FPackageName::GetAssetPackageExtension();

		int32 Overwritten = 0;
		int64 Bytes = 0;
		for (const FString& Package : Packages)
		{
			const FString From = SourceContentDir / Package + Extension;
			const FString To = ProjectContentDir / Package + Extension;
			Overwritten += FileManager.FileExists(*To) ? 1 : 0;
			FileManager.MakeDirectory(*FPaths::GetPath(To), /*Tree*/ true);
			if (FileManager.Copy(*To, *From) != COPY_OK)
			{
				UE_LOG(LogGinkgoAnimation, Error, TEXT("[Import] 复制失败：%s -> %s"), *From, *To);
				return false;
			}
			Bytes += FileManager.FileSize(*To);
		}
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Import] 复制了 %d 个包（%.1f MB），覆盖了已有的 %d 个"), Packages.Num(), Bytes / 1048576.0, Overwritten);
		return true;
	}

	struct FImportStats
	{
		int32 Sequences = 0;

		int32 RemovedNotifies = 0;

		int32 KeptSyncMarkers = 0;

		/** 生成了距离曲线的起步、急停、反向、落地（EDistanceClip 的顺序，第 0 个不用） */
		int32 DistanceCurves[5] = {};

		/** 加了跳跃标记的起跳、走下边缘、落地（EJumpClip 的顺序，第 0 个不用） */
		int32 JumpMarkers[4] = {};

		/** 跳跃片段里找不到离地、到顶或着地的 */
		TArray<FString> JumpsWithoutEvents;

		/** 去掉了骨盆偏转的倾斜姿势、其中改成网格空间叠加的，与去掉的偏转最大多少（°） */
		int32 LeanPosesUntwisted = 0;

		int32 LeanPosesMadeAdditive = 0;

		double MaxLeanTwist = 0.0;

		/** 距离曲线为了不减改过的最大量（cm），与改过的序列个数 */
		double MaxMonotonicFix = 0.0;

		int32 MonotonicFixed = 0;

		/** 急停的序列里到末帧还没停下来的（停止点只好取末帧） */
		TArray<FString> StopsStillMoving;

		/** 生成了剩余转角的原地转身 */
		int32 TurnCurves = 0;

		/** 按接触曲线加了同步标记的序列与标记个数 */
		int32 SyncMarkerSequences = 0;

		int32 SyncMarkersAdded = 0;

		/** 生成了预测落脚的曲线的序列，与其中两只脚一共落下几次 */
		int32 FootstepSequences = 0;

		int32 FootstepLandings = 0;

		int32 RichCurveSequences = 0;

		/** 按根运动的规则（ERootMotionRule 的顺序）各处理了几个序列 */
		int32 ByRootMotionRule[3] = {};

		/** 原来开着、这次关掉根运动的序列 */
		int32 RootMotionTurnedOff = 0;

		/** 原来没有、这次补上强制锁根的序列 */
		int32 RootLockAdded = 0;

		/** 删掉的通知按名字计数（通知的类不在本工程时对象是空的，名字还在） */
		TMap<FName, int32> RemovedNotifyNames;
	};

	/**
	 * @brief 删掉序列上的全部通知，同步标记留着
	 * 同步标记不是通知：脚步的 L / R 标记给同步组对齐步伐用，是动画本身的数据。删完以后没有同步标记的轨道一起去掉，
	 * 留下的轨道重新编号；一条都不剩时留一条空轨道（通知面板至少要有一条）。
	 * @note 通知的类多半不在本工程（GASP 的 Foley 蓝图，没开的 PoseSearch 与 MotionWarping 插件），加载时这些通知的对象已经是空的，
	 *       这里删的是事件本身
	 */
	void StripNotifies(UAnimSequence& Sequence, FImportStats& Stats)
	{
		using namespace UE::Query;

		for (const FAnimNotifyEvent& Notify : Sequence.Notifies)
		{
			++Stats.RemovedNotifyNames.FindOrAdd(Notify.NotifyName);
		}
		Stats.RemovedNotifies += Sequence.Notifies.Num();
		Stats.KeptSyncMarkers += Sequence.AuthoredSyncMarkers.Num();
		Sequence.Notifies.Empty();

		TArray<FAnimNotifyTrack> KeptTracks;
		TArray<int32> NewTrackIndices;
		NewTrackIndices.Init(INDEX_NONE, Sequence.AnimNotifyTracks.Num());
		for (int32 TrackIndex = 0; TrackIndex < Sequence.AnimNotifyTracks.Num(); ++TrackIndex)
		{
			if (Sequence.AuthoredSyncMarkers | Any([TrackIndex](const FAnimSyncMarker& Marker) { return Marker.TrackIndex == TrackIndex; }))
			{
				NewTrackIndices[TrackIndex] = KeptTracks.Add(Sequence.AnimNotifyTracks[TrackIndex]);
			}
		}
		for (FAnimSyncMarker& Marker : Sequence.AuthoredSyncMarkers)
		{
			Marker.TrackIndex = NewTrackIndices.IsValidIndex(Marker.TrackIndex) ? NewTrackIndices[Marker.TrackIndex] : 0;
		}
		Sequence.AnimNotifyTracks = MoveTemp(KeptTracks);
		Sequence.InitializeNotifyTrack();
		Sequence.RefreshCacheData();
	}

	/**
	 * @brief 按目录的规则设根运动：移动类关掉、翻越与交互保留，两者都强制锁根、锁在参考姿势
	 * 强制锁根让序列不管动画实例从不从它身上抽根运动，根骨骼都在参考姿势的位置：动画实例默认只从蒙太奇抽，
	 * 只关掉根运动而不锁根，根骨骼的位移就留在姿势里
	 */
	void ApplyRootMotionRule(UAnimSequence& Sequence, const ERootMotionRule Rule, FImportStats& Stats)
	{
		++Stats.ByRootMotionRule[static_cast<int32>(Rule)];
		if (Rule == ERootMotionRule::Unchanged)
		{
			return;
		}

		const bool bEnableRootMotion = Rule == ERootMotionRule::RootMotion;
		Stats.RootMotionTurnedOff += Sequence.bEnableRootMotion && !bEnableRootMotion ? 1 : 0;
		Stats.RootLockAdded += Sequence.bForceRootLock ? 0 : 1;
		Sequence.bEnableRootMotion = bEnableRootMotion;
		Sequence.bForceRootLock = true;
		Sequence.RootMotionRootLock = ERootMotionRootLock::RefPose;
	}

	// ===== 生成的曲线与同步标记 =====

	/**
	 * 动画蓝图要、GASP 却没有的数据，从原始的根骨骼轨道与接触曲线算出来（Docs/Design/AnimationBlueprint.md 的"导入时生成的曲线与标记"）。
	 * 引擎的 DistanceCurveModifier 遇到关了根运动的序列直接拒绝，而移动类序列全关了根运动，所以在这里自己算；同一条命令重做，结果逐字节相同
	 */

	/** 距离曲线：引擎 AnimDistanceMatchingLibrary 默认的曲线名 */
	const FName DistanceCurveName(TEXT("Distance"));

	/** 原地转身：还剩多少偏航要转（°，向右为正）与它的权重。照 Lyra 的约定，剩余转角除以权重才是这一帧的值，混合时也成立 */
	const FName RemainingTurnYawCurveName(TEXT("RemainingTurnYaw"));
	const FName TurnYawWeightCurveName(TEXT("TurnYawWeight"));

	/** 接触曲线与它的上升沿生成的同步标记，标记放在自己的轨道上 */
	const TPair<FName, FName> ContactMarkers[] = { { TEXT("contact_l"), TEXT("L") }, { TEXT("contact_r"), TEXT("R") } };
	const FName SyncMarkerTrackName(TEXT("Sync"));

	/** 接触曲线过这个值算着地 */
	constexpr float ContactThreshold = 0.5f;

	/**
	 * 预测落脚的曲线（Docs/Design/AnimationBlueprint.md 的"脚：预测式的腿部 IK"），每只脚三条：
	 * FootLandIn 是还有多久落下（片段里的时间，s；着地时为 0），FootLandX、FootLandY 是落下那一刻脚掌在根骨骼空间里的位置（cm，
	 * 小白人朝 +Y、+X 是左边）。着地时 X、Y 是这一步落下的位置
	 */
	struct FFootstepCurveNames
	{
		FName Contact;
		FName Ball;
		FName LandIn;
		FName LandX;
		FName LandY;
	};

	const FFootstepCurveNames FootstepCurves[] = {
		{ TEXT("contact_l"), TEXT("ball_l"), TEXT("FootLandIn_l"), TEXT("FootLandX_l"), TEXT("FootLandY_l") },
		{ TEXT("contact_r"), TEXT("ball_r"), TEXT("FootLandIn_r"), TEXT("FootLandX_r"), TEXT("FootLandY_r") },
	};

	/**
	 * 接触曲线高过 FootPlantOn 才算着地、低过 FootPlantOff 才算离地（腿部 IK 节点用同样的回差）：
	 * GASP 的曲线偶尔在着地中间掉到 0.41–0.45（1909 次摆动里 12 次短于 50 ms），过 0.5 一刀切会多出一步
	 */
	constexpr float FootPlantOn = 0.6f;
	constexpr float FootPlantOff = 0.4f;

	/** 急停的停止点：根骨骼此后的速度一直低于它（cm/s），与引擎 DistanceCurveModifier 的停止速度相同 */
	constexpr double StopSpeedThreshold = 5.0;

	/** 认反向的起始方向用开头几帧的位移 */
	constexpr int32 PivotDirectionFrames = 3;

	/** 要距离曲线的序列 */
	enum class EDistanceClip : uint8
	{
		None,
		/** 从静止起步（滑铲起身接着走的也是）：距离是离起点多远，正值 */
		Start,
		/** 急停：停下之前是离停止点多远，负值 */
		Stop,
		/** 反向：折返点之前是离折返点多远（负值），之后是离开它多远（正值） */
		Pivot,
		/** 落地：着地之前是离着地点多远（负值），之后是离开它多远（正值）。着地以后在动的落地按走过的距离推进 */
		Land,
	};

	/** 跳跃的标记：动画实例按名字找时刻（Docs/Design/AnimationBlueprint.md 的"空中"），放在自己的轨道上 */
	const FName TakeoffMarkerName(TEXT("Takeoff"));
	const FName ApexMarkerName(TEXT("Apex"));
	const FName ImpactMarkerName(TEXT("Impact"));
	const FName JumpMarkerTrackName(TEXT("Jump"));

	/** 根骨骼高过（低过）这么多算离地、着地（cm）：GASP 的根在跳跃里走一条合成的弧线 */
	constexpr double JumpLiftThreshold = 1.0;

	/** 要跳跃标记的序列：起跳加 Takeoff（根开始升高）与 Apex（最高处），走下边缘加 Takeoff（根开始下落），落地加 Impact（根落回地面） */
	enum class EJumpClip : uint8
	{
		None,
		Start,
		Off,
		Land,
	};

	EJumpClip ClassifyJumpClip(const FString& Folder, const FString& Name)
	{
		if (Folder != TEXT("Jump"))
		{
			return EJumpClip::None;
		}
		if (Name.Contains(TEXT("_Start_")))
		{
			return EJumpClip::Start;
		}
		if (Name.Contains(TEXT("_Off_")))
		{
			return EJumpClip::Off;
		}
		return Name.Contains(TEXT("_Land_")) ? EJumpClip::Land : EJumpClip::None;
	}

	/**
	 * GASP 的左右倾斜姿势与各自的基准（Poses 里，都是静止的，每帧一样）。动画蓝图的倾斜节点把它们按网格空间叠加，
	 * 见 Docs/Design/AnimationBlueprint.md 的"倾斜"：
	 * - 跑那套本来就是以 Pose_Base 为基准的网格空间叠加；冲刺那套（GASP 自己没用上）存的是整个姿势，改成同样的叠加；
	 * - 骨盆相对基准的偏转去掉，只留侧倾。冲刺那套整个身体往弯里偏转了 19°（跑那套 3°），叠到循环上时腿不再对着速度，
	 *   着地的脚向后蹬就成了斜着滑（sprintfacing 路线冲刺中转弯，着地的脚横着滑 213 cm/s）。上半身与头往弯里扭的那部分照留
	 */
	struct FLeanPose
	{
		const TCHAR* Name;
		const TCHAR* Base;
		/** 存的是整个姿势，要改成叠加 */
		bool bMakeAdditive;
	};

	const FLeanPose LeanPoses[] = {
		{ TEXT("M_Neutral_Run_Lean_Pose_Left"), TEXT("M_Neutral_Run_Lean_Pose_Base"), false },
		{ TEXT("M_Neutral_Run_Lean_Pose_Right"), TEXT("M_Neutral_Run_Lean_Pose_Base"), false },
		{ TEXT("M_Neutral_Sprint_Pose_Lean_L_hold"), TEXT("M_Neutral_Sprint_Pose_Lean_base"), true },
		{ TEXT("M_Neutral_Sprint_Pose_Lean_R_hold"), TEXT("M_Neutral_Sprint_Pose_Lean_base"), true },
	};

	const FName LeanTwistBone(TEXT("pelvis"));

	/** 偏转去掉以后，骨盆相对基准最多还剩多少（°）：-Verify 查 */
	constexpr double LeanMaxTwist = 0.5;

	const FLeanPose* FindLeanPose(const FString& Folder, const FString& Name)
	{
		if (Folder != TEXT("Poses"))
		{
			return nullptr;
		}
		for (const FLeanPose& Pose : LeanPoses)
		{
			if (Name == Pose.Name)
			{
				return &Pose;
			}
		}
		return nullptr;
	}

	/** 倾斜姿势的骨盆相对基准首帧的偏转（绕组件空间的 Z 轴，°）：把两者之差分成绕 Z 的扭转与其余的摆动，取扭转 */
	FQuat LeanTwist(const FQuat& Pelvis, const FQuat& BasePelvis)
	{
		FQuat Swing, Twist;
		(Pelvis * BasePelvis.Inverse()).ToSwingTwist(FVector::ZAxisVector, Swing, Twist);
		return Twist;
	}

	/**
	 * 按目录与名字认：走、跑、冲刺、蹲四个目录里名字带 _Start_、_Stop_、_Pivot_ 的（转身起跑 Reface_Start 也是起步）；
	 * 滑铲目录里起身接着走的（_Out_Moving_）按起步算：起身时按走过的距离推进，冲刺速度下站起来脚不滑
	 */
	EDistanceClip ClassifyDistanceClip(const FString& Folder, const FString& Name)
	{
		if (Folder == TEXT("Slide"))
		{
			return Name.Contains(TEXT("_Out_Moving_")) ? EDistanceClip::Start : EDistanceClip::None;
		}
		if (Folder == TEXT("Jump"))
		{
			return ClassifyJumpClip(Folder, Name) == EJumpClip::Land ? EDistanceClip::Land : EDistanceClip::None;
		}
		static const TSet<FString> LocomotionFolders = { TEXT("Walk"), TEXT("Run"), TEXT("Sprint"), TEXT("Crouch") };
		if (!LocomotionFolders.Contains(Folder))
		{
			return EDistanceClip::None;
		}
		if (Name.Contains(TEXT("_Start_")))
		{
			return EDistanceClip::Start;
		}
		if (Name.Contains(TEXT("_Stop_")))
		{
			return EDistanceClip::Stop;
		}
		return Name.Contains(TEXT("_Pivot_")) ? EDistanceClip::Pivot : EDistanceClip::None;
	}

	/** 原地转身：站着的在 Idle 里（M_*_Stand_Turn_*），蹲着的在 Crouch 里（M_Neutral_Crouch_Idle_Turn_*）。跑动中的转弯（*_Run_Turn_*）不是 */
	bool IsTurnInPlace(const FString& Folder, const FString& Name)
	{
		return (Folder == TEXT("Idle") && Name.Contains(TEXT("_Stand_Turn_"))) || (Folder == TEXT("Crouch") && Name.Contains(TEXT("_Crouch_Idle_Turn_")));
	}

	/**
	 * 每一帧一个键，默认线性：距离匹配按键的值二分查找、在键之间线性插值，三次插值会在键之间冲过头。
	 * 一段一段不变、到下一段一下子跳过去的值（落脚的位置）用常值的键
	 */
	void WriteCurve(UAnimSequence& Sequence, const FName CurveName, const TArray<float>& Times, const TArray<float>& Values,
		const ERichCurveInterpMode InterpMode = RCIM_Linear)
	{
		TArray<FRichCurveKey> Keys;
		Keys.Reserve(Times.Num());
		for (int32 Index = 0; Index < Times.Num(); ++Index)
		{
			FRichCurveKey& Key = Keys.Emplace_GetRef(Times[Index], Values[Index]);
			Key.InterpMode = InterpMode;
		}
		IAnimationDataController& Controller = Sequence.GetController();
		const FAnimationCurveIdentifier CurveId(CurveName, ERawCurveTrackTypes::RCT_Float);
		IAnimationDataController::FScopedBracket Bracket(Controller, FText::FromString(TEXT("Ginkgo generated curve")), /*bShouldTransact*/ false);
		if (!Sequence.GetDataModel()->FindFloatCurve(CurveId))
		{
			Controller.AddCurve(CurveId, AACF_DefaultCurve, /*bShouldTransact*/ false);
			// 新曲线的编辑器颜色按 FName 的哈希取，哈希是名字表里的序号，每个进程不同，导入就不再逐字节相同了：按名字的 CRC 给定
			const uint8 Hue = static_cast<uint8>(FCrc::StrCrc32(*CurveName.ToString()) & 0xFF);
			Controller.SetCurveColor(CurveId, FLinearColor::MakeFromHSV8(Hue, 196, 196), /*bShouldTransact*/ false);
		}
		Controller.SetCurveKeys(CurveId, Keys, /*bShouldTransact*/ false);
	}

	/** 每一帧的时刻 */
	TArray<float> KeyTimes(const IAnimationDataModel& Model, const int32 NumKeys)
	{
		TArray<float> Times;
		Times.Reserve(NumKeys);
		for (int32 Index = 0; Index < NumKeys; ++Index)
		{
			Times.Add(static_cast<float>(Model.GetFrameRate().AsSeconds(Index)));
		}
		return Times;
	}

	/** 跳跃片段里根骨骼弧线上的时刻（秒），没有时为负 */
	struct FJumpEvents
	{
		/** 起跳：根第一次高过首帧 JumpLiftThreshold；走下边缘：根第一次低过首帧 JumpLiftThreshold */
		float Takeoff = -1.f;
		/** 起跳：根最高处，取最高的那一帧与左右两帧的抛物线顶点 */
		float Apex = -1.f;
		/** 落地：过了最高处以后，根第一次落回末帧的 JumpLiftThreshold 以内 */
		float Impact = -1.f;
		/** 着地之后的第一帧：落地片段的距离曲线以它为参照点 */
		int32 ImpactFrame = INDEX_NONE;
	};

	/** 越过的时刻都在两帧之间线性插值 */
	FJumpEvents FindJumpEvents(const IAnimationDataModel& Model, const EJumpClip Kind)
	{
		FJumpEvents Events;
		TArray<FTransform> Root;
		Model.GetBoneTrackTransforms(TEXT("root"), Root);
		const int32 NumKeys = Root.Num();
		if (Kind == EJumpClip::None || NumKeys < 3)
		{
			return Events;
		}
		const TArray<float> Times = KeyTimes(Model, NumKeys);
		TArray<double> Height;
		for (const FTransform& Key : Root)
		{
			Height.Add(Key.GetLocation().Z);
		}
		int32 Top = 0;
		for (int32 Index = 1; Index < NumKeys; ++Index)
		{
			Top = Height[Index] > Height[Top] ? Index : Top;
		}
		// 从 From 往后第一次越过 Level（往上或往下）的帧与时刻
		const auto Crossing = [&Height, &Times, NumKeys](const int32 From, const double Level, const bool bUpward, int32& OutFrame)
		{
			for (int32 Index = FMath::Max(From, 1); Index < NumKeys; ++Index)
			{
				if (bUpward ? Height[Index] > Level : Height[Index] < Level)
				{
					OutFrame = Index;
					const double Alpha = (Level - Height[Index - 1]) / (Height[Index] - Height[Index - 1]);
					return FMath::Lerp(Times[Index - 1], Times[Index], static_cast<float>(FMath::Clamp(Alpha, 0.0, 1.0)));
				}
			}
			OutFrame = INDEX_NONE;
			return -1.f;
		};
		int32 Frame = INDEX_NONE;
		switch (Kind)
		{
		case EJumpClip::Start:
		{
			Events.Takeoff = Crossing(1, Height[0] + JumpLiftThreshold, /*bUpward*/ true, Frame);
			Events.Apex = Times[Top];
			if (Top > 0 && Top < NumKeys - 1)
			{
				const double Curvature = Height[Top - 1] - 2.0 * Height[Top] + Height[Top + 1];
				const double Offset = Curvature < 0.0 ? 0.5 * (Height[Top - 1] - Height[Top + 1]) / Curvature : 0.0;
				Events.Apex = Times[Top] + static_cast<float>(Offset) * (Times[Top + 1] - Times[Top]);
			}
			if (Events.Takeoff >= Events.Apex)
			{
				Events.Takeoff = -1.f;
			}
			break;
		}
		case EJumpClip::Off:
			Events.Takeoff = Crossing(1, Height[0] - JumpLiftThreshold, /*bUpward*/ false, Frame);
			break;
		default:
			Events.Impact = Crossing(Top + 1, Height.Last() + JumpLiftThreshold, /*bUpward*/ false, Events.ImpactFrame);
			break;
		}
		return Events;
	}

	/** 库给每个新标记一个随机的 GUID（FGuid::NewGuid），导入就不再逐字节相同了：所有标记按序列与次序给定 */
	void AssignDeterministicMarkerGuids(UAnimSequence& Sequence)
	{
		for (int32 Index = 0; Index < Sequence.AuthoredSyncMarkers.Num(); ++Index)
		{
			Sequence.AuthoredSyncMarkers[Index].Guid = FGuid::NewDeterministicGuid(FString::Printf(TEXT("%s#%d"), *Sequence.GetPathName(), Index));
		}
	}

	/**
	 * @brief 距离曲线：根骨骼的水平位置离参照点多远，参照点之前取负、之后取正
	 * 参照点：起步是首帧；急停是根骨骼停下来、此后速度一直低于 StopSpeedThreshold 的第一帧；反向是沿开头的方向走得最远的那一帧（折返点）。
	 * 与引擎 DistanceCurveModifier 的约定相同（它取速度最低的时刻），只是不靠速度采样：30 fps 的原始帧里折返那一刻的速度未必低于阈值。
	 * 距离匹配要求值随时间不减（按值二分），所以最后取累计最大值：停下之后根骨骼的小幅晃动不会让值往回走
	 */
	void GenerateDistanceCurve(UAnimSequence& Sequence, const EDistanceClip Kind, FImportStats& Stats)
	{
		const IAnimationDataModel* Model = Sequence.GetDataModel();
		TArray<FTransform> Root;
		Model->GetBoneTrackTransforms(TEXT("root"), Root);
		const int32 NumKeys = Root.Num();
		if (NumKeys < 2)
		{
			return;
		}
		TArray<FVector> Positions;
		for (const FTransform& Key : Root)
		{
			Positions.Add(FVector(Key.GetLocation().X, Key.GetLocation().Y, 0.0));
		}
		const TArray<float> Times = KeyTimes(*Model, NumKeys);

		int32 Reference = 0;
		if (Kind == EDistanceClip::Stop)
		{
			Reference = NumKeys - 1;
			while (Reference > 0 && FVector::Dist(Positions[Reference - 1], Positions[Reference]) / (Times[Reference] - Times[Reference - 1]) < StopSpeedThreshold)
			{
				--Reference;
			}
			if (Reference == NumKeys - 1)
			{
				Stats.StopsStillMoving.Add(Sequence.GetName());
			}
		}
		else if (Kind == EDistanceClip::Land)
		{
			const int32 ImpactFrame = FindJumpEvents(*Model, EJumpClip::Land).ImpactFrame;
			Reference = ImpactFrame != INDEX_NONE ? ImpactFrame : NumKeys - 1;
		}
		else if (Kind == EDistanceClip::Pivot)
		{
			const FVector Direction = (Positions[FMath::Min(PivotDirectionFrames, NumKeys - 1)] - Positions[0]).GetSafeNormal();
			double Farthest = -UE_BIG_NUMBER;
			for (int32 Index = 0; Index < NumKeys; ++Index)
			{
				const double Along = (Positions[Index] - Positions[0]) | Direction;
				if (Along > Farthest)
				{
					Farthest = Along;
					Reference = Index;
				}
			}
		}

		TArray<float> Values;
		double MaxFix = 0.0;
		for (int32 Index = 0; Index < NumKeys; ++Index)
		{
			const double Distance = FVector::Dist(Positions[Index], Positions[Reference]);
			double Value = Index < Reference ? -Distance : Distance;
			if (Index > 0 && Value < Values.Last())
			{
				MaxFix = FMath::Max(MaxFix, Values.Last() - Value);
				Value = Values.Last();
			}
			Values.Add(static_cast<float>(Value));
		}
		WriteCurve(Sequence, DistanceCurveName, Times, Values);
		++Stats.DistanceCurves[static_cast<int32>(Kind)];
		Stats.MonotonicFixed += MaxFix > 0.0 ? 1 : 0;
		Stats.MaxMonotonicFix = FMath::Max(Stats.MaxMonotonicFix, MaxFix);
	}

	/**
	 * @brief 原地转身的剩余转角：根骨骼的偏航还要转多少才到末帧（°，向右为正），权重整段为 1
	 * 序列锁了根，播放时根骨骼不转；动画实例按剩余转角每帧的变化把根骨骼的偏航补偿还回去，网格这才转过去（Lyra 的做法）
	 */
	void GenerateTurnCurves(UAnimSequence& Sequence, FImportStats& Stats)
	{
		const IAnimationDataModel* Model = Sequence.GetDataModel();
		TArray<FTransform> Root;
		Model->GetBoneTrackTransforms(TEXT("root"), Root);
		const int32 NumKeys = Root.Num();
		if (NumKeys < 2)
		{
			return;
		}
		// 偏航逐帧累加，跨过 ±180° 时不跳
		TArray<double> Yaw = { 0.0 };
		for (int32 Index = 1; Index < NumKeys; ++Index)
		{
			Yaw.Add(Yaw.Last() + FMath::FindDeltaAngleDegrees(Root[Index - 1].Rotator().Yaw, Root[Index].Rotator().Yaw));
		}
		const TArray<float> Times = KeyTimes(*Model, NumKeys);
		TArray<float> Remaining;
		for (const double Value : Yaw)
		{
			Remaining.Add(static_cast<float>(Yaw.Last() - Value));
		}
		WriteCurve(Sequence, RemainingTurnYawCurveName, Times, Remaining);
		WriteCurve(Sequence, TurnYawWeightCurveName, { Times[0], Times.Last() }, { 1.f, 1.f });
		++Stats.TurnCurves;
	}

	/**
	 * @brief 按接触曲线的上升沿（过 ContactThreshold）加同步标记 L、R，时刻在两帧之间线性插值
	 * 循环的序列（名字带 _Loop）首帧已经着地、末帧没有时，算作首帧着地。GASP 自己有同步标记的序列（只有 1 个）不动
	 */
	void GenerateSyncMarkers(UAnimSequence& Sequence, FImportStats& Stats)
	{
		if (!Sequence.AuthoredSyncMarkers.IsEmpty())
		{
			return;
		}
		const IAnimationDataModel* Model = Sequence.GetDataModel();
		const int32 NumKeys = Model->GetNumberOfKeys();
		const TArray<float> Times = KeyTimes(*Model, NumKeys);
		const bool bLooping = Sequence.GetName().Contains(TEXT("_Loop"));
		TArray<TPair<float, FName>> Markers;
		for (const TPair<FName, FName>& Contact : ContactMarkers)
		{
			const FFloatCurve* Curve = Model->FindFloatCurve(FAnimationCurveIdentifier(Contact.Key, ERawCurveTrackTypes::RCT_Float));
			if (!Curve || NumKeys < 2)
			{
				continue;
			}
			TArray<float> Values;
			for (const float Time : Times)
			{
				Values.Add(Curve->Evaluate(Time));
			}
			if (bLooping && Values[0] >= ContactThreshold && Values.Last() < ContactThreshold)
			{
				Markers.Emplace(0.f, Contact.Value);
			}
			for (int32 Index = 1; Index < NumKeys; ++Index)
			{
				if (Values[Index - 1] < ContactThreshold && Values[Index] >= ContactThreshold)
				{
					const float Alpha = (ContactThreshold - Values[Index - 1]) / (Values[Index] - Values[Index - 1]);
					Markers.Emplace(FMath::Lerp(Times[Index - 1], Times[Index], Alpha), Contact.Value);
				}
			}
		}
		if (Markers.IsEmpty())
		{
			return;
		}
		Markers.Sort([](const TPair<float, FName>& A, const TPair<float, FName>& B) { return A.Key < B.Key; });
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(&Sequence, SyncMarkerTrackName);
		for (const TPair<float, FName>& Marker : Markers)
		{
			UAnimationBlueprintLibrary::AddAnimationSyncMarker(&Sequence, Marker.Value, Marker.Key, SyncMarkerTrackName);
		}
		// 这个序列原来没有标记，全是这里加的
		AssignDeterministicMarkerGuids(Sequence);
		++Stats.SyncMarkerSequences;
		Stats.SyncMarkersAdded += Markers.Num();
	}

	/**
	 * @brief 预测落脚的曲线（FootstepCurves），只给两条接触曲线都有的序列。
	 * 着地照接触曲线的回差（FootPlantOn、FootPlantOff），落下的时刻是那一步曲线升过 ContactThreshold 的时刻（两帧之间插值），
	 * 位置是那一刻锁根的姿势里的脚掌（同运行时：状态机里不抽根运动）。循环的片段首尾接上；不循环的片段里，
	 * 第一步之前就着地的取首帧的脚掌，最后一步没落下的当作在片段末尾落下
	 */
	void GenerateFootstepCurves(UAnimSequence& Sequence, FImportStats& Stats)
	{
		const IAnimationDataModel* Model = Sequence.GetDataModel();
		const int32 NumKeys = Model->GetNumberOfKeys();
		const FFloatCurve* Contacts[2] = {
			Model->FindFloatCurve(FAnimationCurveIdentifier(FootstepCurves[0].Contact, ERawCurveTrackTypes::RCT_Float)),
			Model->FindFloatCurve(FAnimationCurveIdentifier(FootstepCurves[1].Contact, ERawCurveTrackTypes::RCT_Float)) };
		if (!Contacts[0] || !Contacts[1] || NumKeys < 2)
		{
			return;
		}
		const TArray<float> Times = KeyTimes(*Model, NumKeys);
		const float Length = Sequence.GetPlayLength();
		const bool bLooping = Sequence.GetName().Contains(TEXT("_Loop"));

		FAnimPoseEvaluationOptions Options;
		Options.EvaluationType = EAnimDataEvalType::Raw;
		Options.bExtractRootMotion = false;
		Options.bIncorporateRootMotionIntoPose = false;
		Options.bEvaluateCurves = false;
		const auto BallAt = [&Sequence, &Options](const float Time, const FName Ball)
		{
			FAnimPose Pose;
			UAnimPoseExtensions::GetAnimPoseAtTime(&Sequence, Time, Options, Pose);
			return UAnimPoseExtensions::GetBonePose(Pose, Ball, EAnimPoseSpaces::World).GetLocation();
		};

		// 两只脚都算完再写：加曲线会让数据模型的曲线数组重新分配，上面取到的接触曲线的指针就失效了
		TArray<float> In[2], X[2], Y[2];
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FFootstepCurveNames& Names = FootstepCurves[Side];
			TArray<float> Values;
			for (const float Time : Times)
			{
				Values.Add(Contacts[Side]->Evaluate(Time));
			}
			// 回差：循环的片段先走一圈，首帧接着上一圈末帧的状态
			const auto Step = [](const bool bWasPlanted, const float Value) { return Value > FootPlantOn || (bWasPlanted && Value >= FootPlantOff); };
			bool bPlanted = Values[0] >= ContactThreshold;
			if (bLooping)
			{
				for (const float Value : Values)
				{
					bPlanted = Step(bPlanted, Value);
				}
			}
			const bool bPlantedBefore = bPlanted;
			TArray<bool> Planted;
			for (const float Value : Values)
			{
				bPlanted = Step(bPlanted, Value);
				Planted.Add(bPlanted);
			}

			// 每一步落下：时刻、位置与它从哪一帧起算着地
			struct FLanding
			{
				float Time;
				FVector Ball;
				int32 Frame;
			};
			TArray<FLanding> Landings;
			if (bLooping && Planted[0] && !bPlantedBefore)
			{
				Landings.Add({ 0.f, BallAt(0.f, Names.Ball), 0 });
			}
			for (int32 Index = 1; Index < NumKeys; ++Index)
			{
				if (!Planted[Index] || Planted[Index - 1])
				{
					continue;
				}
				int32 Rise = Index;
				while (Rise > 1 && Values[Rise - 1] >= ContactThreshold)
				{
					--Rise;
				}
				const float Span = Values[Rise] - Values[Rise - 1];
				const float Alpha = Span > UE_KINDA_SMALL_NUMBER ? FMath::Clamp((ContactThreshold - Values[Rise - 1]) / Span, 0.f, 1.f) : 1.f;
				const float Time = FMath::Lerp(Times[Rise - 1], Times[Rise], Alpha);
				Landings.Add({ Time, BallAt(Time, Names.Ball), Index });
			}

			TOptional<FVector> FirstBall, LastBall;
			for (int32 Index = 0; Index < NumKeys; ++Index)
			{
				FVector Ball;
				float Wait = 0.f;
				if (Planted[Index])
				{
					const FLanding* Current = nullptr;
					for (const FLanding& Landing : Landings)
					{
						Current = Landing.Frame <= Index ? &Landing : Current;
					}
					Current = !Current && bLooping && !Landings.IsEmpty() ? &Landings.Last() : Current;
					if (!Current && !FirstBall)
					{
						FirstBall = BallAt(0.f, Names.Ball);
					}
					Ball = Current ? Current->Ball : *FirstBall;
				}
				else
				{
					const FLanding* Next = Landings.FindByPredicate([Index](const FLanding& Landing) { return Landing.Frame > Index; });
					if (Next)
					{
						Wait = FMath::Max(Next->Time - Times[Index], 0.f);
						Ball = Next->Ball;
					}
					else if (bLooping && !Landings.IsEmpty())
					{
						Wait = Landings[0].Time + Length - Times[Index];
						Ball = Landings[0].Ball;
					}
					else
					{
						LastBall = LastBall ? LastBall : TOptional<FVector>(BallAt(Length, Names.Ball));
						Wait = Length - Times[Index];
						Ball = *LastBall;
					}
				}
				In[Side].Add(Wait);
				X[Side].Add(static_cast<float>(Ball.X));
				Y[Side].Add(static_cast<float>(Ball.Y));
			}
			Stats.FootstepLandings += Landings.Num();
		}
		for (int32 Side = 0; Side < 2; ++Side)
		{
			WriteCurve(Sequence, FootstepCurves[Side].LandIn, Times, In[Side]);
			WriteCurve(Sequence, FootstepCurves[Side].LandX, Times, X[Side], RCIM_Constant);
			WriteCurve(Sequence, FootstepCurves[Side].LandY, Times, Y[Side], RCIM_Constant);
		}
		++Stats.FootstepSequences;
	}

	/** @brief 跳跃的标记（FindJumpEvents）放在 Jump 轨道上：起跳加 Takeoff 与 Apex，走下边缘加 Takeoff，落地加 Impact */
	void GenerateJumpMarkers(UAnimSequence& Sequence, const EJumpClip Kind, FImportStats& Stats)
	{
		const FJumpEvents Events = FindJumpEvents(*Sequence.GetDataModel(), Kind);
		TArray<TPair<float, FName>> Markers;
		if (Events.Takeoff >= 0.f)
		{
			Markers.Emplace(Events.Takeoff, TakeoffMarkerName);
		}
		if (Kind == EJumpClip::Start && Events.Apex >= 0.f)
		{
			Markers.Emplace(Events.Apex, ApexMarkerName);
		}
		if (Events.Impact >= 0.f)
		{
			Markers.Emplace(Events.Impact, ImpactMarkerName);
		}
		if (Markers.Num() < (Kind == EJumpClip::Start ? 2 : 1))
		{
			Stats.JumpsWithoutEvents.Add(Sequence.GetName());
			return;
		}
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(&Sequence, JumpMarkerTrackName);
		for (const TPair<float, FName>& Marker : Markers)
		{
			UAnimationBlueprintLibrary::AddAnimationSyncMarker(&Sequence, Marker.Value, Marker.Key, JumpMarkerTrackName);
		}
		// 接触曲线的 L、R 已经加过，新加的插在它们中间：按次序重新给定
		AssignDeterministicMarkerGuids(Sequence);
		++Stats.JumpMarkers[static_cast<int32>(Kind)];
	}

	/**
	 * 倾斜姿势：去掉骨盆相对基准首帧的偏转（骨盆是根骨骼下的第一根，整个身体跟着转回来）；整个姿势的改成网格空间的旋转叠加，
	 * 以基准的首帧为基准
	 */
	void PrepareLeanPose(UAnimSequence& Sequence, const FLeanPose& Pose, FImportStats& Stats)
	{
		const FString BasePath = FString::Printf(TEXT("%s/%s.%s"), *FPackageName::GetLongPackagePath(Sequence.GetPackage()->GetName()), Pose.Base, Pose.Base);
		const UAnimSequence* Base = LoadObject<UAnimSequence>(nullptr, *BasePath);
		TArray<FTransform> BasePelvis;
		if (Base)
		{
			Base->GetDataModel()->GetBoneTrackTransforms(LeanTwistBone, BasePelvis);
		}
		if (BasePelvis.IsEmpty())
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[Import] 找不到倾斜的基准 %s（或它的骨盆），%s 不改"), *BasePath, *Sequence.GetName());
			return;
		}

		TArray<FTransform> Pelvis;
		Sequence.GetDataModel()->GetBoneTrackTransforms(LeanTwistBone, Pelvis);
		TArray<FVector> Positions;
		TArray<FQuat> Rotations;
		TArray<FVector> Scales;
		for (const FTransform& Key : Pelvis)
		{
			const FQuat Twist = LeanTwist(Key.GetRotation(), BasePelvis[0].GetRotation());
			Stats.MaxLeanTwist = FMath::Max(Stats.MaxLeanTwist, FMath::RadiansToDegrees(Twist.GetAngle()));
			Positions.Add(Key.GetLocation());
			Rotations.Add(Twist.Inverse() * Key.GetRotation());
			Scales.Add(Key.GetScale3D());
		}
		IAnimationDataController& Controller = Sequence.GetController();
		{
			IAnimationDataController::FScopedBracket Bracket(Controller, FText::FromString(TEXT("Ginkgo lean pose untwist")), /*bShouldTransact*/ false);
			Controller.SetBoneTrackKeys(LeanTwistBone, Positions, Rotations, Scales, /*bShouldTransact*/ false);
		}
		++Stats.LeanPosesUntwisted;

		if (Pose.bMakeAdditive)
		{
			Sequence.Modify();
			Sequence.AdditiveAnimType = AAT_RotationOffsetMeshSpace;
			Sequence.RefPoseType = ABPT_AnimFrame;
			Sequence.RefPoseSeq = const_cast<UAnimSequence*>(Base);
			Sequence.RefFrameIndex = 0;
			Sequence.PostEditChange();
			++Stats.LeanPosesMadeAdditive;
		}
	}

	/**
	 * @brief 清理一个序列：删通知；按目录设根运动；在 GASP 里用它自己的曲线压缩设置的，改指引擎自带的同一份；
	 *        生成距离曲线、原地转身的剩余转角与同步标记；冲刺的倾斜姿势改成叠加
	 * @param RichCurveSettings - 引擎自带的 CompressedRichCurve 设置；序列原来用工程默认（ACL）时传空，不动
	 */
	void CleanSequence(UAnimSequence& Sequence, const ERootMotionRule Rule, UAnimCurveCompressionSettings* RichCurveSettings, FImportStats& Stats)
	{
		++Stats.Sequences;
		StripNotifies(Sequence, Stats);
		ApplyRootMotionRule(Sequence, Rule, Stats);
		if (RichCurveSettings)
		{
			Sequence.CurveCompressionSettings = RichCurveSettings;
			++Stats.RichCurveSequences;
		}

		const FString Folder = AnimationFolder(Sequence);
		const FString Name = Sequence.GetName();
		if (const EDistanceClip Kind = ClassifyDistanceClip(Folder, Name); Kind != EDistanceClip::None)
		{
			GenerateDistanceCurve(Sequence, Kind, Stats);
		}
		if (IsTurnInPlace(Folder, Name))
		{
			GenerateTurnCurves(Sequence, Stats);
		}
		GenerateSyncMarkers(Sequence, Stats);
		GenerateFootstepCurves(Sequence, Stats);
		if (const EJumpClip Kind = ClassifyJumpClip(Folder, Name); Kind != EJumpClip::None)
		{
			GenerateJumpMarkers(Sequence, Kind, Stats);
		}
		if (const FLeanPose* Lean = FindLeanPose(Folder, Name))
		{
			PrepareLeanPose(Sequence, *Lean, Stats);
		}
	}

	bool SavePackage(UPackage& Package, UObject& Asset)
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(Package.GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(&Package, &Asset, *Filename, SaveArgs);
	}

	/**
	 * @brief 逐个加载复制过来的包，清理后存成 5.8 的格式
	 * 骨架从头到尾留在内存里、最后存：加载序列时引擎可能往骨架上补曲线的元数据，中途被回收就丢了
	 * @note 加载时缺类、缺包的警告是预期的（通知的类、动画修改器的蓝图、GASP 的曲线压缩设置），最后按次数列出来；
	 *       导入后的包由 -Verify 另起进程检查
	 */
	bool CleanAndSave(const FSelection& Selection)
	{
		FImportStats Stats;
		FWarningCapture Capture;

		const TStrongObjectPtr<USkeleton> Skeleton(Cast<USkeleton>(LoadMannequinAsset(SkeletonAsset)));
		const TStrongObjectPtr<UAnimCurveCompressionSettings> RichCurveSettings(LoadObject<UAnimCurveCompressionSettings>(nullptr, RichCurveCompression));
		if (!Skeleton || !RichCurveSettings)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[Import] 加载不了骨架 %s 或曲线压缩设置 %s"), SkeletonAsset, RichCurveCompression);
			return false;
		}
		// 虚拟骨骼在加载序列之前补上：序列压缩时记下骨架的 VirtualBoneGuid，网格加载时按骨架重建参考骨架（含虚拟骨骼）
		const int32 VirtualBonesAdded = EnsureVirtualBones(*Skeleton);
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Import] 骨架补了 %d 根虚拟骨骼"), VirtualBonesAdded);

		const TArray<FString>& Packages = Selection.Packages;
		for (int32 Index = 0; Index < Packages.Num(); ++Index)
		{
			const FString PackageName = GamePackageName(Packages[Index]);
			UPackage* Package = LoadPackage(nullptr, *PackageName, LOAD_None);
			UObject* Asset = Package ? Package->FindAssetInPackage() : nullptr;
			if (!Asset)
			{
				UE_LOG(LogGinkgoAnimation, Error, TEXT("[Import] 加载不了 %s"), *PackageName);
				return false;
			}

			if (UAnimSequence* Sequence = Cast<UAnimSequence>(Asset))
			{
				const ERootMotionRule Rule = RootMotionRules().FindChecked(AnimationFolder(Packages[Index]));
				const bool bRichCurve = Selection.RichCurveSequences.Contains(Packages[Index]);
				CleanSequence(*Sequence, Rule, bRichCurve ? RichCurveSettings.Get() : nullptr, Stats);
			}
			else if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Asset))
			{
				Mesh->SetPostProcessAnimBlueprint(nullptr);
			}

			// 生成曲线改了数据模型，序列在后台重新压缩：存之前同步压缩一次、等后台的任务都做完。不等的话偶尔（1380 个里 1–2 个，
			// 每次不同）存的是做到一半的状态，导入就不再逐字节相同
			if (UAnimSequence* Sequence = Cast<UAnimSequence>(Asset))
			{
				Sequence->WaitOnExistingCompression(/*bWantResults*/ true);
				Sequence->CacheDerivedDataForCurrentPlatform();
			}
			if (!SavePackage(*Package, *Asset))
			{
				UE_LOG(LogGinkgoAnimation, Error, TEXT("[Import] 存不了 %s"), *PackageName);
				return false;
			}

			if ((Index + 1) % PackagesPerCollect == 0)
			{
				UE_LOG(LogGinkgoAnimation, Display, TEXT("[Import] %d / %d"), Index + 1, Packages.Num());
				CollectGarbage(RF_NoFlags);
			}
		}

		using namespace UE::Query;

		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Import] 存了 %d 个包：%d 个序列删掉 %d 个通知，留下 %d 个同步标记；%d 个序列的曲线压缩改指 %s"),
			Packages.Num(), Stats.Sequences, Stats.RemovedNotifies, Stats.KeptSyncMarkers, Stats.RichCurveSequences, RichCurveCompression);
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Import] 根运动：原地 %d 个（其中 %d 个原来开着根运动），保留 %d 个，原样 %d 个；%d 个补上了强制锁根"),
			Stats.ByRootMotionRule[static_cast<int32>(ERootMotionRule::InPlace)], Stats.RootMotionTurnedOff,
			Stats.ByRootMotionRule[static_cast<int32>(ERootMotionRule::RootMotion)], Stats.ByRootMotionRule[static_cast<int32>(ERootMotionRule::Unchanged)],
			Stats.RootLockAdded);
		Stats.RemovedNotifyNames
			| OrderByDescending([](const TPair<FName, int32>& Name) { return Name.Value; })
			| ForEach([](const TPair<FName, int32>& Name)
			{
				UE_LOG(LogGinkgoAnimation, Display, TEXT("[Import]   删掉的通知 %5d × %s"), Name.Value, *Name.Key.ToString());
			});
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Import] 生成距离曲线：起步 %d、急停 %d、反向 %d、落地 %d 个，%d 个为了不减改过（最多 %.2f cm）；原地转身的剩余转角 %d 个；按接触曲线加同步标记 %d 个序列、%d 个"),
			Stats.DistanceCurves[static_cast<int32>(EDistanceClip::Start)], Stats.DistanceCurves[static_cast<int32>(EDistanceClip::Stop)],
			Stats.DistanceCurves[static_cast<int32>(EDistanceClip::Pivot)], Stats.DistanceCurves[static_cast<int32>(EDistanceClip::Land)], Stats.MonotonicFixed,
			Stats.MaxMonotonicFix, Stats.TurnCurves, Stats.SyncMarkerSequences, Stats.SyncMarkersAdded);
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Import] 跳跃标记：起跳 %d、走下边缘 %d、落地 %d 个"), Stats.JumpMarkers[static_cast<int32>(EJumpClip::Start)],
			Stats.JumpMarkers[static_cast<int32>(EJumpClip::Off)], Stats.JumpMarkers[static_cast<int32>(EJumpClip::Land)]);
		UE_CLOG(!Stats.JumpsWithoutEvents.IsEmpty(), LogGinkgoAnimation, Warning, TEXT("[Import] 跳跃片段里找不到离地、到顶或着地（没加标记）：%s"),
			*FString::Join(Stats.JumpsWithoutEvents, TEXT("、")));
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Import] 预测落脚的曲线：%d 个序列，两只脚一共落下 %d 次"), Stats.FootstepSequences, Stats.FootstepLandings);
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Import] 倾斜姿势：%d 个去掉了骨盆相对基准的偏转（最多 %.1f°），其中 %d 个改成网格空间叠加"),
			Stats.LeanPosesUntwisted, Stats.MaxLeanTwist, Stats.LeanPosesMadeAdditive);
		UE_CLOG(!Stats.StopsStillMoving.IsEmpty(), LogGinkgoAnimation, Display, TEXT("[Import] 急停的序列到末帧还在动（停止点取末帧）：%s"),
			*FString::Join(Stats.StopsStillMoving, TEXT("、")));
		LogTopMessages(TEXT("[Import]"), Capture.Take(), 20);
		return true;
	}

	// ===== 校验 =====

	/** 一个动画子目录里根骨骼怎么动（读原始的骨骼轨道，不受根运动设置影响） */
	struct FRootMotionStats
	{
		int32 Sequences = 0;

		int32 RootMotionEnabled = 0;

		int32 RootLockForced = 0;

		/** 锁根的位置（参考姿势、首帧、原点）各有几个序列 */
		int32 LockTargets[3] = {};

		/** 根骨骼从首帧到末帧的水平距离（cm），每个序列一个 */
		TArray<double> Travel;

		/** 根骨骼比首帧最多高出多少（cm） */
		double MaxRise = 0.0;

		/** 根骨骼从首帧到末帧转了多少（°） */
		double MaxTurn = 0.0;

		/** 骨盆相对根骨骼比首帧最多高出多少（cm）：跳跃的起落记在骨盆上还是根骨骼上 */
		double MaxPelvisRise = 0.0;

		/** 首帧的根骨骼离原点多远（cm）、转了多少（°）：锁在参考姿势与锁在首帧是不是一回事 */
		double MaxFirstFrameOffset = 0.0;

		double MaxFirstFrameTurn = 0.0;
	};

	void CollectRootMotion(const UAnimSequence& Sequence, FRootMotionStats& Stats)
	{
		++Stats.Sequences;
		Stats.RootMotionEnabled += Sequence.bEnableRootMotion ? 1 : 0;
		Stats.RootLockForced += Sequence.bForceRootLock ? 1 : 0;
		++Stats.LockTargets[FMath::Clamp(static_cast<int32>(Sequence.RootMotionRootLock.GetValue()), 0, 2)];

		const IAnimationDataModel* Model = Sequence.GetDataModel();
		TArray<FTransform> Root;
		TArray<FTransform> Pelvis;
		Model->GetBoneTrackTransforms(TEXT("root"), Root);
		Model->GetBoneTrackTransforms(TEXT("pelvis"), Pelvis);
		if (Root.IsEmpty())
		{
			return;
		}

		const FTransform& First = Root[0];
		Stats.Travel.Add(FVector::Dist2D(First.GetLocation(), Root.Last().GetLocation()));
		Stats.MaxTurn = FMath::Max(Stats.MaxTurn, FMath::RadiansToDegrees(First.GetRotation().AngularDistance(Root.Last().GetRotation())));
		Stats.MaxFirstFrameOffset = FMath::Max(Stats.MaxFirstFrameOffset, First.GetLocation().Size());
		Stats.MaxFirstFrameTurn = FMath::Max(Stats.MaxFirstFrameTurn, FMath::RadiansToDegrees(First.GetRotation().AngularDistance(FQuat::Identity)));
		for (const FTransform& Key : Root)
		{
			Stats.MaxRise = FMath::Max(Stats.MaxRise, Key.GetLocation().Z - First.GetLocation().Z);
		}
		for (const FTransform& Key : Pelvis)
		{
			Stats.MaxPelvisRise = FMath::Max(Stats.MaxPelvisRise, Key.GetLocation().Z - Pelvis[0].GetLocation().Z);
		}
	}

	void LogRootMotion(const TMap<FString, FRootMotionStats>& StatsByFolder)
	{
		for (const TPair<FString, FRootMotionStats>& Folder : StatsByFolder)
		{
			const FRootMotionStats& Stats = Folder.Value;
			TArray<double> Travel = Stats.Travel;
			Travel.Sort();
			const double MedianTravel = Travel.IsEmpty() ? 0.0 : Travel[Travel.Num() / 2];
			const double MaxTravel = Travel.IsEmpty() ? 0.0 : Travel.Last();
			UE_LOG(LogGinkgoAnimation, Display,
				TEXT("[Verify] 根运动 %-12s %4d 个：开着 %4d、强制锁根 %4d、锁在参考姿势 / 首帧 / 原点 %d / %d / %d；首末帧水平位移中位 %6.1f cm、最大 %6.1f cm；最多升高 %6.1f cm；最多转 %5.1f°；骨盆最多升高 %6.1f cm；首帧离原点最多 %5.1f cm、%5.1f°"),
				*Folder.Key, Stats.Sequences, Stats.RootMotionEnabled, Stats.RootLockForced, Stats.LockTargets[0], Stats.LockTargets[1], Stats.LockTargets[2],
				MedianTravel, MaxTravel, Stats.MaxRise, Stats.MaxTurn, Stats.MaxPelvisRise, Stats.MaxFirstFrameOffset, Stats.MaxFirstFrameTurn);
		}
	}

	struct FSequenceSummary
	{
		int32 Sequences = 0;

		double Seconds = 0.0;

		int32 WithSyncMarkers = 0;

		int32 WithCurves = 0;

		int32 WithRootMotion = 0;

		int32 Additive = 0;

		/** 骨骼与曲线的压缩设置组合 → 序列个数 */
		TMap<FString, int32> Compression;

		TSet<FName> MarkerNames;

		TSet<FName> CurveNames;

		/** 动画子目录 → 根运动的统计 */
		TMap<FString, FRootMotionStats> RootMotion;

		/** 在状态机里求值过锁根的序列，根骨骼离参考姿势最多多远（cm）、转了多少（°） */
		int32 RootLockChecked = 0;

		double MaxRootLockOffset = 0.0;

		double MaxRootLockTurn = 0.0;

		/** 对照：同样求值、但跳过锁根时根骨骼离参考姿势最远多少（cm）。不为 0 才说明上面的测量看得出锁没锁 */
		double MaxUnlockedOffset = 0.0;

		/** 有距离曲线的起步、急停、反向、落地（EDistanceClip 的顺序），与各自首帧、末帧的值的范围（cm） */
		int32 DistanceCurves[5] = {};

		FFloatInterval FirstDistance[5] = { FFloatInterval(), FFloatInterval(), FFloatInterval(), FFloatInterval(), FFloatInterval() };

		FFloatInterval LastDistance[5] = { FFloatInterval(), FFloatInterval(), FFloatInterval(), FFloatInterval(), FFloatInterval() };

		/** 有跳跃标记的起跳、走下边缘、落地（EJumpClip 的顺序），与离地、离地到最高、着地的时刻的范围（s） */
		int32 JumpMarkers[4] = {};

		FFloatInterval Takeoff;

		FFloatInterval Ascent;

		FFloatInterval Impact;

		/** 有预测落脚曲线的序列，还有多久落下最长多久（s），落脚的位置的范围（cm） */
		int32 FootstepSequences = 0;

		float MaxLandIn = 0.f;

		FFloatInterval LandX;

		FFloatInterval LandY;

		/** 对的倾斜姿势（以基准首帧为基准的网格空间叠加、骨盆相对基准不偏转），与骨盆相对基准最多偏转多少（°） */
		int32 LeanPoses = 0;

		double MaxLeanTwist = 0.0;

		/** 有剩余转角的原地转身，与首帧的剩余转角离名字里的角度最远多少（°） */
		int32 TurnCurves = 0;

		double MaxTurnAngleError = 0.0;

		/** 按接触曲线加的同步标记（L、R）在多少个序列上 */
		int32 WithContactMarkers = 0;
	};

	/** 判断锁根成立的容差：求值的根骨骼离参考姿势不超过 0.01 cm、0.01° */
	constexpr double RootLockTolerance = 0.01;

	/**
	 * @brief 在状态机里求值五个时刻的姿势，返回根骨骼离参考姿势最远多少（cm、°）
	 * 动画实例默认只从蒙太奇抽根运动，状态机里的序列不抽，这里同样不抽。用 AnimPose 求原始数据：原始数据与压缩数据
	 * 两条路径锁根的判断相同（AnimSequence.cpp、AnimationDecompression.cpp）。
	 * @param bIgnoreRootLock - 跳过锁根（AnimPose 的 bIncorporateRootMotionIntoPose），用作对照：这时根骨骼照着轨道走
	 */
	TPair<double, double> MeasureRootLock(const UAnimSequence& Sequence, const FTransform& RefPoseRoot, const bool bIgnoreRootLock)
	{
		FAnimPoseEvaluationOptions Options;
		Options.EvaluationType = EAnimDataEvalType::Raw;
		Options.bExtractRootMotion = false;
		Options.bIncorporateRootMotionIntoPose = bIgnoreRootLock;
		Options.bEvaluateCurves = false;

		constexpr int32 Intervals = 4;
		double MaxOffset = 0.0;
		double MaxTurn = 0.0;
		for (int32 Sample = 0; Sample <= Intervals; ++Sample)
		{
			FAnimPose Pose;
			UAnimPoseExtensions::GetAnimPoseAtTime(&Sequence, Sequence.GetPlayLength() * Sample / Intervals, Options, Pose);
			const FTransform& Root = UAnimPoseExtensions::GetBonePose(Pose, TEXT("root"));
			MaxOffset = FMath::Max(MaxOffset, FVector::Dist(Root.GetLocation(), RefPoseRoot.GetLocation()));
			MaxTurn = FMath::Max(MaxTurn, FMath::RadiansToDegrees(Root.GetRotation().AngularDistance(RefPoseRoot.GetRotation())));
		}
		return { MaxOffset, MaxTurn };
	}

	/** 压缩设置来自引擎（引擎或引擎插件的内容）：GASP 自己的设置没搬，序列还指着它就是没改干净 */
	bool IsEngineContent(const UObject* Settings)
	{
		return Settings && !Settings->GetPackage()->GetName().StartsWith(TEXT("/Game/"));
	}

	/**
	 * @brief 生成的数据：起步、急停、反向有距离曲线，且每一帧的值不比前一帧小（距离匹配按值二分）；
	 *        原地转身有剩余转角，首帧的值是名字里的角度（_090_L 是 −90°），末帧为 0
	 */
	void CheckGeneratedData(const UAnimSequence& Sequence, const FString& Folder, TArray<FString>& Problems, FSequenceSummary& Summary)
	{
		const FString Name = Sequence.GetName();
		const IAnimationDataModel* Model = Sequence.GetDataModel();
		const int32 NumKeys = Model->GetNumberOfKeys();
		const TArray<float> Times = KeyTimes(*Model, NumKeys);
		const auto Sample = [&Times](const FFloatCurve& Curve)
		{
			TArray<float> Values;
			for (const float Time : Times)
			{
				Values.Add(Curve.Evaluate(Time));
			}
			return Values;
		};

		Summary.WithContactMarkers += Sequence.AuthoredSyncMarkers.ContainsByPredicate([](const FAnimSyncMarker& Marker)
		{
			return Marker.MarkerName == ContactMarkers[0].Value || Marker.MarkerName == ContactMarkers[1].Value;
		}) ? 1 : 0;

		if (const EDistanceClip Kind = ClassifyDistanceClip(Folder, Name); Kind != EDistanceClip::None)
		{
			const FFloatCurve* Curve = Model->FindFloatCurve(FAnimationCurveIdentifier(DistanceCurveName, ERawCurveTrackTypes::RCT_Float));
			if (!Curve || NumKeys < 2)
			{
				Problems.Add(FString::Printf(TEXT("%s 没有距离曲线"), *Name));
				return;
			}
			const TArray<float> Values = Sample(*Curve);
			for (int32 Index = 1; Index < Values.Num(); ++Index)
			{
				if (Values[Index] < Values[Index - 1])
				{
					Problems.Add(FString::Printf(TEXT("%s 的距离曲线在 %.3f s 往回走了 %.3f cm"), *Name, Times[Index], Values[Index - 1] - Values[Index]));
					break;
				}
			}
			const int32 KindIndex = static_cast<int32>(Kind);
			++Summary.DistanceCurves[KindIndex];
			Summary.FirstDistance[KindIndex].Include(Values[0]);
			Summary.LastDistance[KindIndex].Include(Values.Last());
		}

		if (const EJumpClip JumpKind = ClassifyJumpClip(Folder, Name); JumpKind != EJumpClip::None)
		{
			const auto MarkerTime = [&Sequence](const FName MarkerName)
			{
				const FAnimSyncMarker* Marker = Sequence.AuthoredSyncMarkers.FindByPredicate([MarkerName](const FAnimSyncMarker& Item) { return Item.MarkerName == MarkerName; });
				return Marker ? Marker->Time : -1.f;
			};
			const float Takeoff = MarkerTime(TakeoffMarkerName);
			const float Apex = MarkerTime(ApexMarkerName);
			const float Impact = MarkerTime(ImpactMarkerName);
			const bool bComplete = JumpKind == EJumpClip::Start ? Takeoff >= 0.f && Apex > Takeoff : JumpKind == EJumpClip::Off ? Takeoff >= 0.f : Impact >= 0.f;
			if (!bComplete)
			{
				Problems.Add(FString::Printf(TEXT("%s 的跳跃标记不全：离地 %.3f、到顶 %.3f、着地 %.3f s"), *Name, Takeoff, Apex, Impact));
			}
			else
			{
				++Summary.JumpMarkers[static_cast<int32>(JumpKind)];
				if (JumpKind != EJumpClip::Land)
				{
					Summary.Takeoff.Include(Takeoff);
				}
				if (JumpKind == EJumpClip::Start)
				{
					Summary.Ascent.Include(Apex - Takeoff);
				}
				if (JumpKind == EJumpClip::Land)
				{
					Summary.Impact.Include(Impact);
				}
			}
		}

		// 两条接触曲线都有的，预测落脚的三条曲线也要有：还有多久落下不为负，着地时为 0，落脚的位置在脚够得着的范围里
		const FFloatCurve* Contacts[2] = {
			Model->FindFloatCurve(FAnimationCurveIdentifier(FootstepCurves[0].Contact, ERawCurveTrackTypes::RCT_Float)),
			Model->FindFloatCurve(FAnimationCurveIdentifier(FootstepCurves[1].Contact, ERawCurveTrackTypes::RCT_Float)) };
		if (Contacts[0] && Contacts[1])
		{
			bool bComplete = true;
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const FFootstepCurveNames& Names = FootstepCurves[Side];
				const FFloatCurve* In = Model->FindFloatCurve(FAnimationCurveIdentifier(Names.LandIn, ERawCurveTrackTypes::RCT_Float));
				const FFloatCurve* X = Model->FindFloatCurve(FAnimationCurveIdentifier(Names.LandX, ERawCurveTrackTypes::RCT_Float));
				const FFloatCurve* Y = Model->FindFloatCurve(FAnimationCurveIdentifier(Names.LandY, ERawCurveTrackTypes::RCT_Float));
				if (!In || !X || !Y)
				{
					Problems.Add(FString::Printf(TEXT("%s 有接触曲线 %s，没有预测落脚的曲线"), *Name, *Names.Contact.ToString()));
					bComplete = false;
					continue;
				}
				for (const float Time : KeyTimes(*Model, Model->GetNumberOfKeys()))
				{
					const float Wait = In->Evaluate(Time);
					const float LandX = X->Evaluate(Time);
					const float LandY = Y->Evaluate(Time);
					if (Wait < -UE_KINDA_SMALL_NUMBER || (Contacts[Side]->Evaluate(Time) > FootPlantOn && Wait > UE_KINDA_SMALL_NUMBER)
						|| FMath::Abs(LandX) > 100.f || FMath::Abs(LandY) > 250.f)
					{
						Problems.Add(FString::Printf(TEXT("%s 在 %.3f s：%s %.3f s、落在 (%.1f, %.1f) cm，接触 %.2f"), *Name, Time, *Names.LandIn.ToString(), Wait,
							LandX, LandY, Contacts[Side]->Evaluate(Time)));
						bComplete = false;
						break;
					}
					Summary.MaxLandIn = FMath::Max(Summary.MaxLandIn, Wait);
					Summary.LandX.Include(LandX);
					Summary.LandY.Include(LandY);
				}
			}
			Summary.FootstepSequences += bComplete ? 1 : 0;
		}

		if (const FLeanPose* Lean = FindLeanPose(Folder, Name))
		{
			const bool bAdditive = Sequence.AdditiveAnimType == AAT_RotationOffsetMeshSpace && Sequence.RefPoseType == ABPT_AnimFrame
				&& Sequence.RefPoseSeq && Sequence.RefPoseSeq->GetName() == Lean->Base && Sequence.RefFrameIndex == 0;
			TArray<FTransform> Pelvis, BasePelvis;
			Model->GetBoneTrackTransforms(LeanTwistBone, Pelvis);
			if (bAdditive)
			{
				Sequence.RefPoseSeq->GetDataModel()->GetBoneTrackTransforms(LeanTwistBone, BasePelvis);
			}
			double Twist = 0.0;
			for (const FTransform& Key : BasePelvis.IsEmpty() ? TArray<FTransform>() : Pelvis)
			{
				Twist = FMath::Max(Twist, FMath::RadiansToDegrees(LeanTwist(Key.GetRotation(), BasePelvis[0].GetRotation()).GetAngle()));
			}
			if (!bAdditive || BasePelvis.IsEmpty() || Twist > LeanMaxTwist)
			{
				Problems.Add(FString::Printf(TEXT("%s 应当是以 %s 首帧为基准的网格空间叠加、骨盆相对基准不偏转，现在叠加 %d、偏转 %.2f°"), *Name, Lean->Base,
					bAdditive ? 1 : 0, Twist));
			}
			else
			{
				++Summary.LeanPoses;
				Summary.MaxLeanTwist = FMath::Max(Summary.MaxLeanTwist, Twist);
			}
		}

		if (IsTurnInPlace(Folder, Name))
		{
			const FFloatCurve* Curve = Model->FindFloatCurve(FAnimationCurveIdentifier(RemainingTurnYawCurveName, ERawCurveTrackTypes::RCT_Float));
			FString AngleText;
			if (!Curve || !Name.Split(TEXT("_Turn_"), nullptr, &AngleText))
			{
				Problems.Add(FString::Printf(TEXT("%s 没有剩余转角"), *Name));
				return;
			}
			const float NamedAngle = FCString::Atof(*AngleText.Left(3)) * (AngleText.EndsWith(TEXT("_L")) ? -1.f : 1.f);
			const TArray<float> Values = Sample(*Curve);
			const double Error = FMath::Abs(Values[0] - NamedAngle);
			if (Error > 15.0 || !FMath::IsNearlyZero(Values.Last(), 1e-3f))
			{
				Problems.Add(FString::Printf(TEXT("%s 的剩余转角从 %.1f° 转到 %.3f°，名字里是 %.0f°"), *Name, Values[0], Values.Last(), NamedAngle));
			}
			++Summary.TurnCurves;
			Summary.MaxTurnAngleError = FMath::Max(Summary.MaxTurnAngleError, Error);
		}
	}

	void CheckSequence(const UAnimSequence& Sequence, const USkeleton* Skeleton, TArray<FString>& Problems, FSequenceSummary& Summary)
	{
		const FString Name = Sequence.GetPathName();
		if (!Sequence.Notifies.IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("%s 还有 %d 个通知"), *Name, Sequence.Notifies.Num()));
		}
		if (Sequence.GetSkeleton() != Skeleton)
		{
			Problems.Add(FString::Printf(TEXT("%s 的骨架是 %s"), *Name, *GetPathNameSafe(Sequence.GetSkeleton())));
		}
		if (!IsEngineContent(Sequence.BoneCompressionSettings) || !IsEngineContent(Sequence.CurveCompressionSettings))
		{
			Problems.Add(FString::Printf(TEXT("%s 的压缩设置不是引擎的：%s、%s"), *Name,
				*GetPathNameSafe(Sequence.BoneCompressionSettings), *GetPathNameSafe(Sequence.CurveCompressionSettings)));
		}

		++Summary.Sequences;
		Summary.Seconds += Sequence.GetPlayLength();
		Summary.WithSyncMarkers += Sequence.AuthoredSyncMarkers.IsEmpty() ? 0 : 1;
		Summary.WithRootMotion += Sequence.bEnableRootMotion ? 1 : 0;
		Summary.Additive += Sequence.IsValidAdditive() ? 1 : 0;
		++Summary.Compression.FindOrAdd(FString::Printf(TEXT("%s + %s"),
			*GetNameSafe(Sequence.BoneCompressionSettings), *GetNameSafe(Sequence.CurveCompressionSettings)));
		for (const FAnimSyncMarker& Marker : Sequence.AuthoredSyncMarkers)
		{
			Summary.MarkerNames.Add(Marker.MarkerName);
		}
		const TArray<FFloatCurve>& Curves = Sequence.GetDataModel()->GetFloatCurves();
		Summary.WithCurves += Curves.IsEmpty() ? 0 : 1;
		for (const FFloatCurve& Curve : Curves)
		{
			Summary.CurveNames.Add(Curve.GetName());
		}

		const FString Folder = AnimationFolder(Sequence);
		CheckGeneratedData(Sequence, Folder, Problems, Summary);
		CollectRootMotion(Sequence, Summary.RootMotion.FindOrAdd(Folder));
		const ERootMotionRule* Rule = RootMotionRules().Find(Folder);
		if (!Rule)
		{
			Problems.Add(FString::Printf(TEXT("%s 所在的目录 \"%s\" 没有根运动的规则"), *Name, *Folder));
			return;
		}
		if (*Rule == ERootMotionRule::Unchanged || !Skeleton)
		{
			return;
		}

		const bool bEnableRootMotion = *Rule == ERootMotionRule::RootMotion;
		if (Sequence.bEnableRootMotion != bEnableRootMotion || !Sequence.bForceRootLock || Sequence.RootMotionRootLock != ERootMotionRootLock::RefPose)
		{
			Problems.Add(FString::Printf(TEXT("%s 的根运动应当是%s，现在开着 %d、强制锁根 %d、锁在 %d"), *Name, RootMotionRuleName(*Rule),
				Sequence.bEnableRootMotion ? 1 : 0, Sequence.bForceRootLock ? 1 : 0, static_cast<int32>(Sequence.RootMotionRootLock.GetValue())));
		}
		const FTransform& RefPoseRoot = Skeleton->GetReferenceSkeleton().GetRefBonePose()[0];
		const TPair<double, double> RootLockError = MeasureRootLock(Sequence, RefPoseRoot, /*bIgnoreRootLock*/ false);
		if (RootLockError.Key > RootLockTolerance || RootLockError.Value > RootLockTolerance)
		{
			Problems.Add(FString::Printf(TEXT("%s 在状态机里根骨骼离参考姿势 %.3f cm、%.3f°"), *Name, RootLockError.Key, RootLockError.Value));
		}
		++Summary.RootLockChecked;
		Summary.MaxRootLockOffset = FMath::Max(Summary.MaxRootLockOffset, RootLockError.Key);
		Summary.MaxRootLockTurn = FMath::Max(Summary.MaxRootLockTurn, RootLockError.Value);
		Summary.MaxUnlockedOffset = FMath::Max(Summary.MaxUnlockedOffset, MeasureRootLock(Sequence, RefPoseRoot, /*bIgnoreRootLock*/ true).Key);
	}

	FString JoinNames(const TSet<FName>& Names)
	{
		TArray<FString> Sorted;
		for (const FName Name : Names)
		{
			Sorted.Add(Name.ToString());
		}
		Sorted.Sort();
		return FString::Join(Sorted, TEXT(", "));
	}

	/**
	 * @brief 校验导入的包
	 * 依赖都在（/Game 与 /Engine 的包在磁盘上，/Script 的模块已加载）；加载时没有警告与错误；序列没有通知、骨架都是 SK_UEFN_Mannequin、
	 * 压缩设置都来自引擎；网格没有后处理动画蓝图。另外报告序列的总时长、同步标记、曲线、根运动、叠加动画与压缩设置的分布
	 * @return 有任何问题或警告时返回 false
	 */
	bool VerifyImport()
	{
		using namespace UE::Query;

		IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
		const FString Root = GamePackageName(MannequinFolder);
		AssetRegistry.ScanPathsSynchronous({ Root }, /*bForceRescan*/ true);
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPath(FName(Root), Assets, /*bRecursive*/ true);
		if (Assets.IsEmpty())
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[Verify] %s 下面什么也没有"), *Root);
			return false;
		}

		TMap<FString, int32> ClassCounts;
		Assets | ForEach([&ClassCounts](const FAssetData& Asset) { ++ClassCounts.FindOrAdd(Asset.AssetClassPath.GetAssetName().ToString()); });

		TArray<FString> Problems;
		for (const FAssetData& Asset : Assets)
		{
			TArray<FName> Dependencies;
			AssetRegistry.GetDependencies(Asset.PackageName, Dependencies);
			Dependencies
				| Where([](const FName Dependency)
				{
					const FString Name = Dependency.ToString();
					return !FindPackage(nullptr, *Name) && !FPackageName::DoesPackageExist(Name);
				})
				| ForEach([&](const FName Dependency)
				{
					Problems.Add(FString::Printf(TEXT("%s 引用的 %s 不存在"), *Asset.PackageName.ToString(), *Dependency.ToString()));
				});
		}

		FWarningCapture Capture;
		const TStrongObjectPtr<USkeleton> Skeleton(Cast<USkeleton>(LoadMannequinAsset(SkeletonAsset)));
		for (const FVirtualBoneSpec& Spec : VirtualBones)
		{
			const FName Name = VirtualBoneName(Spec);
			if (!Skeleton || !Skeleton->GetVirtualBones().ContainsByPredicate([Name](const FVirtualBone& Bone) { return Bone.VirtualBoneName == Name; }))
			{
				Problems.Add(FString::Printf(TEXT("骨架上没有虚拟骨骼 %s（-VirtualBones 补上）"), *Name.ToString()));
			}
		}
		FSequenceSummary Summary;
		for (int32 Index = 0; Index < Assets.Num(); ++Index)
		{
			const UObject* Object = Assets[Index].GetAsset();
			if (!Object)
			{
				Problems.Add(FString::Printf(TEXT("加载不了 %s"), *Assets[Index].GetObjectPathString()));
			}
			else if (const UAnimSequence* Sequence = Cast<UAnimSequence>(Object))
			{
				CheckSequence(*Sequence, Skeleton.Get(), Problems, Summary);
			}
			else if (const USkeletalMesh* Mesh = Cast<USkeletalMesh>(Object))
			{
				if (Mesh->GetPostProcessAnimBlueprint())
				{
					Problems.Add(FString::Printf(TEXT("%s 还有后处理动画蓝图"), *Mesh->GetPathName()));
				}
				if (Mesh->GetSkeleton() != Skeleton.Get())
				{
					Problems.Add(FString::Printf(TEXT("%s 的骨架是 %s"), *Mesh->GetPathName(), *GetPathNameSafe(Mesh->GetSkeleton())));
				}
			}

			if ((Index + 1) % PackagesPerCollect == 0)
			{
				CollectGarbage(RF_NoFlags);
			}
		}
		const TMap<FString, int32> Warnings = Capture.Take();

		ClassCounts.ValueSort(TGreater<int32>());
		for (const TPair<FString, int32>& Class : ClassCounts)
		{
			UE_LOG(LogGinkgoAnimation, Display, TEXT("[Verify] %-24s %4d"), *Class.Key, Class.Value);
		}
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Verify] 骨架 %s：%d 根骨骼"),
			*GetPathNameSafe(Skeleton.Get()), Skeleton ? Skeleton->GetReferenceSkeleton().GetNum() : 0);
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Verify] %d 个序列，共 %.1f 分钟；%d 个有同步标记，%d 个有曲线，%d 个开了根运动，%d 个是叠加动画"),
			Summary.Sequences, Summary.Seconds / 60.0, Summary.WithSyncMarkers, Summary.WithCurves, Summary.WithRootMotion, Summary.Additive);
		Summary.Compression.ValueSort(TGreater<int32>());
		for (const TPair<FString, int32>& Compression : Summary.Compression)
		{
			UE_LOG(LogGinkgoAnimation, Display, TEXT("[Verify] 压缩设置 %s：%d 个序列"), *Compression.Key, Compression.Value);
		}
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Verify] 同步标记：%s；按接触曲线加的在 %d 个序列上"), *JoinNames(Summary.MarkerNames), Summary.WithContactMarkers);
		for (const EDistanceClip Kind : { EDistanceClip::Start, EDistanceClip::Stop, EDistanceClip::Pivot, EDistanceClip::Land })
		{
			const int32 Index = static_cast<int32>(Kind);
			UE_LOG(LogGinkgoAnimation, Display, TEXT("[Verify] 距离曲线 %-6s %3d 个：首帧 %8.1f – %8.1f cm，末帧 %8.1f – %8.1f cm"),
				Kind == EDistanceClip::Start ? TEXT("起步") : Kind == EDistanceClip::Stop ? TEXT("急停") : Kind == EDistanceClip::Pivot ? TEXT("反向") : TEXT("落地"),
				Summary.DistanceCurves[Index], Summary.FirstDistance[Index].Min, Summary.FirstDistance[Index].Max, Summary.LastDistance[Index].Min,
				Summary.LastDistance[Index].Max);
		}
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Verify] 跳跃标记：起跳 %d、走下边缘 %d、落地 %d 个；离地 %.3f – %.3f s，起跳离地到最高 %.3f – %.3f s，着地 %.3f – %.3f s"),
			Summary.JumpMarkers[static_cast<int32>(EJumpClip::Start)], Summary.JumpMarkers[static_cast<int32>(EJumpClip::Off)],
			Summary.JumpMarkers[static_cast<int32>(EJumpClip::Land)], Summary.Takeoff.Min, Summary.Takeoff.Max, Summary.Ascent.Min, Summary.Ascent.Max,
			Summary.Impact.Min, Summary.Impact.Max);
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Verify] 原地转身的剩余转角 %d 个：首帧离名字里的角度最多 %.2f°"), Summary.TurnCurves, Summary.MaxTurnAngleError);
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Verify] 预测落脚的曲线：%d 个序列，还有多久落下最长 %.3f s，落脚的位置 X %.1f – %.1f、Y %.1f – %.1f cm"),
			Summary.FootstepSequences, Summary.MaxLandIn, Summary.LandX.Min, Summary.LandX.Max, Summary.LandY.Min, Summary.LandY.Max);
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Verify] 倾斜姿势：%d 个是以基准首帧为基准的网格空间叠加，骨盆相对基准最多偏转 %.3f°"), Summary.LeanPoses,
			Summary.MaxLeanTwist);
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Verify] 曲线：%s"), *JoinNames(Summary.CurveNames));
		Summary.RootMotion.KeySort(TLess<FString>());
		LogRootMotion(Summary.RootMotion);
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Verify] 状态机里（不抽根运动）求值 %d 个强制锁根的序列、各 5 个时刻：根骨骼离参考姿势（%s）最多 %.4f cm、%.4f°；对照（跳过锁根）最多 %.1f cm"),
			Summary.RootLockChecked, Skeleton ? *Skeleton->GetReferenceSkeleton().GetRefBonePose()[0].GetLocation().ToString() : TEXT("?"),
			Summary.MaxRootLockOffset, Summary.MaxRootLockTurn, Summary.MaxUnlockedOffset);

		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[Verify] %s"), *Problem);
		}
		LogTopMessages(TEXT("[Verify]"), Warnings, 20);
		const bool bPassed = Problems.IsEmpty() && Warnings.IsEmpty();
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Verify] %s：%d 个问题，%d 种警告"), bPassed ? TEXT("通过") : TEXT("没通过"), Problems.Num(), Warnings.Num());
		return bPassed;
	}

	// ===== 画面 =====

	/**
	 * 校验时渲染画面的资产，相对小白人目录：网格，加上几个有代表性的序列（取序列中间那一帧）。
	 * 看材质与贴图在不在，动画套在网格上的姿势对不对
	 */
	const TCHAR* const ShotAssets[] =
	{
		TEXT("Meshes/SKM_UEFN_Mannequin"),
		TEXT("Animations/Idle/M_Neutral_Stand_Idle_Loop"),
		TEXT("Animations/Run/M_Neutral_Run_Loop_F"),
		TEXT("Animations/Sprint/M_Neutral_Sprint_Loop_F"),
		TEXT("Animations/Jump/M_Neutral_Jump_F_Start_Run_Lfoot"),
		TEXT("Animations/Crouch/M_Neutral_Crouch_Loop_F"),
		TEXT("Animations/Slide/M_Neutral_Slide_FootOut_Loop"),
		TEXT("Animations/Traversal/Vault/M_Neutral_Traversal_Vault_1_0_run_F_Lfoot"),
	};

	constexpr uint32 ShotSize = 512;

	/** 画面框住骨盆上下各这么高：翻越时张开的手臂也在画面里 */
	constexpr double ShotHalfHeight = 130.0;

	/** 相机从前侧方稍微往下看 */
	const FRotator ShotViewRotation(-12.f, -120.f, 0.f);

	/**
	 * @brief 摆好姿势：序列取中间那一帧，网格本身是参考姿势；相机从前侧方对准骨盆
	 * 根骨骼的位移不抽走（与编辑器的预览一样），人可能离原点很远，所以对准骨盆而不是原点。也不按包围盒取景：
	 * 包围盒由物理资产的刚体算，翻越这类动作里比人大得多，人在画面里只剩一角
	 */
	void PoseForShot(UDebugSkelMeshComponent& MeshComponent, USceneCaptureComponent2D& Capture, UAnimSequence* Sequence)
	{
		MeshComponent.EnablePreview(Sequence != nullptr, Sequence);
		if (Sequence)
		{
			MeshComponent.Stop();
			MeshComponent.SetPosition(Sequence->GetPlayLength() / 2.f, /*bFireNotifies*/ false);
		}
		MeshComponent.RefreshBoneTransforms(nullptr);

		const FVector Pelvis = MeshComponent.GetBoneLocation(TEXT("pelvis"));
		const double Distance = ShotHalfHeight / FMath::Tan(FMath::DegreesToRadians(Capture.FOVAngle) / 2.0);
		Capture.SetWorldLocationAndRotation(Pelvis - ShotViewRotation.Vector() * Distance, ShotViewRotation);
	}

	/** 画面里有东西：和左上角颜色明显不同的像素超过 1% */
	bool HasContent(const TArray<FColor>& Pixels)
	{
		using namespace UE::Query;

		const FColor Corner = Pixels.IsEmpty() ? FColor::Black : Pixels[0];
		const int32 Different = Pixels
			| Where([Corner](const FColor& Pixel)
			{
				return FMath::Abs(Pixel.R - Corner.R) + FMath::Abs(Pixel.G - Corner.G) + FMath::Abs(Pixel.B - Corner.B) > 24;
			})
			| Count();
		return Different > Pixels.Num() / 100;
	}

	/**
	 * @brief 把网格与几个序列的画面存成 PNG，文件名是资产名；相对路径从工程目录算起
	 * 自己搭预览场景（默认的平行光与天光，加一盏补光），场景捕获画到渲染目标上再读回。不用编辑器的缩略图：它按包围盒取景，
	 * 翻越那张人只剩一角。commandlet 里引擎不自己走帧，每张用 CommandletHelpers::TickEngine 推一帧（引擎在 commandlet 里
	 * 生成 HLOD 时也这样），渲染线程的帧开始与结束才齐
	 * @note 要渲染：命令行带 -AllowCommandletRendering，不能带 -nullrhi；命令本身的 IsClient 要为真（见构造函数）
	 */
	bool RenderShots(const FString& Directory)
	{
		if (!FApp::CanEverRender())
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[Shots] 渲染不了：命令行要带 -AllowCommandletRendering，不能带 -nullrhi"));
			return false;
		}

		FPreviewScene Scene(FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false).SetTransactional(false));

		UDebugSkelMeshComponent* MeshComponent = NewObject<UDebugSkelMeshComponent>(GetTransientPackage());
		MeshComponent->SetSkeletalMesh(Cast<USkeletalMesh>(LoadMannequinAsset(TEXT("Meshes/SKM_UEFN_Mannequin"))));
		Scene.AddComponent(MeshComponent, FTransform::Identity);

		// 补光，从相机这边打、不投影：预览场景只有一盏平行光，天光没有立方体贴图，背光的一面全黑，看不清姿势
		UDirectionalLightComponent* FillLight = NewObject<UDirectionalLightComponent>(GetTransientPackage());
		FillLight->SetIntensity(UE_PI * 0.4f);
		FillLight->SetCastShadows(false);
		Scene.AddComponent(FillLight, FTransform(ShotViewRotation + FRotator(-20.f, 0.f, 0.f)));

		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
		Target->InitCustomFormat(ShotSize, ShotSize, PF_B8G8R8A8, /*bInForceLinearGamma*/ false);
		Target->UpdateResourceImmediate(true);

		USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(GetTransientPackage());
		Capture->TextureTarget = Target;
		Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->FOVAngle = 30.f;
		Scene.AddComponent(Capture, FTransform::Identity);

		bool bAllSaved = true;
		for (const TCHAR* Asset : ShotAssets)
		{
			UObject* Object = LoadMannequinAsset(Asset);
			if (!Object)
			{
				UE_LOG(LogGinkgoAnimation, Error, TEXT("[Shots] 加载不了 %s"), Asset);
				bAllSaved = false;
				continue;
			}

			PoseForShot(*MeshComponent, *Capture, Cast<UAnimSequence>(Object));
			FAssetCompilingManager::Get().FinishAllCompilation();
			Scene.GetWorld()->SendAllEndOfFrameUpdates();
			Capture->CaptureScene();
			CommandletHelpers::TickEngine(Scene.GetWorld());

			TArray<FColor> Pixels;
			Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
			for (FColor& Pixel : Pixels)
			{
				Pixel.A = 255;
			}

			const FString OutputDirectory = FPaths::IsRelative(Directory) ? FPaths::ProjectDir() / Directory : Directory;
			const FString Filename = FPaths::ConvertRelativePathToFull(OutputDirectory / FPaths::GetBaseFilename(Asset) + TEXT(".png"));
			if (!HasContent(Pixels))
			{
				UE_LOG(LogGinkgoAnimation, Error, TEXT("[Shots] %s 画面是空的"), Asset);
				bAllSaved = false;
			}
			if (FImageUtils::SaveImageByExtension(*Filename, FImageView(Pixels.GetData(), ShotSize, ShotSize)))
			{
				UE_LOG(LogGinkgoAnimation, Display, TEXT("[Shots] %s"), *Filename);
			}
			else
			{
				UE_LOG(LogGinkgoAnimation, Error, TEXT("[Shots] 存不了 %s"), *Filename);
				bAllSaved = false;
			}
		}
		return bAllSaved;
	}
}

UGinkgoAnimationImportCommandlet::UGinkgoAnimationImportCommandlet()
{
	// 画面要真的场景：GIsClient 为假时渲染模块给每个世界的都是空场景（FNULLSceneInterface），什么也画不出来
	IsClient = true;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UGinkgoAnimationImportCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> Values;
	ParseCommandLine(*Params, Tokens, Switches, Values);

	if (Switches.Contains(TEXT("Verify")))
	{
		const bool bVerified = VerifyImport();
		const FString* ShotDirectory = Values.Find(TEXT("Shots"));
		const bool bShotsSaved = !ShotDirectory || RenderShots(*ShotDirectory);
		return bVerified && bShotsSaved ? 0 : 1;
	}
	if (Switches.Contains(TEXT("VirtualBones")))
	{
		return AddVirtualBonesToExisting() ? 0 : 1;
	}

	const FString* SourceProject = Values.Find(TEXT("Source"));
	if (!SourceProject)
	{
		UE_LOG(LogGinkgoAnimation, Error, TEXT("[Import] 用法：-run=GinkgoAnimationImport -Source=<GASP 工程目录> [-DryRun]，或 -run=GinkgoAnimationImport -Verify [-Shots=<目录>]，")
			TEXT("或 -run=GinkgoAnimationImport -VirtualBones（只给已经导入的骨架补上虚拟骨骼）"));
		return 1;
	}

	const FString SourceContentDir = FPaths::ConvertRelativePathToFull(*SourceProject / TEXT("Content/"));
	FSelection Selection;
	if (!SelectPackages(SourceContentDir, Selection))
	{
		return 1;
	}
	LogSelection(Selection);
	if (Switches.Contains(TEXT("DryRun")))
	{
		return 0;
	}

	return CopyPackages(SourceContentDir, Selection.Packages) && CleanAndSave(Selection) ? 0 : 1;
}
