#include "GinkgoTraversalNetworking.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/PackageMapClient.h"
#include "Engine/NetSerialization.h"

namespace
{
	/** 世界位置保留 0.01 cm，方向压为单位向量；避免双精度矩阵让 CMC 回包超过 4096 bit。 */
	void SerializeTraversalVector(FArchive& Ar, UPackageMap* Map, FVector& Value, const bool bNormal = false)
	{
		bool bSuccess = true;
		if (bNormal)
		{
			FVector_NetQuantizeNormal Packed(Value);
			Packed.NetSerialize(Ar, Map, bSuccess);
			if (Ar.IsLoading()) { Value = Packed; }
		}
		else
		{
			FVector_NetQuantize100 Packed(Value);
			Packed.NetSerialize(Ar, Map, bSuccess);
			if (Ar.IsLoading()) { Value = Packed; }
		}
		if (!bSuccess) { Ar.SetError(); }
	}

	/** 支撑/起点只需平移、旋转与缩放；欧拉角用短整数量化，缩放保留 float 精度。 */
	void SerializeTraversalTransform(FArchive& Ar, UPackageMap* Map, FTransform& Value)
	{
		FVector Position = Value.GetLocation();
		SerializeTraversalVector(Ar, Map, Position);
		FRotator Rotation = Value.Rotator();
		Rotation.SerializeCompressedShort(Ar);
		FVector3f Scale(Value.GetScale3D());
		Ar << Scale;
		if (Ar.IsLoading()) { Value = FTransform(Rotation, Position, FVector(Scale)); }
	}
}

void FGinkgoTraversalInput::Serialize(FArchive& Ar)
{
	Ar << Sequence;
	Ar << Yaw;
	Ar << WeaponSlot;
	uint8 Moving = bMovingExit ? 1 : 0;
	Ar.SerializeBits(&Moving, 1);
	bMovingExit = Moving != 0;
}

bool FGinkgoTraversalSyncState::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << HandledSequence;
	Ar << ActionSequence;
	uint8 Stage = static_cast<uint8>(Phase);
	Ar.SerializeBits(&Stage, 2);
	Phase = static_cast<EGinkgoTraversalPhase>(Stage);
	if (Stage > static_cast<uint8>(EGinkgoTraversalPhase::Recovering)) { Ar.SetError(); }
	if (Phase == EGinkgoTraversalPhase::Idle)
	{
		if (Ar.IsLoading())
		{
			const uint16 Handled = HandledSequence;
			const uint16 Action = ActionSequence;
			*this = {};
			HandledSequence = Handled;
			ActionSequence = Action;
		}
		bOutSuccess = !Ar.IsError();
		return true;
	}
	Ar << Time;
	Ar << ReleaseTime;
	Ar << WeaponSlot;
	uint8 Flags = (bMovingExit ? 1 : 0) | (bCompleted ? 2 : 0);
	Ar.SerializeBits(&Flags, 2);
	bMovingExit = (Flags & 1) != 0;
	bCompleted = (Flags & 2) != 0;
	SerializeTraversalTransform(Ar, Map, Origin);
	uint8 Kind = static_cast<uint8>(Result.Kind);
	Ar.SerializeBits(&Kind, 2);
	Result.Kind = static_cast<EGinkgoTraversalKind>(Kind);
	Result.Failure = EGinkgoTraversalFailure::None;
	SerializeTraversalVector(Ar, Map, Result.Forward, true);
	Ar << Result.Height;
	Ar << Result.WallDistance;
	Ar << Result.Reach;
	bool bMapped = true;
	int32 Index = 0;
	for (auto* Contact : {&Result.Ledge, &Result.LeftHand, &Result.RightHand, &Result.Landing})
	{
		UObject* Component = Contact->Component.Get();
		if (Map) { bMapped &= Map->SerializeObject(Ar, UPrimitiveComponent::StaticClass(), Component); }
		if (Ar.IsLoading()) { Contact->Component = Cast<UPrimitiveComponent>(Component); }
		SerializeTraversalVector(Ar, Map, Contact->Position);
		SerializeTraversalVector(Ar, Map, Contact->Normal, true);
		SerializeTraversalTransform(Ar, Map, Supports[Index++]);
	}
	if (Ar.IsLoading() && (!FMath::IsFinite(Time) || Time < 0.f || Time > 10.f || !FMath::IsFinite(ReleaseTime)
		|| Origin.ContainsNaN() || Result.Forward.ContainsNaN() || !FMath::IsFinite(Result.Height))) { Ar.SetError(); }
	bOutSuccess = !Ar.IsError();
	return bMapped;
}

bool FGinkgoTraversalSyncState::Identical(const FGinkgoTraversalSyncState* Other, uint32 PortFlags) const
{
	if (!Other || !Matches(*Other)) { return false; }
	if (Phase == EGinkgoTraversalPhase::Idle) { return true; }
	if (Time != Other->Time || ReleaseTime != Other->ReleaseTime || bCompleted != Other->bCompleted || WeaponSlot != Other->WeaponSlot) { return false; }
	const FGinkgoTraversalContact* A[] = {&Result.Ledge, &Result.LeftHand, &Result.RightHand, &Result.Landing};
	const FGinkgoTraversalContact* B[] = {&Other->Result.Ledge, &Other->Result.LeftHand, &Other->Result.RightHand, &Other->Result.Landing};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (A[Index]->Component != B[Index]->Component || !A[Index]->Position.Equals(B[Index]->Position, 0.)
			|| !A[Index]->Normal.Equals(B[Index]->Normal, 0.) || !Supports[Index].Equals(Other->Supports[Index], 0.)) { return false; }
	}
	return true;
}

bool FGinkgoTraversalSyncState::Matches(const FGinkgoTraversalSyncState& Other) const
{
	if (HandledSequence != Other.HandledSequence || ActionSequence != Other.ActionSequence || Phase != Other.Phase) { return false; }
	return Phase == EGinkgoTraversalPhase::Idle || (Result.Kind == Other.Result.Kind && bMovingExit == Other.bMovingExit
		&& FMath::Abs(Time - Other.Time) < .002f && Origin.Equals(Other.Origin, .1)
		&& Result.Landing.Position.Equals(Other.Result.Landing.Position, .1));
}
