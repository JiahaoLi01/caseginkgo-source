#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "GinkgoAnimationImportCommandlet.generated.h"

/**
 * @class UGinkgoAnimationImportCommandlet : 动画素材的导入
 * 从 Game Animation Sample（GASP，UE 5.7）搬 UEFN 小白人的骨架、网格与动画序列。只搬基础动画：动画蓝图、通知、蒙太奇、
 * 混合空间、运动匹配的数据库与选择器都不搬；序列上的通知导入时删掉，同步标记留着。根运动按目录分类：移动类关掉、
 * 翻越与交互保留，两者都强制锁根（规则见 .cpp 的 RootMotionRules）。
 *
 * 导入：UnrealEditor-Cmd.exe CaseGinkgo.uproject -run=GinkgoAnimationImport -Source=<GASP 工程目录> [-DryRun]
 * 1. 把 GASP 的 Content 只读挂到 /GameAnimationSample/，资产注册表扫一遍，按类别挑出要搬的包（-DryRun 只列出来）
 * 2. 原样复制到本工程 Content 的同一相对路径：包之间按路径引用，路径不变就不用修引用
 * 3. 逐个加载、清理（删通知；按目录设根运动；GASP 自己的曲线压缩设置换成引擎自带的同一份；网格去掉后处理动画蓝图），存成 5.8 的格式
 *
 * 校验：UnrealEditor-Cmd.exe CaseGinkgo.uproject -run=GinkgoAnimationImport -Verify [-Shots=<目录> -AllowCommandletRendering]
 * 另起一个进程重新扫描、加载导入的包：依赖都在，加载没有警告，序列上没有通知、都用同一副骨架，压缩设置都来自引擎，根运动的设置合规则、
 * 状态机里求值时根骨骼在原点，网格没有后处理动画蓝图。
 * 带 -Shots 时再把网格与几个序列中间那一帧的画面存成 PNG（不能带 -nullrhi）。有问题时返回 1。
 *
 * 为什么写成 commandlet：一千多个序列要逐个删通知，编辑器里手工做不了；写成命令，导入可以重放，结果可以复查。
 */
UCLASS()
class UGinkgoAnimationImportCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UGinkgoAnimationImportCommandlet();

	virtual int32 Main(const FString& Params) override;
};
