#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "GinkgoWeaponStateTreeCommandlet.generated.h"

/**
 * @class UGinkgoWeaponStateTreeCommandlet : 武器的动作状态的树
 * 生成根树与子树（Docs/Design/ActionStates.md「树」），都在 /Game/WeaponStates 下，进库（LFS）。
 *
 * 生成：UnrealEditor-Cmd.exe CaseGinkgo.uproject -run=GinkgoWeaponStateTree [-Force]
 * 把枪机的子树与片段填进枪的数据资产：加 -Weapon=/Game/Weapons/Mauser98k/DA_Mauser98k；再加 -SaveAs=<包路径> 时不动原来的，存一份副本（先在副本上量）。
 * 第一版用命令生成，之后可以在编辑器里手改：已有的树不动，加 -Force 才按这里的定义重建（手改的就没了）。打出每棵树的状态；编译不过时返回 1。
 *
 * 为什么写成 commandlet：没法点编辑器。引擎的测试也是这样在代码里搭树（UStateTreeEditorData 加编译器）。
 */
UCLASS()
class UGinkgoWeaponStateTreeCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UGinkgoWeaponStateTreeCommandlet();

	virtual int32 Main(const FString& Params) override;
};
