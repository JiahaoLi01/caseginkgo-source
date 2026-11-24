#include "Weapon/GinkgoWeaponDefinition.h"

#include "Character/GinkgoCharacter.h"  // 武器动画的接触事件名
#include "Sound/SoundBase.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"

FGinkgoInstalledAttachments UGinkgoWeaponDefinition::GetDefaultAttachments() const
{
	FGinkgoInstalledAttachments Result;
	for (const FGinkgoWeaponAttachmentMount& Mount : Attachments)
	{
		if (Mount.bDefault && Mount.Part) { Result.Parts.Add(Mount.Part); }
	}
	return Result;
}

FGinkgoInstalledAttachments UGinkgoWeaponDefinition::WithAttachment(const FGinkgoInstalledAttachments& Installed, UGinkgoWeaponAttachment* Part) const
{
	FGinkgoInstalledAttachments Result = Installed;
	const FGinkgoWeaponAttachmentMount* Mount = FindAttachment(Part);
	if (!Mount) { return Result; }
	Result.Parts.RemoveAll([this, Mount](const UGinkgoWeaponAttachment* Old)
	{
		const auto* Other = FindAttachment(Old);
		return !Other || Other->Mount == Mount->Mount;
	});
	Result.Parts.Add(Part);
	return Result;
}

bool UGinkgoWeaponDefinition::CanInstall(const FGinkgoInstalledAttachments& Installed, const UGinkgoWeaponAttachment* Part) const
{
	const FGinkgoWeaponAttachmentMount* Mount = FindAttachment(Part);
	if (!Mount || Installed.Parts.Contains(Part)) { return false; }
	for (const UGinkgoWeaponAttachment* Old : Installed.Parts)
	{
		const auto* Other = FindAttachment(Old);
		if (!Other) { continue; }
		if (Other->Mount == Mount->Mount)
		{
			if (!Modularity) { return false; }
			continue;
		}
		if (!Part->bEmptyModule && !Old->bEmptyModule
			&& (Mount->IncompatibleMounts.Contains(Other->Mount) || Other->IncompatibleMounts.Contains(Mount->Mount)))
		{
			return false;
		}
	}
	return true;
}

UGinkgoWeaponDefinition::UGinkgoWeaponDefinition()
{
	// 踢动：低频，整个画面往上抬，约 80 ms 到顶；回弹单独配，ω 8、ζ 1 不过冲，约 0.2 s 回到一半、0.5 s 回到 10%
	FGinkgoCameraRecoil Kick;
	Kick.Name = TEXT("Kick");
	Kick.Pitch = FFloatInterval(1.9f, 2.5f);
	Kick.Omega = FFloatInterval(14.f, 14.f);
	Kick.Zeta = FFloatInterval(0.75f, 0.75f);
	Kick.bSeparateReturn = true;
	CameraRecoil.Add(Kick);

	// 抖动：高频，约 7.6 Hz，几下收住，偏航、侧倾每枪各自随机往一边，视场跳一下
	FGinkgoCameraRecoil Shake;
	Shake.Name = TEXT("Shake");
	Shake.Pitch = FFloatInterval(0.4f, 0.6f);
	Shake.Yaw = FFloatInterval(0.25f, 0.45f);
	Shake.Roll = FFloatInterval(0.6f, 1.f);
	Shake.bRandomSide = true;
	Shake.FieldOfView = FFloatInterval(1.f, 1.4f);
	Shake.Omega = FFloatInterval(50.f, 50.f);
	Shake.Zeta = FFloatInterval(0.3f, 0.3f);
	CameraRecoil.Add(Shake);

	// 拉栓的四下（2026-11-12 你说的：同《使命召唤》，拉栓时也给一些小的相机震动，不用很大）：都在 1° 以内，约 5 Hz，几下收住。
	// 拉机柄在右边：扳起时整支枪往右滚一点，压下时滚回来；拉到底撞到头往上一顿，推到底往前往下一顿
	const auto AddBoltShake = [this](const FGameplayTag& Cue, const TCHAR* Name, const FFloatInterval& Pitch, const FFloatInterval& Yaw, const FFloatInterval& Roll)
	{
		FGinkgoCameraRecoil Layer;
		Layer.Name = Name;
		Layer.Pitch = Pitch;
		Layer.Yaw = Yaw;
		Layer.Roll = Roll;
		Layer.Omega = FFloatInterval(32.f, 32.f);
		Layer.Zeta = FFloatInterval(0.4f, 0.4f);
		CueCameraShakes.FindOrAdd(Cue).Layers.Add(Layer);
	};
	AddBoltShake(FWeaponTags::Weapon_Cue_BoltLift, TEXT("BoltLift"), FFloatInterval(0.1f, 0.2f), FFloatInterval(0.1f, 0.2f), FFloatInterval(0.5f, 0.7f));
	AddBoltShake(FWeaponTags::Weapon_Cue_BoltPull, TEXT("BoltPull"), FFloatInterval(0.3f, 0.45f), FFloatInterval(0.15f, 0.25f), FFloatInterval(0.1f, 0.2f));
	AddBoltShake(FWeaponTags::Weapon_Cue_BoltPush, TEXT("BoltPush"), FFloatInterval(-0.35f, -0.25f), FFloatInterval(-0.15f, -0.05f), FFloatInterval(-0.25f, -0.15f));
	AddBoltShake(FWeaponTags::Weapon_Cue_BoltPress, TEXT("BoltPress"), FFloatInterval(-0.2f, -0.1f), FFloatInterval(-0.15f, -0.05f), FFloatInterval(-0.6f, -0.4f));

	// 短促的接触各有方向。逐发压弹最轻，弹匣卡入比抽出重；切枪在换挂点时带一下肩膀。
	const auto AddContact = [this](const FName Event, const float Pitch, const float Roll)
	{
		FGinkgoCameraRecoil& Layer = ContactCameraShakes.Add(Event);
		Layer.Name = Event;
		Layer.Pitch = FFloatInterval(Pitch, Pitch);
		Layer.Roll = FFloatInterval(Roll, Roll);
		Layer.Omega = FFloatInterval(19.f, 19.f);
		Layer.Zeta = FFloatInterval(0.7f, 0.7f);
	};
	AddContact(Ginkgo::WeaponEvents::RoundIn, -0.22f, 0.08f);
	AddContact(Ginkgo::WeaponEvents::RoundChamber, -0.15f, 0.05f);
	AddContact(Ginkgo::WeaponEvents::MagOut, -0.28f, -0.13f);
	AddContact(Ginkgo::WeaponEvents::MagIn, 0.42f, 0.18f);
	AddContact(Ginkgo::WeaponEvents::OnBack, -0.18f, -0.26f);
	AddContact(Ginkgo::WeaponEvents::InHand, 0.24f, 0.32f);

	// 子弹与弹壳（Docs/Design/ActionStates.md「子弹与弹壳」）。下面的尺寸都是 98k 的，从 reload_anim.py 换算到枪的坐标
	Rounds.LiveRound = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Weapons/Mauser98k/Rounds/SM_Mauser98k_Round.SM_Mauser98k_Round")));
	Rounds.SpentCase = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Weapons/Mauser98k/Rounds/SM_Mauser98k_Case.SM_Mauser98k_Case")));
	Rounds.EjectMaterial = TSoftObjectPtr<UPhysicalMaterial>(FSoftObjectPath(TEXT("/Game/Weapons/Mauser98k/Rounds/PM_Round_Brass.PM_Round_Brass")));
	// 片段里那一发的中间、尖在手骨里的位置（M_Mauser98k_ReloadLoad.points.json，第 22、28、34 帧一样）
	Rounds.InHand = FTransform(FRotationMatrix::MakeFromX(FVector(-12.01 + 7.94, 5.74 - 5.57, 5.54 - 5.02)).ToQuat(), FVector(-7.94, 5.57, 5.02));
	Rounds.FeedTop = FVector(19.78, 0.0, 5.45);
	Rounds.ChamberRise = 0.88f;
	// 枪栓拉到底时弹壳的中心：膛里的位置退 9.45 cm。抛壳挺在机匣左后方，壳往右上飞；从托弹板的位置往右上 45–75° 不碰机匣和瞄准镜（probe_port.py）
	Rounds.EjectOffset = FVector(18.53, 0.0, 6.33);
	Rounds.EjectDirection = FVector(-0.2, 0.64, 0.74);
	Rounds.HitSoundHard = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Weapons/Mauser98k/Audio/MS_Mauser98k_ShellHard.MS_Mauser98k_ShellHard")));
	Rounds.HitSoundSoft = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Weapons/Mauser98k/Audio/MS_Mauser98k_ShellSoft.MS_Mauser98k_ShellSoft")));

	// 声音（Docs/Design/ActionStates.md「声音」）：开枪是 SVD 的枪声顶替（你说比 98k 的有力量）。提前量是样本切好以后的撞击点（Private/audio/tools/split_weapon_audio.py）
	const auto AddCue = [this](const FGameplayTag& Tag, const FString& Name, const float Volume, const float Lead)
	{
		FGinkgoWeaponCue Cue;
		Cue.Sound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(FString::Printf(TEXT("/Game/Weapons/Mauser98k/Audio/MS_Mauser98k_%s.MS_Mauser98k_%s"), *Name, *Name)));
		Cue.Volume = Volume;
		Cue.Lead = Lead;
		Cues.Add(Tag, Cue);
	};
	AddCue(FWeaponTags::Weapon_Cue_Fire, TEXT("Fire"), 0.8f, 0.f);
	AddCue(FWeaponTags::Weapon_Cue_BoltLift, TEXT("BoltLift"), 1.f, 0.011f);
	AddCue(FWeaponTags::Weapon_Cue_BoltPull, TEXT("BoltPull"), 1.f, 0.197f);
	AddCue(FWeaponTags::Weapon_Cue_BoltPush, TEXT("BoltPush"), 1.f, 0.128f);
	AddCue(FWeaponTags::Weapon_Cue_BoltPress, TEXT("BoltPress"), 1.f, 0.1f);
	AddCue(FWeaponTags::Weapon_Cue_RoundIn, TEXT("RoundIn"), 0.7f, 0.255f);

	// 散布（Docs/Design/Weapons.md「散布」），数都是我定的：栓动步枪开镜准（100 m 上 17 cm 以内），腰射差；一枪散得多，拉完栓（约 1 s）正好收回来
	Spread.HipDefault = 1.2f;
	Spread.AdsDefault = 0.1f;
	Spread.GrowthPerShot = 1.5f;
	Spread.RecoveryRate = 1.5f;
	Spread.HipMax = 4.f;
	Spread.AdsMax = 1.5f;
	Spread.AirFactor = 2.5f;
	// 走 200 cm/s，持枪跑约 390，跑 450，冲刺 720
	FRichCurve* SpeedFactor = Spread.SpeedFactor.GetRichCurve();
	SpeedFactor->AddKey(50.f, 1.f);
	SpeedFactor->AddKey(200.f, 1.25f);
	SpeedFactor->AddKey(450.f, 1.75f);
	SpeedFactor->AddKey(720.f, 2.5f);
}

FGinkgoHoldTimings UGinkgoWeaponDefinition::GetHoldTimings() const
{
	FGinkgoHoldTimings Timings;
	Timings.RaiseTime = RaiseTime;
	Timings.LowerTime = LowerTime;
	Timings.AdsTime = AdsTime;
	Timings.HipHoldTime = HipHoldTime;
	Timings.SprintTime = SprintTime;
	return Timings;
}

FPrimaryAssetId UGinkgoWeaponDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("GinkgoWeapon"), GetFName());
}

const FGinkgoWeaponAttachmentMount* UGinkgoWeaponDefinition::FindAttachment(const UGinkgoWeaponAttachment* Part) const
{
	return Part ? Attachments.FindByPredicate([Part](const FGinkgoWeaponAttachmentMount& Entry)
	{
		return Entry.Part == Part && !Entry.Mount.IsNone();
	}) : nullptr;
}
