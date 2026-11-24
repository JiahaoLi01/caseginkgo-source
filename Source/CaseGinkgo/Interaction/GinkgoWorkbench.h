#pragma once

#include "Animation/GinkgoAnimTypes.h"
#include "Camera/GinkgoSpring.h"
#include "GameFramework/Actor.h"
#include "Interaction/GinkgoInteractable.h"
#include "GinkgoWorkbench.generated.h"

class UCameraComponent;
class UGinkgoAnimAction;
class UGinkgoWeaponAttachment;
class UGinkgoWeaponDefinition;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class USceneComponent;
class USoundBase;
class USpotLightComponent;
class UStaticMeshComponent;

/** @struct FGinkgoWorkbenchVisit : 服务器确定的桌前角色、目标武器和正在安装的配件，各端据此播放动作 */
USTRUCT()
struct FGinkgoWorkbenchVisit
{
	GENERATED_BODY()

	/** 占用这张工作台的角色，服务器设置；为空表示当前没有访问者 */
	UPROPERTY()
	TObjectPtr<AGinkgoCharacter> Character;

	/** 桌上要放的武器槽，空桌选枪时为 INDEX_NONE */
	UPROPERTY()
	int32 Slot = INDEX_NONE;

	/** 进桌时的服务器世界时间（秒），各端用它推进角色走到 Stand 的过程 */
	UPROPERTY()
	double StartTime = 0.0;

	/** 切换时先取回的那把枪 */
	UPROPERTY()
	int32 PreviousSlot = INDEX_NONE;

	/** 服务器已接受退出请求，各端从当前放置进度取回武器、关闭台灯并切回玩家视角 */
	UPROPERTY()
	bool bExiting = false;

	/** 正在安装的配件定义，完成或取消时清空；各端据此启动和结束安装动作 */
	UPROPERTY()
	TObjectPtr<UGinkgoWeaponAttachment> InstallingPart;
};

/**
 * @class AGinkgoWorkbench : 角色开关台灯、放枪、换枪、安装配件的正式工作台交互目标
 * 根在地面，局部 +X 从站位指向桌面。关卡摆放时用 Stand 定角色的脚底和朝向，WeaponRest 定枪匠垫高度，Camera 给本地玩家看桌上的枪。
 *
 * 一次访问：
 * 1. 服务器检查站位、占用与随身武器，写 Visit。各端按同一份目标播放动作，本地玩家同时混合到桌前镜头。
 * 2. 进桌时先由左手拨开台灯照亮桌面，再把目标武器放到枪匠垫上；徒手进桌也先开灯再打开选枪菜单。
 * 3. 桌前换枪按取回、收起、拔出、放下串成 AnimAction，期间台灯保持常亮；放到接触点才换枪网格的父组件，双手随后松开。
 * 4. 安装由所选武器给出挂点。安装动作只借用桌面的变换，成功回调才在服务器写入配件；离桌时先取回武器，再拨关台灯，异常退出或销毁均复位灯光与占用。
 * 5. 桌前相机在有动作播放时拉远以呈现角色与手部操作，无动作且武器已就绪在桌面时平滑推近聚焦枪械（基本看不到人物），选中具体配件时向对应部位柔和偏移构图，并叠加轻微的手持呼吸晃动。
 *
 * Visit 复制的是目标。每端的动作进度、台灯状态、枪的实际挂点、手部 IK 与相机输入在本地维护；动作回调以 Sequence 核对是否仍是这次访问。
 */
UCLASS()
class CASEGINKGO_API AGinkgoWorkbench : public AActor, public IGinkgoInteractable
{
	GENERATED_BODY()

public:
	AGinkgoWorkbench();

	//~ Begin IGinkgoInteractable
	virtual FVector GetInteractionLocation() const override;
	virtual FGinkgoInteractionRanges GetInteractionRanges() const override;
	virtual FText GetInteractionText() const override;
	virtual EGinkgoInteractionAvailability GetAvailability(const FGinkgoInteractionContext& Context, FText& Reason) const override;
	virtual EGinkgoInteractionAvailability GetLocalAvailability(const FGinkgoInteractionContext& Context, FText& Reason) const override;
	virtual void Interact(const FGinkgoInteractionContext& Context) override;
	//~ End IGinkgoInteractable

	/** 取回桌上的枪并关闭台灯，镜头回到玩家 */
	void RequestExit(AGinkgoCharacter& Character);

	/**
	 * @brief 服务器接受桌前角色的选枪请求，开始换枪动作链
	 * @param Slot - 随身武器槽的下标，空槽、当前槽、安装或放枪期间的重复请求均忽略
	 */
	void SelectWeapon(AGinkgoCharacter& Character, int32 Slot);

	/** 本端已完成开灯与放枪（或空桌开灯结束），可以选择随身武器 */
	bool CanSelectWeapon() const;
	int32 GetSelectedSlot() const { return Visit.Slot; }
	bool IsSlotAvailable(int32 Slot) const;
	/** 从当前选择向前或向后循环，跳过空槽；没有另一把时返回 INDEX_NONE */
	int32 GetAdjacentSlot(bool bNext) const;

	/** 按武器定义的顺序找到快捷键安装的第一项，鼠标可以直接选配件 */
	UGinkgoWeaponAttachment* GetNextAttachment() const;
	bool CanInstallAttachment(const UGinkgoWeaponAttachment* Part) const;
	/**
	 * @brief 服务器开始安装。角色仍在这张桌前、枪已放稳且挂点空闲才会接受
	 * @note 动作完成时再次核对槽里的武器。中途离桌先解绑回调，再取消动作，临时网格随动作清理
	 */
	void InstallAttachment(AGinkgoCharacter& Character, UGinkgoWeaponAttachment* Part);
	const UGinkgoWeaponAttachment* GetInstallingPart() const { return Visit.InstallingPart; }

	// 游戏线程取动作进度、台灯状态和手部目标，动画线程从快照读取手的 IK。
	FGinkgoAttachmentHandPose GetInstallHandPose() const;
	float GetGunAlpha() const;
	float GetGripAlpha() const;
	const FTransform& GetRestPose() const { return RestPose; }
	bool IsExiting() const { return Visit.bExiting; }

	/** 当前端台灯开关是否处于开启状态；进桌拨动开关时置真，离桌收枪后拨回置假 */
	bool IsLampOn() const { return bLampOn; }

	/** 当前台灯照度与发光的归一化亮度（0–1），0 表示完全熄灭，1 表示稳定点亮 */
	float GetLampAlpha() const { return LampAlpha; }

	/** 当前桌前相机的聚焦进度（0–1），0 为拉远观察角色的动作机位，1 为贴近枪械的特写机位 */
	float GetCameraFocusAlpha() const { return static_cast<float>(CameraFocusSpring.Value); }

	/** 当前状态是否应聚焦枪械（桌上武器就绪且无动作播放）；有动作或未放枪时返回假，拉远以呈现角色 */
	bool IsCameraFocusingWeapon() const { return WantsFocusCamera(); }

	/** 设置当前本地菜单选中的配件，用于驱动特写相机向该部位柔和偏移；传空表示回到整枪居中特写 */
	void SetFocusedAttachment(const UGinkgoWeaponAttachment* Part);

	/** 返回当前驱动特写镜头偏置的本地选中配件；未选中或已清除时为空 */
	const UGinkgoWeaponAttachment* GetFocusedAttachment() const { return FocusedAttachment.Get(); }

	/** 返回当前已平滑的部件聚焦偏移向量（工作台局部坐标，cm），无选中或拉远时趋于零向量 */
	FVector GetCameraPartFocusOffset() const { return CameraPartOffsetSpring.Value; }

	//~ Begin AActor
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor

protected:
	//~ Begin AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor

	/** 正式改装桌主体网格（实木台面、钢架、抽屉、防滑枪匠垫与背板），承担桌面与桌体碰撞 */
	UPROPERTY(VisibleAnywhere, Category = Workbench)
	TObjectPtr<UStaticMeshComponent> WorkbenchMesh;

	/** 固定在桌面左后方的双杆弹簧台灯网格，仅作视觉与灯罩自发光表现，不参与碰撞 */
	UPROPERTY(VisibleAnywhere, Category = Workbench)
	TObjectPtr<UStaticMeshComponent> LampMesh;

	/** 台灯灯颈上的黄铜拨杆开关网格，局部原点在转轴中心，随开关状态绕轴摆动 */
	UPROPERTY(VisibleAnywhere, Category = Workbench)
	TObjectPtr<UStaticMeshComponent> LampSwitchMesh;

	/** 台灯拨杆末端的锚点组件，提供左手伸手拨动与开关音效的世界位置 */
	UPROPERTY(VisibleAnywhere, Category = Workbench)
	TObjectPtr<USceneComponent> LampSwitchTarget;

	/** 台灯灯罩内的主照明聚光灯，投射柔和拉长高光与阴影，未点亮时隐藏以零开销待机 */
	UPROPERTY(VisibleAnywhere, Category = Workbench)
	TObjectPtr<USpotLightComponent> LampLight;

	/** 桌面近玩家侧的无阴影漫反射补光，防止纯黑环境下深色枪身背光面死黑，随台灯同步启停 */
	UPROPERTY(VisibleAnywhere, Category = Workbench)
	TObjectPtr<UPointLightComponent> LampFillLight;

	/** 角色在桌前站定时的脚底位置与朝向 */
	UPROPERTY(VisibleAnywhere, Category = Workbench)
	TObjectPtr<USceneComponent> Stand;

	/** 枪放下以后的枪匠垫中心，也是放枪动作的目标 */
	UPROPERTY(VisibleAnywhere, Category = Workbench)
	TObjectPtr<USceneComponent> WeaponRest;

	/** 桌前视角，进桌和离桌时由玩家相机管理器混合到这个组件或返回角色 */
	UPROPERTY(VisibleAnywhere, Category = Workbench)
	TObjectPtr<UCameraComponent> Camera;

	/** 打开台灯时在开关位置播放的机械卡位音效；为空时静默切换 */
	UPROPERTY(EditAnywhere, Category = "Workbench|Lamp")
	TObjectPtr<USoundBase> LampOnSound;

	/** 关闭台灯时在开关位置播放的机械释放音效；为空时静默切换 */
	UPROPERTY(EditAnywhere, Category = "Workbench|Lamp")
	TObjectPtr<USoundBase> LampOffSound;

	/** 伸手拨开或拨关台灯单次动作的完整时长（秒），中途折返时按手部当前伸出进度缩放 */
	UPROPERTY(EditAnywhere, Category = "Workbench|Lamp", meta = (ClampMin = "0.15", Units = "s"))
	float LampToggleDuration = 0.50f;

	/** 拨动开关后灯丝升温点亮或余辉熄灭的过渡时长（秒） */
	UPROPERTY(EditAnywhere, Category = "Workbench|Lamp", meta = (ClampMin = "0.02", Units = "s"))
	float LampFadeDuration = 0.11f;

	/** 台灯全亮时主聚光灯的光强（坎德拉 cd），匹配项目日光曝光量级以保证暗处与亮处均清晰可读 */
	UPROPERTY(EditAnywhere, Category = "Workbench|Lamp", meta = (ClampMin = "0.0", Units = "cd"))
	float LampIntensity = 3.2f;

	/** 台灯全亮时近侧漫反射补光的光强（坎德拉 cd），为 0 时关闭暗面提亮 */
	UPROPERTY(EditAnywhere, Category = "Workbench|Lamp", meta = (ClampMin = "0.0", Units = "cd"))
	float LampFillIntensity = 0.35f;

	/** 台灯全亮时写入灯罩材质 LampGlow 参数的自发光倍率，0 表示不发光 */
	UPROPERTY(EditAnywhere, Category = "Workbench|Lamp", meta = (ClampMin = "0.0"))
	float LampEmissiveStrength = 10.0f;

	/** 从手持完整放到桌面的时长（秒），同时决定进桌时对齐站位的速度 */
	UPROPERTY(EditAnywhere, Category = Workbench, meta = (ClampMin = "0.1", Units = "s"))
	float PlaceDuration = 1.05f;

	/** 从桌面完整取回手中的时长（秒），中途折返时按剩余进度缩短 */
	UPROPERTY(EditAnywhere, Category = Workbench, meta = (ClampMin = "0.1", Units = "s"))
	float TakeDuration = 0.90f;

	/** 进桌和离桌时相机视角的混合时长（秒） */
	UPROPERTY(EditAnywhere, Category = "Workbench|Camera", meta = (ClampMin = "0.05", Units = "s"))
	float CameraBlendTime = 0.75f;

	/** 有动作播放时拉远机位相对工作台根的局部位置（cm），展示角色上半身、台灯与桌面 */
	UPROPERTY(EditAnywhere, Category = "Workbench|Camera")
	FVector ActionCameraLocation = FVector(-121.0, 62.0, 181.0);

	/** 有动作播放时拉远机位相对工作台根的注视点（cm），让整把枪和人物动作同时落在左侧画面 */
	UPROPERTY(EditAnywhere, Category = "Workbench|Camera")
	FVector ActionCameraLookAt = FVector(-4.0, 26.0, 95.0);

	/** 有动作播放时拉远机位的水平视场角（度） */
	UPROPERTY(EditAnywhere, Category = "Workbench|Camera", meta = (ClampMin = "35.0", ClampMax = "110.0", Units = "Degrees"))
	float ActionCameraFov = 76.f;

	/** 无动作播放时特写机位相对工作台根的基准局部位置（cm），越过角色肩部以 3/4 俯角聚焦枪械，人物出画且避开灯罩遮挡 */
	UPROPERTY(EditAnywhere, Category = "Workbench|Camera")
	FVector FocusCameraLocation = FVector(-66.0, 26.0, 174.0);

	/** 无动作播放时特写机位相对工作台根的基准注视点（cm），按当前武器长度围绕 WeaponRest 等比缩放以避开右侧菜单 */
	UPROPERTY(EditAnywhere, Category = "Workbench|Camera")
	FVector FocusCameraLookAt = FVector(-14.0, 15.5, 92.0);

	/** 无动作播放时特写机位的水平视场角（度） */
	UPROPERTY(EditAnywhere, Category = "Workbench|Camera", meta = (ClampMin = "35.0", ClampMax = "110.0", Units = "Degrees"))
	float FocusCameraFov = 65.f;

	/** 无动作时向枪械特写平滑推进的临界阻尼弹簧角频率（rad/s），越小推镜越沉稳 */
	UPROPERTY(EditAnywhere, Category = "Workbench|Camera", meta = (ClampMin = "0.5", ForceUnits = "rad/s"))
	float CameraFocusInOmega = 4.2f;

	/** 动作触发时向角色拉远机位回撤的临界阻尼弹簧角频率（rad/s），高于推镜以利索切出角色动作 */
	UPROPERTY(EditAnywhere, Category = "Workbench|Camera", meta = (ClampMin = "0.5", ForceUnits = "rad/s"))
	float CameraPullBackOmega = 9.5f;

	/** 模拟手持相机的轻微呼吸与漂移晃动倍率，0 表示关闭晃动，1 为默认轻微手持感 */
	UPROPERTY(EditAnywhere, Category = "Workbench|Camera", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float HandheldSwayScale = 1.0f;

	/** 选中某个配件时，特写注视点向该部位中心偏移的比例（0 不偏移，1 完全居中该部件，默认 0.34 兼顾部件突出与整枪全貌） */
	UPROPERTY(EditAnywhere, Category = "Workbench|Camera", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FocusPartLookBias = 0.34f;

	/** 选中某个配件时，特写机位沿桌面平面向该部位跟随偏移的比例（小于注视点偏移以产生自然的轻转视角与视差） */
	UPROPERTY(EditAnywhere, Category = "Workbench|Camera", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FocusPartEyeBias = 0.22f;

	/** 选中某个配件时，特写机位沿视线轻微推近的比例（0 不额外推近，默认 0.08 在整枪特写上再靠近约 8%） */
	UPROPERTY(EditAnywhere, Category = "Workbench|Camera", meta = (ClampMin = "0.0", ClampMax = "0.35"))
	float FocusPartZoomIn = 0.08f;

	/** 切换选中配件时镜头偏置与微推近过渡的临界阻尼弹簧角频率（rad/s） */
	UPROPERTY(EditAnywhere, Category = "Workbench|Camera", meta = (ClampMin = "0.5", ForceUnits = "rad/s"))
	float CameraPartFocusOmega = 5.2f;

private:
	/** 服务器确定的访问目标，复制后由 ApplyVisit 衔接这一端的角色、武器与动作 */
	UPROPERTY(ReplicatedUsing = OnRep_Visit)
	FGinkgoWorkbenchVisit Visit;

	UFUNCTION()
	void OnRep_Visit();

	/** 服务器写入或客户端收到 Visit 时，按本地已播到的位置衔接动作 */
	void ApplyVisit();

	/** 接近桌面时移动到 Stand、推进台灯亮度过渡；服务器发现持有者、武器或站位失效时结束访问 */
	void UpdatePresentation(float DeltaSeconds);
	void StartSequence();
	void StartInstall();
	/** 先解除回调与本次访问的关联，再取消执行器里的动作 */
	void CancelSequence();
	UGinkgoAnimAction* MakeLampAction(bool bTurnOn);
	UGinkgoAnimAction* MakeTransfer(float From, float To);
	UGinkgoAnimAction* MakeEquipStep(int32 Slot, bool bDraw);

	/** 更新左手伸向台灯开关的归一化进度（0–1）与拨动压下量，写入 LampHandPose 供动画线程采样 */
	void SetLampReach(float Progress, float PressPulse = 0.f);

	/** 切换台灯目标开关状态；bImmediate 为真时当帧跳到目标亮度且不播开关音效 */
	void SetLampState(bool bInOn, bool bImmediate);

	/** 按当前 LampAlpha 同步聚光灯、漫反射补光、拨杆角度与灯罩自发光材质 */
	void RefreshLampVisuals();

	/** 判断当前是否处于无动作的枪械检视态（桌上武器就绪且无取放/开关灯/安装动作进行） */
	bool WantsFocusCamera() const;

	/** 按当前桌上武器的有效长度求特写机位围绕 WeaponRest 的距离缩放倍率（手枪拉近、重机枪适度放宽） */
	double ComputeFocusDistanceScale() const;

	/** 求当前选中配件相对 WeaponRest 的工作台局部空间偏移（cm），未选中或非特写态返回零向量 */
	FVector ComputeFocusedPartOffset() const;

	/** 每帧推进桌前相机在拉远与聚焦之间的弹簧过渡，并叠加与帧率无关的轻微手持晃动 */
	void UpdateCamera(float DeltaSeconds);

	/** 将桌前相机复位到默认拉远动作机位并清空弹簧与晃动状态；BeginPlay 与 Release 调用 */
	void ResetCamera();

	/** 放枪从 0 走到 1，取枪反向走；跨过接触点时才在手和桌之间换挂点 */
	void SetProgress(float Value);
	void PrepareSlot(int32 Slot);
	void Release();
	void FinishVisit();
	double Now() const;

	/** 台灯与拨杆共用的动态材质实例，BeginPlay 或首次刷新时按需创建以驱动 LampGlow */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> LampMaterial;

	/** 这一端正在呈现的访问者，首次 ApplyVisit 时设置，Release 时清空 */
	TWeakObjectPtr<AGinkgoCharacter> PresentedCharacter;

	/** 此端观察工作台的本地玩家控制器，Release 用它恢复观察输入并切回玩家视角 */
	TWeakObjectPtr<APlayerController> Viewer;

	/** 进桌时控制角色的控制器，服务器发现它改变或失效时结束访问 */
	TWeakObjectPtr<AController> EnteringController;

	/** 此端正在展示的武器定义，取放与切枪过程中可暂时不同于 Visit.Slot 的目标武器 */
	TWeakObjectPtr<UGinkgoWeaponDefinition> PresentedWeapon;

	/** 此端正在展示的武器槽，空桌时为 INDEX_NONE，PrepareSlot 时与 PresentedWeapon 一起更新 */
	int32 PresentedSlot = INDEX_NONE;

	/** 进桌前角色的世界变换，接近桌面时从这里混合到 Stand */
	FTransform ApproachStart;

	/** 当前武器平放在桌面的世界变换，PrepareSlot 按网格包围盒和握点求出，枪网格与动画共用 */
	FTransform RestPose;

	/** 拨台灯阶段游戏线程采样的左手与腰部目标姿势，不拨灯时为零值，Release 时清空 */
	FGinkgoAttachmentHandPose LampHandPose;

	/** 左手伸向台灯开关的连续进度（0–1），0 在待机/护木处、1 在拨杆处；中途取消时从当前进度平滑收回 */
	float LampReachProgress = 0.f;

	/** 台灯开关的目标状态；进桌拨动触点时变真，离桌收完武器拨回或 Release 时变假 */
	bool bLampOn = false;

	/** 台灯当前归一化亮度（0–1），由 UpdatePresentation 向 bLampOn 平滑推进，Release 时归零 */
	float LampAlpha = 0.f;

	/** 当前武器已挂到桌面，SetProgress 跨过接触点时切换，Release 据此把枪还到角色手中 */
	bool bOnTable = false;

	/** 本次进桌的站位对齐已经推进到终点，Release 时复位 */
	bool bAligned = false;

	/** 当前枪的放置进度（0–1），退出时从这个值开始取回，允许在放枪中途折返 */
	float TransferProgress = 0.f;

	/** 开灯与取放动作链已完成或停在已点亮的空桌，CanSelectWeapon 结合访问状态判断能否开始新的选择 */
	bool bReady = false;

	/** 本端上次处理过的安装目标，与 Visit.InstallingPart 比较以识别开始、完成或取消 */
	TWeakObjectPtr<UGinkgoWeaponAttachment> PresentedInstall;

	/** 本端已处理退出请求，用来识别 Visit.bExiting 第一次变为真 */
	bool bPresentedExit = false;

	/** 本端上次处理过的目标槽，与 Visit.Slot 比较以衔接新的切枪动作；空桌为 INDEX_NONE */
	int32 LocalSelection = INDEX_NONE;

	/** 本端正在执行的开关灯、取放动作链或安装动作，取消前先清空以使旧回调失效 */
	TWeakObjectPtr<UGinkgoAnimAction> Sequence;

	/** 本次访问曾禁用角色移动，Release 据此恢复 Walking；模拟代理为假 */
	bool bMovementLocked = false;

	/** 拉远动作机位（0）与枪械特写机位（1）之间的临界阻尼弹簧状态，由 UpdateCamera 每帧推进，Release 时复位 */
	TGinkgoCriticalSpring<double> CameraFocusSpring;

	/** 当前武器特写距离缩放倍率的临界阻尼弹簧状态，切换长短武器时平滑衔接，Release 时复位 */
	TGinkgoCriticalSpring<double> CameraSpanSpring;

	/** 手持相机晃动的淡入淡出包络（0–1），进桌时缓入、离桌与 Release 时归零，避免切镜首帧突变 */
	TGinkgoCriticalSpring<double> CameraSwayEnvelopeSpring;

	/** 桌前手持相机晃动的连续时钟（秒），由 UpdateCamera 按 DeltaSeconds 累加，Release 时清零 */
	double CameraSwayClock = 0.0;

	/** 本地菜单当前高亮选中的配件弱引用，仅影响本端特写相机偏置，不参与网络复制；切枪或 Release 时清空 */
	TWeakObjectPtr<const UGinkgoWeaponAttachment> FocusedAttachment;

	/** 选中配件相对 WeaponRest 的平滑局部偏移向量（cm），由 UpdateCamera 按 CameraPartFocusOmega 推进，Release 时清零 */
	TGinkgoCriticalSpring<FVector> CameraPartOffsetSpring;

	/** 选中配件时轻微推近权重（0–1）的临界阻尼弹簧状态，未选或离桌时归零 */
	TGinkgoCriticalSpring<double> CameraPartZoomSpring;
};
