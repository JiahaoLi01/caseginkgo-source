#pragma once

#include "CoreMinimal.h"
#include "GinkgoCameraParams.generated.h"

/**
 * @struct FGinkgoCameraParams : 相机参数
 * 修改器写的是这些参数，不是相机位姿（Docs/Design/CameraPipeline.md「相机参数」）。基础值 P₀ 在相机配置里，
 * 参数层（M3d）在它上面应用覆盖与增量，再交给参数弹簧；位姿解算由参数与这一帧的视角算出相机位置。
 *
 * 每个参数都是 float：参数层的修改器按反射选参（FGinkgoCameraParamField，按结构缓存的 float 属性，思路同 CRPGFramework 属性系统的选参），
 * 向量参数因此拆成分量。
 *
 * 视场的约定：相机输出的 FMinimalViewInfo 基准宽高比为 4:3，引擎默认的 AspectRatio_MaintainYFOV 先由 4:3 水平视场角换算竖直视场角，
 * 再随实际视口宽高比横向扩展（Hor+）。因此 90° 基准值在 16:9 画面下对应水平视场 106.3°、竖直视场 73.7°
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoCameraParams
{
	GENERATED_BODY()

	/** 中环臂长（cm，不含肩部偏移）：平视时沿视线后退的距离；三环轨道以此为基准，关闭三环时各俯仰角均使用此距离 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (DisplayName = "臂长", ClampMin = 0, Units = "cm"))
	float ArmLength = 150.f;

	/** 肩部偏移（向右）：相机在视角空间里向右挪，角色让开画面中心的准星。臂越短，同样的偏移让得越开 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (DisplayName = "肩部偏移（右）", Units = "cm"))
	float ShoulderOffsetRight = 68.f;

	/** 肩部偏移（向上）：相机在视角空间里向上挪；负的是比跟随点低 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (DisplayName = "肩部偏移（上）", Units = "cm"))
	float ShoulderOffsetUp = -3.f;

	/** 跟随点偏移（向前）：胶囊中心到跟随点，沿视角的水平朝向 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (DisplayName = "跟随点偏移（前）", Units = "cm"))
	float PivotOffsetForward = 0.f;

	/** 跟随点偏移（向右）：沿视角的水平朝向的右方 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (DisplayName = "跟随点偏移（右）", Units = "cm"))
	float PivotOffsetRight = 0.f;

	/** 跟随点偏移（向上）：跟随点比胶囊中心高多少，在肩膀的高度（站着离地约 140 cm）；越高越像从头顶看。相机俯仰时绕它转 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (DisplayName = "跟随点偏移（上）", Units = "cm"))
	float PivotOffsetUp = 50.f;

	/** 视场：4:3 画面的水平视场（见上）。越大速度感越强，边缘拉伸也越明显 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (DisplayName = "视场", ClampMin = 5, ClampMax = 170, Units = "Degrees"))
	float FieldOfView = 90.f;

	/** 侧倾：绕相机前向的旋转，向右为正；墙跑时向墙外倾 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (DisplayName = "侧倾", Units = "Degrees"))
	float Roll = 0.f;

	/** 后处理权重：速度线、暗角等效果的强度，0 为没有。修改器与其他参数走同一套混合规则，不另设控制器 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (DisplayName = "后处理权重", ClampMin = 0, ClampMax = 1))
	float PostProcessWeight = 0.f;
};

/**
 * @struct FGinkgoCameraParamField : 选哪个相机参数
 * 包一个 FName，格式是"声明它的结构.属性名"（如 GinkgoCameraParams.FieldOfView），前缀用声明属性的结构（GetOwnerStruct），思路同 CRPGFramework 的
 * FGameplayAttributeField。修改器、参数弹簧按它找到参数的 FProperty（按结构缓存，见 Ginkgo::Camera::GetParamFieldInfoList）。
 * 编辑器里从下拉框选（GetOptions，选项由 UGinkgoCameraConfig::GetParamFieldOptions 列出：引擎自带的元数据，不用编辑器模块里的属性自定义）
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoCameraParamField
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (DisplayName = "参数", GetOptions = "CaseGinkgo.GinkgoCameraConfig.GetParamFieldOptions"))
	FName Name;

	FGinkgoCameraParamField() = default;
	explicit FGinkgoCameraParamField(const FName InName) : Name(InName) {}

	/** @brief 由属性得到标识：声明它的结构的名字加属性名 */
	static FGinkgoCameraParamField FromProperty(const FProperty* Property);

	/** @brief FGinkgoCameraParams 里的某个成员，写法同 GET_MEMBER_NAME_CHECKED：FromMember(GET_MEMBER_NAME_CHECKED(FGinkgoCameraParams, ArmLength)) */
	static FGinkgoCameraParamField FromMember(FName MemberName);

	bool IsValid() const { return Name != NAME_None; }

	bool operator==(const FGinkgoCameraParamField& Other) const { return Name == Other.Name; }
	bool operator!=(const FGinkgoCameraParamField& Other) const { return Name != Other.Name; }

	friend uint32 GetTypeHash(const FGinkgoCameraParamField& Field) { return GetTypeHash(Field.Name); }
};

/**
 * @struct FGinkgoCameraParamFieldInfo : 反射缓存的一条：参数的属性与它的标识
 */
struct FGinkgoCameraParamFieldInfo
{
	const FFloatProperty* Property = nullptr;
	FGinkgoCameraParamField Field;
};

/**
 * 参数的反射：按结构缓存的 float 属性，修改器按标识读写参数，不用为每个参数写一个分支
 */
namespace Ginkgo::Camera
{
	/**
	 * @brief 一个结构里全部 float 属性的反射缓存：第一次访问时建，之后直接返回（游戏线程）
	 * @param Struct - 参数所在的结构；遍历整条继承链
	 */
	CASEGINKGO_API const TArray<FGinkgoCameraParamFieldInfo>& GetParamFieldInfoList(const UStruct* Struct);

	/** @brief FGinkgoCameraParams 里标识对应的属性；没有这个参数时为空 */
	CASEGINKGO_API const FFloatProperty* FindParamProperty(const FGinkgoCameraParamField& Field);

	/** @brief 按属性读写参数 */
	CASEGINKGO_API float GetParamValue(const FGinkgoCameraParams& Params, const FFloatProperty& Property);
	CASEGINKGO_API void SetParamValue(FGinkgoCameraParams& Params, const FFloatProperty& Property, float Value);
}
