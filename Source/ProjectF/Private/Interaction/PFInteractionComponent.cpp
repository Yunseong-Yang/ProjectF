#include "Interaction/PFInteractionComponent.h"
#include "Player/PFPlayerCharacter.h"
#include "Combat/PFHealthComponent.h"
#include "Combat/PFWeaponComponent.h"
#include "World/PFOutpost.h"
#include "World/PFInteractable.h"
#include "World/PFWorldStateSubsystem.h"
#include "AI/PFEnemyCharacter.h"
#include "Framework/PFGameState.h"
#include "Progression/PFPlayerState.h"
#include "Framework/PFPlayerController.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UPFInteractionComponent::UPFInteractionComponent() { PrimaryComponentTick.bCanEverTick = false; SetIsReplicatedByDefault(true); }
void UPFInteractionComponent::EndPlay(const EEndPlayReason::Type Reason) { Cancel(); Super::EndPlay(Reason); }

void UPFInteractionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UPFInteractionComponent, ActiveTarget);
	DOREPLIFETIME(UPFInteractionComponent, StartTime); DOREPLIFETIME(UPFInteractionComponent, EndTime);
}

AActor* UPFInteractionComponent::FindTarget() const
{
	const APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(GetOwner()); if (!Player) return nullptr;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PFInteractTarget), false, Player);
	FHitResult Hit;
	GetWorld()->LineTraceSingleByChannel(Hit, Player->GetAimOrigin(), Player->GetAimOrigin() + Player->GetAimDirection() * 350.f, ECC_Visibility, Params);
	if (Hit.GetActor() && (Hit.GetActor()->IsA<APFOutpost>() || Hit.GetActor()->IsA<APFInteractable>() || Hit.GetActor()->IsA<APFPlayerCharacter>())) return Hit.GetActor();
	return nullptr;
}

bool UPFInteractionComponent::IsValidTarget(AActor* Target) const
{
	const APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(GetOwner());
	if (!Player || !Player->Health->IsAlive() || !IsValid(Target) || Player->GetDistanceTo(Target) > 350.f) return false;
	FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(PFInteractionLOS), false, Player);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Player->GetAimOrigin(), Target->GetActorLocation(), ECC_Visibility, Params) && Hit.GetActor() != Target) return false;
	if (APFPlayerCharacter* Other = Cast<APFPlayerCharacter>(Target))
		return Other != Player && Other->Health->Life == EPFLifeState::Downed && (!Other->Reviver.IsValid() || Other->Reviver.Get() == Player);
	if (APFOutpost* Outpost = Cast<APFOutpost>(Target)) return Outpost->CanCapture(const_cast<APFPlayerCharacter*>(Player));
	if (APFInteractable* Device = Cast<APFInteractable>(Target)) return Device->CanUse(const_cast<APFPlayerCharacter*>(Player));
	return false;
}

void UPFInteractionComponent::ServerBegin_Implementation(AActor* Target, uint32 Sequence)
{
	if (!FPFGameRules::IsNewSequence(Sequence, LastSequence)) return; LastSequence = Sequence;
	APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(GetOwner());
	if (!Player || !Player->CanAct() || ActiveTarget || !IsValidTarget(Target)) return;
	float Duration = .3f;
	if (APFPlayerCharacter* Other = Cast<APFPlayerCharacter>(Target))
	{
		Duration = Player->HasSkill(TEXT("Revive")) ? 4.f : 5.f;
		Other->Reviver = Player; TargetDamageRevision = Other->Health->DamageRevision;
	}
	else if (Target->IsA<APFOutpost>()) Duration = 3.f;
	else if (const APFInteractable* Device = Cast<APFInteractable>(Target)) if (Device->Kind == EPFInteractableKind::Alarm) Duration = 3.f;
	ActiveTarget = Target; StartTime = GetWorld()->GetTimeSeconds(); EndTime = StartTime + Duration;
	OwnerDamageRevision = Player->Health->DamageRevision; StartLocation = Player->GetActorLocation();
	Player->Weapon->Cancel(); Player->SetAction(TEXT("Interaction"), Duration);
	GetWorld()->GetTimerManager().SetTimer(ProgressTimer, this, &ThisClass::UpdateProgress, .1f, true);
}

void UPFInteractionComponent::UpdateProgress()
{
	APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(GetOwner());
	if (!Player || !IsValidTarget(ActiveTarget) || Player->Health->DamageRevision != OwnerDamageRevision || FVector::DistSquared(Player->GetActorLocation(), StartLocation) > 60.f * 60.f) { Cancel(); return; }
	APFPlayerCharacter* Other = Cast<APFPlayerCharacter>(ActiveTarget);
	if (Other && Other->Health->DamageRevision != TargetDamageRevision) { Cancel(); return; }
	if (GetWorld()->GetTimeSeconds() < EndTime) return;
	if (Other)
	{
		if (Other->Health->Revive())
		{
			if (APFPlayerState* State = Player->GetPlayerState<APFPlayerState>()) State->AddCombatExperience(25);
			for (const auto& Pair : GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->GetOutposts())
				if (Pair.Value.IsValid() && Pair.Value->Phase == EPFOutpostPhase::Engaged && Pair.Value->GetDistanceTo(Player) <= Pair.Value->ParticipationRadius) Pair.Value->RecordContribution(Player);
		}
	}
	else if (APFOutpost* Outpost = Cast<APFOutpost>(ActiveTarget)) Outpost->Capture(Player);
	else if (APFInteractable* Device = Cast<APFInteractable>(ActiveTarget))
	{
		if (Device->Use(Player))
			if (APFPlayerController* PC = Cast<APFPlayerController>(Player->GetController()))
			{
				PC->CaptureSession(); GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->Save();
				if (APFPlayerState* State = Player->GetPlayerState<APFPlayerState>()) PC->ClientPersistProfile(State->Profile);
				PC->ClientNotify(FText::FromString(Device->Kind == EPFInteractableKind::Exploration ? TEXT("탐험 보급품과 설계도를 획득했습니다. 안전 지점에서 장비를 준비하세요.") : TEXT("상호작용을 완료했습니다.")));
			}
	}
	Cancel();
}

void UPFInteractionComponent::ServerCancel_Implementation(uint32 Sequence)
{
	if (!FPFGameRules::IsNewSequence(Sequence, LastSequence)) return; LastSequence = Sequence; Cancel();
}

void UPFInteractionComponent::Cancel()
{
	GetWorld()->GetTimerManager().ClearTimer(ProgressTimer);
	if (!GetOwner()->HasAuthority()) return;
	APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(GetOwner());
	if (APFPlayerCharacter* Other = Cast<APFPlayerCharacter>(ActiveTarget)) if (Other->Reviver.Get() == Player) Other->Reviver.Reset();
	ActiveTarget = nullptr; StartTime = 0.f; EndTime = 0.f;
	if (Player && Player->ActionName == TEXT("Interaction")) Player->ClearAction();
}

void UPFInteractionComponent::ServerObserve_Implementation(AActor* Target, uint32 Sequence)
{
	if (!FPFGameRules::IsNewSequence(Sequence, LastSequence)) return; LastSequence = Sequence;
	APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(GetOwner());
	const float Now = GetWorld()->GetTimeSeconds();
	if (!Player || !Player->CanAct() || Now - LastObserveRequest < .15f) return;
	LastObserveRequest = Now;
	if (!IsValid(Target) || Player->GetDistanceTo(Target) > 10000.f) { ObservedTarget.Reset(); return; }
	const FVector ToTarget = (Target->GetActorLocation() - Player->GetAimOrigin()).GetSafeNormal();
	if (FVector::DotProduct(Player->GetAimDirection(), ToTarget) < .98f) { ObservedTarget.Reset(); return; }
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PFReconLOS), false, Player); FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Player->GetAimOrigin(), Target->GetActorLocation(), ECC_Visibility, Params) && Hit.GetActor() != Target) { ObservedTarget.Reset(); return; }
	if (ObservedTarget.Get() != Target) { ObservedTarget = Target; ObservationStart = Now; return; }
	if (Now - ObservationStart < .7f) return;
	APFGameState* State = GetWorld()->GetGameState<APFGameState>();
	if (APFEnemyCharacter* Enemy = Cast<APFEnemyCharacter>(Target))
	{
		if (!Enemy->Health->IsAlive() || !State) return;
		FPFReconMark* Mark = State->ReconMarks.FindByPredicate([Enemy](const FPFReconMark& M) { return M.Id == Enemy->GetUniqueID(); });
		if (!Mark)
		{
			if (State->ReconMarks.Num() >= 128) State->ReconMarks.RemoveAt(0);
			Mark = &State->ReconMarks.AddDefaulted_GetRef(); Mark->Id = Enemy->GetUniqueID();
		}
		Mark->Location = Enemy->GetActorLocation(); Mark->ObservedTime = Now; Mark->Role = Enemy->EnemyRole; State->ForceNetUpdate();
		if (Enemy->Outpost) { Enemy->Outpost->RecordContribution(Player); GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->Discover(Enemy->Outpost->OutpostId); }
	}
	if (APFOutpost* Outpost = Cast<APFOutpost>(Target))
	{
		Outpost->RecordContribution(Player); GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->Discover(Outpost->OutpostId);
	}
}
