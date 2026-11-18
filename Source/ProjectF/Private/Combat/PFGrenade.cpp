#include "Combat/PFGrenade.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Combat/PFHealthComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Perception/AISense_Hearing.h"
#include "TimerManager.h"

APFGrenade::APFGrenade()
{
	bReplicates = true; SetReplicateMovement(true); PrimaryActorTick.bCanEverTick = false;
	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision")); SetRootComponent(Collision);
	Collision->InitSphereRadius(8.f); Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
	Movement->InitialSpeed = 1400.f; Movement->MaxSpeed = 1800.f; Movement->bShouldBounce = true; Movement->Bounciness = .35f;
}

void APFGrenade::BeginPlay()
{
	Super::BeginPlay(); Collision->IgnoreActorWhenMoving(GetOwner(), true);
	if (HasAuthority()) GetWorldTimerManager().SetTimer(FuseTimer, this, &ThisClass::Explode, 3.f);
}

void APFGrenade::EndPlay(const EEndPlayReason::Type Reason) { GetWorldTimerManager().ClearTimer(FuseTimer); Super::EndPlay(Reason); }

void APFGrenade::Explode()
{
	if (!HasAuthority()) return;
	TArray<FOverlapResult> Hits;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PFGrenade), false, this);
	GetWorld()->OverlapMultiByObjectType(Hits, GetActorLocation(), FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(600.f), Params);
	TSet<AActor*> Applied;
	for (const FOverlapResult& Hit : Hits)
	{
		AActor* Target = Hit.GetActor();
		if (!IsValid(Target) || Applied.Contains(Target)) continue;
		Applied.Add(Target);
		FHitResult Block;
		if (GetWorld()->LineTraceSingleByChannel(Block, GetActorLocation(), Target->GetActorLocation(), ECC_Visibility, Params) && Block.GetActor() != Target) continue;
		if (UPFHealthComponent* Health = Target->FindComponentByClass<UPFHealthComponent>())
			Health->ApplyDamage(FMath::GetMappedRangeValueClamped(FVector2D(0.f, 600.f), FVector2D(160.f, 20.f), GetDistanceTo(Target)), GetOwner());
	}
	UAISense_Hearing::ReportNoiseEvent(GetWorld(), GetActorLocation(), 1.f, GetOwner(), 12000.f, TEXT("Explosion"));
	MulticastExplosion(); Destroy();
}

void APFGrenade::MulticastExplosion_Implementation() { OnExplosion(); }
