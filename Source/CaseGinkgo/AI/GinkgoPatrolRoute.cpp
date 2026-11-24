#include "AI/GinkgoPatrolRoute.h"

#include "Components/SplineComponent.h"

AGinkgoPatrolRoute::AGinkgoPatrolRoute()
{
	Preview = CreateDefaultSubobject<USplineComponent>(TEXT("RoutePreview"));
	SetRootComponent(Preview);
	Preview->SetHiddenInGame(true);
	Preview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Points.AddDefaulted(2);
	Points[1].Location = FVector(500.0, 0.0, 0.0);
	Points[1].WaitSeconds = 2.f;
}

void AGinkgoPatrolRoute::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Preview->ClearSplinePoints(false);
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		Preview->AddSplinePoint(Points[Index].Location, ESplineCoordinateSpace::Local, false);
		Preview->SetSplinePointType(Index, ESplinePointType::Linear, false);
	}
	Preview->SetClosedLoop(Points.Num() > 1, true);
}

FVector AGinkgoPatrolRoute::GetPointLocation(const int32 Index) const
{
	return Points.IsValidIndex(Index) ? GetActorTransform().TransformPosition(Points[Index].Location) : GetActorLocation();
}
