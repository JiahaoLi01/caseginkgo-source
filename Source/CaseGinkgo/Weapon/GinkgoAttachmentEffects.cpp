#include "Weapon/GinkgoAttachmentEffects.h"

void FGinkgoAttachmentEffects::Add(const FGinkgoAttachmentEffects& Other)
{
	AdsTime += Other.AdsTime;
	ReloadTime += Other.ReloadTime;
	AdsSpeed += Other.AdsSpeed;
	HipSpeed += Other.HipSpeed;
	HipHandling += Other.HipHandling;
	HipSpread += Other.HipSpread;
	AdsSpread += Other.AdsSpread;
	MovingSpread += Other.MovingSpread;
	if (Other.MagazineCapacity > 0)
	{
		MagazineCapacity = Other.MagazineCapacity;
	}
}
