#include "GinkgoCameraCollision.h"

double FGinkgoCameraCollision::Step(const double RequiredCompression, const double DeltaTime, const double PushOutOmega)
{
	if (!bActive)
	{
		bActive = true;
		Compression.Reset(RequiredCompression);
		LastRequired = RequiredCompression;
		return RequiredCompression;
	}
	if (DeltaTime > 0.0)
	{
		Compression.StepMoving(LastRequired, RequiredCompression, PushOutOmega, DeltaTime);
		LastRequired = RequiredCompression;
	}
	// 拉近是瞬时的：压缩少于要求时相机就在几何体里，直接拉到要求的位置、速度清零（推远时弹簧冲过了头也一样）
	if (Compression.Value <= RequiredCompression)
	{
		Compression.Reset(RequiredCompression);
	}
	return Compression.Value;
}
