#include "AI/PFCoverPoint.h"
#include "AI/PFEnemyCharacter.h"
#include "Combat/PFHealthComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"

APFCoverPoint::APFCoverPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

FVector APFCoverPoint::GetPeekLocation() const { return GetActorTransform().TransformPosition(PeekOffset); }

bool APFCoverPoint::CanProtectFrom(const FVector& Threat, const APFEnemyCharacter* Enemy) const
{
	if (!Enemy || (Occupant.IsValid() && Occupant.Get() != Enemy && Occupant->Health->IsAlive())) return false;
	const FVector Shelter = GetActorLocation() + FVector(0.f, 0.f, bLowCover ? 0.f : 50.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PFCoverCheck), false, Enemy);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Threat, Shelter, ECC_Visibility, Params)) return false;
	// A teammate or a temporary pawn is never accepted as architectural cover.
	if (!Hit.GetActor() || Hit.GetActor()->IsA<APawn>()) return false;
	return !GetWorld()->LineTraceSingleByChannel(Hit, Threat, GetPeekLocation() + FVector(0.f, 0.f, 50.f), ECC_Visibility, Params);
}

bool APFCoverPoint::Reserve(APFEnemyCharacter* Enemy)
{
	if (!HasAuthority() || !IsValid(Enemy) || !Enemy->Health->IsAlive()) return false;
	if (Occupant.IsValid() && Occupant.Get() != Enemy && Occupant->Health->IsAlive()) return false;
	Occupant = Enemy; return true;
}

void APFCoverPoint::Release(APFEnemyCharacter* Enemy) { if (HasAuthority() && Occupant.Get() == Enemy) Occupant.Reset(); }
bool APFCoverPoint::IsReservedBy(const APFEnemyCharacter* Enemy) const { return Occupant.Get() == Enemy; }
