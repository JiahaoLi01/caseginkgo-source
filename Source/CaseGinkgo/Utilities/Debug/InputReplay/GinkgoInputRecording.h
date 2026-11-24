#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Control/GinkgoInputIntent.h"
#include "Engine/EngineTypes.h"  // EMovementMode

/**
 * @struct FGinkgoInputSample : 录制里的一帧输入
 * 控制器解析好的意图加上时刻，而不是原始按键。
 */
struct FGinkgoInputSample : FGinkgoInputIntent
{
	/** 从录制开始算起的时刻（秒） */
	double Time = 0.0;
};

/**
 * @struct FGinkgoInputRecordingStart : 回放开始前把角色放回的状态
 */
struct FGinkgoInputRecordingStart
{
	/** 为真时 Location 是相对回放开始时角色位置的偏移（合成的测量路线用），否则是世界坐标（录下来的路线用） */
	bool bRelative = false;

	FVector Location = FVector::ZeroVector;

	FVector Velocity = FVector::ZeroVector;

	FRotator ControlRotation = FRotator::ZeroRotator;

	EMovementMode MovementMode = MOVE_Walking;
};

/**
 * @class FGinkgoInputRecording : 输入录制
 * 调试工具，发行版不编译。按时刻采样，回放时按回放自己的时刻取值，所以同一份输入可以在不同帧率下回放：
 * 视角在相邻采样之间线性插值（按最短角度），移动与按住类按键取前一个采样，跳跃按下沿与方向键按下按时间区间判断。
 * 存成 CSV：开头两行注释是格式版本与开始状态，之后是表头与逐帧数据；Python 可以直接生成或读取。
 * v2 在最后多一列方向键按下（direction_press，这一帧按下了几个），v3 再多两列方向键的朝向（keys_x、keys_y，后按的赢，同移动输入一样取前一个采样），
 * v4 再多一列交互键按下（interact，按下沿，同跳跃按时间区间判断），v5 再多一列开镜键按着（aim），v6 再多一列换枪的键（weapon_keys，EGinkgoWeaponKey 的位，按下沿），
 * v7 再多一列开火键按着（fire_held），v8 再多五列：装填键按着（reload_held）、弹种轮盘的鼠标（wheel_x、wheel_y：这一帧动的量，度，按时间区间加起来）、视角的摇杆（stick_x、stick_y，取前一个采样）。
 * 旧版本的录制照样能读：v1 读出来没有方向键按下；v1、v2 的朝向按移动输入每个分量的符号取；v3 以前的没有按过交互键；v5 以前的没有按过换枪的键。
 */
class CASEGINKGO_API FGinkgoInputRecording
{
public:
	FGinkgoInputRecordingStart Start;

	/** 按时刻升序 */
	TArray<FGinkgoInputSample> Samples;

	double GetDuration() const { return Samples.IsEmpty() ? 0.0 : Samples.Last().Time; }

	/**
	 * @brief 时刻 Time 的输入
	 * @note 早于第一个采样时取第一个，晚于最后一个时取最后一个；返回值的 bJumpPressed、bInteractPressed、bSwitchShoulderPressed 总为假、DirectionPresses 总为 0、WeaponKeys 与 WheelMouse 总为空，
	 *       按下沿用 HasJumpPressBetween、HasInteractPressBetween、ShouldSwitchShoulderBetween、CountDirectionPressesBetween 与 GetWeaponKeysBetween 判断
	 */
	FGinkgoInputSample Evaluate(double Time) const;

	/** @brief (From, To] 之间有没有按下跳跃 */
	bool HasJumpPressBetween(double From, double To) const;

	/** @brief (From, To] 之间有没有按下交互键 */
	bool HasInteractPressBetween(double From, double To) const;

	/** @brief (From, To] 之间换肩次数是否为奇数：跨越两个按下沿时抵消，回放帧率改变也不多切或漏切 */
	bool ShouldSwitchShoulderBetween(double From, double To) const;

	/** @brief (From, To] 之间按下了几次方向键 */
	uint8 CountDirectionPressesBetween(double From, double To) const;

	/** @brief (From, To] 之间按下了哪些换枪的键 */
	EGinkgoWeaponKey GetWeaponKeysBetween(double From, double To) const;

	/** @brief (From, To] 之间弹种轮盘的鼠标一共动了多少（度）：回放的帧率同录的不一样时也不丢、不重复 */
	FVector2D GetWheelMouseBetween(double From, double To) const;

	FString ToCsv() const;

	/**
	 * @brief 从 CSV 读出录制
	 * @param Text - CSV 全文
	 * @param OutRecording - 读出的录制
	 * @param OutError - 失败原因，带行号
	 * @return 是否成功
	 */
	static bool FromCsv(const FString& Text, FGinkgoInputRecording& OutRecording, FString& OutError);
};

#endif // !UE_BUILD_SHIPPING
