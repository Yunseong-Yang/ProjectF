#include "World/PFWorldStateSubsystem.h"
#include "World/PFOutpost.h"
#include "Framework/PFGameState.h"
#include "Framework/PFPlayerController.h"
#include "Progression/PFPlayerState.h"
#include "Save/PFSaveSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "AI/PFEnemyCharacter.h"
#include "Combat/PFHealthComponent.h"
#include "GameFramework/Pawn.h"

bool UPFWorldStateSubsystem::DoesSupportWorldType(EWorldType::Type Type) const { return Type == EWorldType::Game || Type == EWorldType::PIE; }

void UPFWorldStateSubsystem::Load()
{
	if (bLoaded || GetWorld()->GetNetMode() == NM_Client) return;
	bLoaded = true;
	if (UPFSaveSubsystem* Saves = GetWorld()->GetGameInstance()->GetSubsystem<UPFSaveSubsystem>()) Records = Saves->WorldSave->Outposts;
	// In-flight battles are restarted after load; completed world transactions are retained.
	for (FPFOutpostRecord& Record : Records) if (Record.Phase == EPFOutpostPhase::Engaged) Record.Phase = EPFOutpostPhase::Discovered;
}

void UPFWorldStateSubsystem::OnWorldBeginPlay(UWorld& InWorld) { Super::OnWorldBeginPlay(InWorld); Load(); Publish(); }

bool UPFWorldStateSubsystem::RegisterOutpost(APFOutpost* Outpost)
{
	if (!Outpost || !Outpost->HasAuthority() || Outpost->OutpostId.IsNone()) return false;
	Load();
	if (const TWeakObjectPtr<APFOutpost>* Existing = LoadedOutposts.Find(Outpost->OutpostId))
		if (Existing->IsValid() && Existing->Get() != Outpost) return false;
	LoadedOutposts.Add(Outpost->OutpostId, Outpost);
	if (!Find(Outpost->OutpostId))
	{
		FPFOutpostRecord Record; Record.Id = Outpost->OutpostId; Record.Facility = Outpost->Facility;
		Record.Title = Outpost->DisplayName; Record.MapLocation = Outpost->GetActorLocation();
		Record.Supports = Outpost->Supports; Records.Add(Record);
	}
	Publish(); return true;
}

void UPFWorldStateSubsystem::UnregisterOutpost(APFOutpost* Outpost)
{
	if (Outpost && LoadedOutposts.FindRef(Outpost->OutpostId).Get() == Outpost) LoadedOutposts.Remove(Outpost->OutpostId);
}

const FPFOutpostRecord* UPFWorldStateSubsystem::Find(FName Id) const
{
	return Records.FindByPredicate([Id](const FPFOutpostRecord& Record) { return Record.Id == Id; });
}

void UPFWorldStateSubsystem::Discover(FName Id)
{
	if (GetWorld()->GetNetMode() == NM_Client) return;
	if (FPFOutpostRecord* Record = Records.FindByPredicate([Id](const FPFOutpostRecord& R) { return R.Id == Id; }))
		if (Record->Phase == EPFOutpostPhase::Undiscovered) { Record->Phase = EPFOutpostPhase::Discovered; Publish(); }
}

void UPFWorldStateSubsystem::SetEngaged(FName Id)
{
	if (GetWorld()->GetNetMode() == NM_Client) return;
	if (FPFOutpostRecord* Record = Records.FindByPredicate([Id](const FPFOutpostRecord& R) { return R.Id == Id; }))
		if (Record->Phase != EPFOutpostPhase::Captured) { Record->Phase = EPFOutpostPhase::Engaged; Publish(); }
}

bool UPFWorldStateSubsystem::CompleteOutpost(FName Id, const TArray<APFPlayerState*>& Participants, const TSet<FString>& EligibleIds)
{
	if (GetWorld()->GetNetMode() == NM_Client) return false;
	FPFOutpostRecord* Record = Records.FindByPredicate([Id](const FPFOutpostRecord& R) { return R.Id == Id; });
	if (!Record || Record->Phase == EPFOutpostPhase::Captured || Participants.IsEmpty()) return false;
	UPFSaveSubsystem* Saves = GetWorld()->GetGameInstance()->GetSubsystem<UPFSaveSubsystem>();
	if (!Saves) return false;
	const FPFOutpostRecord Previous = *Record;
	Record->Phase = EPFOutpostPhase::Captured; Record->CompletionId = FGuid::NewGuid();
	FPFPendingReward Reward; Reward.RewardId = FName(*(TEXT("Outpost.") + Id.ToString()));
	Reward.Participants = EligibleIds.Array();
	for (APFPlayerState* Player : Participants)
		if (IsValid(Player) && !Player->Profile.PlayerId.IsEmpty()) Reward.Participants.AddUnique(Player->Profile.PlayerId);
	if (Reward.Participants.IsEmpty()) { *Record = Previous; return false; }
	Saves->WorldSave->Rewards.Add(Reward);
	Saves->WorldSave->Outposts = Records;
	// Persist the completion and its reward ledger together, before exposing success.
	if (!Saves->SaveWorld()) { Saves->WorldSave->Rewards.Pop(); *Record = Previous; Saves->WorldSave->Outposts = Records; return false; }
	for (APFPlayerState* Player : Participants) ApplyPendingRewards(Player);
	Save(); Publish(); return true;
}

bool UPFWorldStateSubsystem::ApplyPendingRewards(APFPlayerState* Player)
{
	if (!IsValid(Player) || !Player->HasAuthority()) return false;
	UPFSaveSubsystem* Saves = GetWorld()->GetGameInstance()->GetSubsystem<UPFSaveSubsystem>();
	bool bChanged = false;
	for (const FPFPendingReward& Reward : Saves->WorldSave->Rewards)
		if (Reward.Participants.Contains(Player->Profile.PlayerId)) bChanged |= Player->GrantReward(Reward.RewardId, Reward.Experience, Reward.Parts, Reward.Supplies);
	return bChanged;
}

bool UPFWorldStateSubsystem::IsCommunicationsDisrupted(FName Target) const
{
	return Records.ContainsByPredicate([Target](const FPFOutpostRecord& R)
	{
		return R.Facility == EPFFacilityType::Communications && R.Phase == EPFOutpostPhase::Captured && R.Supports.Contains(Target);
	});
}

bool UPFWorldStateSubsystem::IsSafeLocation(const FVector& Location) const
{
	for (TActorIterator<APFEnemyCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->Health->IsAlive()) continue;
		const FVector Offset = Location + FVector(0.f, 0.f, 64.f) - It->GetPawnViewLocation();
		if (Offset.SizeSquared() < FMath::Square(2500.f)) return false;
		if (Offset.SizeSquared() > FMath::Square(5500.f)) continue;
		if (FVector::DotProduct(It->GetViewRotation().Vector(), Offset.GetSafeNormal()) < FMath::Cos(FMath::DegreesToRadians(50.f))) continue;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PFSafeSpawn), false, *It); FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, It->GetPawnViewLocation(), Location + FVector(0, 0, 64), ECC_Visibility, Params)
			|| Cast<APawn>(Hit.GetActor())) return false;
	}
	return true;
}

bool UPFWorldStateSubsystem::Save()
{
	if (GetWorld()->GetNetMode() == NM_Client) return false;
	Load();
	UPFSaveSubsystem* Saves = GetWorld()->GetGameInstance()->GetSubsystem<UPFSaveSubsystem>();
	Saves->WorldSave->Outposts = Records;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		if (APFPlayerController* PC = Cast<APFPlayerController>(It->Get())) PC->CaptureSession();
	for (TActorIterator<APFPlayerState> It(GetWorld()); It; ++It)
	{
		if (It->Profile.PlayerId.IsEmpty()) continue;
		FPFProfile* Existing = Saves->WorldSave->Profiles.FindByPredicate([&It](const FPFProfile& Profile) { return Profile.PlayerId == It->Profile.PlayerId; });
		if (Existing) *Existing = It->Profile; else Saves->WorldSave->Profiles.Add(It->Profile);
	}
	return Saves->SaveWorld();
}

void UPFWorldStateSubsystem::Publish()
{
	if (GetWorld()->GetNetMode() == NM_Client) return;
	if (APFGameState* State = GetWorld()->GetGameState<APFGameState>())
	{
		State->Outposts = Records;
		State->WorldId = GetWorld()->GetGameInstance()->GetSubsystem<UPFSaveSubsystem>()->WorldSave->WorldId;
		State->ForceNetUpdate();
	}
}
