#include "Framework/PFNetValidationComponent.h"
#include "Framework/PFPlayerController.h"
#include "Framework/PFGameState.h"
#include "Player/PFPlayerCharacter.h"
#include "Progression/PFPlayerState.h"
#include "Combat/PFWeaponComponent.h"
#include "Combat/PFHealthComponent.h"
#include "Combat/PFTakedownComponent.h"
#include "Interaction/PFInteractionComponent.h"
#include "AI/PFEnemyCharacter.h"
#include "AI/PFAIController.h"
#include "Perception/AISense_Hearing.h"
#include "World/PFOutpost.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "HAL/PlatformMisc.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Components/StateTreeAIComponent.h"
#include "StateTree.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "HAL/FileManager.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UI/PFFrontEndWidget.h"
#include "UI/PFHUDWidget.h"
#include "UI/PFUserSettings.h"
#include "Navigation/PathFollowingComponent.h"
#include "Components/CapsuleComponent.h"
#include "CollisionShape.h"
#include "CollisionQueryParams.h"
#include "Framework/Application/SlateApplication.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

namespace
{
bool IsActiveBattleThreat(const APFEnemyCharacter& Enemy, float Now)
{
	if (!Enemy.Health->IsAlive() || !IsValid(Enemy.Outpost) || Enemy.Outpost->Phase != EPFOutpostPhase::Engaged
		|| Enemy.GetDistanceTo(Enemy.Outpost) > 4500.f || Enemy.Alert == EPFAlertState::Routine || Enemy.Alert == EPFAlertState::Dead) return false;
	const APFAIController* AI = Cast<APFAIController>(Enemy.GetController());
	if (!AI) return false;
	for (const auto& Pair : AI->GetMemories())
	{
		const FPFTargetMemory& Memory = Pair.Value; const APFPlayerCharacter* Player = Memory.Target.Get();
		if (Player && Player->bProfileReady && Player->Health->IsAlive() && Now - FMath::Max(Memory.LastSeenTime, Memory.LastHeardTime) <= 5.f) return true;
	}
	return false;
}
}

UPFNetValidationComponent::UPFNetValidationComponent()
{
	PrimaryComponentTick.bCanEverTick = true; PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.f; SetIsReplicatedByDefault(true);
}

void UPFNetValidationComponent::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	bTempleTraversal = FParse::Param(FCommandLine::Get(), TEXT("PFTempleTraversal"));
	bEnabled = FParse::Param(FCommandLine::Get(), TEXT("PFNetSmoke"));
	bVisualCapture = FParse::Param(FCommandLine::Get(), TEXT("PFVisualCapture"));
	bFrameProbe = FParse::Param(FCommandLine::Get(), TEXT("PFFrameProbe"));
	bStress = FParse::Param(FCommandLine::Get(), TEXT("PFStress")); bStressSplit = FParse::Param(FCommandLine::Get(), TEXT("PFStressSplit"));
	bFrontEndCheck = FParse::Param(FCommandLine::Get(), TEXT("PFFrontEndCheck"));
	bExecutionCapture = FParse::Param(FCommandLine::Get(), TEXT("PFExecutionCapture")); bExecutionFirstPerson = FParse::Param(FCommandLine::Get(), TEXT("PFExecutionFirstPerson"));
	FParse::Value(FCommandLine::Get(), TEXT("PFNetExpected="), ExpectedPlayers);
	ExpectedPlayers = FMath::Clamp(ExpectedPlayers, 1, 4); StageTime = GetWorld()->GetTimeSeconds(); SetComponentTickEnabled(bEnabled || bVisualCapture || bFrameProbe || bStress || bFrontEndCheck || bExecutionCapture || bTempleTraversal);
#endif
}

void UPFNetValidationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (TempleTraversalController.IsValid()) TempleTraversalController->Destroy();
	if (TempleTraversalPawn.IsValid()) TempleTraversalPawn->Destroy();
	GetWorld()->GetTimerManager().ClearTimer(BurstStopTimer); Super::EndPlay(Reason);
}

void UPFNetValidationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick)
{
	Super::TickComponent(DeltaTime, TickType, ThisTick);
	APFPlayerController* PC = Cast<APFPlayerController>(GetOwner()); if (!PC || !PC->IsLocalController()) return;
	if (bTempleTraversal) { if (PC->HasAuthority()) CheckTempleTraversal(GetWorld()->GetTimeSeconds()); return; }
	if (bFrontEndCheck) { CheckFrontEnd(PC, GetWorld()->GetTimeSeconds() - StageTime); return; }
	if (bExecutionCapture) { CaptureExecution(PC, GetWorld()->GetTimeSeconds() - StageTime); return; }
#if WITH_EDITOR
	if (bVisualCapture && !bVisualAssetsReady && FParse::Param(FCommandLine::Get(), TEXT("PFWaitForAssets")))
	{
		const int32 Assets = FAssetCompilingManager::Get().GetNumRemainingAssets();
		const bool bShaders = GShaderCompilingManager && GShaderCompilingManager->IsCompiling();
		const float Now = GetWorld()->GetTimeSeconds();
		if (Assets > 0 || bShaders)
		{
			if (Now - LastVisualAssetLog > 5.f) { LastVisualAssetLog = Now; UE_LOG(LogTemp, Display, TEXT("PF_VISUAL_PREPARING assets=%d shaders=%d"), Assets, bShaders); }
			if (Now - StageTime > 600.f) { bVisualCapture = false; FPlatformMisc::RequestExitWithStatus(false, 1); }
			return;
		}
		bVisualAssetsReady = true; StageTime = Now;
	}
#endif
	if (bStress && PC->HasAuthority()) UpdateStress(GetWorld()->GetTimeSeconds());
	if (!bReviewCameraReady && (bVisualCapture || bFrameProbe))
	{
		FString Position; TArray<FString> Parts;
		const bool bExplicitCamera = FParse::Value(FCommandLine::Get(), TEXT("PFReviewCamera="), Position, false) && Position.ParseIntoArray(Parts, TEXT(","), true) == 5;
		if (PC->GetPawn() || bExplicitCamera)
		{
			bReviewCameraReady = true; PC->SetIgnoreLookInput(true); PC->SetIgnoreMoveInput(true);
			if (const APawn* Pawn = PC->GetPawn()) PC->SetControlRotation(FRotator(0, Pawn->GetActorRotation().Yaw, 0));
			if (bExplicitCamera)
			{
				const FVector Point(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]));
				ACameraActor* Camera = GetWorld()->SpawnActor<ACameraActor>(Point, FRotator(FCString::Atof(*Parts[3]), FCString::Atof(*Parts[4]), 0));
				if (Camera) { Camera->GetCameraComponent()->SetFieldOfView(85.f); PC->SetViewTarget(Camera); }
			}
		}
	}
	if (bFrameProbe && (!bStress || StressPhase >= 2)) SamplePerformance(GetWorld()->GetTimeSeconds() - StageTime);
	if (bVisualCapture)
	{
		const float Elapsed = GetWorld()->GetTimeSeconds() - StageTime;
		float Warmup = 6.f; FParse::Value(FCommandLine::Get(), TEXT("PFVisualWarmup="), Warmup);
#if WITH_EDITOR
		if (FParse::Param(FCommandLine::Get(), TEXT("PFViewActions")))
		{
			APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(PC->GetPawn());
			if (!Player) return;
			if (VisualStage == 0 && FParse::Param(FCommandLine::Get(), TEXT("PFViewPistol")) && Player->Weapon->State.Type != EPFWeaponType::Pistol)
			{ Player->Weapon->ServerEquip(EPFWeaponType::Pistol, 900); return; }
			auto Capture = [](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/") / Name, true, false); };
			if (VisualStage == 0 && Elapsed > Warmup) { Capture(TEXT("PF_ViewIdle.png")); VisualStage = 1; }
			else if (VisualStage == 1 && Elapsed > Warmup + 1.f) { Player->ServerSetMovementState(false, true); VisualStage = 2; }
			else if (VisualStage == 2 && Elapsed > Warmup + 2.f)
			{
				int32 Width = 0, Height = 0; PC->GetViewportSize(Width, Height); FVector2D Screen;
				const bool Projected = PC->ProjectWorldLocationToScreen(Player->GetViewSightLocation(), Screen);
				const float Error = Projected ? FVector2D::Distance(Screen, FVector2D(Width * .5f, Height * .5f)) : UE_BIG_NUMBER;
				UE_LOG(LogTemp, Display, TEXT("PF_VIEW_AIM pixels=%.2f point=%s"), Error, *Player->GetViewSightLocation().ToString());
				Capture(TEXT("PF_ViewAim.png")); VisualStage = 3;
			}
			else if (VisualStage == 3 && Elapsed > Warmup + 3.f)
			{
				Player->Weapon->ServerSetTrigger(true, 1001); Player->Weapon->ServerSetTrigger(false, 1002);
				Capture(TEXT("PF_ViewFire.png")); VisualStage = 4;
			}
			else if (VisualStage == 4 && Elapsed > Warmup + 3.3f)
			{ Player->ServerSetMovementState(false, false); Player->Weapon->ServerReload(1003); VisualStage = 5; }
			else if (VisualStage == 5 && Player->Weapon->State.bReloading
				&& GetWorld()->GetTimeSeconds() >= Player->Weapon->State.ReloadEndTime - Player->Weapon->PresentationReloadDuration * .5f)
			{ Capture(TEXT("PF_ViewReload.png")); VisualStage = 6; }
			else if (VisualStage == 6 && !Player->Weapon->State.bReloading)
			{ Capture(TEXT("PF_ViewReady.png")); VisualStage = 7; }
			else if (VisualStage == 7 && Elapsed > Warmup + 8.f)
			{
				const FPFWeaponDefinition Definition = FPFGameRules::Weapon(Player->Weapon->State.Type);
				const APFPlayerCharacter* Defaults = Player->GetClass()->GetDefaultObject<APFPlayerCharacter>();
				const bool Passed = Player->Weapon->State.Magazine == Definition.Magazine && Player->Weapon->State.Reserve == Definition.Reserve - 1 && !Player->bAiming
					&& Player->FirstPersonMesh->GetRelativeLocation().Equals(Defaults->FirstPersonMesh->GetRelativeLocation(), .1f);
				UE_LOG(LogTemp, Display, TEXT("PF_VIEW_ACTIONS passed=%d magazine=%d reserve=%d"), Passed, Player->Weapon->State.Magazine, Player->Weapon->State.Reserve);
				bVisualCapture = false; SetComponentTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false, Passed ? 0 : 1);
			}
			else if (Elapsed > Warmup + 15.f) { bVisualCapture = false; SetComponentTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false, 1); }
			return;
		}
#endif
		if (VisualStage == 0 && Elapsed > Warmup)
		{
			FVector ViewLocation; FRotator ViewRotation; PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
			const FString PawnLocation = PC->GetPawn() ? PC->GetPawn()->GetActorLocation().ToString() : TEXT("none");
			UE_LOG(LogTemp, Display, TEXT("PF_VISUAL_STATE pawn=%s camera=%s rotation=%s"), *PawnLocation, *ViewLocation.ToString(), *ViewRotation.ToString());
			if (const APFPlayerCharacter* PlayerPawn = Cast<APFPlayerCharacter>(PC->GetPawn()))
				UE_LOG(LogTemp, Display, TEXT("PF_VISUAL_ARMS parent=%s local=%s world=%s socket=%s visible=%d hidden=%d anim=%s weapon=%s weapon_position=%s"),
					*GetNameSafe(PlayerPawn->FirstPersonMesh->GetAttachParent()), *PlayerPawn->FirstPersonMesh->GetRelativeLocation().ToString(), *PlayerPawn->FirstPersonMesh->GetComponentLocation().ToString(),
					*PlayerPawn->FirstPersonMesh->GetSocketLocation(TEXT("HandGrip_R")).ToString(), PlayerPawn->FirstPersonMesh->IsVisible(), PlayerPawn->FirstPersonMesh->bHiddenInGame,
					*GetNameSafe(PlayerPawn->FirstPersonMesh->GetAnimInstance()), *GetNameSafe(PlayerPawn->ViewWeapon->GetStaticMesh()), *PlayerPawn->ViewWeapon->GetComponentLocation().ToString());
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/PF_HUD.png"), true, false); VisualStage = 1;
		}
		else if (VisualStage == 1 && Elapsed > Warmup + 3.f) { PC->ToggleMenu(); VisualStage = 2; }
		else if (VisualStage == 2 && Elapsed > Warmup + 5.f)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/PF_Menu.png"), true, false); VisualStage = 3;
		}
		else if (VisualStage == 3 && Elapsed > Warmup + 8.f) { FPlatformMisc::RequestExit(false); bVisualCapture = false; }
	}
	if (!bEnabled) return;
	const float Now = GetWorld()->GetTimeSeconds(); TickLocal(Now);
	if (PC->HasAuthority() && Now - LastServerUpdate >= .1f) { LastServerUpdate = Now; TickServer(Now); }
}

void UPFNetValidationComponent::CheckTempleTraversal(float Now)
{
	auto Complete = [this](bool Passed, const TCHAR* Reason)
	{
		UE_LOG(LogTemp, Display, TEXT("PF_TEMPLE_TRAVERSAL passed=%d stage=%d reason=%s"), Passed, TempleTraversalStage, Reason);
		bTempleTraversal = false; FPlatformMisc::RequestExitWithStatus(false, Passed ? 0 : 1);
	};
	const bool bGarrisonPatrol = FParse::Param(FCommandLine::Get(), TEXT("PFTempleGarrisonPatrol"));
	const bool bPOIPatrol = FParse::Param(FCommandLine::Get(), TEXT("PFTemplePOIPatrol"));
	if (Now - StageTime > (bPOIPatrol ? 60.f : 110.f)) { Complete(false, TEXT("Timed out during physical AI traversal")); return; }
	const FVector Goals[] = {bPOIPatrol ? FVector(12800, 10000, 4205) : FVector(14500, 10000, 4205), bPOIPatrol ? FVector(14200, 10000, 4205) : FVector(16200, 10000, 4605),
		bPOIPatrol ? FVector(12300, 10000, 4205) : bGarrisonPatrol ? FVector(12600, 10200, 4205) : FVector(13000, 10000, 4205)};
	const bool bStateTreePatrol = bPOIPatrol || bGarrisonPatrol || FParse::Param(FCommandLine::Get(), TEXT("PFTempleStateTree"));
	if (TempleTraversalStage == 0)
	{
		if (Now - StageTime < 5.f) return;
		UClass* Guard = LoadClass<APFEnemyCharacter>(nullptr, TEXT("/Game/ProjectF/AI/BP_Guard.BP_Guard_C"));
		FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APFEnemyCharacter* Pawn = nullptr;
		if (bGarrisonPatrol)
		{
			for (TActorIterator<APFEnemyCharacter> It(GetWorld()); It; ++It)
				if (It->ThreatId == TEXT("Stronghold.Garrison.3")) { Pawn = *It; break; }
			if (!Pawn || Pawn->PatrolPoints.Num() != 3) { Complete(false, TEXT("Authored garrison route unavailable")); return; }
		}
		else if (Guard) Pawn = GetWorld()->SpawnActor<APFEnemyCharacter>(Guard, FVector(bPOIPatrol ? 12300 : 13000, 10000, 4300), FRotator::ZeroRotator, Spawn);
		if (!Pawn) { Complete(false, TEXT("Fixture pawn unavailable")); return; }
		TempleTraversalPawn = Pawn;
		if (bStateTreePatrol)
		{
			APFAIController* Controller = Cast<APFAIController>(Pawn->GetController());
			if (!Controller || !Controller->Behavior || !Controller->StateTree->IsRunning()) { Complete(false, TEXT("Production StateTree not running")); return; }
			TempleTraversalController = Controller;
			if (!bGarrisonPatrol) for (const FVector& Goal : Goals) Pawn->PatrolPoints.Add(Goal + FVector(0, 0, Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
			if (bPOIPatrol) Pawn->PatrolPoints[0] = FVector(12800, 10000, 4250);
			TempleTraversalStage = 1;
			UE_LOG(LogTemp, Display, TEXT("PF_TEMPLE_TRAVERSAL production_statetree=1 authored_garrison=%d poi_patrol=%d capsule_radius=%.1f half_height=%.1f"), bGarrisonPatrol, bPOIPatrol, Pawn->GetCapsuleComponent()->GetScaledCapsuleRadius(), Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			return;
		}
		if (AController* Previous = Pawn->GetController()) { Previous->UnPossess(); Previous->Destroy(); }
		AAIController* Controller = GetWorld()->SpawnActor<AAIController>();
		if (!Controller) { Complete(false, TEXT("Fixture controller unavailable")); return; }
		TempleTraversalController = Controller; Controller->Possess(Pawn); Pawn->GetCharacterMovement()->MaxWalkSpeed = 240.f;
		TempleTraversalStage = 1;
		if (Controller->MoveToLocation(Goals[0], 45.f, true, true, true, true, nullptr, false) == EPathFollowingRequestResult::Failed)
			Complete(false, TEXT("Entry path request failed"));
		return;
	}
	APFEnemyCharacter* Pawn = TempleTraversalPawn.Get(); AAIController* Controller = TempleTraversalController.Get();
	if (!Pawn || !Controller) { Complete(false, TEXT("Fixture destroyed")); return; }
	if (Now - TempleTraversalLogTime > 4.f)
	{
		TempleTraversalLogTime = Now;
		UE_LOG(LogTemp, Display, TEXT("PF_TEMPLE_TRAVERSAL_STATE stage=%d position=%s velocity=%s mode=%d move=%d"), TempleTraversalStage,
			*Pawn->GetActorLocation().ToString(), *Pawn->GetVelocity().ToString(), static_cast<int32>(Pawn->GetCharacterMovement()->MovementMode), static_cast<int32>(Controller->GetMoveStatus()));
		FHitResult Block; FCollisionQueryParams Query(SCENE_QUERY_STAT(PFTempleTraversal), false, Pawn);
		if (GetWorld()->SweepSingleByChannel(Block, Pawn->GetActorLocation(), Pawn->GetActorLocation() + FVector(250, 0, 0), FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(Pawn->GetCapsuleComponent()->GetScaledCapsuleRadius(), Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), Query))
			UE_LOG(LogTemp, Display, TEXT("PF_TEMPLE_TRAVERSAL_BLOCK actor=%s component=%s point=%s profile=%s"), *GetNameSafe(Block.GetActor()), *GetNameSafe(Block.GetComponent()),
				*Block.ImpactPoint.ToString(), Block.GetComponent() ? *Block.GetComponent()->GetCollisionProfileName().ToString() : TEXT("none"));
	}
	const FVector Goal = Goals[TempleTraversalStage - 1];
	if (FVector::DistSquared2D(Pawn->GetActorLocation(), Goal) < FMath::Square(bPOIPatrol && TempleTraversalStage == 1 ? 250.f : 100.f) && FMath::Abs(Pawn->GetActorLocation().Z - Goal.Z) < 120.f)
	{
		UE_LOG(LogTemp, Display, TEXT("PF_TEMPLE_TRAVERSAL waypoint=%d position=%s mode=%d"), TempleTraversalStage, *Pawn->GetActorLocation().ToString(), static_cast<int32>(Pawn->GetCharacterMovement()->MovementMode));
		if (++TempleTraversalStage > UE_ARRAY_COUNT(Goals)) { Complete(true, bPOIPatrol ? TEXT("Reached POI approach, advanced patrol waypoint, and returned without teleporting") : TEXT("Walked entry, courtyard stairs, and return without teleporting")); return; }
		if (!bStateTreePatrol && Controller->MoveToLocation(Goals[TempleTraversalStage - 1], 45.f, true, true, true, true, nullptr, false) == EPathFollowingRequestResult::Failed)
			Complete(false, TEXT("Next path request failed"));
	}
	else if (!bStateTreePatrol && Controller->GetMoveStatus() == EPathFollowingStatus::Idle) Complete(false, TEXT("Movement stopped before reaching waypoint"));
}

void UPFNetValidationComponent::CaptureExecution(APFPlayerController* PC, float Elapsed)
{
	APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(PC->GetPawn()); if (!Player) return;
	if (ExecutionCaptureStage == 0 && Elapsed > 15.f)
	{
		UClass* GuardClass = LoadClass<APFEnemyCharacter>(nullptr, TEXT("/Game/ProjectF/AI/BP_Guard.BP_Guard_C"));
		const FTransform Transform(FRotator::ZeroRotator, FVector(500, 0, 96));
		APFEnemyCharacter* Enemy = GetWorld()->SpawnActorDeferred<APFEnemyCharacter>(GuardClass, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Enemy) { Finish(false, TEXT("Execution review guard missing")); return; }
		Enemy->AutoPossessAI = EAutoPossessAI::Disabled; Enemy->FinishSpawning(Transform); Enemy->Health->InitializeHealth(1000000.f);
		ExecutionReviewTarget = Enemy;
		Player->TeleportTo(FVector(400, 0, 96), FRotator::ZeroRotator); PC->SetControlRotation(FRotator::ZeroRotator);
		Player->Crouch(); Player->GetCharacterMovement()->Crouch(false);
		PC->SetIgnoreLookInput(true); PC->SetIgnoreMoveInput(true);
		if (!bExecutionFirstPerson)
		{
			ACameraActor* Camera = GetWorld()->SpawnActor<ACameraActor>(FVector(445, -270, 160), FRotator(-3, 90, 0));
			if (Camera) { Camera->GetCameraComponent()->SetFieldOfView(85.f); PC->SetViewTarget(Camera); }
		}
		ExecutionCaptureStage = 1;
	}
	else if (ExecutionCaptureStage == 1 && Elapsed > 17.f)
	{
		Player->Takedown->ServerRequest(ExecutionReviewTarget.Get(), EPFTakedownType::Rear, 500);
		ExecutionCaptureTime = GetWorld()->GetTimeSeconds(); ExecutionCaptureStage = 2;
	}
	else if (ExecutionCaptureStage >= 2 && ExecutionCaptureStage <= 5)
	{
		static const float Times[] = {.05f, .36f, .7f, 1.25f};
		const int32 Index = ExecutionCaptureStage - 2;
		if (GetWorld()->GetTimeSeconds() - ExecutionCaptureTime >= Times[Index])
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Screenshots/PF_Execution_%d.png"), Index), true, false);
			++ExecutionCaptureStage;
		}
	}
	else if (ExecutionCaptureStage == 6 && GetWorld()->GetTimeSeconds() - ExecutionCaptureTime > 1.6f)
	{
		const bool bPassed = ExecutionReviewTarget.IsValid() && !ExecutionReviewTarget->Health->IsAlive() && Player->ActionName.IsNone() && Player->bIsCrouched && !Player->Takedown->IsPresenting();
		UE_LOG(LogTemp, Display, TEXT("PF_EXECUTION_REVIEW passed=%d view=%s"), bPassed, bExecutionFirstPerson ? TEXT("first_person") : TEXT("remote_body"));
		Finish(bPassed, TEXT("Execution capture and crouch cleanup"));
	}
}

void UPFNetValidationComponent::CheckFrontEnd(APFPlayerController* PC, float Elapsed)
{
	if (FrontEndStage == 0 && Elapsed > 15.f)
	{
		if (!PC->bFrontEndOpen || !PC->FrontEndWidget || PC->GetPawn() || PC->bInitializedProfile) { Finish(false, TEXT("Frontend must wait for an explicit start before profile admission and pawn spawn")); return; }
		auto Key = [](FKey Value)
		{
			const FKeyEvent Event(Value, FModifierKeysState(), 0, false, 0, 0);
			FSlateApplication::Get().ProcessKeyDownEvent(Event); FSlateApplication::Get().ProcessKeyUpEvent(Event);
		};
		Key(EKeys::Down); const bool bKeyboardDown = PC->FrontEndWidget->GetFocusedAction() == TEXT("Host");
		Key(EKeys::Up); const bool bKeyboardUp = PC->FrontEndWidget->GetFocusedAction() == TEXT("Continue");
		Key(EKeys::Gamepad_DPad_Down); const bool bPadDown = PC->FrontEndWidget->GetFocusedAction() == TEXT("Host");
		Key(EKeys::Gamepad_DPad_Up); const bool bPadUp = PC->FrontEndWidget->GetFocusedAction() == TEXT("Continue");
		UE_LOG(LogTemp, Display, TEXT("PF_FRONTEND_NAV keyboard=%d gamepad_event=%d"), bKeyboardDown && bKeyboardUp, bPadDown && bPadUp);
		if (!bKeyboardDown || !bKeyboardUp || !bPadDown || !bPadUp) { Finish(false, TEXT("Frontend focus navigation failed")); return; }
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/PF_Title.png"), true, false);
		FrontEndStage = 1;
	}
	else if (FrontEndStage == 1 && Elapsed > 18.f)
	{
		PC->FrontEndWidget->Dispatch(TEXT("JoinPage")); FrontEndStage = 2;
	}
	else if (FrontEndStage == 2 && Elapsed > 20.f)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/PF_TitleJoin.png"), true, false);
		FrontEndStage = 3;
	}
	else if (FrontEndStage == 3 && Elapsed > 21.f)
	{
		PC->FrontEndWidget->Dispatch(TEXT("Back")); PC->FrontEndWidget->Dispatch(TEXT("SettingsPage")); FrontEndStage = 4;
	}
	else if (FrontEndStage == 4 && Elapsed > 23.f)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/PF_TitleSettings.png"), true, false);
		FrontEndStage = 5;
	}
	else if (FrontEndStage == 5 && Elapsed > 24.f)
	{
		PC->FrontEndWidget->Dispatch(TEXT("Back")); PC->FrontEndWidget->Dispatch(TEXT("Continue")); FrontEndStage = 6;
	}
	else if (FrontEndStage == 6 && Elapsed > 28.f)
	{
		const bool bAdmitted = PC->bInitializedProfile && PC->GetPawn() && PC->HUDWidget && !PC->bFrontEndOpen && !PC->FrontEndWidget && PC->GetViewTarget() == PC->GetPawn();
		Finish(bAdmitted, TEXT("Frontend pages and explicit continuation profile/pawn/HUD admission")); bFrontEndCheck = false;
	}
}

void UPFNetValidationComponent::SamplePerformance(float Elapsed)
{
	float Warmup = 20.f; int32 Frames = 1200;
	FParse::Value(FCommandLine::Get(), TEXT("PFFrameProbeWarmup="), Warmup);
	FParse::Value(FCommandLine::Get(), TEXT("PFFrameProbeFrames="), Frames); Frames = FMath::Clamp(Frames, 120, 3600);
	Warmup = FMath::IsFinite(Warmup) ? FMath::Clamp(Warmup, 5.f, 1800.f) : 20.f;
	if (Elapsed < Warmup) return;
	FrameSamples.Add(FApp::GetDeltaTime() * 1000.f); GameSamples.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
	RenderSamples.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
	const uint32 GPUCycles = RHIGetGPUFrameCycles(); if (GPUCycles > 0) GPUSamples.Add(FPlatformTime::ToMilliseconds(GPUCycles));
	if (FrameSamples.Num() < Frames) return;
	auto Summary = [](TArray<float>& Values)
	{
		if (Values.IsEmpty()) return FString(TEXT("null"));
		Values.Sort(); double Sum = 0; for (float Value : Values) Sum += Value;
		return FString::Printf(TEXT("{\"samples\":%d,\"mean_ms\":%.4f,\"p95_ms\":%.4f,\"p99_ms\":%.4f,\"max_ms\":%.4f}"), Values.Num(), Sum / Values.Num(), Values[FMath::FloorToInt((Values.Num() - 1) * .95f)], Values[FMath::FloorToInt((Values.Num() - 1) * .99f)], Values.Last());
	};
	int32 Living = 0, Combat = 0, ReadyPlayers = 0, LivingPlayers = 0;
	float MinimumPlayerHealth = UE_BIG_NUMBER, MinimumPlayerMaximum = UE_BIG_NUMBER;
	int32 PlayersByPost[2] = {0, 0}, CombatByPost[2] = {0, 0}, ActiveByPost[2] = {0, 0};
	for (TActorIterator<APFPlayerCharacter> It(GetWorld()); It; ++It) if (It->bProfileReady)
	{
		++ReadyPlayers;
		MinimumPlayerHealth = FMath::Min(MinimumPlayerHealth, It->Health->Current);
		MinimumPlayerMaximum = FMath::Min(MinimumPlayerMaximum, It->Health->Maximum);
		if (!It->Health->IsAlive()) continue;
		++LivingPlayers;
		for (int32 Index = 0; Index < FMath::Min(StressOutposts.Num(), 2); ++Index)
			if (StressOutposts[Index].IsValid() && It->GetDistanceTo(StressOutposts[Index].Get()) <= 4500.f) ++PlayersByPost[Index];
	}
	for (TActorIterator<APFEnemyCharacter> It(GetWorld()); It; ++It) if (It->Health->IsAlive())
	{
		++Living; const bool bCombat = It->Alert == EPFAlertState::Combat;
		Combat += bCombat; const bool bActive = IsActiveBattleThreat(**It, GetWorld()->GetTimeSeconds());
		for (int32 Index = 0; Index < FMath::Min(StressOutposts.Num(), 2); ++Index)
			if (It->Outpost == StressOutposts[Index].Get()) { CombatByPost[Index] += bCombat; ActiveByPost[Index] += bActive; }
	}
	const TCHAR* Scope = bStress ? (bStressSplit ? TEXT("4 players split 2+2; normal AI; synthetic high player health; no CSV") : TEXT("4 players dense; normal AI; synthetic high player health; no CSV")) : TEXT("stationary local player; native frame and published thread/GPU timers; no CSV");
	const FString Result = FString::Printf(TEXT("{\"map\":\"%s\",\"rhi\":\"%s\",\"warmup_seconds\":%.1f,\"living_enemies\":%d,\"combat_enemies\":%d,\"players_ready\":%d,\"players_alive\":%d,\"players_at_outposts\":[%d,%d],\"combat_at_outposts\":[%d,%d],\"stress_player_range_cm\":4500,\"frame\":%s,\"game_thread\":%s,\"render_thread\":%s,\"gpu\":%s,\"scope\":\"%s\"}"), *GetWorld()->GetMapName(), GDynamicRHI ? GDynamicRHI->GetName() : TEXT("None"), Warmup, Living, Combat, ReadyPlayers, LivingPlayers, PlayersByPost[0], PlayersByPost[1], CombatByPost[0], CombatByPost[1], *Summary(FrameSamples), *Summary(GameSamples), *Summary(RenderSamples), *Summary(GPUSamples), Scope);
	FString RecordedResult = Result;
	RecordedResult.RemoveFromEnd(TEXT("}"));
	RecordedResult += FString::Printf(TEXT(",\"minimum_player_health\":%.0f,\"minimum_player_max_health\":%.0f}"), ReadyPlayers ? MinimumPlayerHealth : 0.f, ReadyPlayers ? MinimumPlayerMaximum : 0.f);
	RecordedResult.RemoveFromEnd(TEXT("}"));
	RecordedResult += FString::Printf(TEXT(",\"active_battle_at_outposts\":[%d,%d],\"stress_gunfire_cues\":%s}"), ActiveByPost[0], ActiveByPost[1], bStress && FParse::Param(FCommandLine::Get(), TEXT("PFStressGunfire")) ? TEXT("true") : TEXT("false"));
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Profiling"); IFileManager::Get().MakeDirectory(*Directory, true);
	const bool Saved = FFileHelper::SaveStringToFile(RecordedResult, *(Directory / TEXT("PF_FrameProbe.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogTemp, Display, TEXT("PF_FRAME_PROBE: saved=%d %s"), Saved, *RecordedResult);
	bFrameProbe = false; FPlatformMisc::RequestExit(false);
}

void UPFNetValidationComponent::UpdateStress(float Now)
{
	if (StressPhase == 0)
	{
		TArray<APFPlayerController*> Ready;
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (APFPlayerController* PC = Cast<APFPlayerController>(It->Get())) if (APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(PC->GetPawn()))
				if (Player->bProfileReady) Ready.Add(PC);
		if (Ready.Num() != 4) return;
		FParse::Value(FCommandLine::Get(), TEXT("PFStressHealth="), StressSyntheticHealth);
		StressSyntheticHealth = FMath::IsFinite(StressSyntheticHealth) ? FMath::Clamp(StressSyntheticHealth, 100000.f, 100000000.f) : 100000.f;
		for (TActorIterator<APFOutpost> It(GetWorld()); It; ++It) StressOutposts.Add(*It);
		if (StressOutposts.Num() < 2) return;
		FString RequestedPost;
		if (FParse::Value(FCommandLine::Get(), TEXT("PFStressOutpost="), RequestedPost))
		{
			const int32 RequestedIndex = StressOutposts.IndexOfByPredicate([&RequestedPost](const TWeakObjectPtr<APFOutpost>& Post)
			{
				return Post.IsValid() && Post->OutpostId == FName(*RequestedPost);
			});
			if (RequestedIndex == INDEX_NONE)
			{
				UE_LOG(LogTemp, Error, TEXT("PF_STRESS_SETUP requested outpost is missing: %s"), *RequestedPost);
				bFrameProbe = false; FPlatformMisc::RequestExitWithStatus(false, 1); return;
			}
			StressOutposts.Swap(0, RequestedIndex);
		}
		for (int32 Index = 0; Index < Ready.Num(); ++Index)
		{
			APFOutpost* Post = StressOutposts[bStressSplit && Index >= 2 ? 1 : 0].Get(); APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(Ready[Index]->GetPawn());
			FVector Position = Post->GetActorLocation() + FVector(-2900, -2900 + Index * 160, 3000); FHitResult Ground;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(PFStressGround), false, Player);
			if (GetWorld()->LineTraceSingleByChannel(Ground, Position, Position - FVector(0, 0, 6000), ECC_Visibility, Params)) Position.Z = Ground.ImpactPoint.Z + 100.f;
			const FName AnchorTag(*FString::Printf(TEXT("PF.Stress.%s.%d"), *Post->OutpostId.ToString(), Index));
			for (TActorIterator<AActor> Anchor(GetWorld()); Anchor; ++Anchor)
				if (Anchor->ActorHasTag(AnchorTag)) { Position = Anchor->GetActorLocation(); break; }
			Player->TeleportTo(Position, FRotator::ZeroRotator); Player->Health->InitializeHealth(StressSyntheticHealth);
			const FRotator Away = (Position - Post->GetActorLocation()).Rotation(); Ready[Index]->SetControlRotation(Away); Ready[Index]->ClientSetRotation(Away, true);
		}
		const int32 Groups = bStressSplit ? 2 : 1;
		for (int32 Index = 0; Index < Groups; ++Index)
		{
			APFOutpost* Post = StressOutposts[Index].Get(); Post->Engage(Cast<APFPlayerCharacter>(Ready[Index * 2]->GetPawn()));
			Post->CancelAlarm(); Post->bAlarmCalled = true;
		}
		StressPhase = 1; StressTime = Now; StageTime = Now;
		UE_LOG(LogTemp, Display, TEXT("PF_STRESS_SETUP players=4 split=%d synthetic_health=%.0f normal_ai=1"), bStressSplit, StressSyntheticHealth);
	}
	else if (StressPhase == 1 && Now - StressTime >= 5.f)
	{
		const int32 Groups = bStressSplit ? 2 : 1;
		for (int32 Index = 0; Index < Groups; ++Index)
		{
			APFOutpost* Post = StressOutposts[Index].Get();
			const int32 Wave = Post->SpawnGroup(Post->ReinforcementSpawns, 4, TEXT("Reinforcement"), FPFGameRules::ScaleFor(bStressSplit ? 2 : 4));
			UE_LOG(LogTemp, Display, TEXT("PF_STRESS_WAVE group=%d spawned=%d garrison=%d"), Index, Wave, Post->BattleScale.Garrison);
		}
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (APlayerController* PC = It->Get()) if (APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(PC->GetPawn()))
			{
				APFOutpost* Closest = StressOutposts[0].Get();
				if (bStressSplit && Player->GetDistanceTo(StressOutposts[1].Get()) < Player->GetDistanceTo(Closest)) Closest = StressOutposts[1].Get();
				const FRotator Facing = (Closest->GetActorLocation() - Player->GetAimOrigin()).Rotation(); PC->SetControlRotation(Facing); PC->ClientSetRotation(Facing, true);
			}
		StressPhase = 2;
	}
	else if (StressPhase == 2 && Now - LastStressAuditTime >= 60.f) AuditStress(Now);
	if (StressPhase == 2 && FParse::Param(FCommandLine::Get(), TEXT("PFStressGunfire")) && Now - LastStressGunfireTime >= 3.f)
	{
		LastStressGunfireTime = Now;
		// Explicit maximum-combat fixture: the same hearing stimulus as an
		// unsuppressed player shot. Perception/StateTree still decide the response.
		for (TActorIterator<APFPlayerCharacter> It(GetWorld()); It; ++It) if (It->bProfileReady && It->Health->IsAlive())
			UAISense_Hearing::ReportNoiseEvent(GetWorld(), It->GetAimOrigin(), 1.f, *It, 10000.f, TEXT("Gunshot"));
	}
}

void UPFNetValidationComponent::AuditStress(float Now)
{
	LastStressAuditTime = Now;
	int32 Ready = 0, Alive = 0, ByPost[2] = {0, 0};
	float MinimumHealth = UE_BIG_NUMBER;
	bool bExpectedHealth = true;
	for (TActorIterator<APFPlayerCharacter> It(GetWorld()); It; ++It) if (It->bProfileReady)
	{
		++Ready; MinimumHealth = FMath::Min(MinimumHealth, It->Health->Current);
		bExpectedHealth &= It->Health->Maximum == StressSyntheticHealth;
		if (!It->Health->IsAlive()) continue;
		++Alive;
		for (int32 Index = 0; Index < FMath::Min(StressOutposts.Num(), 2); ++Index)
			if (StressOutposts[Index].IsValid() && It->GetDistanceTo(StressOutposts[Index].Get()) <= 4500.f) ++ByPost[Index];
	}
	const bool Passed = Ready == 4 && Alive == 4 && bExpectedHealth && ByPost[0] == (bStressSplit ? 2 : 4) && ByPost[1] == (bStressSplit ? 2 : 0);
	UE_LOG(LogTemp, Display, TEXT("PF_STRESS_AUDIT players=%d alive=%d at_posts=%d/%d min_health=%.0f expected_max=%.0f passed=%d"), Ready, Alive, ByPost[0], ByPost[1], Ready ? MinimumHealth : 0.f, StressSyntheticHealth, Passed);
	for (int32 Index = 0; Index < (bStressSplit ? 2 : 1); ++Index)
	{
		APFOutpost* Post = StressOutposts[Index].Get();
		int32 Living = 0, Nearby = 0, Combat = 0, Active = 0, Arrived = 0, Advancing = 0;
		for (TActorIterator<APFEnemyCharacter> It(GetWorld()); It; ++It) if (It->Outpost == Post && It->Health->IsAlive())
		{
			++Living; Nearby += It->GetDistanceTo(Post) <= 4500.f; Combat += It->Alert == EPFAlertState::Combat;
			Active += IsActiveBattleThreat(**It, Now);
			Arrived += It->bReinforcement && It->bHasArrived; Advancing += It->bAdvancingToPost;
			if (FParse::Param(FCommandLine::Get(), TEXT("PFStressDetails")))
				UE_LOG(LogTemp, Display, TEXT("PF_STRESS_ENEMY threat=%s position=%s alert=%d advancing=%d arrived=%d velocity=%s"), *It->ThreatId.ToString(), *It->GetActorLocation().ToString(), static_cast<int32>(It->Alert), It->bAdvancingToPost, It->bHasArrived, *It->GetVelocity().ToString());
		}
		UE_LOG(LogTemp, Display, TEXT("PF_STRESS_POPULATION group=%d living=%d nearby=%d combat=%d active=%d reinforcements_arrived=%d advancing=%d"), Index, Living, Nearby, Combat, Active, Arrived, Advancing);
	}
	if (!Passed)
	{
		for (TActorIterator<APFPlayerCharacter> It(GetWorld()); It; ++It)
			UE_LOG(LogTemp, Error, TEXT("PF_STRESS_ACTOR name=%s life=%d health=%.0f/%.0f position=%s"), *It->GetName(), int32(It->Health->Life), It->Health->Current, It->Health->Maximum, *It->GetActorLocation().ToString());
		bFrameProbe = false; bStress = false; FPlatformMisc::RequestExitWithStatus(false, 1);
	}
}

void UPFNetValidationComponent::ClientBeginAmmoCheck_Implementation()
{
	if (!bEnabled) return;
	LocalStage = 4; LocalTime = GetWorld()->GetTimeSeconds();
}

void UPFNetValidationComponent::TickLocal(float Now)
{
	if (bWaitingForWorld)
	{
		const APFGameState* State = GetWorld()->GetGameState<APFGameState>();
		int32 TeamReady = 0;
		if (State) for (const APlayerState* Entry : State->PlayerArray)
			if (const APFPlayerState* Mate = Cast<APFPlayerState>(Entry)) if (Mate->bJoinedWorld) ++TeamReady;
		const APFPlayerCharacter* OwnerPawn = Cast<APFPlayerCharacter>(CastChecked<APFPlayerController>(GetOwner())->GetPawn());
		const APFPlayerCharacter* Defaults = OwnerPawn ? OwnerPawn->GetClass()->GetDefaultObject<APFPlayerCharacter>() : nullptr;
		const bool bViewRestored = Defaults && OwnerPawn->FirstPersonMesh->GetRelativeLocation().Equals(Defaults->FirstPersonMesh->GetRelativeLocation(), .1f)
			&& FMath::IsNearlyEqual(OwnerPawn->Camera->FirstPersonFieldOfView, Defaults->Camera->FirstPersonFieldOfView, .1f);
		const bool bPoseClean = OwnerPawn && bViewRestored && OwnerPawn->ActionName.IsNone() && !OwnerPawn->Takedown->IsPresenting() && (!bUsedRearExecutionCheck || OwnerPawn->bIsCrouched);
		const bool Matches = bPoseClean && State && TeamReady == ExpectedPlayers && State->Outposts.ContainsByPredicate([this](const FPFOutpostRecord& Record)
		{
			return Record.Id == ExpectedOutpostId && Record.Phase == EPFOutpostPhase::Captured && Record.CompletionId == ExpectedCompletion;
		});
		if (Matches || Now - WorldCheckTime > 10.f)
		{
			UE_LOG(LogTemp, Display, TEXT("PF_NET_CLIENT_WORLD passed=%d outpost=%s team_states=%d"), Matches, *ExpectedOutpostId.ToString(), TeamReady);
			ServerReportWorld(ExpectedOutpostId, ExpectedCompletion, Matches); bWaitingForWorld = false;
		}
	}
	if (LocalStage == 0) return;
	APFPlayerController* PC = Cast<APFPlayerController>(GetOwner()); APFPlayerCharacter* PlayerPawn = PC ? Cast<APFPlayerCharacter>(PC->GetPawn()) : nullptr;
	if (!PlayerPawn) return;
	if (Now - LocalTime > 12.f)
	{
		UE_LOG(LogTemp, Error, TEXT("PF_NET_AMMO_TIMEOUT stage=%d type=%d magazine=%d reserve=%d reload=%d predicted=%d played=%d equipped=%d local=%d can_act=%d"), LocalStage,
			static_cast<int32>(PlayerPawn->Weapon->State.Type), PlayerPawn->Weapon->State.Magazine, PlayerPawn->Weapon->State.Reserve,
			PlayerPawn->Weapon->State.bReloading, PlayerPawn->Weapon->LocalPredictedEffects, PlayerPawn->Weapon->LocalPlayedEffects,
			static_cast<int32>(PlayerPawn->Weapon->EquippedType), PlayerPawn->IsLocallyControlled(), PlayerPawn->CanAct());
		ServerReportAmmo(false, PlayerPawn->Weapon->State.Magazine, PlayerPawn->Weapon->State.Reserve); LocalStage = 0; return;
	}
	const APFGameState* CurrentState = GetWorld()->GetGameState<APFGameState>();
	const float ServerNow = CurrentState ? CurrentState->GetServerWorldTimeSeconds() : Now;
	const bool ReadyForLocalShot = PlayerPawn->IsLocallyControlled() && PlayerPawn->CanAct() && !PlayerPawn->bSprinting
		&& !PlayerPawn->Weapon->State.bReloading && PlayerPawn->Weapon->EquippedType == PlayerPawn->Weapon->State.Type
		&& ServerNow >= PlayerPawn->WeaponReadyTime;
	if (LocalStage == 4 && PlayerPawn->bProfileReady && PlayerPawn->IsLocallyControlled())
	{
		// Test real movement replication along a clear lane, rather than pushing
		// every pawn into the same teammate or camp wall.
		const UCapsuleComponent* Capsule = PlayerPawn->GetCapsuleComponent();
		const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight() - 4.f);
		const FVector Origin = PlayerPawn->GetActorLocation();
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(PFNetMovementLane), false, PlayerPawn);
		bool bClearLane = false;
		for (const FVector& Direction : {FVector::RightVector, -FVector::RightVector, FVector::ForwardVector, -FVector::ForwardVector})
		{
			FHitResult Hit;
			if (!GetWorld()->SweepSingleByProfile(Hit, Origin, Origin + Direction * 150.f, FQuat::Identity, Capsule->GetCollisionProfileName(), Shape, Params))
			{
				LocalMovementDirection = Direction; bClearLane = true; break;
			}
		}
		if (!bClearLane) return;
		PC->SetControlRotation(FRotator(80, 0, 0));
		PlayerPawn->Weapon->ServerEquip(EPFWeaponType::Pistol, 101); LocalStage = 1; LocalTime = Now; StartPosition = PlayerPawn->GetActorLocation();
	}
	else if (LocalStage == 1 && ReadyForLocalShot && PlayerPawn->Weapon->State.Type == EPFWeaponType::Pistol && Now - LocalTime >= .8f)
	{
		PlayerPawn->Weapon->BeginLocalShotEffects(102); PlayerPawn->Weapon->ServerSetTrigger(true, 102);
		PlayerPawn->Weapon->StopLocalShotEffects(); PlayerPawn->Weapon->ServerSetTrigger(false, 103); LocalStage = 2; LocalTime = Now;
	}
	else if (LocalStage == 2)
	{
		PlayerPawn->AddMovementInput(LocalMovementDirection);
		if (Now - LocalTime >= 1.f && PlayerPawn->Weapon->State.Magazine == 11)
		{
			PlayerPawn->Weapon->ServerReload(104); LocalStage = 3; LocalTime = Now;
		}
	}
	else if (LocalStage == 3 && Now - LocalTime > 2.5f)
	{
		const bool Passed = PlayerPawn->Weapon->State.Magazine == 12 && PlayerPawn->Weapon->State.Reserve == 47 && !PlayerPawn->Weapon->State.bReloading
			&& (PC->HasAuthority() || (PlayerPawn->Weapon->LocalPredictedEffects == 1 && PlayerPawn->Weapon->LocalPlayedEffects == 1));
		if (!Passed) return; // Replication completion is observed; the bounded timeout above still rejects a mismatch.
		PlayerPawn->Weapon->ServerEquip(EPFWeaponType::Rifle, 105); PlayerPawn->ServerSetMovementState(false, true); LocalStage = 5; LocalTime = Now;
	}
	else if (LocalStage == 5 && ReadyForLocalShot && PlayerPawn->bAiming && PlayerPawn->Weapon->State.Type == EPFWeaponType::Rifle && Now - LocalTime > .8f)
	{
		PlayerPawn->Weapon->BeginLocalShotEffects(106); ServerTestBurst(); LocalStage = 6; LocalTime = Now;
	}
	else if (LocalStage == 6 && Now - LocalTime > .25f)
	{
		PlayerPawn->Weapon->StopLocalShotEffects(); LocalStage = 7; LocalTime = Now;
	}
	else if (LocalStage == 7 && Now - LocalTime > 1.f)
	{
		const bool Passed = PlayerPawn->Weapon->State.Magazine == 27
			&& (PC->HasAuthority() || (PlayerPawn->Weapon->LocalPredictedEffects >= 2 && PlayerPawn->Weapon->LocalPredictedEffects <= 4 && PlayerPawn->Weapon->LocalPlayedEffects == 4));
		if (!Passed) return;
		UE_LOG(LogTemp, Display, TEXT("PF_NET_SHOT_PRESENTATION passed=1 rifle_magazine=%d predicted=%d played=%d"),
			PlayerPawn->Weapon->State.Magazine, PlayerPawn->Weapon->LocalPredictedEffects, PlayerPawn->Weapon->LocalPlayedEffects);
		PlayerPawn->ServerSetMovementState(false, false); PlayerPawn->Weapon->ServerEquip(EPFWeaponType::Pistol, 108); LocalStage = 8; LocalTime = Now;
	}
	else if (LocalStage == 8 && PlayerPawn->Weapon->State.Type == EPFWeaponType::Pistol && Now - LocalTime > .8f)
	{
		const bool Passed = PlayerPawn->Weapon->State.Magazine == 12 && PlayerPawn->Weapon->State.Reserve == 47;
		if (!Passed) return;
		UE_LOG(LogTemp, Display, TEXT("PF_NET_CLIENT_AMMO passed=%d magazine=%d reserve=%d moved=%.1f"), Passed, PlayerPawn->Weapon->State.Magazine, PlayerPawn->Weapon->State.Reserve, FVector::Dist(StartPosition, PlayerPawn->GetActorLocation()));
		ServerReportAmmo(Passed, PlayerPawn->Weapon->State.Magazine, PlayerPawn->Weapon->State.Reserve); LocalStage = 0;
	}
}

void UPFNetValidationComponent::ServerTestBurst_Implementation()
{
	if (!bEnabled) return;
	APFPlayerController* PC = Cast<APFPlayerController>(GetOwner());
	APFPlayerCharacter* PlayerPawn = PC ? Cast<APFPlayerCharacter>(PC->GetPawn()) : nullptr;
	if (!PlayerPawn) return;
	// Fixed authority-side duration isolates cosmetic reconciliation from reliable
	// input queue compression during loss. The single-shot check covers trigger transport.
	PlayerPawn->Weapon->ServerSetTrigger(true, 106);
	GetWorld()->GetTimerManager().SetTimer(BurstStopTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (const APFPlayerController* Controller = Cast<APFPlayerController>(GetOwner()))
			if (APFPlayerCharacter* Pawn = Cast<APFPlayerCharacter>(Controller->GetPawn())) Pawn->Weapon->ServerSetTrigger(false, 107);
	}), .25f, false);
}

void UPFNetValidationComponent::ServerReportAmmo_Implementation(bool Passed, int32 Magazine, int32 Reserve)
{
	if (!bEnabled) return;
	const APFPlayerController* PC = Cast<APFPlayerController>(GetOwner()); const APFPlayerCharacter* PlayerPawn = PC ? Cast<APFPlayerCharacter>(PC->GetPawn()) : nullptr;
	bAmmoReported = true;
	bAmmoPassed = Passed && PlayerPawn && Magazine == PlayerPawn->Weapon->State.Magazine && Reserve == PlayerPawn->Weapon->State.Reserve && Magazine == 12 && Reserve == 47;
	UE_LOG(LogTemp, Display, TEXT("PF_NET_SERVER_AMMO player=%s passed=%d client_passed=%d client=%d/%d server=%d/%d"), *GetNameSafe(GetOwner()), bAmmoPassed,
		Passed, Magazine, Reserve, PlayerPawn ? PlayerPawn->Weapon->State.Magazine : -1, PlayerPawn ? PlayerPawn->Weapon->State.Reserve : -1);
}

void UPFNetValidationComponent::ClientExecute_Implementation(APFEnemyCharacter* Enemy, uint32 Sequence)
{
	if (!bEnabled) return;
	if (APFPlayerController* PC = Cast<APFPlayerController>(GetOwner()))
		if (APFPlayerCharacter* PlayerPawn = Cast<APFPlayerCharacter>(PC->GetPawn()))
		{
			bUsedRearExecutionCheck = PlayerPawn->HasRearExecutionMotion();
			PlayerPawn->Crouch(); PlayerPawn->GetCharacterMovement()->Crouch(false);
			PlayerPawn->Takedown->ServerRequest(Enemy, EPFTakedownType::Rear, Sequence);
		}
}

void UPFNetValidationComponent::ClientCapture_Implementation(APFOutpost* Outpost)
{
	if (!bEnabled) return;
	if (APFPlayerController* PC = Cast<APFPlayerController>(GetOwner()))
		if (APFPlayerCharacter* PlayerPawn = Cast<APFPlayerCharacter>(PC->GetPawn())) PlayerPawn->Interaction->ServerBegin(Outpost, 401);
}

void UPFNetValidationComponent::ClientVerifyWorld_Implementation(FName OutpostId, FGuid CompletionId)
{
	if (!bEnabled) return;
	ExpectedOutpostId = OutpostId; ExpectedCompletion = CompletionId; WorldCheckTime = GetWorld()->GetTimeSeconds(); bWaitingForWorld = true;
}

void UPFNetValidationComponent::ServerReportWorld_Implementation(FName OutpostId, FGuid CompletionId, bool Passed)
{
	if (!bEnabled) return;
	const APFGameState* State = GetWorld()->GetGameState<APFGameState>();
	bWorldReported = true;
	bWorldPassed = Passed && State && State->Outposts.ContainsByPredicate([OutpostId, CompletionId](const FPFOutpostRecord& Record)
	{
		return Record.Id == OutpostId && Record.CompletionId == CompletionId && Record.Phase == EPFOutpostPhase::Captured;
	});
}

void UPFNetValidationComponent::ClientReconnect_Implementation(const FString& Address)
{
	if (!bEnabled) return;
	if (APFPlayerController* PC = Cast<APFPlayerController>(GetOwner())) PC->ClientTravel(Address, TRAVEL_Absolute);
}

void UPFNetValidationComponent::TickServer(float Now)
{
	if (ServerStage == 1)
	{
		for (const auto& Pair : InitialEnemyPositions) if (Pair.Key.IsValid())
			MaximumPatrolDisplacement = FMath::Max(MaximumPatrolDisplacement, FVector::Dist(Pair.Key->GetActorLocation(), Pair.Value));
	}
	if (ServerStage == 0)
	{
		Players.Reset();
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (APFPlayerController* PC = Cast<APFPlayerController>(It->Get())) if (APFPlayerCharacter* PlayerPawn = Cast<APFPlayerCharacter>(PC->GetPawn()))
				if (PC->bInitializedProfile && PlayerPawn->bProfileReady) Players.Add(PC);
		if (Players.Num() < ExpectedPlayers) { if (Now - StageTime > 90.f) Finish(false, TEXT("four_player_join_timeout")); return; }
		for (int32 First = 0; First < Players.Num(); ++First)
			for (int32 Second = First + 1; Second < Players.Num(); ++Second)
			{
				const APFPlayerCharacter* A = CastChecked<APFPlayerCharacter>(Players[First]->GetPawn());
				const APFPlayerCharacter* B = CastChecked<APFPlayerCharacter>(Players[Second]->GetPawn());
				if (FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()) < A->GetCapsuleComponent()->GetScaledCapsuleRadius() + B->GetCapsuleComponent()->GetScaledCapsuleRadius())
				{ Finish(false, TEXT("player_spawn_overlap")); return; }
			}
		UE_LOG(LogTemp, Display, TEXT("PF_NET_SPAWN passed=1 players=%d nonoverlapping=1"), Players.Num());
		for (const auto& PC : Players)
		{
			StartingPositions.Add(PC, PC->GetPawn()->GetActorLocation());
			PC->FindComponentByClass<UPFNetValidationComponent>()->ClientBeginAmmoCheck();
		}
		UE_LOG(LogTemp, Display, TEXT("PF_NET_JOIN passed=1 players=%d"), Players.Num()); ServerStage = 1; StageTime = Now;
		for (TActorIterator<APFEnemyCharacter> It(GetWorld()); It; ++It) InitialEnemyPositions.Add(*It, It->GetActorLocation());
	}
	else if (ServerStage == 1)
	{
		bool AllReported = true, AllPassed = true;
		for (const auto& PC : Players)
		{
			if (!PC.IsValid()) { Finish(false, TEXT("unexpected_disconnect")); return; }
			const auto* Probe = PC->FindComponentByClass<UPFNetValidationComponent>(); AllReported &= Probe->bAmmoReported; AllPassed &= Probe->bAmmoPassed;
		}
		if (!AllReported) { if (Now - StageTime > 25.f) Finish(false, TEXT("ammo_report_timeout")); return; }
		if (!AllPassed) { Finish(false, TEXT("ammo_mismatch")); return; }
		for (const auto& PC : Players) if (FVector::Dist(StartingPositions[PC], PC->GetPawn()->GetActorLocation()) < 5.f) { Finish(false, TEXT("movement_not_replicated")); return; }
		UE_LOG(LogTemp, Display, TEXT("PF_NET_MOVEMENT passed=1 players=%d"), Players.Num());
		const bool bEnemyMoved = MaximumPatrolDisplacement > 25.f;
		if (!bEnemyMoved)
		{
			int32 Logged = 0;
			for (const auto& Pair : InitialEnemyPositions) if (Pair.Key.IsValid() && Logged++ < 3)
			{
				APFEnemyCharacter* Enemy = Pair.Key.Get(); APFAIController* AI = Cast<APFAIController>(Enemy->GetController());
				FNavLocation Projected; UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
				const bool OnNav = Nav && Nav->ProjectPointToNavigation(Enemy->GetActorLocation(), Projected, FVector(200, 200, 300));
				UE_LOG(LogTemp, Display, TEXT("PF_NET_AI_DIAGNOSTIC enemy=%s ai=%s tree=%s running=%d desired=%d alert=%d nav=%d mode=%d velocity=%s patrol=%d"),
					*GetNameSafe(Enemy), *GetNameSafe(AI), AI ? *GetNameSafe(AI->Behavior) : TEXT("none"), AI && AI->StateTree->IsRunning(),
					AI ? static_cast<int32>(AI->DesiredState) : -1, static_cast<int32>(Enemy->Alert), OnNav, static_cast<int32>(Enemy->GetCharacterMovement()->MovementMode), *Enemy->GetVelocity().ToString(), Enemy->PatrolPoints.Num());
			}
			Finish(false, TEXT("statetree_navigation_not_moving")); return;
		}
		UE_LOG(LogTemp, Display, TEXT("PF_NET_AI_NAV passed=1 enemies=%d max_displacement=%.1f"), InitialEnemyPositions.Num(), MaximumPatrolDisplacement);
		// Freeze unrelated enemies after the patrol check to isolate execution concurrency.
		for (TActorIterator<APFEnemyCharacter> It(GetWorld()); It; ++It)
		{
			if (APFAIController* AI = Cast<APFAIController>(It->GetController())) AI->Shutdown();
			It->Alert = EPFAlertState::Routine;
		}
		FString RequestedOutpost; FParse::Value(FCommandLine::Get(), TEXT("PFNetOutpost="), RequestedOutpost);
		for (TActorIterator<APFEnemyCharacter> It(GetWorld()); It; ++It) if (It->Health->IsAlive() && It->EnemyRole == EPFEnemyRole::Soldier
			&& (RequestedOutpost.IsEmpty() || (It->Outpost && It->Outpost->OutpostId == FName(*RequestedOutpost))))
		{
			ExecutionTarget = *It; CaptureTarget = It->Outpost;
			if (APFAIController* AI = Cast<APFAIController>(It->GetController())) AI->Shutdown();
			It->Alert = EPFAlertState::Routine; It->InitializeCombatScale(FPFGameRules::ScaleFor(4)); It->Health->InitializeHealth(1000000.f); break;
		}
		if (!ExecutionTarget.IsValid() || Players.Num() < 2) { Finish(false, TEXT("execution_fixture_missing")); return; }
		BeforeExperience = 0;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			APFPlayerCharacter* PlayerPawn = Cast<APFPlayerCharacter>(Players[Index]->GetPawn());
			PlayerPawn->TeleportTo(ExecutionTarget->GetActorLocation() - ExecutionTarget->GetActorForwardVector() * 100.f + ExecutionTarget->GetActorRightVector() * (Index ? 80.f : -80.f), ExecutionTarget->GetActorRotation());
			BeforeExperience += Players[Index]->GetPlayerState<APFPlayerState>()->Profile.Experience;
		}
		ServerStage = 2; StageTime = Now;
	}
	else if (ServerStage == 2 && Now - StageTime > 1.f)
	{
		for (int32 Index = 0; Index < 2; ++Index) Players[Index]->FindComponentByClass<UPFNetValidationComponent>()->ClientExecute(ExecutionTarget.Get(), 201);
		ServerStage = 3; StageTime = Now;
	}
	else if (ServerStage == 3 && Now - StageTime > 3.f)
	{
		const int32 Experience = Players[0]->GetPlayerState<APFPlayerState>()->Profile.Experience + Players[1]->GetPlayerState<APFPlayerState>()->Profile.Experience;
		if (!ExecutionTarget.IsValid() || ExecutionTarget->Health->Life != EPFLifeState::Dead || !ExecutionTarget->KillId.IsValid() || Experience - BeforeExperience != 35)
		{
			Finish(false, TEXT("exclusive_execution_failed")); return;
		}
		UE_LOG(LogTemp, Display, TEXT("PF_NET_EXECUTION passed=1 initial_health=1000000 requests=2 reward_count=1"));
		if (!CaptureTarget.IsValid()) { Finish(false, TEXT("outpost_missing")); return; }
		for (TActorIterator<APFEnemyCharacter> It(GetWorld()); It; ++It) if (It->Outpost == CaptureTarget.Get()) It->Health->Execute();
		APFPlayerCharacter* PlayerPawn = Cast<APFPlayerCharacter>(Players[0]->GetPawn());
		FVector CapturePosition = CaptureTarget->GetActorLocation() - FVector(180.f, 0.f, 0.f);
		FHitResult Ground; FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(PFValidationGround), false, PlayerPawn); GroundParams.AddIgnoredActor(CaptureTarget.Get());
		if (GetWorld()->LineTraceSingleByChannel(Ground, CapturePosition + FVector(0, 0, 1000), CapturePosition - FVector(0, 0, 2000), ECC_Visibility, GroundParams)) CapturePosition.Z = Ground.ImpactPoint.Z + 98.f;
		PlayerPawn->TeleportTo(CapturePosition, FRotator::ZeroRotator);
		ServerStage = 4; StageTime = Now;
	}
	else if (ServerStage == 4 && Now - StageTime > 1.f)
	{
		Players[0]->FindComponentByClass<UPFNetValidationComponent>()->ClientCapture(CaptureTarget.Get()); ServerStage = 5; StageTime = Now;
	}
	else if (ServerStage == 5 && Now - StageTime > 5.f)
	{
		const APFGameState* State = GetWorld()->GetGameState<APFGameState>();
		const bool Captured = CaptureTarget.IsValid() && CaptureTarget->Phase == EPFOutpostPhase::Captured && State && State->Outposts.ContainsByPredicate([this](const FPFOutpostRecord& R) { return R.Id == CaptureTarget->OutpostId && R.Phase == EPFOutpostPhase::Captured && R.CompletionId.IsValid(); });
		if (!Captured) { Finish(false, TEXT("outpost_capture_failed")); return; }
		UE_LOG(LogTemp, Display, TEXT("PF_NET_OUTPOST passed=1 threats=0 captured=1 saved=1"));
		const FPFOutpostRecord* Record = State->Outposts.FindByPredicate([this](const FPFOutpostRecord& R) { return R.Id == CaptureTarget->OutpostId; });
		ExpectedOutpostId = Record->Id; ExpectedCompletion = Record->CompletionId;
		for (const auto& PC : Players) PC->FindComponentByClass<UPFNetValidationComponent>()->ClientVerifyWorld(ExpectedOutpostId, ExpectedCompletion);
		ServerStage = 6; StageTime = Now;
	}
	else if (ServerStage == 6)
	{
		bool AllReported = true, AllPassed = true;
		for (const auto& PC : Players)
		{
			if (!PC.IsValid()) { Finish(false, TEXT("unexpected_world_sync_disconnect")); return; }
			const auto* Probe = PC->FindComponentByClass<UPFNetValidationComponent>(); AllReported &= Probe->bWorldReported; AllPassed &= Probe->bWorldPassed;
		}
		if (!AllReported) { if (Now - StageTime > 15.f) Finish(false, TEXT("world_sync_timeout")); return; }
		if (!AllPassed) { Finish(false, TEXT("world_sync_mismatch")); return; }
		UE_LOG(LogTemp, Display, TEXT("PF_NET_WORLD_SYNC passed=1 clients=%d"), Players.Num());
		for (const auto& PC : Players) if (!PC->IsLocalController())
		{
			PC->FindComponentByClass<UPFNetValidationComponent>()->ClientReconnect(FString::Printf(TEXT("127.0.0.1:%d?PFBuild=%d"), GetWorld()->URL.Port, FPFGameRules::ProtocolVersion)); break;
		}
		ServerStage = 7; StageTime = Now;
	}
	else if (ServerStage == 7 && Now - StageTime > 2.f)
	{
		Players.Reset();
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (APFPlayerController* PC = Cast<APFPlayerController>(It->Get())) if (APFPlayerCharacter* PlayerPawn = Cast<APFPlayerCharacter>(PC->GetPawn()))
				if (PC->bInitializedProfile && PlayerPawn->bProfileReady) Players.Add(PC);
		if (Players.Num() != ExpectedPlayers) { if (Now - StageTime > 25.f) Finish(false, TEXT("reconnect_timeout")); return; }
		bool FoundNewConnection = false;
		for (const auto& PC : Players)
		{
			const auto* Probe = PC->FindComponentByClass<UPFNetValidationComponent>();
			if (!Probe->bWorldReported)
			{
				FoundNewConnection = true;
				PC->FindComponentByClass<UPFNetValidationComponent>()->ClientVerifyWorld(ExpectedOutpostId, ExpectedCompletion);
				const APFPlayerCharacter* PlayerPawn = Cast<APFPlayerCharacter>(PC->GetPawn());
				if (!PlayerPawn || PlayerPawn->Weapon->State.Magazine != 12 || PlayerPawn->Weapon->State.Reserve != 47) { Finish(false, TEXT("reconnect_inventory_mismatch")); return; }
			}
		}
		if (!FoundNewConnection) { if (Now - StageTime > 25.f) Finish(false, TEXT("new_connection_not_observed")); return; }
		ServerStage = 8; StageTime = Now;
	}
	else if (ServerStage == 8)
	{
		bool AllReported = true, AllPassed = true;
		TSet<FString> Ids;
		for (const auto& PC : Players)
		{
			if (!PC.IsValid()) { Finish(false, TEXT("reconnect_connection_lost")); return; }
			const auto* Probe = PC->FindComponentByClass<UPFNetValidationComponent>(); AllReported &= Probe->bWorldReported; AllPassed &= Probe->bWorldPassed;
			Ids.Add(PC->GetPlayerState<APFPlayerState>()->Profile.PlayerId);
		}
		if (!AllReported) { if (Now - StageTime > 15.f) Finish(false, TEXT("reconnect_state_timeout")); return; }
		if (!AllPassed || Ids.Num() != ExpectedPlayers) { Finish(false, TEXT("reconnect_state_mismatch")); return; }
		UE_LOG(LogTemp, Display, TEXT("PF_NET_RECONNECT passed=1 profiles=%d inventory_retained=1 world_restored=1"), Ids.Num());
		Finish(true, TEXT("join_ai_movement_ammo_execution_capture_reconnect"));
	}
}

void UPFNetValidationComponent::Finish(bool Passed, const TCHAR* Reason)
{
	if (!bEnabled && !bFrontEndCheck && !bExecutionCapture) return;
	bEnabled = false; bFrontEndCheck = false; bExecutionCapture = false; SetComponentTickEnabled(false);
	UE_LOG(LogTemp, Display, TEXT("PF_NET_RESULT passed=%d reason=%s"), Passed, Reason);
	FPlatformMisc::RequestExitWithStatus(false, Passed ? 0 : 1);
}
