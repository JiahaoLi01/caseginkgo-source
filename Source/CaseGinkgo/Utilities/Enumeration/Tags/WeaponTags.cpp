#include "WeaponTags.h"

namespace FWeaponTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ammo_NATO556x45_Ball, "Ammo.NATO556x45.Ball", "5.56×45 普通弹，M4");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_ADS,	"Weapon.ADS",
		"开镜中（持枪的姿态是开镜）。相机：开镜覆盖 FOV 等参数，并屏蔽速度类增量。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Value_AdsAlpha,	"Weapon.Value.AdsAlpha",
		"相机上下文的连续量：开镜进度（0–1，两头缓过）。相机：开镜晃动的包络。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Value_AdsZoom,	"Weapon.Value.AdsZoom",
		"相机上下文的连续量：手上这把枪的开镜缩放倍率。相机：开镜的视场。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Value_SightClearance,	"Weapon.Value.SightClearance",
		"相机上下文的连续量：手上这把枪的拉机柄扳起来、往后拉了多少（0–1，枪栓这一帧的样子）。相机：第一人称开镜时眼睛离开瞄具多少，手不挡在眼前。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Event_Fire,	"Weapon.Event.Fire",
		"相机上下文的事件：开了一枪。相机：后坐的踢动与抖动。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Event_Handling,	"Weapon.Event.Handling",
		"相机上下文的事件：枪的一下操作（拉栓的扳起、拉到底、推到底、压下）。相机：枪配的小震动（UGinkgoWeaponDefinition::CueCameraShakes）。")

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Cue_Fire,	"Weapon.Cue.Fire",
		"枪的声音：开了一枪。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Cue_BoltLift,	"Weapon.Cue.BoltLift",
		"枪的声音：拉机柄开始抬。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Cue_BoltPull,	"Weapon.Cue.BoltPull",
		"枪的声音：枪栓拉到底。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Cue_BoltPush,	"Weapon.Cue.BoltPush",
		"枪的声音：枪栓推到底。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Cue_BoltPress,	"Weapon.Cue.BoltPress",
		"枪的声音：拉机柄压到底。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Cue_RoundIn,	"Weapon.Cue.RoundIn",
		"枪的声音：压进去一发。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Cue_MagOut,	"Weapon.Cue.MagOut",
		"枪的声音：弹匣卡榫松开，弹匣开始出来。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Cue_MagIn,	"Weapon.Cue.MagIn",
		"枪的声音：弹匣到位。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Cue_SlideHome,	"Weapon.Cue.SlideHome",
		"枪的声音：挂住的套筒放回去，到位。")

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_State_Action,	"Weapon.State.Action",
		"动作状态根树里链接子树的状态：按手上的枪换子树。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Step_Draw,	"Weapon.Step.Draw",
		"一步：从背上拔出目标槽那把枪。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Step_Stow,	"Weapon.Step.Stow",
		"一步：把手上的枪收到背上。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Step_Fire,	"Weapon.Step.Fire",
		"一步：开一枪。按下那一帧改机构、算命中，片段只是表现。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Step_DryFire,	"Weapon.Step.DryFire",
		"一步：想开火，不能响，咔一声。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Step_CycleBolt,	"Weapon.Step.CycleBolt",
		"一步：拉栓，抛壳、上膛。弹仓空了拉开就停。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Step_CloseBolt,	"Weapon.Step.CloseBolt",
		"一步：枪栓开着，推回去上膛。手不在拉柄上时先伸过去。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Step_OpenBolt,	"Weapon.Step.OpenBolt",
		"一步：装填的开栓，手留在拉柄上。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Step_ReloadStart,	"Weapon.Step.ReloadStart",
		"一步：枪栓已经开着，手伸到拉柄上（拔出来接着压）。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Step_LoadRound,	"Weapon.Step.LoadRound",
		"一步：压一发，手回到拉柄上。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Step_FireLast,	"Weapon.Step.FireLast",
		"动作状态的一步：最后一发开火的片段，枪机留在后面。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Step_ReloadTactical,	"Weapon.Step.ReloadTactical",
		"动作状态的一步：可拆弹匣，枪机关着时换弹匣。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Step_ReloadEmpty,	"Weapon.Step.ReloadEmpty",
		"动作状态的一步：可拆弹匣，打空了枪机挂在后面时换弹匣，再放枪机回去。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Step_IdleRelax,	"Weapon.Step.IdleRelax",
		"动作状态的一步：站了一阵放一次的放松，一动就打断。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Step_LoadRoundTwin,	"Weapon.Step.LoadRoundTwin",
		"一步：一次压两发（霰弹枪的竞赛装填），手回到装填口下面。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Step_ReloadEnd,	"Weapon.Step.ReloadEnd",
		"一步：压完了，手从装填口回到护木上，枪放回低持（管状弹仓）。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Step_CycleBoltLowered,	"Weapon.Step.CycleBoltLowered",
		"一步：枪在低持时的拉栓、泵动（霰弹枪压完以后上膛）。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Step_LoadChamber,	"Weapon.Step.LoadChamber",
		"一步：从抛壳口直接压一发进膛（霰弹枪的紧急装填、换弹种）：护木拉到底，膛里那一发出来，放一发进去，推回去。")

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ammo_Mauser792x57_Standard,	"Ammo.Mauser792x57.Standard",
		"7.92 × 57mm Mauser 的普通弹（98k）。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ammo_ACP45_Standard,	"Ammo.ACP45.Standard",
		"弹种：.45 ACP，M1911。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ammo_M43762x39_Standard,	"Ammo.M43762x39.Standard",
		"弹种：7.62 × 39mm M43，AK-47。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ammo_Gauge12_Buckshot,	"Ammo.Gauge12.Buckshot",
		"弹种：12 号 2 3/4 英寸 00 鹿弹，九颗弹丸，泵动霰弹枪。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ammo_NATO762x51_Ball,	"Ammo.NATO762x51.Ball",
		"弹种：7.62 × 51mm NATO 普通弹（弹链），MG3。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ammo_Gauge12_Slug,	"Ammo.Gauge12.Slug",
		"弹种：12 号 2 3/4 英寸 1 盎司独头弹（Foster 式），一颗弹头，开镜更准，泵动霰弹枪。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ammo_Mauser792x57_SmK,	"Ammo.Mauser792x57.SmK",
		"弹种：7.92 × 57mm S.m.K.（Spitzgeschoß mit Kern，钢芯尖头穿甲弹，底火一圈红漆），98k。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ammo_Grenade40x46_HE,	"Ammo.Grenade40x46.HE",
		"弹种：40 × 46mm M406 高爆榴弹，76 m/s 抛射物投送，碰炸范围伤害，M79。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ammo_Grenade40x46_Flash,	"Ammo.Grenade40x46.Flash",
		"弹种：40 × 46mm 碰炸闪光弹，82 m/s 高速抛射物投送，触碰即爆闪致盲，M79。")
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ammo_Grenade40x46_Flechette,	"Ammo.Grenade40x46.Flechette",
		"弹种：40 × 46mm XM576 钢镖霰弹，单次 16 发射线检测钢镖，M79。")
}
