#include "World/PFInteractable.h"
#include "World/PFOutpost.h"
#include "Player/PFPlayerCharacter.h"
#include "Combat/PFHealthComponent.h"
#include "Combat/PFWeaponComponent.h"
#include "Progression/PFPlayerState.h"
#include "Framework/PFPlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

APFInteractable::APFInteractable()
{
	bReplicates = true; PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh")); SetRootComponent(Mesh);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
}

void APFInteractable::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(APFInteractable, bDestroyed); DOREPLIFETIME(APFInteractable, Durability);
}

bool APFInteractable::CanUse(APFPlayerCharacter* Player) const
{
	if (!IsValid(Player) || !Player->Health->IsAlive() || bDestroyed || PersistentId.IsNone()) return false;
	if (Kind == EPFInteractableKind::Alarm) return true;
	if (Kind == EPFInteractableKind::SafePoint) return Player->IsSafe();
	const APFPlayerState* State = Player->GetPlayerState<APFPlayerState>();
	return State && !State->Profile.ClaimedRewards.Contains(PersistentId);
}

bool APFInteractable::Use(APFPlayerCharacter* Player)
{
	if (!HasAuthority() || !CanUse(Player)) return false;
	if (Kind == EPFInteractableKind::Alarm)
	{
		bDestroyed = true; Durability = 0.f; if (Outpost) { Outpost->CancelAlarm(); Outpost->RecordContribution(Player); } ForceNetUpdate(); return true;
	}
	if (Kind == EPFInteractableKind::SafePoint)
	{
		Player->Health->Heal(100.f);
		if (APFPlayerController* PC = Cast<APFPlayerController>(Player->GetController())) PC->ClientOpenPreparation();
		return true;
	}
	APFPlayerState* State = Player->GetPlayerState<APFPlayerState>();
	if (!State || !State->GrantReward(PersistentId, Kind == EPFInteractableKind::Exploration ? 125 : 0, Kind == EPFInteractableKind::Exploration ? 5 : 0, 3)) return false;
	if (Kind == EPFInteractableKind::Exploration)
	{
		const FName Blueprint = PersistentId == TEXT("Exploration.0") ? TEXT("Suppressor") : PersistentId == TEXT("Exploration.1") ? TEXT("Shotgun") : TEXT("Optic");
		State->Profile.Blueprints.AddUnique(Blueprint); ++State->Profile.Revision;
	}
	Player->Weapon->AddAmmo(60); Player->Medkits = FMath::Min(Player->Medkits + 1, Player->HasSkill(TEXT("Medicine")) ? 4 : 3);
	Player->ForceNetUpdate(); return true;
}

float APFInteractable::TakeDamage(float Damage, const FDamageEvent& Event, AController* InstigatorController, AActor* Causer)
{
	if (!HasAuthority() || Kind != EPFInteractableKind::Alarm || bDestroyed || !FMath::IsFinite(Damage) || Damage <= 0.f) return 0.f;
	Durability = FMath::Max(0.f, Durability - Damage);
	if (Durability <= 0.f) { bDestroyed = true; if (Outpost) Outpost->CancelAlarm(); }
	ForceNetUpdate(); return Damage;
}
