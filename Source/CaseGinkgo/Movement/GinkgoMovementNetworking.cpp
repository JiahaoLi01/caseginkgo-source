#include "GinkgoMovementNetworking.h"

#include "GameFramework/Character.h"
#include "Character/GinkgoCharacter.h"
#include "Traversal/GinkgoTraversalComponent.h"
#include "GinkgoCharacterMovementComponent.h"
#include "GinkgoMovementConfig.h"

#if !UE_BUILD_SHIPPING
namespace
{
	TAutoConsoleVariable<int32> CVarVerifySimState(
		TEXT("ginkgo.net.verifySimState"),
		0,
		TEXT("比对两端的模拟状态（服务器上设）：0 关，1 开。\n")
		TEXT("打开后服务器在确认好移动的回包里也带上模拟状态（落地计时、滑铲）与是否蹲着，客户端与存档里那次移动结束时的状态比对，")
		TEXT("不一致时打一行 [Net] 日志。校正的回包总带模拟状态，与这个开关无关。"));

	TAutoConsoleVariable<int32> CVarRevertSimStateOnCombine(
		TEXT("ginkgo.net.revertSimStateOnCombine"),
		1,
		TEXT("客户端合并移动时，把模拟状态退回到被合并那次移动开始时：1 退回（正确做法），0 不退回（只用来对照测量）。"));

	TAutoConsoleVariable<int32> CVarNoCombineOnWindowChange(
		TEXT("ginkgo.net.noCombineOnWindowChange"),
		1,
		TEXT("客户端不合并窗口有变化、发过相机事件的移动：1 不合并（正确做法），0 照常合并（只用来对照测量）。\n")
		TEXT("合并后从被合并那次移动的开头重算，那次移动里的窗口变化与相机事件会再发生一遍：两端的状态仍然一致，表现层与相机却收到两次。"));
}
#endif

namespace
{
	/** 窗口有变化、发过相机事件的移动不合并；调试版可以用 ginkgo.net.noCombineOnWindowChange 关掉来对照 */
	bool ShouldBlockCombineOnWindowChange()
	{
#if !UE_BUILD_SHIPPING
		return CVarNoCombineOnWindowChange.GetValueOnGameThread() != 0;
#else
		return true;
#endif
	}
}

// ===== FGinkgoSavedMove =====

void FGinkgoSavedMove::Clear()
{
	Super::Clear();
	bWantsToSprint = false;
	DirectionPressCount = 0;
	DirectionKeys = 0;
	HeldSpeedScale = 255;
	AdsSpeedScale = 255;
	StartSimState = FGinkgoMovementSimState();
	TraversalInput = {};
	StartTraversal = {};
	EndTraversal = {};
	bStartedInLandingGrace = false;
	StartWindowChangeCount = 0;
	EndWindowChangeCount = 0;
	StartCameraEventCount = 0;
	EndCameraEventCount = 0;
#if !UE_BUILD_SHIPPING
	EndSimState = FGinkgoMovementSimState();
	bEndCrouched = false;
#endif
}

void FGinkgoSavedMove::SetMoveFor(ACharacter* Character, const float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
	const UGinkgoCharacterMovementComponent* Movement = CastChecked<UGinkgoCharacterMovementComponent>(Character->GetCharacterMovement());
	bWantsToSprint = Movement->WantsToSprint();
	DirectionPressCount = Movement->GetDirectionPressCount();
	DirectionKeys = Movement->GetPackedDirectionKeys();
	HeldSpeedScale = Movement->GetPackedHeldSpeedScale();
	AdsSpeedScale = Movement->GetPackedAdsSpeedScale();
	TraversalInput = CastChecked<AGinkgoCharacter>(Character)->GetTraversal()->GetInput();
}

void FGinkgoSavedMove::SetInitialPosition(ACharacter* Character)
{
	// SetMoveFor 里调一次；合并移动、引擎把角色退回到被合并那次移动开始时之后再调一次，记下合并后那次移动真正的开始状态
	Super::SetInitialPosition(Character);
	const UGinkgoCharacterMovementComponent* Movement = CastChecked<UGinkgoCharacterMovementComponent>(Character->GetCharacterMovement());
	StartSimState = Movement->GetSimState();
	StartTraversal = CastChecked<AGinkgoCharacter>(Character)->GetTraversal()->GetSyncState();
	bStartedInLandingGrace = Movement->IsMovingOnGround() && StartSimState.TimeSinceLanded < Movement->GetConfig().LandingFrictionGrace;
	StartWindowChangeCount = Movement->GetWindowChangeCount();
	StartCameraEventCount = Movement->GetCameraEventCount();
}

bool FGinkgoSavedMove::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, const float MaxDelta) const
{
	const FGinkgoSavedMove* Other = static_cast<const FGinkgoSavedMove*>(NewMove.Get());
	if (TraversalInput.Sequence != Other->TraversalInput.Sequence || StartTraversal.Phase != EGinkgoTraversalPhase::Idle
		|| EndTraversal.Phase != EGinkgoTraversalPhase::Idle || Other->StartTraversal.Phase != EGinkgoTraversalPhase::Idle
		|| bWantsToSprint != Other->bWantsToSprint || DirectionPressCount != Other->DirectionPressCount || bStartedInLandingGrace || Other->bStartedInLandingGrace
		|| HeldSpeedScale != Other->HeldSpeedScale || AdsSpeedScale != Other->AdsSpeedScale)
	{
		return false;
	}
	// 两次移动之间窗口有变化、发过相机事件（起跳的事件发生在新的那次移动开始记录之前）
	if ((EndWindowChangeCount != Other->StartWindowChangeCount || EndCameraEventCount != Other->StartCameraEventCount) && ShouldBlockCombineOnWindowChange())
	{
		return false;
	}
	return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
}

void FGinkgoSavedMove::CombineWith(const FSavedMove_Character* OldMove, ACharacter* InCharacter, APlayerController* PC, const FVector& OldStartLocation)
{
	// 父类只把位置、速度、地面与跳跃计数退回到被合并那次移动开始时，模拟产生的状态在这里一起退回
	Super::CombineWith(OldMove, InCharacter, PC, OldStartLocation);
	UGinkgoCharacterMovementComponent* Movement = CastChecked<UGinkgoCharacterMovementComponent>(InCharacter->GetCharacterMovement());
#if !UE_BUILD_SHIPPING
	const FGinkgoSavedMove* Old = static_cast<const FGinkgoSavedMove*>(OldMove);
	Movement->NotifyMovesCombined(Old->EndWindowChangeCount - Old->StartWindowChangeCount);
	if (CVarRevertSimStateOnCombine.GetValueOnGameThread() == 0)
	{
		return;
	}
#endif
	Movement->RestoreSimState(static_cast<const FGinkgoSavedMove*>(OldMove)->StartSimState);
	CastChecked<AGinkgoCharacter>(InCharacter)->GetTraversal()->RestoreSyncState(static_cast<const FGinkgoSavedMove*>(OldMove)->StartTraversal);
}

void FGinkgoSavedMove::PostUpdate(ACharacter* Character, const EPostUpdateMode PostUpdateMode)
{
	// 父类重放后也把 SavedControlRotation 换成此刻的实时视角。发给服务器的是记录时的视角，这次移动再被重放
	// （GetMoveControlRotation）或被当作 OldMove 重发时都要用它，否则两端判断滑铲方向时用的视角不同
	const FRotator RecordedControlRotation = SavedControlRotation;
	Super::PostUpdate(Character, PostUpdateMode);
	EndTraversal = CastChecked<AGinkgoCharacter>(Character)->GetTraversal()->GetSyncState();
	bForceNoCombine |= StartTraversal.Phase != EGinkgoTraversalPhase::Idle || EndTraversal.Phase != EGinkgoTraversalPhase::Idle
		|| StartTraversal.HandledSequence != EndTraversal.HandledSequence;
	if (PostUpdateMode == PostUpdate_Replay)
	{
		SavedControlRotation = RecordedControlRotation;
	}
	else
	{
		// 这次移动里窗口有变化、发过相机事件就不合并：合并后从被合并那次移动的开头重算，同一个变化、同一个事件会再发生一遍
		const UGinkgoCharacterMovementComponent* Movement = CastChecked<UGinkgoCharacterMovementComponent>(Character->GetCharacterMovement());
		EndWindowChangeCount = Movement->GetWindowChangeCount();
		EndCameraEventCount = Movement->GetCameraEventCount();
		if ((EndWindowChangeCount != StartWindowChangeCount || EndCameraEventCount != StartCameraEventCount) && ShouldBlockCombineOnWindowChange())
		{
			bForceNoCombine = true;
		}
	}
#if !UE_BUILD_SHIPPING
	EndSimState = CastChecked<UGinkgoCharacterMovementComponent>(Character->GetCharacterMovement())->GetSimState();
	bEndCrouched = Character->bIsCrouched;
#endif
}

bool FGinkgoSavedMove::IsImportantMove(const FSavedMovePtr& LastAckedMove) const
{
	// 同父类对压缩标志位的判断：与最后确认的那次移动的计数不同，就是还没确认的按下；地速倍率同理
	const FGinkgoSavedMove* LastAcked = static_cast<const FGinkgoSavedMove*>(LastAckedMove.Get());
	return TraversalInput.Sequence != LastAcked->TraversalInput.Sequence || DirectionPressCount != LastAcked->DirectionPressCount || HeldSpeedScale != LastAcked->HeldSpeedScale || AdsSpeedScale != LastAcked->AdsSpeedScale
		|| Super::IsImportantMove(LastAckedMove);
}

uint8 FGinkgoSavedMove::GetCompressedFlags() const
{
	uint8 Flags = Super::GetCompressedFlags();
	if (bWantsToSprint)
	{
		Flags |= FLAG_Custom_0;
	}
	return Flags;
}

// ===== FGinkgoNetworkPredictionData_Client =====

FSavedMovePtr FGinkgoNetworkPredictionData_Client::AllocateNewMove()
{
	return FSavedMovePtr(new FGinkgoSavedMove());
}

// ===== FGinkgoNetworkMoveData =====

void FGinkgoNetworkMoveData::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, const ENetworkMoveType MoveType)
{
	Super::ClientFillNetworkMoveData(ClientMove, MoveType);
	// 存档都是 FGinkgoSavedMove（FGinkgoNetworkPredictionData_Client::AllocateNewMove）
	const FGinkgoSavedMove& Move = static_cast<const FGinkgoSavedMove&>(ClientMove);
	DirectionPressCount = Move.GetDirectionPressCount();
	DirectionKeys = Move.GetDirectionKeys();
	HeldSpeedScale = Move.GetHeldSpeedScale();
	AdsSpeedScale = Move.GetAdsSpeedScale();
	TraversalInput = Move.GetTraversalInput();
	TraversalAction = Move.GetEndTraversal().ActionSequence;
	TraversalPhase = Move.GetEndTraversal().Phase;
}

bool FGinkgoNetworkMoveData::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, const ENetworkMoveType MoveType)
{
	if (!Super::Serialize(CharacterMovement, Ar, PackageMap, MoveType))
	{
		return false;
	}
	Ar << DirectionPressCount;
	// 4 位：读的时候只覆盖低 4 位，其余位要先清零
	if (Ar.IsLoading())
	{
		DirectionKeys = 0;
	}
	Ar.SerializeBits(&DirectionKeys, 4);
	Ar << HeldSpeedScale;
	Ar << AdsSpeedScale;
	TraversalInput.Serialize(Ar);
	Ar << TraversalAction;
	uint8 Phase = static_cast<uint8>(TraversalPhase);
	Ar.SerializeBits(&Phase, 2);
	TraversalPhase = static_cast<EGinkgoTraversalPhase>(Phase);
	return !Ar.IsError();
}

// ===== FGinkgoMoveResponseDataContainer =====

void FGinkgoMoveResponseDataContainer::ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment)
{
	Super::ServerFillResponseData(CharacterMovement, PendingAdjustment);
	// 回包在帧末发出，服务器这时可能已经模拟过被校正那次移动之后的移动（见 ServerMoveHandleClientError）；
	// 父类的位置、速度是记下校正那一刻的，模拟状态也取那一刻的，不取现在的
	const UGinkgoCharacterMovementComponent& Movement = static_cast<const UGinkgoCharacterMovementComponent&>(CharacterMovement);
	SimState = Movement.GetPendingAdjustmentSimState();
	Traversal = Movement.GetPendingAdjustmentTraversal();
	bServerCrouched = Movement.IsPendingAdjustmentCrouched();
	bHasSimState = IsCorrection();
#if !UE_BUILD_SHIPPING
	bHasSimState |= CVarVerifySimState.GetValueOnGameThread() != 0;
#endif
}

bool FGinkgoMoveResponseDataContainer::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap)
{
	if (!Super::Serialize(CharacterMovement, Ar, PackageMap))
	{
		return false;
	}
	// 多写一位标记后面有没有模拟状态：确认好移动的回包平时不带，不变大
	uint8 HasSimStateBit = !Ar.IsLoading() && bHasSimState ? 1 : 0;
	Ar.SerializeBits(&HasSimStateBit, 1);
	bHasSimState = HasSimStateBit != 0;
	if (bHasSimState)
	{
		SimState.NetSerialize(Ar);
		bool bSuccess = true;
		Traversal.NetSerialize(Ar, PackageMap, bSuccess);
		if (!bSuccess) { Ar.SetError(); }
		uint8 CrouchedBit = !Ar.IsLoading() && bServerCrouched ? 1 : 0;
		Ar.SerializeBits(&CrouchedBit, 1);
		bServerCrouched = CrouchedBit != 0;
	}
	else if (Ar.IsLoading())
	{
		bServerCrouched = false;
	}
	return !Ar.IsError();
}
