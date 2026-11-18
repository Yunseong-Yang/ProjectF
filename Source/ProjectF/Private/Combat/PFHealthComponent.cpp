#include "Combat/PFHealthComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/GameStateBase.h"

UPFHealthComponent::UPFHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPFHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPFHealthComponent, Maximum); DOREPLIFETIME(UPFHealthComponent, Current);
	DOREPLIFETIME(UPFHealthComponent, Life); DOREPLIFETIME(UPFHealthComponent, BleedOutTime);
}

void UPFHealthComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorld()->GetTimerManager().ClearTimer(BleedTimer);
	OnDamaged.Clear();
	OnBeforeDamaged.Clear();
	Super::EndPlay(Reason);
}

void UPFHealthComponent::InitializeHealth(float Value)
{
	if (!GetOwner()->HasAuthority() || !FMath::IsFinite(Value) || Value <= 0.f) return;
	Maximum = Value; Current = Value;
}

float UPFHealthComponent::ApplyDamage(float Amount, AActor* Source)
{
	if (!GetOwner()->HasAuthority() || Life == EPFLifeState::Dead || !FMath::IsFinite(Amount) || Amount <= 0.f) return 0.f;
	if (Source && Source != GetOwner() && Source->ActorHasTag(TEXT("PF.Player")) && GetOwner()->ActorHasTag(TEXT("PF.Player"))) return 0.f;
	// Battle scaling must be frozen before subtracting the first damage event.
	OnBeforeDamaged.Broadcast(Amount, Source);
	if (Life == EPFLifeState::Dead) return 0.f;
	const float Applied = FMath::Min(Amount, Current);
	++DamageRevision;
	Current = FMath::Max(0.f, Current - Amount);
	OnDamaged.Broadcast(Amount, Source);
	if (Current <= 0.f && Life == EPFLifeState::Alive)
	{
		const AGameStateBase* State = GetWorld()->GetGameState();
		if (bCanBeDowned && (!State || State->PlayerArray.Num() > 1))
		{
			BleedOutTime = GetWorld()->GetTimeSeconds() + 30.f;
			SetLife(EPFLifeState::Downed);
			GetWorld()->GetTimerManager().SetTimer(BleedTimer, this, &ThisClass::BleedOut, 30.f);
		}
		else SetLife(EPFLifeState::Dead);
	}
	GetOwner()->ForceNetUpdate();
	return Applied;
}

bool UPFHealthComponent::Heal(float Amount)
{
	if (!GetOwner()->HasAuthority() || !IsAlive() || Current >= Maximum || !FMath::IsFinite(Amount) || Amount <= 0.f) return false;
	Current = FMath::Min(Maximum, Current + Amount); GetOwner()->ForceNetUpdate(); return true;
}

bool UPFHealthComponent::Revive()
{
	if (!GetOwner()->HasAuthority() || Life != EPFLifeState::Downed) return false;
	GetWorld()->GetTimerManager().ClearTimer(BleedTimer);
	Current = FMath::Min(40.f, Maximum); BleedOutTime = 0.f; SetLife(EPFLifeState::Alive); return true;
}

bool UPFHealthComponent::Execute()
{
	if (!GetOwner()->HasAuthority() || Life == EPFLifeState::Dead) return false;
	GetWorld()->GetTimerManager().ClearTimer(BleedTimer);
	Current = 0.f; BleedOutTime = 0.f; SetLife(EPFLifeState::Dead); return true;
}

void UPFHealthComponent::BleedOut() { Execute(); }

void UPFHealthComponent::SetLife(EPFLifeState NewLife)
{
	if (Life == NewLife) return;
	const EPFLifeState Previous = Life; Life = NewLife;
	OnLifeChanged.Broadcast(Previous, Life); GetOwner()->ForceNetUpdate();
}

void UPFHealthComponent::OnRep_Life(EPFLifeState Previous) { OnLifeChanged.Broadcast(Previous, Life); }
