#pragma once

#include "CoreMinimal.h"

/**
 * @enum EGinkgoWeaponKey : 武器的键（按下沿，一帧里按下了哪些）
 * 意图与录制的 weapon_keys 列都是这几位。换枪的规则见 Ginkgo::Weapon::ApplyWeaponKey；开火、装填记成武器组件上的意图（Docs/Design/ActionStates.md「每种枪机一棵子树」）
 */
enum class EGinkgoWeaponKey : uint8
{
	None = 0,
	Slot1 = 1 << 0,
	Slot2 = 1 << 1,
	Holster = 1 << 2,
	Switch = 1 << 3,
	Fire = 1 << 4,
	Reload = 1 << 5,
	Sidearm = 1 << 6,
	/** 把手上的枪丢到地上（Docs/Design/Interaction.md「丢枪与捡枪」） */
	Drop = 1 << 7,
};
ENUM_CLASS_FLAGS(EGinkgoWeaponKey)

/**
 * @struct FGinkgoInputIntent : 一帧的输入意图
 * 控制器把实时输入解析成意图，再在 PlayerTick 里交给角色；调试用的录制与回放存取的也是它。
 */
struct FGinkgoInputIntent
{
	/** 处理完视角输入后的视角（绝对值） */
	FRotator ControlRotation = FRotator::ZeroRotator;

	/** 移动输入：X 向右、Y 向前 */
	FVector2D Move = FVector2D::ZeroVector;

	/** 这一帧按下了跳跃 */
	bool bJumpPressed = false;

	/** 这一帧按下了几个键盘方向键（lurch）：松开不算，手柄的摇杆没有按下沿也不算 */
	uint8 DirectionPresses = 0;

	/**
	 * 按着的键盘方向键合起来的朝向（lurch 的方向）：X 向右、Y 向前，各为 −1、0、1；同一条轴上两个反方向的键都按着时后按的那个赢（FGinkgoDirectionKeys）。
	 * 移动输入 Move 不这样合，反方向的两个键相加抵消
	 */
	FIntPoint DirectionKeys = FIntPoint::ZeroValue;

	bool bCrouchHeld = false;

	bool bSprintHeld = false;

	/** 这一帧按下了交互键：交互上一帧画在屏幕上的焦点（UGinkgoInteractorComponent） */
	bool bInteractPressed = false;

	/** 本帧换肩键的切换奇偶性：按一次换侧、同帧两次抵消，录制回放跨帧合并时也保持最终肩位 */
	bool bSwitchShoulderPressed = false;
	/** 按着开镜键：能不能开镜由武器组件定（第一批的临时规则，第 4 批换成动作状态的树） */
	bool bAimHeld = false;

	/** 这一帧按下了哪些武器的键：第一把、第二把、收枪、切枪、开火、装填 */
	EGinkgoWeaponKey WeaponKeys = EGinkgoWeaponKey::None;

	/** 按着开火键：连发的枪按着就接着打（按下沿在 WeaponKeys 里） */
	bool bFireHeld = false;

	/** 按着装填键：有弹种轮盘的枪按够了打开轮盘、松开时确认（Docs/Design/Weapons.md「切换弹种」，按下沿在 WeaponKeys 里） */
	bool bReloadHeld = false;

	/**
	 * 这一帧鼠标动的量，按灵敏度换成度（X 右、Y 上；不反转、不加速、不乘开镜）：弹种轮盘开着时拨它的指针，这时视角不转。
	 * 轮盘关着时也记（回放里轮盘开没开是算出来的）
	 */
	FVector2D WheelMouse = FVector2D::ZeroVector;

	/** 视角那根摇杆（过了视角的死区，X 右、Y 上，不反转）：轮盘开着时指着选 */
	FVector2D WheelStick = FVector2D::ZeroVector;
};
