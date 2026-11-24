#pragma once

#include "Animation/Actions/GinkgoAnimAction.h"
#include "GinkgoMagazineAttachmentAction.generated.h"

class AGinkgoCharacter;
class UGinkgoWeaponAttachment;
class UGinkgoWeaponDefinition;
class UPoseableMeshComponent;
class UAnimSequence;

/**
 * 复用武器战术换弹片段更换弹匣模块。旧件由整枪动画驱动，新件临时网格采样同一枪械轨道。
 * MagIn 在服务器提交配件并通知武器组件结算弹药；新容量和补弹同时生效，
 * 入位前取消保留旧件，入位后取消保留新件。已入位的预览交给模块外观等待异步网格，未入位则销毁并恢复显隐。
 * 工作台只负责编排取枪和放回；容量变化及多余弹药归还仍由武器组件处理。
 */
UCLASS()
class UGinkgoMagazineAttachmentAction : public UGinkgoAnimAction
{
	GENERATED_BODY()

public:
	/** 创建一次安装动作；开始时重新核对槽与武器，缺少片段、挂点或入位事件会中止，不修改装配 */
	static UGinkgoMagazineAttachmentAction* Make(AGinkgoCharacter& Character, int32 Slot, UGinkgoWeaponAttachment& Part);

protected:
	virtual void OnStart(FGinkgoAnimActionSetup& Setup) override;

	virtual void OnTick(FGinkgoAnimActionContext& Context, float DeltaSeconds) override;

	virtual void OnFinished(const FGinkgoAnimActionResult& InResult) override;

private:
	/** 发起安装的角色；不延长角色生命周期，动作期间退出则中止 */
	TWeakObjectPtr<AGinkgoCharacter> Character;

	/** 本次操作的武器槽，在 Make 时确定；动作期间换枪会中止 */
	int32 Slot = INDEX_NONE;

	/** 开始前的武器定义，用于检查槽身份；动作持有到结束 */
	UPROPERTY(Transient)
	TObjectPtr<UGinkgoWeaponDefinition> Weapon;

	/** 将要装入的模块，来自武器适配表；只在 MagIn 时交给武器组件提交 */
	UPROPERTY(Transient)
	TObjectPtr<UGinkgoWeaponAttachment> Part;

	/** 武器动画对应的枪械轨道；以蒙太奇时钟采样，不积累独立时间 */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Track;

	/** 本端整枪网格；只在动作有效时隐藏两根弹匣骨骼，不持有角色 */
	TWeakObjectPtr<UPoseableMeshComponent> Gun;

	/** 新弹匣的临时网格，挂在整枪上共用枪空间；入位后结束可移交外观管理器，其余情况显式销毁 */
	UPROPERTY(Transient)
	TObjectPtr<UPoseableMeshComponent> Preview;

	/** 正式弹匣骨骼，来自适配表的 ModuleBone */
	FName MagazineBone;

	/** 轨道中的替换弹匣骨骼，来自适配表的 ReloadSpareBone */
	FName SpareBone;

	/** 已走过入位事件；服务器在同一事件提交新件，之后中止不回滚已发生的换件 */
	bool bSeated = false;

	/** 开始时正式弹匣的隐藏标记，结束时原样还原，避免覆盖其他显示约束 */
	bool bMagazineWasHidden = false;

	/** 开始时替换弹匣的隐藏标记，结束时原样还原 */
	bool bSpareWasHidden = false;

	/** 用枪械轨道驱动新件；Time 为蒙太奇秒数，正式网格由现有 Anim BP 管线更新 */
	void SamplePreview(float Time);
};
