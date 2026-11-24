#include "Combat/GinkgoCombatTypes.h"

float FGinkgoBodyPartMultipliers::Get(const EGinkgoBodyPart Part) const
{
	switch (Part)
	{
	case EGinkgoBodyPart::Head:
		return Head;
	case EGinkgoBodyPart::UpperChest:
		return UpperChest;
	case EGinkgoBodyPart::Abdomen:
		return Abdomen;
	case EGinkgoBodyPart::UpperLimbs:
		return UpperLimbs;
	case EGinkgoBodyPart::Legs:
		return Legs;
	case EGinkgoBodyPart::Feet:
		return Feet;
	default:
		return 1.f;
	}
}

const FGinkgoHitSound* FGinkgoHitSounds::Get(const EGinkgoHitSound Kind) const
{
	switch (Kind)
	{
	case EGinkgoHitSound::Normal:
		return &Normal;
	case EGinkgoHitSound::Armor:
		return &Armor;
	case EGinkgoHitSound::ArmorBreak:
		return &ArmorBreak;
	case EGinkgoHitSound::Head:
		return &Head;
	case EGinkgoHitSound::Kill:
		return &Kill;
	case EGinkgoHitSound::HeadKill:
		return &HeadKill;
	default:
		return nullptr;
	}
}

namespace Ginkgo::Combat
{
	float FalloffMultiplier(const FGinkgoDamageFalloff& Falloff, const float Distance)
	{
		if (Falloff.MinRange <= Falloff.FullRange || Distance <= Falloff.FullRange)
		{
			return 1.f;
		}
		const float Alpha = FMath::Clamp((Distance - Falloff.FullRange) / (Falloff.MinRange - Falloff.FullRange), 0.f, 1.f);
		return FMath::Lerp(1.f, Falloff.MinMultiplier, Alpha);
	}

	FGinkgoDamageOutcome ApplyHitDamage(const FGinkgoHitDamage& Hit, float& InOutArmor)
	{
		FGinkgoDamageOutcome Outcome;
		const float Health = FMath::Max(Hit.Health, 0.f);
		if (InOutArmor <= 0.f)
		{
			InOutArmor = 0.f;
			Outcome.HealthDamage = Health;
			return Outcome;
		}
		Outcome.bArmorHit = true;
		const float Penetration = FMath::Clamp(Hit.Penetration, 0.f, 1.f);
		const float Blocked = 1.f - Penetration;
		// 穿甲的那一份直接扣血；挡的那一份扣护甲
		Outcome.HealthDamage = Health * Penetration;
		const float ArmorWanted = FMath::Max(Hit.Armor, 0.f) * Blocked;
		if (ArmorWanted < InOutArmor)
		{
			Outcome.ArmorDamage = ArmorWanted;
			InOutArmor -= ArmorWanted;
			return Outcome;
		}
		// 这一下打穿：剩下的护甲挡得住 InOutArmor / ArmorWanted 这么多，没挡住的那一份接着扣血
		const float Absorbed = ArmorWanted > 0.f ? InOutArmor / ArmorWanted : 1.f;
		Outcome.ArmorDamage = InOutArmor;
		Outcome.bArmorBroken = true;
		Outcome.HealthDamage += Health * Blocked * (1.f - Absorbed);
		InOutArmor = 0.f;
		return Outcome;
	}

	float GetPenetrationLeft(const float Remaining, const float Thickness)
	{
		if (Thickness < 0.f)
		{
			return 0.f;
		}
		// 刚好用完（差不到 UE_KINDA_SMALL_NUMBER）也停在里面：AK 的 0.6 穿过一个人剩 0.3，再碰到一个人（0.3）不该按浮点的舍入一会儿穿过去、一会儿停下
		const float Left = Remaining - Thickness;
		return Left > UE_KINDA_SMALL_NUMBER ? Left : 0.f;
	}

	void TracePenetration(const FHitResult& First, const float Coefficient, const TFunctionRef<float(const FHitResult&)> GetThickness,
		const TFunctionRef<bool(const FHitResult& Through, FHitResult& OutNext)> TraceOn, FGinkgoPenetrationPath& OutPath)
	{
		OutPath.Reset();
		float Remaining = FMath::Max(Coefficient, 0.f);
		FHitResult Next = First;
		for (;;)
		{
			FGinkgoPenetrationHit& Entry = OutPath.AddDefaulted_GetRef();
			Entry.Hit = Next;
			Entry.Thickness = GetThickness(Entry.Hit);
			Entry.Remaining = Remaining;
			// 第一个挨满的；穿过东西以后挨剩的系数那么多倍（你说的），系数大过 1 的只多穿、不多打
			Entry.DamageScale = OutPath.Num() == 1 ? 1.f : FMath::Min(Remaining, 1.f);
			const float Left = GetPenetrationLeft(Remaining, Entry.Thickness);
			// 满 MaxPenetrationHits 个就停在这一个里面（你定的上限，数组是定长的）
			Entry.bPassed = Left > 0.f && OutPath.Num() < MaxPenetrationHits;
			if (!Entry.bPassed || !TraceOn(Entry.Hit, Next))
			{
				return;
			}
			Remaining = Left;
		}
	}

	EGinkgoBodyPart FindBodyPart(FName Bone, const TMap<FName, EGinkgoBodyPart>& Parts, const EGinkgoBodyPart Default, TFunctionRef<FName(FName)> GetParent)
	{
		// 骨骼树不会比这深：防一个坏的父骨骼表绕圈
		for (int32 Depth = 0; Depth < 64 && !Bone.IsNone(); ++Depth)
		{
			if (const EGinkgoBodyPart* Part = Parts.Find(Bone))
			{
				return *Part;
			}
			Bone = GetParent(Bone);
		}
		return Default;
	}

	TMap<FName, EGinkgoBodyPart> MakeMannequinBodyParts()
	{
		TMap<FName, EGinkgoBodyPart> Parts;
		for (const TCHAR* Bone : {TEXT("head"), TEXT("neck_01"), TEXT("neck_02")})
		{
			Parts.Add(Bone, EGinkgoBodyPart::Head);
		}
		for (const TCHAR* Bone : {TEXT("spine_04"), TEXT("spine_05"), TEXT("clavicle_l"), TEXT("clavicle_r")})
		{
			Parts.Add(Bone, EGinkgoBodyPart::UpperChest);
		}
		for (const TCHAR* Bone : {TEXT("pelvis"), TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03")})
		{
			Parts.Add(Bone, EGinkgoBodyPart::Abdomen);
		}
		for (const TCHAR* Bone : {TEXT("upperarm_l"), TEXT("upperarm_r"), TEXT("lowerarm_l"), TEXT("lowerarm_r"), TEXT("hand_l"), TEXT("hand_r")})
		{
			Parts.Add(Bone, EGinkgoBodyPart::UpperLimbs);
		}
		for (const TCHAR* Bone : {TEXT("thigh_l"), TEXT("thigh_r"), TEXT("calf_l"), TEXT("calf_r")})
		{
			Parts.Add(Bone, EGinkgoBodyPart::Legs);
		}
		for (const TCHAR* Bone : {TEXT("foot_l"), TEXT("foot_r"), TEXT("ball_l"), TEXT("ball_r")})
		{
			Parts.Add(Bone, EGinkgoBodyPart::Feet);
		}
		return Parts;
	}

	const TCHAR* DescribeBodyPart(const EGinkgoBodyPart Part)
	{
		switch (Part)
		{
		case EGinkgoBodyPart::Head:
			return TEXT("头部");
		case EGinkgoBodyPart::UpperChest:
			return TEXT("上胸");
		case EGinkgoBodyPart::Abdomen:
			return TEXT("腹部");
		case EGinkgoBodyPart::UpperLimbs:
			return TEXT("上肢");
		case EGinkgoBodyPart::Legs:
			return TEXT("腿部");
		case EGinkgoBodyPart::Feet:
			return TEXT("脚");
		default:
			return TEXT("?");
		}
	}

	EGinkgoHitSound SelectHitSound(const FGinkgoHitConfirm& Confirm)
	{
		if (Confirm.Hits <= 0)
		{
			return EGinkgoHitSound::None;
		}
		// 打死盖过别的：打穿护甲的同一枪打死了，听到的是打死
		if (Confirm.bKill)
		{
			return Confirm.bKillHead ? EGinkgoHitSound::HeadKill : EGinkgoHitSound::Kill;
		}
		// 打穿比爆头少见、要紧（下一枪就不挡了）：头盔被一枪打穿时放打穿
		if (Confirm.bArmorBroken)
		{
			return EGinkgoHitSound::ArmorBreak;
		}
		// 打在头上盖过打在护甲上：头盔挡住的那一下也是爆头的声音，是不是护甲看白 X 的颜色
		if (Confirm.bHead)
		{
			return EGinkgoHitSound::Head;
		}
		return Confirm.bArmor ? EGinkgoHitSound::Armor : EGinkgoHitSound::Normal;
	}

	bool IsPriorityHitSound(const EGinkgoHitSound Sound)
	{
		return Sound == EGinkgoHitSound::ArmorBreak || Sound == EGinkgoHitSound::Kill || Sound == EGinkgoHitSound::HeadKill;
	}

	const TCHAR* DescribeHitSound(const EGinkgoHitSound Sound)
	{
		switch (Sound)
		{
		case EGinkgoHitSound::Normal:
			return TEXT("命中");
		case EGinkgoHitSound::Armor:
			return TEXT("打在护甲上");
		case EGinkgoHitSound::ArmorBreak:
			return TEXT("打穿护甲");
		case EGinkgoHitSound::Head:
			return TEXT("爆头");
		case EGinkgoHitSound::Kill:
			return TEXT("打死");
		case EGinkgoHitSound::HeadKill:
			return TEXT("爆头打死");
		default:
			return TEXT("没有");
		}
	}
}
