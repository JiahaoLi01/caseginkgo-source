#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "GinkgoAnimMontageCommandlet.generated.h"

/**
 * @class UGinkgoAnimMontageCommandlet : 动画动作用的蒙太奇
 * 从序列生成蒙太奇：槽、淡入淡出、事件（PlayMontageNotify，分支点）。见 Docs/Design/AnimActions.md「动画蓝图与素材」。
 *
 * 事件的时刻按序列上的曲线定，不在两个键之间插值（Blender 那边的曲线是按帧的阶跃，键上的姿势才对得上）：
 * - 上升沿：曲线第一次 ≥ 0.5 的那个键。收枪的 OnBack：枪落到背上的那一帧。
 * - 下降沿：曲线跌到 0.5 以下之前最后一个 ≥ 0.5 的键。拔枪的 InHand：枪离开背之前的最后一帧。
 * 两个都落在"ik_hand_gun 就在背上的插槽上"的那几帧里，换挂点不跳。
 *
 * 生成：UnrealEditor-Cmd.exe CaseGinkgo.uproject -run=GinkgoAnimMontage [-Only=<蒙太奇名>]
 * 已有的蒙太奇换成新生成的（同一个包、同一个名字），结果可以重做。打出每个事件的时刻与自动淡出开始的时刻；
 * 事件落在自动淡出的区间里（那里的通知在完成之后才到）、曲线里找不到边沿时返回 1。
 *
 * 为什么写成 commandlet：Python 改不了蒙太奇的槽（SlotAnimTracks 只读），通知的名字也要按曲线算，手工做重做不了。
 */
UCLASS()
class UGinkgoAnimMontageCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UGinkgoAnimMontageCommandlet();

	virtual int32 Main(const FString& Params) override;
};
