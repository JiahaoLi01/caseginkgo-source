#include "UI/GinkgoVitals.h"

#include "Combat/GinkgoDamageableComponent.h"

FGinkgoVitals FGinkgoVitals::Read(const UGinkgoDamageableComponent* Source)
{
	FGinkgoVitals Result;
	if (!Source || !FMath::IsFinite(Source->GetMaxHealth()) || Source->GetMaxHealth() <= 0.f)
	{
		return Result;
	}
	Result.bAvailable = true;
	Result.MaxHealth = Source->GetMaxHealth();
	Result.Health = !Source->IsDead() && FMath::IsFinite(Source->GetHealth()) ? FMath::Clamp(Source->GetHealth(), 0.f, Result.MaxHealth) : 0.f;
	for (const FGinkgoArmorPiece& Piece : Source->GetArmor())
	{
		if (FMath::IsFinite(Piece.MaxDurability) && Piece.MaxDurability > 0.f && (Source->HasFullBodyArmor() || !Piece.Covers.IsEmpty()))
		{
			Result.MaxArmor += Piece.MaxDurability;
			Result.Armor += FMath::IsFinite(Piece.Durability) ? FMath::Clamp(Piece.Durability, 0.f, Piece.MaxDurability) : 0.f;
		}
	}
	return Result;
}

bool FGinkgoVitals::HasArmor() const
{
	return Armor > 0.f;
}

bool FGinkgoVitals::operator==(const FGinkgoVitals& Other) const
{
	return bAvailable == Other.bAvailable && Health == Other.Health && MaxHealth == Other.MaxHealth && Armor == Other.Armor && MaxArmor == Other.MaxArmor;
}
