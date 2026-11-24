#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "GinkgoAnimBlueprintCommandlet.generated.h"

/**
 * @class UGinkgoAnimBlueprintCommandlet : 生成角色的动画蓝图
 * 从代码搭出 /Game/Characters/Ginkgo/ABP_GinkgoCharacter（父类 UGinkgoAnimInstance）：
 * - 动画图：移动状态机 → 蒙太奇槽（上半身 UpperBody 分层、全身 DefaultSlot）→ 朝向修正 → 步幅修正 → 混合（Dead Blending）→
 *   根骨骼的偏航补偿 → 输出，修正节点的值读动画实例的成员。混合在修正之后，换片段、换状态时混的是修正过的姿势；
 * - 移动状态机：Idle、原地转身、起步、循环、急停、反向、空中，滑铲的进入、滑行、起身，每个状态一个序列播放器或求值器，绑定动画实例的节点函数，
 *   转换规则读动画实例算好的条件，全部发惯性化的请求；
 * - 类默认值里按 GASP 的名字填好各步态、各方向的片段（Neutral 的那套）。
 * 生成一次，之后在编辑器里改：资产已经在时不覆盖，加 -Force 才重新生成（编辑器里手改的会丢）。-Package= 生成到别的路径，
 * 试图的改法时不碰手改过的那份。设计见 Docs/Design/AnimationBlueprint.md。
 *
 *   UnrealEditor-Cmd.exe CaseGinkgo.uproject -run=GinkgoAnimBlueprint [-Force] [-Package=/Game/Developers/ABP_Test]
 *
 * -AddSlots 不重新生成，只在已有的那份（-Package，默认手改的那份）里把蒙太奇槽插在状态机与它原来的下游之间，别的节点不动；
 * 原文件先备份到 Saved/Ginkgo/Backup，已经有槽节点的不插：
 *
 *   UnrealEditor-Cmd.exe CaseGinkgo.uproject -run=GinkgoAnimBlueprint -AddSlots [-Package=...]
 *
 * -AddLean、-AddControlRig 同样只插左右倾斜、角色的 Control Rig（腿部 IK；没有时先生成 /Game/Characters/Ginkgo/CR_GinkgoCharacter，
 * -ControlRig= 换路径），-From= 时先从那份复制到 -Package= 再插：
 *
 *   UnrealEditor-Cmd.exe CaseGinkgo.uproject -run=GinkgoAnimBlueprint -AddControlRig [-Package=...] [-From=...] [-ControlRig=...]
 *
 * -AddWeaponLayers 先生成武器的层接口 ALI_GinkgoWeaponLayers、步枪的层 ABP_GinkgoWeaponLayers_Rifle 与 98k 的数据资产 DA_Mauser98k
 * （已经有的不动），再在那份里实现层接口（自己的实现原样输出，徒手时用），把 UpperBodyPose 插在状态机之后、AimOffset 插在混合（Dead Blending）之后
 * （Docs/Design/Weapons.md「动画」），同样先备份、-From= 时先复制：
 *
 *   UnrealEditor-Cmd.exe CaseGinkgo.uproject -run=GinkgoAnimBlueprint -AddWeaponLayers [-Package=...] [-From=...]
 *
 * -FixUpperBodyCurves 只把上半身槽那个分层混合的曲线改成移动那份优先（早先 -AddSlots 插的是默认的 Override，
 * 上半身的蒙太奇放着时腿部 IK 的曲线被乘小），同样先备份、-From= 时先复制：
 *
 *   UnrealEditor-Cmd.exe CaseGinkgo.uproject -run=GinkgoAnimBlueprint -FixUpperBodyCurves [-Package=...] [-From=...]
 *
 * -AddHoldLoops 给已有的步枪的层加上三个持枪循环（低持的姿势按地速混到跑的循环，再按冲刺进度换到冲刺的循环；开镜的循环代替开镜的姿势，
 * Docs/Design/Weapons.md「持枪循环」），98k 的数据资产填上三个循环、量好冲刺的握法；两个资产都先备份，层里已经有的循环不加，主蓝图不动：
 *
 *   UnrealEditor-Cmd.exe CaseGinkgo.uproject -run=GinkgoAnimBlueprint -AddHoldLoops
 *
 * -AddPeek 在已有动画图的转根之前插入参考捕获和左右网格空间探身叠加；先备份原资产，已存在捕获节点时不重复插入。
 * -CompileOnly 只编译当前 -Package 对应的动画蓝图并报告诊断，不改图、不保存。
 * -KeepLocomotionActive 仅开启现有 DefaultSlot 的源姿势持续更新，编译并保存；全身动作覆盖时仍推进底层 Locomotion。
 * -DumpAnimGraph 从输出节点往上游打出动画图（查插的节点在不在通往输出的那条路上）。
 *
 * 片段由导入命令（GinkgoAnimationImport）生成，要先导入。编译有错误或警告、缺片段时返回 1。
 */
UCLASS()
class UGinkgoAnimBlueprintCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UGinkgoAnimBlueprintCommandlet();

	virtual int32 Main(const FString& Params) override;
};
