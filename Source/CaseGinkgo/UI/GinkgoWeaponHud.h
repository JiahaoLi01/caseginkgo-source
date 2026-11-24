#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"      // ELevelTick
#include "UObject/StrongObjectPtr.h"     // 持有 weft 的 View：weft 的子系统只记弱引用
#include "WeftTypes.h"                   // FWeftAnchor

class APlayerController;
class AGinkgoPlayerController;
class SWeftViewport;
class UGameViewportClient;
class UGinkgoWeaponComponent;
class UWeftView;
class UWorld;
struct FGinkgoHitConfirm;

/**
 * @enum EGinkgoAimSource : 不开镜的点这一帧跟着谁
 */
enum class EGinkgoAimSource : uint8
{
	/** 枪在手上：瞄准方向（不开镜时是视线后面拖的弹簧，Docs/Design/Weapons.md「腰射操作性」） */
	Aim,
	/** 徒手、枪在背上：屏幕中心 */
	View,
};

/**
 * @struct FGinkgoHudAim : 这一帧瞄准的记号
 * 准星（不开镜的点、开镜准星）画的都是预期方向：玩家想打哪里，不管前面有什么。子弹实际打到哪里是另一个记号：半透明的红 X
 */
struct FGinkgoHudAim
{
	EGinkgoAimSource Source = EGinkgoAimSource::View;

	/** 预期方向上很远的一个点（世界）：不开镜的点投影它 */
	FVector Point = FVector::ZeroVector;

	/** 开镜进度（两头缓过）：点收拢、开镜准星张开，同相机拉近一起走 */
	float AdsAlpha = 0.f;

	/** 这一帧开得了枪：枪抬到位，这一步开得了枪。下面三项才有意义 */
	bool bHasHit = false;

	/** 真实的命中：这一帧开枪打到的地方（UGinkgoWeaponComponent::TraceShot） */
	FVector HitPoint = FVector::ZeroVector;

	/** 真实的命中离准星多远（1080p 高的像素）。腰射按叠加之前的视线比，画面晃、枪没晃，不算对不上；开镜子弹打这一帧画面的中心，按这一帧的画面比 */
	double HitGap = 0.0;

	/** 红 X 的缩放：对得上时是 0，不画（Ginkgo::Weapon::HitMarkScale） */
	float HitScale = 0.f;

	/** 扩散的圆锥在画面上的半径（1080p 高的像素，Ginkgo::Weapon::SpreadToPixels）：准星反馈的就是它 */
	double SpreadPixels = 0.0;

	/**
	 * SpreadRing 的腰射与越肩开镜共用锚点 10。圆环按 SpreadPixels 换算到页面尺寸，中心点只跟锚点的位置。
	 * 腰射取枪口预测点，达到开镜射击阈值后取屏幕中心；进第一人称瞄具时交给镜内分划
	 */
	bool bSpreadRing = false;

	/** 准星两边的横线的内端离中心多远（1080p 高的像素，Ginkgo::Weapon::CrosshairDashOffset）；0 是没有横线 */
	float DashOffset = 0.f;

	/** 开镜准星的两根横柱比平时多张开多少（1080p 高的像素）：圆锥比它们之间的空隙大时才张开 */
	float ReticleOpen = 0.f;

	/**
	 * 第一人称开镜（带瞄具的枪，相机滑向瞄具的后面，Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）：越肩的开镜准星不画（镜内有真的分划），
	 * 红 X 透过瞄具画（镜内的像放大了），命中的白 X 在分划的尖上
	 */
	bool bScoped = false;

	/** 红 X 画在哪（世界）：越肩时就是打到的地方；第一人称时是透过瞄具看到它的那个方向上很远的一点 */
	FVector HitDrawPoint = FVector::ZeroVector;

	/** 分划的尖（瞄具的光轴）方向上很远的一点（世界）：第一人称时命中的白 X 画在这里 */
	FVector SightPoint = FVector::ZeroVector;
};

/**
 * @class FGinkgoWeaponHud : 武器的界面：右下角的武器栏与画面中间的准星
 * 画在 weft 页面 Content/UI/WeaponHud 上，见 Docs/Design/Weapons.md「界面：武器栏与准星」。样子照《最后生还者2》：半透明的白色、小
 * （2026-10-31 你定的，不再照 Docs/Design/UIArt.md 的纸签：TPS 里不透明的 HUD 挡视线）。
 * 不是调试工具，发行版里也有。属于一个本地玩家控制器，开始时创建；weft 不画时（-nullrhi、别的 RHI）什么也不做。
 *
 * 每帧所有 Actor 更新之后（FWorldDelegates::OnWorldPostActorTick：相机已经更新过，这一帧的姿势也求完了，枪的网格跟着骨骼动过）：
 * - 准星走锚点（FindAim）。不开镜的点（半透明，画预期方向）、这把枪的开镜准星、红 X（没偏的那条线打到的地方）；两边的横线与开镜准星的横柱反馈扩散。点与开镜准星
 *   （98k 是 ZF39 三柱式分划的缩小版），缩放跟着开镜进度：点收拢成没有，开镜准星从中心张开，同相机拉近一样快。
 * - 武器栏的状态变了才发一条事件（"weapon"）：名字、口径、图标与开镜准星的名字、在手上还是背上、抬没抬起来、供弹与容量、膛、枪机、后备。
 *   页面按枪栓的状态换图，按膛里是什么画那一发。不每帧推 JSON。
 * - 打中了可打击的目标发一条 "confirm"（Docs/Design/Damage.md「命中的白 X」）：页面在准星的中心弹一下白 X，按打死、爆头、护甲换样子。
 * - 弹种轮盘（Docs/Design/Weapons.md「切换弹种」）开、关、选的变了发一条 "wheel"；轮盘在屏幕中心（锚点 9）；消息里带键位提示（确认、取消是哪个键，按打开轮盘的设备）。
 *   武器栏按每一发的弹种画格子、膛里那一发与它写的口径（"weapon" 里的 ammo、rounds、chamberAmmo、load）。
 */
class CASEGINKGO_API FGinkgoWeaponHud
{
public:
	explicit FGinkgoWeaponHud(AGinkgoPlayerController& InController);
	~FGinkgoWeaponHud();

	FGinkgoWeaponHud(const FGinkgoWeaponHud&) = delete;
	FGinkgoWeaponHud& operator=(const FGinkgoWeaponHud&) = delete;

	/**
	 * @brief 建 weft 的 View 与视口上的控件，页面在视口有了大小之后载入
	 * @return 是否建了；weft 不画、没有游戏视口时为假
	 */
	bool Show();

	/** 拿掉控件、放掉 View */
	void Hide();

	bool IsShown() const { return View.IsValid(); }

	/**
	 * @brief 这一帧瞄准的记号：预期方向（枪在手上时是瞄准方向，徒手或枪在背上时是屏幕中心）与真实的命中（枪抬到位才有，同准星对不上才画）
	 * @note 要在这一帧的姿势求完以后调：所有 Actor 更新之后。回放也调它，记下记号该在哪里
	 */
	static FGinkgoHudAim FindAim(const APlayerController& Controller);

	/**
	 * @brief 不开镜的点在画面上的位置（像素）：预期方向投影，扣掉相机的叠加（后坐的踢动、抖动、滑铲的震动都只晃画面）。枪停着时就在屏幕中心
	 * @return 在相机前面
	 */
	static bool ProjectDot(const APlayerController& Controller, const FGinkgoHudAim& Aim, FVector2D& OutScreen);

	/**
	 * @brief 红 X 在画面上的位置（像素）：真实的命中投影到这一帧的画面，不扣叠加，粘在世界里那个点上。画面晃时它跟着世界走，点不走
	 * @return 在相机前面
	 */
	static bool ProjectHit(const APlayerController& Controller, const FGinkgoHudAim& Aim, FVector2D& OutScreen);

	/** 打线最远多远（cm）：同动画实例的瞄准点 */
	static constexpr double AimTraceDistance = 10000.0;

private:
	/** 每帧所有 Actor 更新之后：载入页面、写准星的锚点，武器栏变了时发事件 */
	void OnWorldPostActorTick(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds);

	/** 武器栏这一帧的样子（JSON），同上一次发的比 */
	FString BuildWeaponMessage() const;

	/** 一枪的命中（JSON）：打中几下、打死没有、爆头没有、打在护甲上没有、护甲打穿没有、扣的血与护甲 */
	static FString BuildConfirmMessage(const FGinkgoHitConfirm& Confirm);

	/** 弹种轮盘（JSON，Docs/Design/Weapons.md「切换弹种」）：开着没有、上一次怎么关的、选第几项、打开时装的第几项，每一项的名字、画哪种子弹、说明、后备，确认与取消的键 */
	static FString BuildWheelMessage(const UGinkgoWeaponComponent& Weapons, const APlayerController& PlayerController);

	/** 所属本地控制器，持有普通 HUD 容器；弱引用避免延长玩家生命周期。 */
	TWeakObjectPtr<AGinkgoPlayerController> Controller;

	TStrongObjectPtr<UWeftView> View;

	TSharedPtr<SWeftViewport> Widget;

	TWeakObjectPtr<UGameViewportClient> ViewportClient;

	FDelegateHandle PostActorTickHandle;

	bool bLoaded = false;

	/** 上一次发给页面的武器栏：一样的不再发 */
	FString LastMessage;

	/** 上一次发给页面的命中是第几次（FGinkgoHitConfirm::Serial）；INDEX_NONE 是页面刚载入，只记下现在的，不补放以前的 */
	int32 LastConfirmSerial = INDEX_NONE;

	/** 上一次发给页面的弹种轮盘（FGinkgoAmmoWheel::Serial）；INDEX_NONE 是页面刚载入，发一次现在的 */
	int32 LastWheelSerial = INDEX_NONE;

	/** 这一帧的锚点（复用，不每帧分配） */
	TArray<FWeftAnchor> Anchors;
};
