#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "GinkgoWeaponFxCommandlet.generated.h"

/**
 * @class UGinkgoWeaponFxCommandlet : 枪口的火光、烟与细烟
 * 生成 Niagara 系统 /Game/Weapons/FX/NS_MuzzleBlast、NS_MuzzleWisp（Docs/Design/ActionStates.md「后坐」）、NS_MuzzleHeat（Docs/Design/Weapons.md「枪管的热」）、NS_BulletTrail（「弹道的烟」）。
 * 之后可以在编辑器里手改：已有的不动，加 -Force 才按这里的定义重建。
 *
 * 生成：UnrealEditor-Cmd.exe CaseGinkgo.uproject -run=GinkgoWeaponFx [-Force]
 * -Rounds：只给子弹、弹壳的网格加碰撞（round_ue_import.py 之后跑一次）
 * -Heat：只重建 NS_MuzzleHeat（材质 M_HeatHaze 由 Private/anim/Mauser98k/tools/heat_haze_ue.py 先做好）
 * -Trail：只重建 NS_BulletTrail，弹道的烟（Docs/Design/Weapons.md「弹道的烟」；材质 M_BulletTrail 由 Private/anim/Mauser98k/tools/bullet_trail_ue.py 先做好）
 * -Impacts：重建墙体扬尘、碎屑和角色血雾；先用 Scripts/Assets/bullet_impact_decals.py 生成贴花和碎屑材质。
 * -SmokeOwnerScale：已有的 NS_MuzzleBlast 上，烟的三个发射器开 Apply Owner Scale To Attributes（精灵的大小跟着枪上的 MuzzleSmokeScale），别的不动
 * 贴图与材质由 Private/anim/Mauser98k/tools/muzzle_fx_ue.py 先做好。
 *
 * 用引擎的轻量（Stateless）发射器：一次性的爆发不用脚本，模块的参数就是属性。它的类都在 Niagara 的 Internal 头文件里，项目的模块包含不到，所以按反射改属性；
 * 属性的类型（分布）在公开的头文件里。从引擎的模板 DirectionalBurstLightweight 复制，同编辑器里新建系统选模板
 * 细烟（NS_MuzzleWisp）是例外：轻量发射器只有局部空间，飘带要留在世界里，所以用普通发射器，从引擎的模板 Fountain 复制
 */
UCLASS()
class UGinkgoWeaponFxCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UGinkgoWeaponFxCommandlet();

	virtual int32 Main(const FString& Params) override;
};
