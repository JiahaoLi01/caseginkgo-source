#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "GinkgoCombatCommandlet.generated.h"

/**
 * @class UGinkgoCombatCommandlet : 伤害的数据（Docs/Design/Damage.md）
 * -Ammo：生成五种子弹 /Game/Weapons/Ammo/DA_Ammo_*，数照 Damage.md「子弹的数」。之后可以在编辑器里手改：已有的不动，加 -Force 才按这里的数重写（原地改，引用不断）。
 *
 * 生成：UnrealEditor-Cmd.exe CaseGinkgo.uproject -run=GinkgoCombat -Ammo [-Force]
 */
UCLASS()
class UGinkgoCombatCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UGinkgoCombatCommandlet();

	virtual int32 Main(const FString& Params) override;
};
