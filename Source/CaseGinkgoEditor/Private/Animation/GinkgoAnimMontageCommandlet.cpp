#include "GinkgoAnimMontageCommandlet.h"

#include "AnimationBlueprintLibrary.h"     // 曲线的键、通知轨道与通知
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Character/GinkgoCharacter.h"     // 武器的事件名（Ginkgo::WeaponEvents）
#include "Factories/Factory.h"
#include "Interaction/GinkgoPickup.h"     // 拾取的事件名（Ginkgo::PickupEvents）
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogGinkgoAnimMontage, Log, All);

namespace GinkgoAnimMontage
{
	/** 一个事件：按曲线的边沿放一个 PlayMontageNotify */
	struct FCurveEvent
	{
		FName Event;
		FName Curve;
		/** 上升沿（第一次 ≥ 0.5 的键）或下降沿（跌到 0.5 以下之前最后一个 ≥ 0.5 的键） */
		bool bRising = true;
		/** 每一个这样的沿都发（霰弹枪一次压两发：Weapon_RoundIn 上升两次，两发之间回到 0）；默认只发第一个 */
		bool bEveryEdge = false;
	};

	struct FDefinition
	{
		FString Sequence;
		FString Montage;
		FName Slot;
		float BlendIn = 0.2f;
		float BlendOut = 0.25f;
		TArray<FCurveEvent> Events;
		/** 分段（名字、从哪一帧开始，从 1 数）：第一段从头开始，一段放完接下一段。空的就是一段到底 */
		TArray<TPair<FName, int32>> Sections;
	};

	constexpr float EdgeThreshold = 0.5f;

	/** 通知放在这条轨道上 */
	const TCHAR* const EventTrack = TEXT("Events");

	/** 在函数里建：用到运行时模块的 FName 常量，不能放进静态初始化 */
	TArray<FDefinition> MakeDefinitions()
	{
		// 收枪、拔枪：Blender 做的片段（Private/anim/Mauser98k），放在右臂的槽上（你说的：跑着切枪只动右臂，上半身不绷直，左臂还能拾取）。
		// 胳膊与枪挂的虚拟骨骼都是相对胸口的，手和枪的关系同片段；左臂、脊柱照移动的姿势，持枪的层跟着 Weapon_HoldPose 曲线让开。
		// Weapon_OnBack 为 1 的那几帧 ik_hand_gun 就在背上的插槽上。
		// 拾取：Blender 做的 Pickup_L/R（Private/anim/Pickup），放在只动那条胳膊的槽上；Item_InHand 为 1 的那几帧东西在手里：
		// 第一帧是 Grab（手碰到东西），最后一帧是 Stow（放进胸前的口袋，Docs/Design/Interaction.md「拾取子弹盒」）
		// 开火、拉栓：Blender 做的片段的叠加版（以开镜的姿势为基准），放在上半身的槽上，什么姿态都叠得上去，不盖姿势，枪也照姿态走
		// （Docs/Design/ActionStates.md「开火与拉栓：实现」）。开火第一帧就是开镜的姿势，所以不用淡入，第 17 帧起又回到它。
		// 拉栓的枪机事件在第 16、24 帧；第 18 帧枪栓在最后面，从那里分段：弹仓空了只放前一段。
		// 装填（Private/anim/Mauser98k/tools/reload_steps.py）：同样是叠加，以低持的姿势为基准。接缝是同一个姿势（手在拉到后面的拉柄上），前一步淡出、后一步淡入，
		// 两个 0.1 s 交叉。闭栓分两段：手伸过去（第 1 到 14 帧）、推回去；压完一发接着闭栓时只放后一段
		// M1911（Private/anim/M1911/README.txt）：开火、换弹同 98k 是叠加，开火的基准是开镜的姿势，换弹的是低持的；连着开时前一发的叠加还没回去，淡入一点点不跳。
		// 拔枪、收枪只动右臂，Weapon_OnBack 在这把枪上是"枪在枪套里"
		// AK-47（Private/anim/AK47/README.txt）：同 M1911，开火是开镜上的叠加，换弹是低持上的叠加。没有空仓挂机：空仓换弹的枪机是拉开再放手，Weapon_BoltOpen 的上升、下降沿各一个事件；
		// 第 57 帧起是拉机柄，卸了匣收枪、拔出来时单独放这一段
		// 泵动霰弹枪（Private/anim/PumpShotgun/README.txt）：开火是开镜上的叠加；泵动每个姿势一条（腰射的、低持的，手沿着枪走），Weapon_BoltOpen 第 8 帧上升（枪机到后面）、
		// 第 15 帧前下降（推到底），第 9 帧起是往前推：泵到一半被打断时只放后一段。装填分三步，接缝是同一个姿势（手在装填口下面）：起手、压一发或两发（一次两发时
		// Weapon_RoundIn 上升两次，两发之间回到 0，每次一个事件）、收回来
		// MG3（Private/anim/MG3/README.txt）：开火是开镜上的叠加，1200 发/分时一发 0.05 s，每发从头放（淡入同 AK 0.03 s）；放松是低持上的叠加。
		// 收不起来（你说的），没有收枪；拔枪只在捡起来以后接后一段
		return {
			// M79 中折单发榴弹发射器（Private/anim/M79/README.txt）：开火是开镜上的叠加；有弹重装填（折开高举重力甩弹）与空仓换弹（捏取退壳）都是低持上的叠加，
			// Weapon_BoltOpen 上升（折开到位）/ 下降（合膛锁死）、Weapon_MagOut 上升（高举甩出旧弹 / 捏出空壳）、Weapon_RoundChamber 上升（新弹入膛平齐）触发机构事件
			{TEXT("/Game/Developers/JiahaoLi/M79Actions/M_M79_Fire_Add"), TEXT("/Game/Weapons/M79/AM_M79_Fire"), TEXT("UpperBody"), 0.03f, 0.12f, {}, {}},
			{TEXT("/Game/Developers/JiahaoLi/M79Reload/M_M79_Reload_Tactical_Add"), TEXT("/Game/Weapons/M79/AM_M79_ReloadTactical"), TEXT("UpperBody"), 0.15f, 0.2f,
				{{Ginkgo::WeaponEvents::ActionOpen, TEXT("Weapon_BoltOpen"), true}, {Ginkgo::WeaponEvents::MagOut, TEXT("Weapon_MagOut"), true},
					{Ginkgo::WeaponEvents::RoundChamber, TEXT("Weapon_RoundChamber"), true}, {Ginkgo::WeaponEvents::ActionClose, TEXT("Weapon_BoltOpen"), false}},
				{{Ginkgo::WeaponSections::BoltOpen, 1}, {Ginkgo::WeaponSections::PortLoad, 38}, {Ginkgo::WeaponSections::BoltClose, 81}}},
			{TEXT("/Game/Developers/JiahaoLi/M79Reload/M_M79_Reload_Empty_Add"), TEXT("/Game/Weapons/M79/AM_M79_ReloadEmpty"), TEXT("UpperBody"), 0.15f, 0.2f,
				{{Ginkgo::WeaponEvents::ActionOpen, TEXT("Weapon_BoltOpen"), true}, {Ginkgo::WeaponEvents::MagOut, TEXT("Weapon_MagOut"), true},
					{Ginkgo::WeaponEvents::RoundChamber, TEXT("Weapon_RoundChamber"), true}, {Ginkgo::WeaponEvents::ActionClose, TEXT("Weapon_BoltOpen"), false}},
				{{Ginkgo::WeaponSections::BoltOpen, 1}, {Ginkgo::WeaponSections::Extract, 20}, {Ginkgo::WeaponSections::PortLoad, 35}, {Ginkgo::WeaponSections::BoltClose, 79}}},
			{TEXT("/Game/Developers/JiahaoLi/M79Actions/M_M79_Inspect_Add"), TEXT("/Game/Weapons/M79/AM_M79_Inspect"), TEXT("UpperBody"), 0.15f, 0.2f, {}, {}},
			{TEXT("/Game/Developers/JiahaoLi/M79Actions/M_M79_Idle_Relax_Add"), TEXT("/Game/Weapons/M79/AM_M79_IdleRelax"), TEXT("UpperBody"), 0.25f, 0.25f, {}, {}},
			{TEXT("/Game/Developers/JiahaoLi/M79Actions/M_M79_Stow"), TEXT("/Game/Weapons/M79/AM_M79_Stow"), TEXT("ArmR"), 0.2f, 0.25f,
				{{Ginkgo::WeaponEvents::OnBack, TEXT("Weapon_OnBack"), true}}, {}},
			{TEXT("/Game/Developers/JiahaoLi/M79Actions/M_M79_Draw"), TEXT("/Game/Weapons/M79/AM_M79_Draw"), TEXT("ArmR"), 0.2f, 0.25f,
				{{Ginkgo::WeaponEvents::InHand, TEXT("Weapon_OnBack"), false}}, {}},
			{TEXT("/Game/Developers/JiahaoLi/MG3Actions/M_MG3_Fire_Add"), TEXT("/Game/Weapons/MG3/AM_MG3_Fire"), TEXT("UpperBody"), 0.03f, 0.1f, {}, {}},
			{TEXT("/Game/Developers/JiahaoLi/MG3Actions/M_MG3_Idle_Relax_Add"), TEXT("/Game/Weapons/MG3/AM_MG3_IdleRelax"), TEXT("UpperBody"), 0.25f, 0.25f, {}, {}},
			{TEXT("/Game/Developers/JiahaoLi/MG3Actions/M_MG3_Draw"), TEXT("/Game/Weapons/MG3/AM_MG3_Draw"), TEXT("ArmR"), 0.2f, 0.25f,
				{{Ginkgo::WeaponEvents::InHand, TEXT("Weapon_OnBack"), false}}, {}},
			{TEXT("/Game/Developers/JiahaoLi/ShotgunActions/M_Shotgun_Fire_Add"), TEXT("/Game/Weapons/Shotgun/AM_Shotgun_Fire"), TEXT("UpperBody"), 0.03f, 0.1f, {}, {}},
			{TEXT("/Game/Developers/JiahaoLi/ShotgunActions/M_Shotgun_Pump_Hip_Add"), TEXT("/Game/Weapons/Shotgun/AM_Shotgun_PumpHip"), TEXT("UpperBody"), 0.1f, 0.15f,
				{{Ginkgo::WeaponEvents::ActionOpen, TEXT("Weapon_BoltOpen"), true}, {Ginkgo::WeaponEvents::ActionClose, TEXT("Weapon_BoltOpen"), false}},
				{{Ginkgo::WeaponSections::BoltOpen, 1}, {Ginkgo::WeaponSections::BoltClose, 9}}},
			{TEXT("/Game/Developers/JiahaoLi/ShotgunActions/M_Shotgun_Pump_Idle_Add"), TEXT("/Game/Weapons/Shotgun/AM_Shotgun_PumpIdle"), TEXT("UpperBody"), 0.1f, 0.15f,
				{{Ginkgo::WeaponEvents::ActionOpen, TEXT("Weapon_BoltOpen"), true}, {Ginkgo::WeaponEvents::ActionClose, TEXT("Weapon_BoltOpen"), false}},
				{{Ginkgo::WeaponSections::BoltOpen, 1}, {Ginkgo::WeaponSections::BoltClose, 9}}},
			{TEXT("/Game/Developers/JiahaoLi/ShotgunReload/M_Shotgun_Reload_Start_Add"), TEXT("/Game/Weapons/Shotgun/AM_Shotgun_ReloadStart"), TEXT("UpperBody"), 0.15f, 0.1f, {}, {}},
			{TEXT("/Game/Developers/JiahaoLi/ShotgunReload/M_Shotgun_Reload_One_Add"), TEXT("/Game/Weapons/Shotgun/AM_Shotgun_LoadOne"), TEXT("UpperBody"), 0.1f, 0.1f,
				{{Ginkgo::WeaponEvents::RoundIn, TEXT("Weapon_RoundIn"), true}}, {}},
			{TEXT("/Game/Developers/JiahaoLi/ShotgunReload/M_Shotgun_Reload_Twin_Add"), TEXT("/Game/Weapons/Shotgun/AM_Shotgun_LoadTwo"), TEXT("UpperBody"), 0.1f, 0.1f,
				{{Ginkgo::WeaponEvents::RoundIn, TEXT("Weapon_RoundIn"), true, /*bEveryEdge*/ true}}, {}},
			{TEXT("/Game/Developers/JiahaoLi/ShotgunReload/M_Shotgun_Reload_End_Add"), TEXT("/Game/Weapons/Shotgun/AM_Shotgun_ReloadEnd"), TEXT("UpperBody"), 0.1f, 0.15f, {}, {}},
			// 抛壳口装填（紧急装填、换弹种，Docs/Design/Weapons.md「切换弹种」）：低持上的叠加，三段：拉开（第 12 帧枪机到后面，膛里的出来）、取一发放进去
			// （第 46 帧 Weapon_RoundChamber 上升，这一发直接进膛）再握回护木、推回去（第 75 帧推到底）。护木已经在后面时从中间那一段开始
			{TEXT("/Game/Developers/JiahaoLi/ShotgunReload/M_Shotgun_Reload_Port_Add"), TEXT("/Game/Weapons/Shotgun/AM_Shotgun_LoadPort"), TEXT("UpperBody"), 0.15f, 0.15f,
				{{Ginkgo::WeaponEvents::ActionOpen, TEXT("Weapon_BoltOpen"), true}, {Ginkgo::WeaponEvents::RoundChamber, TEXT("Weapon_RoundChamber"), true},
					{Ginkgo::WeaponEvents::ActionClose, TEXT("Weapon_BoltOpen"), false}},
				{{Ginkgo::WeaponSections::BoltOpen, 1}, {Ginkgo::WeaponSections::PortLoad, 14}, {Ginkgo::WeaponSections::BoltClose, 70}}},
			{TEXT("/Game/Developers/JiahaoLi/ShotgunActions/M_Shotgun_Idle_Relax_Add"), TEXT("/Game/Weapons/Shotgun/AM_Shotgun_IdleRelax"), TEXT("UpperBody"), 0.25f, 0.25f, {}, {}},
			{TEXT("/Game/Developers/JiahaoLi/ShotgunActions/M_Shotgun_Stow"), TEXT("/Game/Weapons/Shotgun/AM_Shotgun_Stow"), TEXT("ArmR"), 0.2f, 0.25f,
				{{Ginkgo::WeaponEvents::OnBack, TEXT("Weapon_OnBack"), true}}, {}},
			{TEXT("/Game/Developers/JiahaoLi/ShotgunActions/M_Shotgun_Draw"), TEXT("/Game/Weapons/Shotgun/AM_Shotgun_Draw"), TEXT("ArmR"), 0.2f, 0.25f,
				{{Ginkgo::WeaponEvents::InHand, TEXT("Weapon_OnBack"), false}}, {}},
			// M4 与 AK 共用切枪和 AO；机械通知来自 M4 自己的接触曲线。
			{TEXT("/Game/Developers/JiahaoLi/M4Actions/M_M4_Fire_Add"), TEXT("/Game/Weapons/M4/AM_M4_Fire"), TEXT("UpperBody"), 0.03f, 0.1f, {}, {}},
			{TEXT("/Game/Developers/JiahaoLi/M4Actions/M_M4_FireLast_Add"), TEXT("/Game/Weapons/M4/AM_M4_FireLast"), TEXT("UpperBody"), 0.03f, 0.1f, {}, {}},
			{TEXT("/Game/Developers/JiahaoLi/M4Reload/M_M4_Reload_Tactical_Add"), TEXT("/Game/Weapons/M4/AM_M4_ReloadTactical"), TEXT("UpperBody"), 0.15f, 0.2f,
				{{Ginkgo::WeaponEvents::MagOut, TEXT("Weapon_MagOut"), true}, {Ginkgo::WeaponEvents::MagIn, TEXT("Weapon_MagIn"), true}}, {}},
			{TEXT("/Game/Developers/JiahaoLi/M4Reload/M_M4_Reload_Fast_Add"), TEXT("/Game/Weapons/M4/AM_M4_ReloadFast"), TEXT("UpperBody"), 0.15f, 0.2f,
				{{Ginkgo::WeaponEvents::MagOut, TEXT("Weapon_MagOut"), true}, {Ginkgo::WeaponEvents::MagIn, TEXT("Weapon_MagIn"), true}}, {}},
			{TEXT("/Game/Developers/JiahaoLi/M4Reload/M_M4_Reload_Empty_Add"), TEXT("/Game/Weapons/M4/AM_M4_ReloadEmpty"), TEXT("UpperBody"), 0.15f, 0.2f,
				{{Ginkgo::WeaponEvents::MagOut, TEXT("Weapon_MagOut"), true}, {Ginkgo::WeaponEvents::MagIn, TEXT("Weapon_MagIn"), true}, {Ginkgo::WeaponEvents::ActionClose, TEXT("Weapon_BoltOpen"), false}},
				{{Ginkgo::WeaponSections::Magazine, 1}, {Ginkgo::WeaponSections::SlideRelease, 57}}},
			{TEXT("/Game/Developers/JiahaoLi/M4Actions/M_M4_Idle_Relax_Add"), TEXT("/Game/Weapons/M4/AM_M4_IdleRelax"), TEXT("UpperBody"), 0.25f, 0.25f, {}, {}},
			{TEXT("/Game/Developers/JiahaoLi/M4Actions/M_M4_Inspect_Add"), TEXT("/Game/Weapons/M4/AM_M4_Inspect"), TEXT("UpperBody"), 0.15f, 0.2f, {}, {}},
			{TEXT("/Game/Developers/JiahaoLi/AK47Actions/M_AK47_Fire_Add"), TEXT("/Game/Weapons/AK47/AM_AK47_Fire"), TEXT("UpperBody"), 0.03f, 0.1f, {}, {}},
			{TEXT("/Game/Developers/JiahaoLi/AK47Reload/M_AK47_Reload_Tactical_Add"), TEXT("/Game/Weapons/AK47/AM_AK47_ReloadTactical"), TEXT("UpperBody"), 0.15f, 0.2f,
				{{Ginkgo::WeaponEvents::MagOut, TEXT("Weapon_MagOut"), true}, {Ginkgo::WeaponEvents::MagIn, TEXT("Weapon_MagIn"), true}}, {}},
			{TEXT("/Game/Developers/JiahaoLi/AK47Reload/M_AK47_Reload_Empty_Add"), TEXT("/Game/Weapons/AK47/AM_AK47_ReloadEmpty"), TEXT("UpperBody"), 0.15f, 0.2f,
				{{Ginkgo::WeaponEvents::MagOut, TEXT("Weapon_MagOut"), true}, {Ginkgo::WeaponEvents::MagIn, TEXT("Weapon_MagIn"), true},
					{Ginkgo::WeaponEvents::ActionOpen, TEXT("Weapon_BoltOpen"), true}, {Ginkgo::WeaponEvents::ActionClose, TEXT("Weapon_BoltOpen"), false}},
				{{Ginkgo::WeaponSections::Magazine, 1}, {Ginkgo::WeaponSections::SlideRelease, 57}}},
			{TEXT("/Game/Developers/JiahaoLi/AK47Actions/M_AK47_Idle_Relax_Add"), TEXT("/Game/Weapons/AK47/AM_AK47_IdleRelax"), TEXT("UpperBody"), 0.25f, 0.25f, {}, {}},
			{TEXT("/Game/Developers/JiahaoLi/AK47Actions/M_AK47_Stow"), TEXT("/Game/Weapons/AK47/AM_AK47_Stow"), TEXT("ArmR"), 0.2f, 0.25f,
				{{Ginkgo::WeaponEvents::OnBack, TEXT("Weapon_OnBack"), true}}, {}},
			{TEXT("/Game/Developers/JiahaoLi/AK47Actions/M_AK47_Draw"), TEXT("/Game/Weapons/AK47/AM_AK47_Draw"), TEXT("ArmR"), 0.2f, 0.25f,
				{{Ginkgo::WeaponEvents::InHand, TEXT("Weapon_OnBack"), false}}, {}},
			{TEXT("/Game/Developers/JiahaoLi/M1911Actions/M_M1911_Fire_Add"), TEXT("/Game/Weapons/M1911/AM_M1911_Fire"), TEXT("UpperBody"), 0.03f, 0.1f, {}, {}},
			{TEXT("/Game/Developers/JiahaoLi/M1911Actions/M_M1911_FireLast_Add"), TEXT("/Game/Weapons/M1911/AM_M1911_FireLast"), TEXT("UpperBody"), 0.03f, 0.1f, {}, {}},
			{TEXT("/Game/Developers/JiahaoLi/M1911Reload/M_M1911_Reload_Tactical_Add"), TEXT("/Game/Weapons/M1911/AM_M1911_ReloadTactical"), TEXT("UpperBody"), 0.15f, 0.2f,
				{{Ginkgo::WeaponEvents::MagOut, TEXT("Weapon_MagOut"), true}, {Ginkgo::WeaponEvents::MagIn, TEXT("Weapon_MagIn"), true}}, {}},
			{TEXT("/Game/Developers/JiahaoLi/M1911Reload/M_M1911_Reload_Empty_Add"), TEXT("/Game/Weapons/M1911/AM_M1911_ReloadEmpty"), TEXT("UpperBody"), 0.15f, 0.2f,
				{{Ginkgo::WeaponEvents::MagOut, TEXT("Weapon_MagOut"), true}, {Ginkgo::WeaponEvents::MagIn, TEXT("Weapon_MagIn"), true}, {Ginkgo::WeaponEvents::ActionClose, TEXT("Weapon_BoltOpen"), false}},
				{{Ginkgo::WeaponSections::Magazine, 1}, {Ginkgo::WeaponSections::SlideRelease, 43}}},
			{TEXT("/Game/Developers/JiahaoLi/M1911Actions/M_M1911_Idle_Relax_Add"), TEXT("/Game/Weapons/M1911/AM_M1911_IdleRelax"), TEXT("UpperBody"), 0.25f, 0.25f, {}, {}},
			{TEXT("/Game/Developers/JiahaoLi/M1911Actions/M_M1911_Stow"), TEXT("/Game/Weapons/M1911/AM_M1911_Stow"), TEXT("ArmR"), 0.2f, 0.25f,
				{{Ginkgo::WeaponEvents::OnBack, TEXT("Weapon_OnBack"), true}}, {}},
			{TEXT("/Game/Developers/JiahaoLi/M1911Actions/M_M1911_Draw"), TEXT("/Game/Weapons/M1911/AM_M1911_Draw"), TEXT("ArmR"), 0.2f, 0.25f,
				{{Ginkgo::WeaponEvents::InHand, TEXT("Weapon_OnBack"), false}}, {}},
			{TEXT("/Game/Developers/JiahaoLi/Mauser98kReload/M_Mauser98k_ReloadOpen_Add"), TEXT("/Game/Weapons/Mauser98k/AM_Mauser98k_ReloadOpen"), TEXT("UpperBody"), 0.15f, 0.1f,
				{{Ginkgo::WeaponEvents::ActionOpen, TEXT("Weapon_BoltOpen"), true}}, {}},
			{TEXT("/Game/Developers/JiahaoLi/Mauser98kReload/M_Mauser98k_ReloadReach_Add"), TEXT("/Game/Weapons/Mauser98k/AM_Mauser98k_ReloadReach"), TEXT("UpperBody"), 0.15f, 0.1f, {}, {}},
			{TEXT("/Game/Developers/JiahaoLi/Mauser98kReload/M_Mauser98k_ReloadLoad_Add"), TEXT("/Game/Weapons/Mauser98k/AM_Mauser98k_ReloadLoad"), TEXT("UpperBody"), 0.1f, 0.1f,
				{{Ginkgo::WeaponEvents::RoundIn, TEXT("Weapon_RoundIn"), true}}, {}},
			{TEXT("/Game/Developers/JiahaoLi/Mauser98kReload/M_Mauser98k_ReloadReachClose_Add"), TEXT("/Game/Weapons/Mauser98k/AM_Mauser98k_ReloadClose"), TEXT("UpperBody"), 0.1f, 0.15f,
				{{Ginkgo::WeaponEvents::ActionClose, TEXT("Weapon_BoltOpen"), false}},
				{{Ginkgo::WeaponSections::Reach, 1}, {Ginkgo::WeaponSections::BoltClose, 14}}},
			{TEXT("/Game/Developers/JiahaoLi/Mauser98kFire/M_Mauser98k_Fire_Add"), TEXT("/Game/Weapons/Mauser98k/AM_Mauser98k_Fire"), TEXT("UpperBody"), 0.f, 0.1f, {}, {}},
			{TEXT("/Game/Developers/JiahaoLi/Mauser98kFire/M_Mauser98k_BoltCycle_Add"), TEXT("/Game/Weapons/Mauser98k/AM_Mauser98k_BoltCycle"), TEXT("UpperBody"), 0.15f, 0.15f,
				{{Ginkgo::WeaponEvents::ActionOpen, TEXT("Weapon_BoltOpen"), true}, {Ginkgo::WeaponEvents::ActionClose, TEXT("Weapon_BoltOpen"), false}},
				{{Ginkgo::WeaponSections::BoltOpen, 1}, {Ginkgo::WeaponSections::BoltClose, 18}}},
			{TEXT("/Game/Developers/JiahaoLi/Mauser98kStow/M_Mauser98k_Stow"), TEXT("/Game/Weapons/Mauser98k/AM_Mauser98k_Stow"), TEXT("ArmR"), 0.2f, 0.25f,
				{{Ginkgo::WeaponEvents::OnBack, TEXT("Weapon_OnBack"), true}}, {}},
			{TEXT("/Game/Developers/JiahaoLi/Mauser98kStow/M_Mauser98k_Draw"), TEXT("/Game/Weapons/Mauser98k/AM_Mauser98k_Draw"), TEXT("ArmR"), 0.2f, 0.25f,
				{{Ginkgo::WeaponEvents::InHand, TEXT("Weapon_OnBack"), false}}, {}},
			{TEXT("/Game/Developers/JiahaoLi/Pickup/M_Pickup_L"), TEXT("/Game/Pickups/Animations/AM_Pickup_L"), TEXT("ArmL"), 0.2f, 0.25f,
				{{Ginkgo::PickupEvents::Grab, TEXT("Item_InHand"), true}, {Ginkgo::PickupEvents::Stow, TEXT("Item_InHand"), false}}, {}},
			{TEXT("/Game/Developers/JiahaoLi/Pickup/M_Pickup_R"), TEXT("/Game/Pickups/Animations/AM_Pickup_R"), TEXT("ArmR"), 0.2f, 0.25f,
				{{Ginkgo::PickupEvents::Grab, TEXT("Item_InHand"), true}, {Ginkgo::PickupEvents::Stow, TEXT("Item_InHand"), false}}, {}},
		};
	}

	/** 曲线的边沿落在哪些键上（bEveryEdge 为假时只要第一个）；找不到时是空的 */
	TArray<float> FindEdges(UAnimSequence& Sequence, const FCurveEvent& Event)
	{
		TArray<float> Times;
		TArray<float> Values;
		TArray<float> Edges;
		UAnimationBlueprintLibrary::GetFloatKeys(&Sequence, Event.Curve, Times, Values);
		for (int32 Index = 1; Index < Values.Num(); ++Index)
		{
			const bool bWasHigh = Values[Index - 1] >= EdgeThreshold;
			const bool bIsHigh = Values[Index] >= EdgeThreshold;
			if (Event.bRising && !bWasHigh && bIsHigh)
			{
				Edges.Add(Times[Index]);
			}
			if (!Event.bRising && bWasHigh && !bIsHigh)
			{
				Edges.Add(Times[Index - 1]);
			}
			if (!Event.bEveryEdge && !Edges.IsEmpty())
			{
				break;
			}
		}
		return Edges;
	}

	bool SavePackage(UPackage& Package, UObject& Asset)
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(Package.GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(&Package, &Asset, *Filename, SaveArgs);
	}

	/**
	 * 用编辑器自己的蒙太奇工厂建：段、分段、长度都是它设好的。工厂类没有导出，按类路径找、经 UFactory 的虚函数调用，源序列经反射设
	 */
	UAnimMontage* CreateMontage(UAnimSequence& Sequence, const FString& MontagePath)
	{
		UClass* FactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/UnrealEd.AnimMontageFactory"));
		FObjectProperty* SourceProperty = FactoryClass ? FindFProperty<FObjectProperty>(FactoryClass, TEXT("SourceAnimation")) : nullptr;
		if (!SourceProperty)
		{
			UE_LOG(LogGinkgoAnimMontage, Error, TEXT("找不到 AnimMontageFactory 或它的 SourceAnimation"));
			return nullptr;
		}
		UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
		SourceProperty->SetObjectPropertyValue_InContainer(Factory, &Sequence);

		const FString PackageName = FPackageName::ObjectPathToPackageName(MontagePath);
		const FName AssetName(*FPackageName::GetShortName(PackageName));
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		// 已有的挪开，在同一个包里重新建：段、通知都按定义重做
		if (UObject* Existing = StaticFindObject(UObject::StaticClass(), Package, *AssetName.ToString()))
		{
			Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			Existing->ClearFlags(RF_Public | RF_Standalone);
		}
		UAnimMontage* Montage = Cast<UAnimMontage>(Factory->FactoryCreateNew(UAnimMontage::StaticClass(), Package, AssetName,
			RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
		if (Montage)
		{
			FAssetRegistryModule::AssetCreated(Montage);
		}
		return Montage;
	}

	/** @return 有没有问题 */
	bool Build(const FDefinition& Definition)
	{
		UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *Definition.Sequence);
		if (!Sequence)
		{
			UE_LOG(LogGinkgoAnimMontage, Error, TEXT("读不到序列 %s"), *Definition.Sequence);
			return true;
		}
		UAnimMontage* Montage = CreateMontage(*Sequence, Definition.Montage);
		if (!Montage || Montage->SlotAnimTracks.IsEmpty())
		{
			UE_LOG(LogGinkgoAnimMontage, Error, TEXT("建不了蒙太奇 %s"), *Definition.Montage);
			return true;
		}

		Montage->SlotAnimTracks[0].SlotName = Definition.Slot;
		Montage->BlendIn.SetBlendTime(Definition.BlendIn);
		Montage->BlendOut.SetBlendTime(Definition.BlendOut);
		Montage->bEnableAutoBlendOut = true;
		// 自动淡出的触发时间写明（同淡出时长）：不写（-1）时引擎的淡出时长是触发那一帧剩下的时间（FAnimMontageInstance::Advance），
		// 那一帧越过触发点多少就短多少。几步接起来时（装填）后一步淡入满 0.1 s，前一步淡出只有 0.083 s，上半身的槽总比重在接缝上掉到 0.845：
		// 叠加的姿势退回去 15%，左手握枪、右手轨迹的比重也跟着漏。写明了淡出总是满的，两步的比重加起来是 1；触发的时刻不变
		Montage->BlendOutTriggerTime = Definition.BlendOut;
		if (!Definition.Sections.IsEmpty())
		{
			// 工厂建好的第一段改成第一个名字；一段放完接下一段要写明，默认是停
			const double FrameRate = Sequence->GetSamplingFrameRate().AsDecimal();
			Montage->CompositeSections[0].SectionName = Definition.Sections[0].Key;
			for (int32 Index = 1; Index < Definition.Sections.Num(); ++Index)
			{
				const int32 Section = Montage->AddAnimCompositeSection(Definition.Sections[Index].Key, static_cast<float>((Definition.Sections[Index].Value - 1) / FrameRate));
				Montage->CompositeSections[Section > 0 ? Section - 1 : 0].NextSectionName = Definition.Sections[Index].Key;
			}
		}

		UClass* NotifyClass = FindObject<UClass>(nullptr, TEXT("/Script/AnimGraphRuntime.AnimNotify_PlayMontageNotify"));
		FNameProperty* NameProperty = NotifyClass ? FindFProperty<FNameProperty>(NotifyClass, TEXT("NotifyName")) : nullptr;
		if (!NameProperty)
		{
			UE_LOG(LogGinkgoAnimMontage, Error, TEXT("找不到 AnimNotify_PlayMontageNotify 或它的 NotifyName"));
			return true;
		}
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Montage, EventTrack);

		bool bProblems = false;
		const float Length = Montage->GetPlayLength();
		// 自动淡出从结尾前一个淡出时长处开始（BlendOutTriggerTime 为负时）：那之后的通知在完成之后才到
		const float BlendOutStart = Length - (Montage->BlendOutTriggerTime >= 0.f ? Montage->BlendOutTriggerTime : Definition.BlendOut);
		for (const FCurveEvent& Event : Definition.Events)
		{
			const TArray<float> Edges = FindEdges(*Sequence, Event);
			if (Edges.IsEmpty())
			{
				UE_LOG(LogGinkgoAnimMontage, Error, TEXT("%s：曲线 %s 里找不到%s沿"), *Definition.Montage, *Event.Curve.ToString(), Event.bRising ? TEXT("上升") : TEXT("下降"));
				bProblems = true;
				continue;
			}
			for (const float Time : Edges)
			{
				UAnimNotify* Notify = UAnimationBlueprintLibrary::AddAnimationNotifyEvent(Montage, EventTrack, Time, NotifyClass);
				if (!Notify)
				{
					UE_LOG(LogGinkgoAnimMontage, Error, TEXT("%s：加不上通知 %s"), *Definition.Montage, *Event.Event.ToString());
					bProblems = true;
					continue;
				}
				NameProperty->SetPropertyValue_InContainer(Notify, Event.Event);
				// 分支点：在推进蒙太奇的当下发出，早于 NativeUpdateAnimation，事件里改的东西这一帧就用上
				for (FAnimNotifyEvent& NotifyEvent : Montage->Notifies)
				{
					if (NotifyEvent.Notify == Notify)
					{
						NotifyEvent.MontageTickType = EMontageNotifyTickType::BranchingPoint;
					}
				}
				const bool bInBlendOut = Time > BlendOutStart;
				bProblems |= bInBlendOut;
				UE_LOG(LogGinkgoAnimMontage, Display, TEXT("%s：事件 %s 在 %.4f s（曲线 %s 的%s沿，第 %d 帧），自动淡出从 %.4f s 开始%s"),
					*FPackageName::GetShortName(Definition.Montage), *Event.Event.ToString(), Time, *Event.Curve.ToString(), Event.bRising ? TEXT("上升") : TEXT("下降"),
					FMath::RoundToInt(Time * Sequence->GetSamplingFrameRate().AsDecimal()) + 1, BlendOutStart, bInBlendOut ? TEXT("：事件落在淡出里了") : TEXT(""));
			}
		}
		Montage->RefreshCacheData();
		Montage->PostEditChange();
		Montage->MarkPackageDirty();
		if (!SavePackage(*Montage->GetPackage(), *Montage))
		{
			UE_LOG(LogGinkgoAnimMontage, Error, TEXT("存不了 %s"), *Definition.Montage);
			return true;
		}
		UE_LOG(LogGinkgoAnimMontage, Display, TEXT("%s：%.4f s，槽 %s，淡入 %.2f s、淡出 %.2f s，%d 个通知"), *Definition.Montage, Length,
			*Definition.Slot.ToString(), Definition.BlendIn, Definition.BlendOut, Montage->Notifies.Num());
		return bProblems;
	}
}

UGinkgoAnimMontageCommandlet::UGinkgoAnimMontageCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UGinkgoAnimMontageCommandlet::Main(const FString& Params)
{
	using namespace GinkgoAnimMontage;
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> Values;
	ParseCommandLine(*Params, Tokens, Switches, Values);
	const FString* Only = Values.Find(TEXT("Only"));
	// -Prefix=AM_M1911：只生成这把枪的，别的蒙太奇文件不动
	const FString* Prefix = Values.Find(TEXT("Prefix"));

	bool bProblems = false;
	int32 Built = 0;
	for (const FDefinition& Definition : MakeDefinitions())
	{
		if ((Only && FPackageName::GetShortName(Definition.Montage) != *Only) || (Prefix && !FPackageName::GetShortName(Definition.Montage).StartsWith(*Prefix)))
		{
			continue;
		}
		bProblems |= Build(Definition);
		++Built;
	}
	UE_LOG(LogGinkgoAnimMontage, Display, TEXT("生成了 %d 个蒙太奇%s"), Built, bProblems ? TEXT("，有问题，见上面") : TEXT(""));
	return bProblems ? 1 : 0;
}
