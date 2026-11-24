#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * @namespace FWeaponTags : 武器状态标签
 * 武器系统每帧覆盖写进相机上下文组件；开镜修改器读 Weapon.ADS。导出方式同 FMovementTags。
 */
namespace FWeaponTags
{
	extern CASEGINKGO_API FNativeGameplayTag Weapon_ADS;

	/** 相机上下文的连续量：开镜进度，两头缓过（FGinkgoHoldState::GetAdsAlpha），武器组件每帧写 */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Value_AdsAlpha;

	/** 相机上下文的连续量：手上这把枪的开镜缩放倍率（定义的 AdsZoom），武器组件每帧写；徒手时不写，留着最后一把枪的 */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Value_AdsZoom;

	/**
	 * 相机上下文的连续量：手上这把枪的拉机柄扳起来、往后拉了多少（0–1，枪栓这一帧的样子，扳起与拉后取大的），角色摆完枪栓时写（本地控制的角色）。
	 * 第一人称开镜时拉栓，手到眼前几厘米：相机按它让眼睛离开瞄具（Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）
	 */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Value_SightClearance;

	/** 相机上下文的事件：开了一枪。强度的正负是这一枪往哪边晃（±1，每枪随机） */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Event_Fire;

	/** 相机上下文的事件：枪的一下操作（拉栓的几个时刻，枪的 CueCameraShakes），事件上带着这一下的相机震动 */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Event_Handling;

	// ===== 枪的声音（Docs/Design/ActionStates.md「声音」）：枪上配哪个时刻放什么 =====
	/** 开了一枪 */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Cue_Fire;
	/** 拉机柄开始抬 */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Cue_BoltLift;
	/** 枪栓拉到底 */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Cue_BoltPull;
	/** 枪栓推到底 */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Cue_BoltPush;
	/** 拉机柄压到底 */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Cue_BoltPress;
	/** 压进去一发 */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Cue_RoundIn;
	/** 弹匣卡榫松开，弹匣开始出来 */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Cue_MagOut;
	/** 弹匣到位 */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Cue_MagIn;
	/** 挂住的套筒放回去，到位 */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Cue_SlideHome;

	// ===== 动作状态（Docs/Design/ActionStates.md） =====

	/** 根树里链接子树的那个状态的标签：按手上的枪换子树（FStateTreeReferenceOverrides 按它找） */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_State_Action;

	/** 一步：动作状态的任务（FGinkgoWeaponStepTask）按它找这把枪的片段 */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Step_Draw;
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Step_Stow;
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Step_Fire;
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Step_DryFire;
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Step_CycleBolt;
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Step_CloseBolt;
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Step_OpenBolt;
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Step_ReloadStart;
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Step_LoadRound;
	/** 最后一发开火的片段：枪机留在后面（M1911）。没配的枪开火都用 Weapon_Step_Fire 的 */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Step_FireLast;
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Step_ReloadTactical;
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Step_ReloadEmpty;
	/** 站了一阵放一次的放松：枪配了这一步的片段才有 */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Step_IdleRelax;
	/** 一次压两发（霰弹枪）：压一发（LoadRound）时管里空位、后备都够两发就换成它的片段，枪配了才有 */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Step_LoadRoundTwin;
	/** 压完收回来（管状弹仓）：手从装填口回到护木上，枪放回低持 */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Step_ReloadEnd;
	/** 低持时的拉栓、泵动：拉栓（CycleBolt）时枪在低持就换成它的片段，枪配了才有（霰弹枪每个姿势一条，手沿着枪走） */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Step_CycleBoltLowered;
	/** 从抛壳口直接压一发进膛（霰弹枪的紧急装填，换弹种也走它）：枪机开到底、膛里那一发出来，放一发进去，推回去。机构允许直接压膛的枪才有 */
	extern CASEGINKGO_API FNativeGameplayTag Weapon_Step_LoadChamber;

	// ===== 弹种 =====
	// 机构里每一发记着弹种，后备子弹按弹种记数（Docs/Design/Weapons.md「机构」）。父标签是口径：不同口径的弹不会混着记，
	// 以后的特殊弹是同一口径下的另一个子标签

	extern CASEGINKGO_API FNativeGameplayTag Ammo_Mauser792x57_Standard;
	extern CASEGINKGO_API FNativeGameplayTag Ammo_ACP45_Standard;
	extern CASEGINKGO_API FNativeGameplayTag Ammo_M43762x39_Standard;
	extern CASEGINKGO_API FNativeGameplayTag Ammo_Gauge12_Buckshot;
	extern CASEGINKGO_API FNativeGameplayTag Ammo_NATO762x51_Ball;

	/** M4 的普通 5.56×45 弹，独立后备与弹种目录，不与 7.62 枪混用 */
	extern CASEGINKGO_API FNativeGameplayTag Ammo_NATO556x45_Ball;
	/** 特殊弹（Docs/Design/Weapons.md「弹种」）：12 号独头弹、7.92 × 57 的 S.m.K. 钢芯穿甲弹 */
	extern CASEGINKGO_API FNativeGameplayTag Ammo_Gauge12_Slug;
	extern CASEGINKGO_API FNativeGameplayTag Ammo_Mauser792x57_SmK;

	/** M79 的 40 × 46mm M406 低速高爆榴弹，抛射物投送并碰炸范围伤害 */
	extern CASEGINKGO_API FNativeGameplayTag Ammo_Grenade40x46_HE;

	/** M79 的 40 × 46mm 碰炸闪光弹，高速抛射物投送，触碰即触发与手投闪光弹一致的爆闪致盲 */
	extern CASEGINKGO_API FNativeGameplayTag Ammo_Grenade40x46_Flash;

	/** M79 的 40 × 46mm XM576 钢镖霰弹，与普通霰弹一致，单次发射 16 枚射线检测钢镖 */
	extern CASEGINKGO_API FNativeGameplayTag Ammo_Grenade40x46_Flechette;
}
