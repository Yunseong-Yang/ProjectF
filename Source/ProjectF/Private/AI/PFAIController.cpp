#include "AI/PFAIController.h"
#include "AI/PFEnemyCharacter.h"
#include "AI/PFCoverPoint.h"
#include "Player/PFPlayerCharacter.h"
#include "Combat/PFHealthComponent.h"
#include "World/PFOutpost.h"
#include "World/PFInteractable.h"
#include "Components/StateTreeAIComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "StateTree.h"
#include "TimerManager.h"

APFAIController::APFAIController()
{
	PrimaryActorTick.bCanEverTick = true;
	StateTree = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTree")); StateTree->SetStartLogicAutomatically(false);
	Perception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception")); SetPerceptionComponent(*Perception);
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("Sight"));
	SightConfig->SightRadius = 5000.f; SightConfig->LoseSightRadius = 5500.f; SightConfig->PeripheralVisionAngleDegrees = 50.f;
	SightConfig->SetMaxAge(35.f); SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true; SightConfig->DetectionByAffiliation.bDetectFriendlies = true;
	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("Hearing")); HearingConfig->HearingRange = 12000.f;
	HearingConfig->SetMaxAge(35.f); HearingConfig->DetectionByAffiliation = SightConfig->DetectionByAffiliation;
	Perception->ConfigureSense(*SightConfig); Perception->ConfigureSense(*HearingConfig);
	Perception->SetDominantSense(UAISense_Sight::StaticClass());
}

void APFAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (!HasAuthority()) return;
	PostLocation = InPawn->GetActorLocation();
	Perception->OnTargetPerceptionUpdated.AddDynamic(this, &ThisClass::OnPerceived);
	GetWorldTimerManager().SetTimer(MemoryTimer, this, &ThisClass::UpdateMemory, .2f, true);
	if (Behavior) { StateTree->SetStateTree(Behavior); StateTree->StartLogic(); }
	else UE_LOG(LogTemp, Error, TEXT("ProjectF enemy has no compiled StateTree: %s"), *GetNameSafe(InPawn));
}

void APFAIController::OnUnPossess() { Shutdown(); Super::OnUnPossess(); }
void APFAIController::EndPlay(const EEndPlayReason::Type Reason) { Shutdown(); Super::EndPlay(Reason); }

void APFAIController::Shutdown()
{
	GetWorldTimerManager().ClearTimer(MemoryTimer);
	Perception->OnTargetPerceptionUpdated.RemoveDynamic(this, &ThisClass::OnPerceived);
	StateTree->StopLogic(TEXT("Owner unavailable")); ExitBehavior(); Memories.Reset(); VisibleTarget = nullptr;
}

void APFAIController::OnPerceived(AActor* Actor, FAIStimulus Stimulus)
{
	APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(Actor);
	if (!HasAuthority() || !IsValid(Player)) return;
	FPFTargetMemory& Memory = Memories.FindOrAdd(Player); Memory.Target = Player;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>())
	{
		Memory.bVisible = Stimulus.WasSuccessfullySensed();
		if (Memory.bVisible) Memory.Source = TEXT("Sight");
	}
	else if (Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>() && Stimulus.WasSuccessfullySensed())
	{
		Memory.LastHeard = Stimulus.StimulusLocation; Memory.LastHeardTime = Now; Memory.Source = TEXT("Sound");
	}
}

void APFAIController::UpdateMemory()
{
	APFEnemyCharacter* Enemy = Cast<APFEnemyCharacter>(GetPawn());
	if (!Enemy || !Enemy->Health->IsAlive()) { DesiredState = EPFAlertState::Dead; return; }
	const float Now = GetWorld()->GetTimeSeconds();
	VisibleTarget = nullptr;
	float Closest = TNumericLimits<float>::Max(), Newest = -1000.f;
	bool bHasLastSeen = false, bHasSound = false;
	for (auto It = Memories.CreateIterator(); It; ++It)
	{
		FPFTargetMemory& Memory = It.Value(); APFPlayerCharacter* Target = Memory.Target.Get();
		if (!Target || !Target->Health->IsAlive() || (!Memory.bVisible && Now - FMath::Max(Memory.LastSeenTime, Memory.LastHeardTime) > 45.f)) { It.RemoveCurrent(); continue; }
		const float EvidenceTime = FMath::Max(Memory.LastSeenTime, Memory.LastHeardTime);
		if (Now - EvidenceTime <= 35.f) Target->MarkThreat(EvidenceTime);
		if (Memory.bVisible)
		{
			// Only a currently visible target may refresh its actual position.
			if (!LineOfSightTo(Target)) { Memory.bVisible = false; SearchUntil = Now + 35.f; }
			else
			{
				const float Distance = Enemy->GetDistanceTo(Target);
				Memory.Awareness = FMath::Min(1.f, Memory.Awareness + FPFGameRules::DetectionGain(.2f, Distance, Target->bIsCrouched, Target->bSprinting));
				Target->ReportDetection(Enemy->GetActorLocation(), Memory.Awareness);
				if (Memory.Awareness >= 1.f)
				{
					Memory.LastSeen = Target->GetActorLocation(); Memory.LastSeenTime = Now;
					if (Distance < Closest) { Closest = Distance; VisibleTarget = Target; }
				}
			}
		}
		else Memory.Awareness = FMath::Max(0.f, Memory.Awareness - .12f);
		if (Memory.LastSeenTime > Newest && Now - Memory.LastSeenTime <= 35.f)
		{
			Newest = Memory.LastSeenTime; InvestigationPoint = Memory.LastSeen; bHasLastSeen = true;
		}
		if (!bHasLastSeen && Memory.LastHeardTime > Newest && Now - Memory.LastHeardTime <= 35.f)
		{
			Newest = Memory.LastHeardTime; InvestigationPoint = Memory.LastHeard; bHasSound = true;
		}
	}
	DesiredState = VisibleTarget ? EPFAlertState::Combat : bHasLastSeen || Now < SearchUntil ? EPFAlertState::Search : bHasSound ? EPFAlertState::Investigate : EPFAlertState::Routine;
	if (DesiredState != EPFAlertState::Combat)
	{
		Enemy->bAtCoverCorner = false;
		for (TActorIterator<APFCoverPoint> It(GetWorld()); It; ++It)
			if (Enemy->GetDistanceTo(*It) <= 150.f) { Enemy->bAtCoverCorner = true; break; }
	}
	if (VisibleTarget) { VisibleTarget->MarkThreat(); if (Enemy->Outpost) Enemy->Outpost->Engage(VisibleTarget); ShareReport(); }
	// Bodies are local stimuli, never a broadcast of every player's position.
	if (Now - LastBodyCheckTime >= 1.f)
	{
		LastBodyCheckTime = Now;
		for (TActorIterator<APFEnemyCharacter> It(GetWorld()); It; ++It)
			if (It->Health->Life == EPFLifeState::Dead && Enemy->GetDistanceTo(*It) < 1600.f && !KnownBodies.Contains(*It) && LineOfSightTo(*It))
			{
				KnownBodies.Add(*It);
				if (!VisibleTarget) { InvestigationPoint = It->GetActorLocation(); SearchUntil = Now + 35.f; DesiredState = EPFAlertState::Search; }
			}
	}
}

void APFAIController::ReceiveReport(APFPlayerCharacter* Target, const FVector& Location, float Time)
{
	if (!HasAuthority() || !IsValid(Target) || Location.ContainsNaN()) return;
	FPFTargetMemory& Memory = Memories.FindOrAdd(Target); Memory.Target = Target;
	if (Time <= Memory.LastHeardTime) return;
	Memory.LastHeard = Location; Memory.LastHeardTime = Time; Memory.Source = TEXT("Report");
}

void APFAIController::ShareReport()
{
	const float Now = GetWorld()->GetTimeSeconds(); APFEnemyCharacter* Enemy = Cast<APFEnemyCharacter>(GetPawn());
	if (!Enemy || !VisibleTarget || Now - LastReportTime < 2.f) return;
	LastReportTime = Now;
	const FPFTargetMemory* Memory = Memories.Find(VisibleTarget); if (!Memory) return;
	for (TActorIterator<APFAIController> It(GetWorld()); It; ++It)
		if (*It != this && It->GetPawn() && FVector::DistSquared(It->GetPawn()->GetActorLocation(), Enemy->GetActorLocation()) <= FMath::Square(2500.f))
			It->ReceiveReport(VisibleTarget, Memory->LastSeen, Memory->LastSeenTime);
}

void APFAIController::ReleaseCover()
{
	APFEnemyCharacter* Enemy = Cast<APFEnemyCharacter>(GetPawn());
	if (Cover.IsValid()) Cover->Release(Enemy);
	Cover.Reset(); if (Enemy) { Enemy->UnCrouch(); Enemy->bAtCoverCorner = false; }
}

void APFAIController::UpdateTactics()
{
	APFEnemyCharacter* Enemy = Cast<APFEnemyCharacter>(GetPawn()); if (!Enemy || !VisibleTarget) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Cover.IsValid() && !Cover->IsReservedBy(Enemy)) ReleaseCover();
	if (Now >= NextTacticTime)
	{
		NextTacticTime = Now + (Enemy->EnemyRole == EPFEnemyRole::Heavy ? 9.f : 5.f);
		APFCoverPoint* Best = nullptr; float BestScore = FLT_MAX;
		for (TActorIterator<APFCoverPoint> It(GetWorld()); It; ++It)
		{
			const float Distance = Enemy->GetDistanceTo(*It);
			if (Distance > 2600.f || !It->CanProtectFrom(VisibleTarget->GetAimOrigin(), Enemy)) continue;
			const float Score = Distance + (Cover.Get() == *It ? 300.f : 0.f);
			if (Score < BestScore) { Best = *It; BestScore = Score; }
		}
		if (Best && Best != Cover.Get() && Best->Reserve(Enemy)) { ReleaseCover(); Cover = Best; PeekUntil = Now + 1.8f; }
	}
	if (Cover.IsValid())
	{
		const bool bShelter = FMath::Fmod(Now + Enemy->GetUniqueID() * .17f, 4.5f) < 1.8f;
		const FVector Goal = bShelter ? Cover->GetActorLocation() : Cover->GetPeekLocation();
		if (FVector::DistSquared(Enemy->GetActorLocation(), Goal) > FMath::Square(80.f)) { Enemy->UnCrouch(); MoveToKnownPoint(Goal, 45.f); }
		else
		{
			StopMovement(); Enemy->bAtCoverCorner = true;
			if (bShelter && Cover->bLowCover) Enemy->Crouch();
			else { Enemy->UnCrouch(); Enemy->FireAt(VisibleTarget); }
		}
	}
	else if (Enemy->GetDistanceTo(VisibleTarget) > (Enemy->EnemyRole == EPFEnemyRole::Sentry ? 3500.f : 2200.f)) MoveToKnownPoint(VisibleTarget->GetActorLocation(), 1600.f);
	else
	{
		StopMovement(); Enemy->FireAt(VisibleTarget);
		if (Now >= PeekUntil && Enemy->EnemyRole == EPFEnemyRole::Soldier)
		{
			PeekUntil = Now + 5.f;
			const FVector Side = FVector::CrossProduct((VisibleTarget->GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal2D(), FVector::UpVector);
			if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
			{
				FNavLocation Point;
				if (Nav->ProjectPointToNavigation(Enemy->GetActorLocation() + Side * (Enemy->GetUniqueID() % 2 ? 450.f : -450.f), Point, FVector(150, 150, 250))) MoveToKnownPoint(Point.Location, 60.f);
			}
		}
	}
}

bool APFAIController::MoveToKnownPoint(const FVector& Location, float Acceptance)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextMoveTime && FVector::DistSquared(LastMoveGoal, Location) < 10000.f) return false;
	NextMoveTime = Now + 1.f; LastMoveGoal = Location;
	// POI centers can be inside solid terminals or cover. Find nearby floor on
	// the same story; the engine's narrow default projection can reject it.
	EPathFollowingRequestResult::Type Result = EPathFollowingRequestResult::Failed;
	FNavLocation Destination;
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (Nav && Nav->ProjectPointToNavigation(Location, Destination, FVector(300.f, 300.f, 160.f), &GetNavAgentPropertiesRef()))
		Result = MoveToLocation(Destination.Location, Acceptance, true, true, false, false, nullptr, false);
	if (Result == EPathFollowingRequestResult::Failed)
	{
		NextMoveTime = Now + 2.f;
		if (APFEnemyCharacter* Enemy = Cast<APFEnemyCharacter>(GetPawn())) if (Enemy->Outpost) Enemy->Outpost->ReleaseAlarm(Enemy);
	}
	return Result == EPathFollowingRequestResult::AlreadyAtGoal;
}

void APFAIController::PerformBehavior(EPFAlertState State, float DeltaTime)
{
	APFEnemyCharacter* Enemy = Cast<APFEnemyCharacter>(GetPawn());
	if (!HasAuthority() || !Enemy || !Enemy->Health->IsAlive() || Enemy->IsExecutionReserved()) return;
	Enemy->Alert = State;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Enemy->bReinforcement && !Enemy->bHasArrived) Enemy->TryArrive();
	if (Enemy->bAdvancingToPost && !VisibleTarget)
	{
		Enemy->GetCharacterMovement()->MaxWalkSpeed = 400.f;
		if (FVector::DistSquared(Enemy->GetActorLocation(), Enemy->EntryDestination) > FMath::Square(2000.f)) { MoveToKnownPoint(Enemy->EntryDestination, 150.f); return; }
		Enemy->bAdvancingToPost = false; Enemy->PatrolPoints = {Enemy->GetActorLocation(), Enemy->EntryDestination};
	}
	if (State == EPFAlertState::Combat && VisibleTarget)
	{
		Enemy->GetCharacterMovement()->MaxWalkSpeed = 400.f;
		APFOutpost* Outpost = Enemy->Outpost;
		if (Outpost && !Outpost->bAlarmCalled && IsValid(Outpost->Alarm) && !Outpost->Alarm->bDestroyed)
		{
			if (!Outpost->AlarmCaller && Now >= AlarmAttemptTime) { Outpost->ReserveAlarm(Enemy); AlarmAttemptTime = Now + 10.f; }
			if (Outpost->AlarmCaller == Enemy)
			{
				MoveToKnownPoint(Outpost->Alarm->GetActorLocation(), 100.f);
				if (Enemy->GetDistanceTo(Outpost->Alarm) <= 180.f) { StopMovement(); Outpost->BeginAlarm(Enemy); }
				return;
			}
		}
		SetFocus(VisibleTarget);
		UpdateTactics();
	}
	else if (State == EPFAlertState::Search || State == EPFAlertState::Investigate)
	{
		Enemy->GetCharacterMovement()->MaxWalkSpeed = 250.f;
		MoveToKnownPoint(InvestigationPoint);
		if (FVector::DistSquared(Enemy->GetActorLocation(), InvestigationPoint) < 250.f * 250.f)
			SetFocalPoint(Enemy->GetActorLocation() + FRotator(0.f, FMath::Fmod(Now * 40.f, 360.f), 0.f).Vector() * 500.f);
	}
	else if (State == EPFAlertState::Routine)
	{
		Enemy->GetCharacterMovement()->MaxWalkSpeed = 180.f;
		if (Enemy->PatrolPoints.IsEmpty()) { MoveToKnownPoint(PostLocation); return; }
		const FVector Goal = Enemy->PatrolPoints[PatrolIndex % Enemy->PatrolPoints.Num()];
		// Use the same projected destination and acceptance as the movement request.
		if (MoveToKnownPoint(Goal, 60.f)) { ++PatrolIndex; NextMoveTime = Now + 2.f; }
	}
}

void APFAIController::ExitBehavior()
{
	ReleaseCover();
	StopMovement(); ClearFocus(EAIFocusPriority::Gameplay); LastMoveGoal = FVector(FLT_MAX); NextMoveTime = 0.f;
	if (APFEnemyCharacter* Enemy = Cast<APFEnemyCharacter>(GetPawn())) if (Enemy->Outpost) Enemy->Outpost->ReleaseAlarm(Enemy);
}
