#include "World/PFOutpost.h"
#include "World/PFInteractable.h"
#include "World/PFWorldStateSubsystem.h"
#include "AI/PFEnemyCharacter.h"
#include "Combat/PFHealthComponent.h"
#include "Player/PFPlayerCharacter.h"
#include "Progression/PFPlayerState.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

APFOutpost::APFOutpost()
{
	bReplicates = true; bAlwaysRelevant = true; PrimaryActorTick.bCanEverTick = false; SetNetUpdateFrequency(5.f);
	Terminal = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Terminal")); SetRootComponent(Terminal);
	AlarmSpeaker = CreateDefaultSubobject<UAudioComponent>(TEXT("AlarmSpeaker")); AlarmSpeaker->SetupAttachment(Terminal); AlarmSpeaker->bAutoActivate = false;
	EnemyClass = APFEnemyCharacter::StaticClass();
}

void APFOutpost::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority()) return;
	UPFWorldStateSubsystem* World = GetWorld()->GetSubsystem<UPFWorldStateSubsystem>();
	if (!World->RegisterOutpost(this)) { UE_LOG(LogTemp, Error, TEXT("Invalid or duplicate outpost ID: %s"), *OutpostId.ToString()); SetActorEnableCollision(false); return; }
	if (const FPFOutpostRecord* Record = World->Find(OutpostId)) Phase = Record->Phase;
	if (Phase != EPFOutpostPhase::Captured) SpawnGarrison();
}

void APFOutpost::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(AlarmTimer); GetWorldTimerManager().ClearTimer(ReinforcementTimer);
	if (HasAuthority()) if (UPFWorldStateSubsystem* World = GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()) World->UnregisterOutpost(this);
	Super::EndPlay(Reason);
}

void APFOutpost::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APFOutpost, Phase); DOREPLIFETIME(APFOutpost, RemainingThreats); DOREPLIFETIME(APFOutpost, BattleScale);
	DOREPLIFETIME(APFOutpost, ReinforcementTime); DOREPLIFETIME(APFOutpost, AlarmStartTime);
	DOREPLIFETIME(APFOutpost, bAlarmCalled); DOREPLIFETIME(APFOutpost, AlarmCaller);
	DOREPLIFETIME(APFOutpost, AlarmSentTime);
}

bool APFOutpost::RegisterEnemy(APFEnemyCharacter* Enemy, FName ThreatId)
{
	if (!HasAuthority() || !IsValid(Enemy) || ThreatId.IsNone() || Phase == EPFOutpostPhase::Captured) return false;
	if (FPFThreatEntry* Existing = Threats.Find(ThreatId))
	{
		if (Existing->bResolved || Existing->Enemy.IsValid()) return false;
		Existing->Enemy = Enemy; return true;
	}
	FPFThreatEntry Entry; Entry.Enemy = Enemy; Threats.Add(ThreatId, Entry); ++RemainingThreats; ForceNetUpdate(); return true;
}

void APFOutpost::ResolveEnemy(FName ThreatId, APFEnemyCharacter* Enemy)
{
	if (!HasAuthority()) return;
	FPFThreatEntry* Entry = Threats.Find(ThreatId);
	if (!Entry || Entry->bResolved || Entry->Enemy.Get() != Enemy) return;
	Entry->bResolved = true; RemainingThreats = FMath::Max(0, RemainingThreats - 1);
	ReleaseAlarm(Enemy); ForceNetUpdate();
}

int32 APFOutpost::CountParticipants() const
{
	int32 Count = 0;
	for (TActorIterator<APFPlayerCharacter> It(GetWorld()); It; ++It)
		if (It->bProfileReady && It->Health->IsAlive() && FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) <= FMath::Square(ParticipationRadius)) ++Count;
	return FMath::Clamp(Count, 1, FPFGameRules::MaxPlayers);
}

void APFOutpost::RecordContribution(APFPlayerCharacter* Player)
{
	if (!HasAuthority() || !IsValid(Player)) return;
	if (APFPlayerState* State = Player->GetPlayerState<APFPlayerState>())
		if (!State->Profile.PlayerId.IsEmpty()) ContributorIds.Add(State->Profile.PlayerId);
}

void APFOutpost::Engage(APFPlayerCharacter* Player)
{
	if (!HasAuthority() || Phase == EPFOutpostPhase::Captured) return;
	RecordContribution(Player);
	if (Phase == EPFOutpostPhase::Engaged) return;
	Phase = EPFOutpostPhase::Engaged; BattleScale = FPFGameRules::ScaleFor(CountParticipants());
	for (const TPair<FName, FPFThreatEntry>& Pair : Threats) if (Pair.Value.Enemy.IsValid()) Pair.Value.Enemy->InitializeCombatScale(BattleScale);
	// Additional defenders use authored, unseen entry points; visible points are skipped.
	if (BattleScale.Garrison > 8) SpawnGroup(ReinforcementSpawns, BattleScale.Garrison - 8, TEXT("Defender"), BattleScale);
	GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->SetEngaged(OutpostId); ForceNetUpdate();
}

void APFOutpost::SpawnGarrison()
{
	if (bGarrisonSpawned) return;
	bGarrisonSpawned = true; SpawnGroup(GarrisonSpawns, 8, TEXT("Garrison"), FPFGameRules::ScaleFor(1));
}

int32 APFOutpost::SpawnGroup(const TArray<FTransform>& Spawns, int32 Count, const TCHAR* Prefix, const FPFCombatScale& Scale)
{
	if (!HasAuthority() || !EnemyClass) return 0;
	int32 Spawned = 0;
	for (int32 Index = 0; Index < Spawns.Num() && Spawned < Count; ++Index)
	{
		const FTransform& Transform = Spawns[Index];
		const FName SpawnId(*FString::Printf(TEXT("%s.%s.%d"), *OutpostId.ToString(), Prefix, Index));
		if (SpawnedThreatIds.Contains(SpawnId)) continue;
		if (Phase == EPFOutpostPhase::Engaged)
		{
			bool bVisible = false;
			for (TActorIterator<APFPlayerCharacter> It(GetWorld()); It; ++It)
			{
				const FVector Offset = Transform.GetLocation() - It->GetAimOrigin();
				if (FVector::DotProduct(It->GetAimDirection(), Offset.GetSafeNormal()) < .25f) continue;
				FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(PFSpawnVisibility), false, *It);
				if (!GetWorld()->LineTraceSingleByChannel(Hit, It->GetAimOrigin(), Transform.GetLocation(), ECC_Visibility, Params)) { bVisible = true; break; }
			}
			if (bVisible) continue;
		}
		APFEnemyCharacter* Enemy = GetWorld()->SpawnActorDeferred<APFEnemyCharacter>(EnemyClass, Transform, this, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
		if (!Enemy) continue;
		Enemy->Outpost = this; Enemy->ThreatId = SpawnId;
		Enemy->bReinforcement = FCString::Strcmp(Prefix, TEXT("Reinforcement")) == 0;
		Enemy->bAdvancingToPost = Enemy->bReinforcement || FCString::Strcmp(Prefix, TEXT("Defender")) == 0;
		Enemy->EntryDestination = GetActorLocation();
		Enemy->EnemyRole = Enemy->bReinforcement ? EPFEnemyRole::Soldier : Index == 7 ? EPFEnemyRole::Heavy : Index == 3 ? EPFEnemyRole::Sentry : EPFEnemyRole::Soldier;
		Enemy->PatrolPoints = {Transform.GetLocation(), Transform.GetLocation() + Transform.GetRotation().GetForwardVector() * 450.f};
		if (FCString::Strcmp(Prefix, TEXT("Garrison")) == 0)
		{
			const FPFGarrisonPatrolRoute* Route = GarrisonPatrolRoutes.FindByPredicate([Index](const FPFGarrisonPatrolRoute& Item) { return Item.SpawnIndex == Index; });
			if (Route && Route->Waypoints.Num() >= 2 && !Route->Waypoints.ContainsByPredicate([](const FVector& Point) { return Point.ContainsNaN(); }))
				Enemy->PatrolPoints = Route->Waypoints;
		}
		Enemy->FinishSpawning(Transform);
		if (!IsValid(Enemy)) continue;
		SpawnedThreatIds.Add(SpawnId);
		if (Phase == EPFOutpostPhase::Engaged) Enemy->InitializeCombatScale(Scale);
		++Spawned;
	}
	return Spawned;
}

bool APFOutpost::CanCapture(APFPlayerCharacter* Player) const
{
	return HasAuthority() && IsValid(Player) && Player->Health->IsAlive() && Player->bProfileReady
		&& (Player->ActionName.IsNone() || Player->ActionName == TEXT("Interaction")) && Phase != EPFOutpostPhase::Captured
		&& bGarrisonSpawned && !GarrisonSpawns.IsEmpty() && RemainingThreats == 0;
}

bool APFOutpost::Capture(APFPlayerCharacter* Player)
{
	if (!CanCapture(Player)) return false;
	RecordContribution(Player);
	TArray<APFPlayerState*> Participants;
	for (TActorIterator<APFPlayerState> It(GetWorld()); It; ++It) if (ContributorIds.Contains(It->Profile.PlayerId)) Participants.Add(*It);
	if (!GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->CompleteOutpost(OutpostId, Participants, ContributorIds)) return false;
	for (FName Supported : Supports) GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->Discover(Supported);
	Phase = EPFOutpostPhase::Captured; CancelAlarm(); GetWorldTimerManager().ClearTimer(ReinforcementTimer); PendingReinforcements = 0; ReinforcementTime = 0.f; ForceNetUpdate(); return true;
}

bool APFOutpost::ReserveAlarm(APFEnemyCharacter* Enemy)
{
	if (!HasAuthority() || !IsValid(Enemy) || !Enemy->Health->IsAlive() || bAlarmCalled || AlarmCaller || !IsValid(Alarm) || Alarm->bDestroyed || Phase != EPFOutpostPhase::Engaged) return false;
	AlarmCaller = Enemy; ForceNetUpdate(); return true;
}

void APFOutpost::ReleaseAlarm(APFEnemyCharacter* Enemy) { if (HasAuthority() && AlarmCaller == Enemy) CancelAlarm(); }

bool APFOutpost::BeginAlarm(APFEnemyCharacter* Enemy)
{
	if (!HasAuthority() || AlarmCaller != Enemy || !IsValid(Alarm) || Alarm->bDestroyed || !Enemy->Health->IsAlive() || Enemy->GetDistanceTo(Alarm) > 180.f) return false;
	if (GetWorldTimerManager().IsTimerActive(AlarmTimer)) return true;
	AlarmStartTime = GetWorld()->GetTimeSeconds(); GetWorldTimerManager().SetTimer(AlarmTimer, this, &ThisClass::FinishAlarm, 4.f); OnRep_AlarmState(); ForceNetUpdate(); return true;
}

void APFOutpost::CancelAlarm()
{
	if (!HasAuthority()) return;
	GetWorldTimerManager().ClearTimer(AlarmTimer); AlarmCaller = nullptr; AlarmStartTime = 0.f; OnRep_AlarmState(); ForceNetUpdate();
}

void APFOutpost::FinishAlarm()
{
	if (!IsValid(AlarmCaller) || !IsValid(Alarm) || Alarm->bDestroyed || !AlarmCaller->Health->IsAlive() || AlarmCaller->GetDistanceTo(Alarm) > 180.f) { CancelAlarm(); return; }
	bAlarmCalled = true; AlarmSentTime = GetWorld()->GetTimeSeconds(); CancelAlarm();
	const float Delay = GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->IsCommunicationsDisrupted(OutpostId) ? 75.f : 30.f;
	ReinforcementTime = GetWorld()->GetTimeSeconds() + Delay;
	PendingReinforcements = 4;
	GetWorldTimerManager().SetTimer(ReinforcementTimer, this, &ThisClass::SpawnReinforcements, Delay); ForceNetUpdate();
}

void APFOutpost::SpawnReinforcements()
{
	ReinforcementTime = 0.f;
	if (Phase != EPFOutpostPhase::Captured && PendingReinforcements > 0)
	{
		PendingReinforcements -= SpawnGroup(ReinforcementSpawns, PendingReinforcements, TEXT("Reinforcement"), FPFGameRules::ScaleFor(CountParticipants()));
		if (PendingReinforcements > 0)
		{
			ReinforcementTime = GetWorld()->GetTimeSeconds() + 3.f;
			GetWorldTimerManager().SetTimer(ReinforcementTimer, this, &ThisClass::SpawnReinforcements, 3.f);
		}
	}
	ForceNetUpdate();
}

bool APFOutpost::IsSafeSpawn() const
{
	if (Phase != EPFOutpostPhase::Captured) return false;
	return GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->IsSafeLocation(GetSafeSpawnLocation());
}

FVector APFOutpost::GetSafeSpawnLocation() const { return GetActorLocation() + GetActorForwardVector() * 250.f + FVector(0.f, 0.f, 100.f); }

void APFOutpost::OnRep_AlarmState()
{
	if (GetNetMode() == NM_DedicatedServer) return;
	const AGameStateBase* State = GetWorld()->GetGameState(); const float Now = State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	if (Phase == EPFOutpostPhase::Captured) { AlarmSpeaker->Stop(); return; }
	if (bAlarmCalled && !bPlayedSentSound)
	{
		bPlayedSentSound = true; AlarmSpeaker->Stop();
		if (AlarmSentSound && Now - AlarmSentTime < 3.f) { AlarmSpeaker->SetSound(AlarmSentSound); AlarmSpeaker->Play(FMath::Max(0.f, Now - AlarmSentTime)); }
	}
	else if (!bAlarmCalled && AlarmStartTime > 0.f && AlarmStartTime != AudibleCallStart)
	{
		AudibleCallStart = AlarmStartTime; AlarmSpeaker->Stop();
		if (AlarmCallingSound) { AlarmSpeaker->SetSound(AlarmCallingSound); AlarmSpeaker->Play(FMath::Max(0.f, Now - AlarmStartTime)); }
	}
	else if (!bAlarmCalled && AlarmStartTime <= 0.f) AlarmSpeaker->Stop();
}
