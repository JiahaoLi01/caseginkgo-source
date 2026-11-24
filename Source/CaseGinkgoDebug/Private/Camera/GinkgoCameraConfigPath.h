#pragma once

#include "CoreMinimal.h"

/**
 * 按路径读写相机配置里的一项（ginkgo.camera.set / get / list 与调参面板共用）。
 * 路径：结构的成员用 . 隔开，数组用 [下标] 或 [名字]（元素上叫 Name 的成员：修改器、叠加效果），实例化的子对象往里走。
 * 例：BaseParams.ArmLength、Modifiers[SlideFov].BlendOutTime、OverlayEffects[FootstepShake].Impulse.Pitch、OverlayPivot
 */
namespace Ginkgo::Debug::Camera
{
	/** 路径解析到的一项：属性与它的地址，写的时候 ImportText 要知道所属的对象 */
	struct FConfigItem
	{
		/** 解析出的叶子属性，生命周期由对应的 UClass / UScriptStruct 管理 */
		const FProperty* Property = nullptr;

		/** 配置实例中的地址；数组发生增删后必须重新解析，不能长期缓存 */
		void* Address = nullptr;

		/** 属性所属对象，供 Unreal 属性文本导入导出解析对象引用 */
		UObject* Owner = nullptr;
	};

	/** 路径上的一段：走过的属性，与它是数组时取的下标 */
	struct FPathSegment
	{
		/** 当前路径段对应的属性；数组段保存数组属性，下一段再进入元素 */
		const FProperty* Property = nullptr;

		/** 数组下标；非数组段为 INDEX_NONE */
		int32 Index = INDEX_NONE;
	};

	/**
	 * @brief 按路径找到配置里的一项；找不到时 OutError 说为什么
	 * @param OutChain - 不为空时记下路径上每一段的属性（调参面板拿它们的显示名与所属的结构）
	 */
	bool ResolveConfigPath(UObject& Root, const FString& Path, FConfigItem& Out, FString& OutError, TArray<FPathSegment>* OutChain = nullptr);

	/** @brief 一项的文本（属性的文本导出：数是 150.000000，开关是 True、False，枚举是名字；射线列表为紧凑 JSON） */
	FString ExportItem(const FConfigItem& Item);

	/** @brief 按文本写一项；射线列表先整组校验再替换，其他属性沿用 Unreal 文本导入；失败返回假 */
	bool ImportItem(const FConfigItem& Item, const FString& Text);

	/** 调参面板的一行：配置里一个能调的数（float、double、整数）、开关、枚举或完整射线列表 */
	struct FTunable
	{
		/** 从配置根对象开始的稳定路径，作为页面消息和本地文件的键 */
		FString Path;

		/** 所在的组：路径里第一个数组元素（Modifiers[SlideFov]）或第一层的结构（BaseParams）；配置自己的散项为空 */
		FString Group;

		/** 叶子属性的反射定义，用于识别控件类型、单位与边界，不持有实例地址 */
		const FProperty* Property = nullptr;
	};

	/** @brief 按声明的顺序列出配置里所有能调的项。标签、标签查询、曲线、选参与元素的名字不列：它们不是"调"的数 */
	TArray<FTunable> CollectTunables(UObject& Root);
}
