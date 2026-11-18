#include "Combat/PFTakedownComponent.h"
#include "Combat/PFHealthComponent.h"
#include "AI/PFEnemyCharacter.h"
#include "Player/PFPlayerCharacter.h"
#include "Interaction/PFInteractionComponent.h"
#include "Combat/PFWeaponComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

UPFTakedownComponent::UPFTakedownComponent() { PrimaryComponentTick.bCanEverTick = false; SetIsReplicatedByDefault(true); }
void UPFTakedownComponent::EndPlay(const EEndPlayReason::Type Reason) { Cancel(); Super::EndPlay(Reason); }

void UPFTakedownComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UPFTakedownComponent, ActionId);
	DOREPLIFETIME(UPFTakedownComponent, StartTime); DOREPLIFETIME(UPFTakedownComponent, ActiveType);
	DOREPLIFETIME(UPFTakedownComponent, PresentationTarget);
	DOREPLIFETIME(UPFTakedownComponent, bCrouchedBeforeExecution);
}

APFEnemyCharacter* UPFTakedownComponent::FindCandidate() const
{
	APFEnemyCharacter* Best = nullptr; float Distance = FLT_MAX;
	for (TActorIterator<APFEnemyCharacter> It(GetWorld()); It; ++It)
	{
		const float Squared = GetOwner()->GetSquaredDistanceTo(*It);
		if (Squared < Distance && Squared <= FMath::Square(550.f) && IsEligible(*It, ChooseType(*It))) { Best = *It; Distance = Squared; }
	}
	return Best;
}

float UPFTakedownComponent::GetServerTime() const
{
	const AGameStateBase* State = GetWorld()->GetGameState();
	return State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

bool UPFTakedownComponent::HasChainWindow() const
{
	const float Elapsed = GetServerTime() - StartTime;
	return ActionId > 0 && Elapsed >= -.1f && Elapsed <= 3.f;
}

bool UPFTakedownComponent::IsPresenting() const
{
	const float Elapsed = GetServerTime() - StartTime;
	return PresentationTarget && ActionId > 0 && Elapsed >= -.1f && Elapsed < FPFGameRules::TakedownSeconds;
}

void UPFTakedownComponent::OnRep_Presentation()
{
	APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(GetOwner()); if (!Player) return;
	if (!PresentationTarget || !IsPresenting()) { GetWorld()->GetTimerManager().ClearTimer(ImpactTimer); Player->StopRearExecution(); RestoreStance(Player); return; }
	if (LastPresentedAction == ActionId) return;
	LastPresentedAction = ActionId;
	if ((bCrouchedBeforeExecution || Player->bIsCrouched) && (Player->HasAuthority() || Player->IsLocallyControlled()))
	{
		bRestoreCrouch = true; Player->UnCrouch(); Player->GetCharacterMovement()->UnCrouch(false);
	}
	Player->PlayRearExecution(FMath::Max(0.f, GetServerTime() - StartTime));
	const float ContactIn = FPFGameRules::TakedownContactSeconds - (GetServerTime() - StartTime);
	if (ContactIn > 0.f) GetWorld()->GetTimerManager().SetTimer(ImpactTimer, this, &ThisClass::PlayImpactCue, ContactIn, false);
	if (Player->IsLocallyControlled()) if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
		PC->SetControlRotation((PresentationTarget->GetActorLocation() + FVector(0, 0, 50) - Player->GetAimOrigin()).Rotation());
}

void UPFTakedownComponent::PlayImpactCue()
{
	if (PresentationTarget) if (APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(GetOwner())) Player->PlayExecutionSound(true);
}

void UPFTakedownComponent::RestoreStance(APFPlayerCharacter* Player)
{
	if (bRestoreCrouch && Player && Player->Health->IsAlive())
	{
		Player->Crouch();
		if (Player->GetCharacterMovement()->IsMovingOnGround()) Player->GetCharacterMovement()->Crouch(false);
	}
	bRestoreCrouch = false;
}

APFEnemyCharacter* UPFTakedownComponent::FindThrowCandidate() const
{
	const APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(GetOwner()); if (!Player || !HasChainWindow()) return nullptr;
	APFEnemyCharacter* Best = nullptr; float BestAlignment = .95f;
	for (TActorIterator<APFEnemyCharacter> It(GetWorld()); It; ++It)
	{
		const float Alignment = FVector::DotProduct(Player->GetAimDirection(), (It->GetActorLocation() - Player->GetAimOrigin()).GetSafeNormal());
		if (Alignment > BestAlignment && IsEligible(*It, EPFTakedownType::Throw)) { BestAlignment = Alignment; Best = *It; }
	}
	return Best;
}

EPFTakedownType UPFTakedownComponent::ChooseType(APFEnemyCharacter* Enemy) const
{
	const APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(GetOwner());
	if (!Player || !Enemy) return EPFTakedownType::Rear;
	if (Player->GetActorLocation().Z - Enemy->GetActorLocation().Z >= 150.f) return EPFTakedownType::Aerial;
	if (Enemy->EnemyRole == EPFEnemyRole::Heavy) return EPFTakedownType::Heavy;
	if (HasChainWindow() && Player->HasSkill(TEXT("Chain"))) return EPFTakedownType::Chain;
	if (Enemy->bAtCoverCorner && Player->HasSkill(TEXT("Cover"))) return EPFTakedownType::Cover;
	return EPFTakedownType::Rear;
}

bool UPFTakedownComponent::IsEligible(APFEnemyCharacter* Enemy, EPFTakedownType Type) const
{
	const APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(GetOwner());
	if (!Player || !Player->CanAct() || !IsValid(Enemy) || !Enemy->Health->IsAlive() || Enemy->IsExecutionReserved()) return false;
	if (Enemy->EnemyRole == EPFEnemyRole::Heavy && !Player->HasSkill(TEXT("ExecutionMaster"))) return false;
	const FVector Offset = Player->GetActorLocation() - Enemy->GetActorLocation();
	const bool bRear = FPFGameRules::IsRearApproach(Enemy->GetActorForwardVector(), Offset);
	bool bValid = false;
	switch (Type)
	{
	case EPFTakedownType::Rear: bValid = Offset.Size() <= 150.f && bRear && Enemy->Alert != EPFAlertState::Combat; break;
	case EPFTakedownType::Heavy: bValid = Enemy->EnemyRole == EPFEnemyRole::Heavy && Offset.Size() <= 150.f && bRear && Enemy->Alert != EPFAlertState::Combat; break;
	case EPFTakedownType::Aerial: bValid = Player->HasSkill(TEXT("Aerial")) && Offset.Z >= 150.f && Offset.Z <= 450.f && Offset.Size2D() <= 150.f; break;
	case EPFTakedownType::Cover: bValid = Player->HasSkill(TEXT("Cover")) && Enemy->bAtCoverCorner && Offset.Size() <= 150.f && Enemy->Alert != EPFAlertState::Combat; break;
	case EPFTakedownType::Chain: bValid = Player->HasSkill(TEXT("Chain")) && HasChainWindow() && Offset.Size() <= 300.f; break;
	case EPFTakedownType::Throw: bValid = Player->HasSkill(TEXT("ExecutionMaster")) && Player->Grenades > 0 && HasChainWindow() && Offset.Size() <= 1200.f; break;
	default: return false;
	}
	if (!bValid) return false;
	FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(PFExecutionLOS), false, Player);
	return !GetWorld()->LineTraceSingleByChannel(Hit, Player->GetAimOrigin(), Enemy->GetActorLocation(), ECC_Visibility, Params) || Hit.GetActor() == Enemy;
}

void UPFTakedownComponent::ServerRequest_Implementation(APFEnemyCharacter* Target, EPFTakedownType Type, uint32 Sequence)
{
	if (!FPFGameRules::IsNewSequence(Sequence, LastSequence)) return; LastSequence = Sequence;
	APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(GetOwner());
	if (!IsEligible(Target, Type) || !Target->ReserveExecution(Player)) return;
	ReservedEnemy = Target;
	const bool bRearMotion = Type == EPFTakedownType::Rear && Player->HasRearExecutionMotion();
	bCrouchedBeforeExecution = bRearMotion && Player->bIsCrouched;
	bRestoreCrouch = bCrouchedBeforeExecution;
	if (Type != EPFTakedownType::Throw)
	{
		FVector Destination = Target->GetActorLocation() - Target->GetActorForwardVector() * 90.f;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PFExecutionAlignment), false, Player); Params.AddIgnoredActor(Target);
		FHitResult Hit;
		const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
		const float HalfHeight = bRearMotion ? Player->GetClass()->GetDefaultObject<APFPlayerCharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() * Capsule->GetShapeScale() : Capsule->GetScaledCapsuleHalfHeight();
		Destination.Z += HalfHeight - Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FVector StandingStart = Player->GetActorLocation() + FVector(0, 0, HalfHeight - Capsule->GetScaledCapsuleHalfHeight());
		const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), HalfHeight);
		if (GetWorld()->SweepSingleByChannel(Hit, StandingStart, Destination, FQuat::Identity, ECC_Pawn, Shape, Params)) { Cancel(); return; }
		if (bCrouchedBeforeExecution)
		{
			Player->UnCrouch(); Player->GetCharacterMovement()->UnCrouch(false);
			if (Player->bIsCrouched) { Cancel(); return; }
		}
		if (!Player->TeleportTo(Destination, Target->GetActorRotation(), false, false)) { Cancel(); return; }
	}
	Player->ClearWeaponStance(); Player->Weapon->Cancel(); Player->Interaction->Cancel();
	const float Now = GetServerTime();
	if (!Target->CommitExecution(Player, bRearMotion ? Now : -100.f)) { Cancel(); return; }
	if (Type == EPFTakedownType::Throw) --Player->Grenades;
	++ActionId; StartTime = Now; ActiveType = Type; PresentationTarget = bRearMotion ? Target : nullptr;
	Player->SetAction(TEXT("Takedown"), FPFGameRules::TakedownSeconds); OnRep_Presentation();
	Player->GetCharacterMovement()->StopMovementImmediately(); Player->GetCharacterMovement()->DisableMovement();
	GetWorld()->GetTimerManager().SetTimer(CleanupTimer, this, &ThisClass::Cancel, FPFGameRules::TakedownSeconds); Player->ForceNetUpdate();
}

void UPFTakedownComponent::Cancel()
{
	GetWorld()->GetTimerManager().ClearTimer(CleanupTimer);
	GetWorld()->GetTimerManager().ClearTimer(ImpactTimer);
	APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(GetOwner());
	if (Player) Player->StopRearExecution();
	if (Player && Player->HasAuthority()) PresentationTarget = nullptr;
	if (ReservedEnemy.IsValid()) ReservedEnemy->ReleaseExecution(Player);
	ReservedEnemy.Reset();
	if (Player && Player->HasAuthority() && Player->ActionName == TEXT("Takedown"))
	{
		if (Player->Health->IsAlive()) Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Player->ClearAction();
	}
	RestoreStance(Player);
}
