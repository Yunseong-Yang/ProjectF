#include "Framework/PFGameMode.h"
#include "Framework/PFGameState.h"
#include "Framework/PFPlayerController.h"
#include "Progression/PFPlayerState.h"
#include "Player/PFPlayerCharacter.h"
#include "Combat/PFHealthComponent.h"
#include "World/PFWorldStateSubsystem.h"
#include "World/PFOutpost.h"
#include "AI/PFEnemyCharacter.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Components/CapsuleComponent.h"
#include "CollisionShape.h"
#include "CollisionQueryParams.h"
#include "GameFramework/CharacterMovementComponent.h"

APFGameMode::APFGameMode()
{
	GameStateClass = APFGameState::StaticClass(); PlayerStateClass = APFPlayerState::StaticClass();
	PlayerControllerClass = APFPlayerController::StaticClass(); DefaultPawnClass = APFPlayerCharacter::StaticClass();
	bStartPlayersAsSpectators = true;
}

void APFGameMode::BeginPlay()
{
	Super::BeginPlay(); GetWorldTimerManager().SetTimer(RecoveryTimer, this, &ThisClass::CheckRecovery, 2.f, true);
}

void APFGameMode::CheckRecovery()
{
	bool bAnyAlive = false;
	TArray<APFPlayerCharacter*> Downed;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APFPlayerController* PC = Cast<APFPlayerController>(It->Get()); if (!PC || !PC->bInitializedProfile) continue;
		if (APFPlayerCharacter* PlayerPawn = Cast<APFPlayerCharacter>(PC->GetPawn()))
		{
			bAnyAlive |= PlayerPawn->Health->IsAlive();
			if (PlayerPawn->Health->Life == EPFLifeState::Downed) Downed.Add(PlayerPawn);
		}
		else RestartPlayer(PC);
	}
	if (!bAnyAlive) for (APFPlayerCharacter* PlayerPawn : Downed) PlayerPawn->Health->Execute();
}

void APFGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& Error)
{
	Super::PreLogin(Options, Address, UniqueId, Error);
	if (!Error.IsEmpty()) return;
	if (GetNumPlayers() + GetNumSpectators() >= FPFGameRules::MaxPlayers) Error = TEXT("방이 가득 찼습니다. 최대 4명입니다.");
	else if (UGameplayStatics::GetIntOption(Options, TEXT("PFBuild"), -1) != FPFGameRules::ProtocolVersion) Error = TEXT("게임 빌드가 호환되지 않습니다.");
}

void APFGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// The bounded profile handshake must complete before a controllable pawn is created.
}

void APFGameMode::AdmitPlayer(APFPlayerController* Controller)
{
	RestartPlayer(Controller);
	// The camp briefing supplies the first objective; other sites still require discovery.
	GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->Discover(TEXT("RadioStation"));
}

void APFGameMode::RestartPlayer(AController* NewPlayer)
{
	APFPlayerController* PC = Cast<APFPlayerController>(NewPlayer); if (!PC || !PC->bInitializedProfile) return;
	FTransform SpawnTransform; bool bFound = false;
	auto Safe = [this](const FVector& Location)
	{
		return GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->IsSafeLocation(Location);
	};
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		if (It->ActorHasTag(TEXT("PF.SafeSpawn")) && Safe(It->GetActorLocation())) { SpawnTransform = It->GetActorTransform(); bFound = true; break; }
	if (!bFound) for (TActorIterator<APFOutpost> It(GetWorld()); It; ++It)
		if (It->IsSafeSpawn()) { SpawnTransform = FTransform(It->GetActorRotation(), It->GetSafeSpawnLocation()); bFound = true; break; }
	if (!bFound) { PC->ClientNotify(FText::FromString(TEXT("안전한 재등장 지점을 기다립니다."))); return; }
	// Profile handshakes may finish after every connection has already joined.
	// Player count is therefore not a unique spawn slot: search vacant floor space.
	const APFPlayerCharacter* Defaults = DefaultPawnClass ? Cast<APFPlayerCharacter>(DefaultPawnClass->GetDefaultObject()) : nullptr;
	if (!Defaults) return;
	const UCapsuleComponent* Capsule = Defaults->GetCapsuleComponent();
	const FVector Anchor = SpawnTransform.GetLocation();
	FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(PFSpawnFloor), false);
	for (TActorIterator<APFPlayerCharacter> It(GetWorld()); It; ++It) GroundParams.AddIgnoredActor(*It);
	const FCollisionQueryParams SpaceParams(SCENE_QUERY_STAT(PFSpawnSpace), false);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
	bFound = false;
	for (int32 Ring = 0; Ring <= 2 && !bFound; ++Ring)
		for (int32 X = -Ring; X <= Ring && !bFound; ++X)
			for (int32 Y = -Ring; Y <= Ring && !bFound; ++Y)
			{
				if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) != Ring) continue;
				FVector Location = Anchor + SpawnTransform.GetRotation().RotateVector(FVector(X * 220.f, Y * 220.f, 0.f));
				FHitResult Floor;
				if (!GetWorld()->LineTraceSingleByChannel(Floor, Location + FVector(0, 0, 250), Location - FVector(0, 0, 600), ECC_Visibility, GroundParams)
					|| Floor.ImpactNormal.Z < Defaults->GetCharacterMovement()->GetWalkableFloorZ()) continue;
				Location.Z = Floor.ImpactPoint.Z + Capsule->GetScaledCapsuleHalfHeight() + 2.f;
				if (!Safe(Location) || GetWorld()->OverlapBlockingTestByProfile(Location, SpawnTransform.GetRotation(), Capsule->GetCollisionProfileName(), Shape, SpaceParams)) continue;
				SpawnTransform.SetLocation(Location); bFound = true;
			}
	if (!bFound) { PC->ClientNotify(FText::FromString(TEXT("재등장할 공간이 비기를 기다립니다."))); return; }
	RestartPlayerAtTransform(PC, SpawnTransform);
	if (APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(PC->GetPawn())) { Player->bProfileReady = true; PC->RestoreSession(); Player->ForceNetUpdate(); }
}

void APFGameMode::Respawn(APFPlayerController* Controller)
{
	if (!Controller) return;
	Controller->CaptureSession(); APawn* OldPawn = Controller->GetPawn(); Controller->UnPossess();
	if (OldPawn) OldPawn->Destroy(); RestartPlayer(Controller);
}

void APFGameMode::Logout(AController* Exiting)
{
	if (APFPlayerController* PC = Cast<APFPlayerController>(Exiting))
	{
		PC->CaptureSession(); if (APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(PC->GetPawn())) Player->CancelActions();
		if (APFPlayerState* State = PC->GetPlayerState<APFPlayerState>()) PC->ClientPersistProfile(State->Profile);
		GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->Save();
	}
	Super::Logout(Exiting);
}

void APFGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(RecoveryTimer);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		if (APFPlayerController* PC = Cast<APFPlayerController>(It->Get())) PC->CaptureSession();
	if (UPFWorldStateSubsystem* World = GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()) World->Save();
	Super::EndPlay(Reason);
}
