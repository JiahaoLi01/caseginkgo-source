#pragma once

#include "CoreMinimal.h"
#include "Interaction/GinkgoPickup.h"
#include "Weapon/GinkgoWeaponAttachment.h"
#include "Weapon/GinkgoWeaponMechanism.h"
#include "GinkgoWeaponPickup.generated.h"

class UGinkgoWeaponDefinition;
class UPoseableMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * @class AGinkgoWeaponPickup : 地上的枪（Docs/Design/Interaction.md「丢枪与捡枪」）
 * 复制的 Actor：哪把枪（数据资产）、它丢下时的机构状态（子弹、膛、枪机、弹匣，原样）与已装配件。按 G 丢的、捡枪时腾出的、敌人死亡掉落的都是它；
 * 关卡里可以直接摆，调试命令 ginkgo.weapon.drop 也是它。没有状态的（摆的）捡起来同新枪，装满、带上后备。
 *
 * - 样子：骨骼网格按状态摆（枪机、弹匣、弹仓顶上那一发），静态网格只管碰撞。丢出去时物理抛落（Launch），服务器算，位置复制给各端。
 * - 只能右手捡（你说的）：手上有枪时先收枪（同一种槽位满了时另外腾地方，见 Ginkgo::Weapon::PlanWeaponPickup），右手空了才伸手。
 * - 永远放动画（你说的）：走开、离地都不中止，够不着时手伸向它的方向，抓住那一帧枪收到手里。手要到的点是持枪时右手在枪上的位置。
 * - 抓住以后枪在手上：接拔枪片段的后半段，落到低持（UGinkgoWeaponComponent::FinishWeaponPickup）。
 */
UCLASS()
class CASEGINKGO_API AGinkgoWeaponPickup : public AGinkgoPickup
{
	GENERATED_BODY()

public:
	AGinkgoWeaponPickup();

	UGinkgoWeaponDefinition* GetWeapon() const { return Weapon; }

	/** @brief 换成哪把枪：网格、手抓的点与提示里的名字跟着换。只在服务器上，或在生成时（FinishSpawning 之前）设 */
	void SetWeapon(UGinkgoWeaponDefinition* InWeapon);

	/** 丢下这把枪时已经安装的配件，捡回时随枪转入新槽 */
	void SetAttachments(const FGinkgoInstalledAttachments& Installed);

	/** @brief 这把枪现在的机构状态（丢下时的）。只在服务器上，或在生成时设 */
	void SetMechanismState(const FGinkgoMechanismState& State);

	/** 丢下时的机构状态；摆在关卡里的没有 */
	const FGinkgoMechanismState* GetMechanismState() const { return bHasMechanismState ? &MechanismState : nullptr; }

	/**
	 * @brief 丢出去：开始物理模拟。只在服务器上
	 * @param Velocity - 线速度（cm/s）
	 * @param AngularVelocity - 角速度（度/秒）
	 */
	void Launch(const FVector& Velocity, const FVector& AngularVelocity);

	/**
	 * @brief 放在地上的姿势：最长的一边水平、朝 Direction，最薄的一边竖直（枪侧躺），网格的最低点贴着 Ground
	 * @param Mesh - 哪把枪
	 * @param Ground - 地面上的点
	 * @param Direction - 枪身朝哪（水平）
	 */
	static FTransform MakeLyingTransform(const UStaticMesh& Mesh, const FVector& Ground, const FVector& Direction);

	//~ Begin AGinkgoPickup
	virtual EGinkgoInteractionAvailability GetLocalAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const override;
	virtual void BeginLocalInteract(const FGinkgoInteractionContext& Context) override;
	virtual void OnInteractResult(const FGinkgoInteractionContext& Context, EGinkgoInteractionResult Result) override;
	//~ End AGinkgoPickup

	//~ Begin AActor
	/** 编辑器里摆放、改属性时预览网格；生成时也走这里 */
	virtual void OnConstruction(const FTransform& Transform) override;
	/** 关卡里摆的在运行时不再走 OnConstruction，这里再换一遍 */
	virtual void PostInitializeComponents() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor

protected:
	//~ Begin AGinkgoPickup
	virtual void GrantLocal(AGinkgoCharacter& Character) override;
	virtual EGinkgoInteractionAvailability GetPickupAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const override;
	virtual void OnServerClaimed(const FGinkgoInteractionContext& Context) override;
	virtual TOptional<EGinkgoHand> GetRequiredHand() const override { return EGinkgoHand::Right; }
	virtual bool AbortsWhenInterrupted() const override { return false; }
	virtual bool GrantsAtGrab() const override { return true; }
	//~ End AGinkgoPickup

private:
	/** 网格换上这把枪的，手抓的点放到持枪时右手的位置，提示里写它的名字 */
	void ApplyWeapon();

	/** 骨骼网格按机构状态摆：枪机、弹匣、弹仓顶上那一发 */
	void ApplyMechanismPose();

	/** 丢出去的枪的碰撞：只同世界碰，不挡人 */
	void ApplyLaunchedCollision();

	UFUNCTION()
	void OnRep_Weapon();
	UFUNCTION()
	void RefreshAttachmentMeshes();

	UFUNCTION()
	void OnRep_MechanismState();

	UFUNCTION()
	void OnRep_Launched();

	/** 物理睡了：枪落定了，记一条日志（路线看落在哪、落了多久） */
	UFUNCTION()
	void OnMeshSettled(UPrimitiveComponent* Component, FName BoneName);

	/** 哪把枪；空着时用设置里开局的那把 */
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_Weapon, Category = "Weapon")
	TObjectPtr<UGinkgoWeaponDefinition> Weapon;

	/** 丢下时的机构状态 */
	UPROPERTY(ReplicatedUsing = OnRep_MechanismState)
	FGinkgoMechanismState MechanismState;

	/** 有丢下时的状态：捡起来照它，不再装满、不再送后备 */
	UPROPERTY(ReplicatedUsing = OnRep_MechanismState)
	bool bHasMechanismState = false;

	/** 丢出去的（物理抛落）；摆的、调试命令放的不是 */
	UPROPERTY(ReplicatedUsing = OnRep_Launched)
	bool bLaunched = false;

	/** 看得见的枪：骨骼网格，按状态摆 */
	UPROPERTY(VisibleAnywhere, Category = "Weapon")
	TObjectPtr<UPoseableMeshComponent> GunPose;

	/** 随枪复制的已装配件；服务器占领拾取物时转入新持有者的槽，不跟随原角色清槽而丢失。 */
	UPROPERTY(ReplicatedUsing = RefreshAttachmentMeshes)
	FGinkgoInstalledAttachments InstalledAttachments;

	/** 拾取物当前显示的配件网格，由本 Actor 持有，配件复制或武器模型改变时刷新。 */
	UPROPERTY(Transient)
	FGinkgoAttachmentMeshes AttachmentMeshes;

	/** 枪机开着时看得见的弹仓顶上那一发 */
	UPROPERTY(VisibleAnywhere, Category = "Weapon")
	TObjectPtr<UStaticMeshComponent> FeedRound;

	/** 丢出去时的质量：弹匣、膛里有没有弹差得不多，不细算 */
	UPROPERTY(EditAnywhere, Category = "Weapon", meta = (ClampMin = 0, Units = "kg"))
	float LaunchedMass = 4.f;

	/** 丢出去的世界时间 */
	double LaunchTime = 0.0;
};
