#pragma once

#include "CoreMinimal.h"
#include "GinkgoInputMath.h"
#include "InputModifiers.h"
#include "GinkgoInputModifiers.generated.h"

/**
 * 把玩家的设置（UGinkgoInputSettingsSubsystem）接进 Enhanced Input 的修改器：挂在映射上，每次都读正在生效的设置，算法在 GinkgoInputMath。
 * 不把设置的值抄到修改器上：映射重建时引擎会把修改器复制一份（RebuildControlMappings 的 DeepCopyPtrArray），抄上去的值改不到正在用的那份。
 * IA_Look 的值就是这一帧要转的度数（X 偏航、Y 俯仰），IA_Move 的值是过了死区的摇杆。
 * 回放绕过这一层：录制里存的是处理完的视角与移动（FGinkgoInputIntent），所以设置影响不到回放。
 */

/**
 * @class UGinkgoInputModifierMouseLook : 鼠标的视角
 * 输入是这一帧鼠标的 count（Mouse2D，DefaultInput.ini 里它的 AxisConfig 灵敏度是 1），输出是度数
 */
UCLASS(NotBlueprintable, meta = (DisplayName = "鼠标视角（玩家设置）"))
class CASEGINKGO_API UGinkgoInputModifierMouseLook : public UInputModifier
{
	GENERATED_BODY()

protected:
	virtual FInputActionValue ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime) override;
};

/**
 * @class UGinkgoInputModifierStickLook : 摇杆的视角
 * 输入是摇杆的值，输出是度数。转身加速的计时存在这里：只有摇杆在动时才运行，松开的那一帧运行一次，正好把计时清掉
 */
UCLASS(NotBlueprintable, meta = (DisplayName = "摇杆视角（玩家设置）"))
class CASEGINKGO_API UGinkgoInputModifierStickLook : public UInputModifier
{
	GENERATED_BODY()

public:
	/** 这条映射是摇杆互换时才用的那条（左摇杆转视角）：设置不符时输出零。两种摆法的映射都在，换摆法不用重建映射 */
	UPROPERTY()
	bool bForSwappedSticks = false;

protected:
	virtual FInputActionValue ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime) override;
	virtual void ReceiveModifierReinstanced_Implementation(const UInputModifier* OldModifier) override;

private:
	Ginkgo::Input::FGinkgoTurnRamp Ramp;
};

/**
 * @class UGinkgoInputModifierWheelMouse : 弹种轮盘的鼠标（Docs/Design/Weapons.md「切换弹种」）
 * 输入是这一帧鼠标的 count，输出是按灵敏度换的度数（同视角的尺度，Ginkgo::Input::WheelMouseDegrees）：不反转、不加速、不乘开镜，往上推就是往上
 */
UCLASS(NotBlueprintable, meta = (DisplayName = "轮盘鼠标（玩家设置）"))
class CASEGINKGO_API UGinkgoInputModifierWheelMouse : public UInputModifier
{
	GENERATED_BODY()

protected:
	virtual FInputActionValue ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime) override;
};

/**
 * @class UGinkgoInputModifierWheelStick : 弹种轮盘的摇杆：视角那根（摇杆互换时是左摇杆），过了视角的死区，不反转（Ginkgo::Input::WheelStickInput）
 */
UCLASS(NotBlueprintable, meta = (DisplayName = "轮盘摇杆（玩家设置）"))
class CASEGINKGO_API UGinkgoInputModifierWheelStick : public UInputModifier
{
	GENERATED_BODY()

public:
	/** 这条映射是摇杆互换时才用的那条（左摇杆转视角） */
	UPROPERTY()
	bool bForSwappedSticks = false;

protected:
	virtual FInputActionValue ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime) override;
};

/**
 * @class UGinkgoInputModifierStickMove : 摇杆的移动
 */
UCLASS(NotBlueprintable, meta = (DisplayName = "摇杆移动（玩家设置）"))
class CASEGINKGO_API UGinkgoInputModifierStickMove : public UInputModifier
{
	GENERATED_BODY()

public:
	/** 这条映射是摇杆互换时才用的那条（右摇杆移动） */
	UPROPERTY()
	bool bForSwappedSticks = false;

protected:
	virtual FInputActionValue ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime) override;
};
