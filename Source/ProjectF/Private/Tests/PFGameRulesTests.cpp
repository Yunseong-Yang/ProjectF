#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Core/PFTypes.h"
#include "Combat/PFHealthComponent.h"
#include "Combat/PFWeaponComponent.h"
#include "Combat/PFTakedownComponent.h"
#include "AI/PFEnemyCharacter.h"
#include "Player/PFPlayerCharacter.h"
#include "Progression/PFPlayerState.h"
#include "Save/PFSaveSubsystem.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/Crc.h"
#include "World/PFOutpost.h"
#include "World/PFWorldStateSubsystem.h"
#include "Components/BoxComponent.h"
#include "AI/PFCoverPoint.h"
#include "World/PFInteractable.h"
#include "Player/PFTraversalComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UI/PFUserSettings.h"
#include "Combat/PFShotEffectLedger.h"
#include "Animation/AnimInstance.h"

namespace
{
	struct FPFTestWorld
	{
		UWorld* World;
		uint64 OriginalFrame = GFrameCounter;
		FPFTestWorld()
		{
			const UWorld::InitializationValues Options = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Options);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}
		~FPFTestWorld() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); GFrameCounter = OriginalFrame; }
		void Advance(float Seconds)
		{
			for (float Time = 0.f; Time < Seconds; Time += .05f) { ++GFrameCounter; World->Tick(LEVELTICK_All, .05f); }
		}
		APFPlayerCharacter* Player(FVector Location)
		{
			FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APFPlayerCharacter* Result = World->SpawnActor<APFPlayerCharacter>(Location, FRotator::ZeroRotator, Params);
			Result->bProfileReady = true; return Result;
		}
		APFEnemyCharacter* Enemy(FVector Location)
		{
			FTransform Transform(FRotator::ZeroRotator, Location);
			APFEnemyCharacter* Result = World->SpawnActorDeferred<APFEnemyCharacter>(APFEnemyCharacter::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			Result->AutoPossessAI = EAutoPossessAI::Disabled; Result->FinishSpawning(Transform); Result->DispatchBeginPlay(); return Result;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFScalingTest, "ProjectF.Rules.CooperativeScale", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFScalingTest::RunTest(const FString& Parameters)
{
	for (int32 Count = 1; Count <= 4; ++Count)
	{
		const FPFCombatScale Scale = FPFGameRules::ScaleFor(Count);
		TestEqual(TEXT("Participant count"), Scale.Participants, Count);
		TestEqual(TEXT("Garrison count"), Scale.Garrison, 6 + 2 * Count);
		TestTrue(TEXT("Health scale"), FMath::IsNearlyEqual(Scale.Health, 1.f + .15f * (Count - 1)));
		TestTrue(TEXT("Damage scale"), FMath::IsNearlyEqual(Scale.Damage, 1.f + .05f * (Count - 1)));
	}
	TestEqual(TEXT("Invalid low count clamps"), FPFGameRules::ScaleFor(-100).Participants, 1);
	TestEqual(TEXT("Invalid high count clamps"), FPFGameRules::ScaleFor(100).Participants, 4); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFSkillTest, "ProjectF.Rules.SkillPrerequisitesAndProfileValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFSkillTest::RunTest(const FString& Parameters)
{
	FPFProfile Profile; Profile.PlayerId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	TestEqual(TEXT("Twelve initial nodes"), FPFGameRules::Skills().Num(), 12);
	TestFalse(TEXT("No free level one skill"), FPFGameRules::CanUnlock(Profile, TEXT("Aerial")));
	Profile.Experience = 250;
	TestTrue(TEXT("First point at first level"), FPFGameRules::CanUnlock(Profile, TEXT("Aerial")));
	TestFalse(TEXT("Cannot skip prerequisite"), FPFGameRules::CanUnlock(Profile, TEXT("Cover")));
	Profile.Skills.Add(TEXT("Aerial")); Profile.SpentPoints = 1;
	TestTrue(TEXT("Valid learned profile"), FPFGameRules::ValidateProfile(Profile));
	TestFalse(TEXT("Cannot learn twice"), FPFGameRules::CanUnlock(Profile, TEXT("Aerial")));
	Profile.Skills.Add(TEXT("Aerial")); TestFalse(TEXT("Duplicate profile rejected"), FPFGameRules::ValidateProfile(Profile));
	Profile.Skills.Reset(); Profile.SpentPoints = 0; Profile.Skills.Add(TEXT("ExecutionMaster")); Profile.SpentPoints = 2;
	TestFalse(TEXT("Forged high skill rejected"), FPFGameRules::ValidateProfile(Profile));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFSequenceTest, "ProjectF.Rules.RequestReplayAndWraparound", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFSequenceTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Next request"), FPFGameRules::IsNewSequence(2, 1));
	TestFalse(TEXT("Duplicate rejected"), FPFGameRules::IsNewSequence(2, 2));
	TestFalse(TEXT("Old request rejected"), FPFGameRules::IsNewSequence(1, 2));
	TestTrue(TEXT("Wrap accepted"), FPFGameRules::IsNewSequence(1, MAX_uint32));
	TestFalse(TEXT("Old pre-wrap rejected"), FPFGameRules::IsNewSequence(MAX_uint32, 1)); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFHealthTest, "ProjectF.Combat.DamageDownReviveAndExecution", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFHealthTest::RunTest(const FString& Parameters)
{
	FPFTestWorld Fixture; APFPlayerCharacter* Player = Fixture.Player(FVector(0, 0, 96)); APFPlayerCharacter* Teammate = Fixture.Player(FVector(500, 0, 96));
	TestEqual(TEXT("Friendly fire rejected"), Player->Health->ApplyDamage(50.f, Teammate), 0.f);
	TestEqual(TEXT("Health unchanged"), Player->Health->Current, 100.f);
	TestEqual(TEXT("Environment damage applies"), Player->Health->ApplyDamage(100.f, nullptr), 100.f);
	TestEqual(TEXT("Co-op downed"), Player->Health->Life, EPFLifeState::Downed);
	TestFalse(TEXT("Cannot heal a downed player"), Player->Health->Heal(60.f));
	TestTrue(TEXT("Can revive once"), Player->Health->Revive());
	TestFalse(TEXT("Duplicate revive rejected"), Player->Health->Revive());
	TestEqual(TEXT("Revive gives forty health"), Player->Health->Current, 40.f);
	TestTrue(TEXT("Guaranteed execution"), Player->Health->Execute());
	TestFalse(TEXT("Duplicate execution rejected"), Player->Health->Execute());
	TestEqual(TEXT("Dead ignores damage"), Player->Health->ApplyDamage(100.f, nullptr), 0.f); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFExecutionTest, "ProjectF.Takedown.ExclusiveReservationAndHealthIndependence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFExecutionTest::RunTest(const FString& Parameters)
{
	FPFTestWorld Fixture; APFPlayerCharacter* First = Fixture.Player(FVector(-100, 0, 96)); APFPlayerCharacter* Second = Fixture.Player(FVector(-100, 100, 96));
	APFEnemyCharacter* Enemy = Fixture.Enemy(FVector(0, 0, 96)); Enemy->Health->InitializeHealth(1000000.f);
	TestTrue(TEXT("Death lifecycle delegate bound"), Enemy->Health->OnLifeChanged.IsBound());
	TestTrue(TEXT("First reservation wins"), Enemy->ReserveExecution(First));
	TestFalse(TEXT("Second request loses"), Enemy->ReserveExecution(Second));
	TestFalse(TEXT("Non-owner cannot commit"), Enemy->CommitExecution(Second));
	TestTrue(TEXT("Owner kills arbitrary health"), Enemy->CommitExecution(First));
	TestEqual(TEXT("Dead regardless of health"), Enemy->Health->Life, EPFLifeState::Dead);
	TestFalse(TEXT("No second kill"), Enemy->CommitExecution(First));
	const FGuid Kill = Enemy->KillId;
	TestTrue(TEXT("Kill id recorded"), Kill.IsValid());
	Enemy->Health->Execute(); TestEqual(TEXT("Id remains stable"), Enemy->KillId, Kill);
	APFEnemyCharacter* Other = Fixture.Enemy(FVector(500, 0, 96));
	TestTrue(TEXT("Another enemy independent"), Other->ReserveExecution(Second));
	Other->ReleaseExecution(Second); TestFalse(TEXT("Reservation released"), Other->IsExecutionReserved()); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFFrozenScaleTest, "ProjectF.Combat.BattleScaleCannotChangeMidFight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFFrozenScaleTest::RunTest(const FString& Parameters)
{
	FPFTestWorld Fixture; APFEnemyCharacter* Enemy = Fixture.Enemy(FVector(0, 0, 96));
	Enemy->InitializeCombatScale(FPFGameRules::ScaleFor(2)); Enemy->Health->ApplyDamage(30.f, nullptr);
	const float Remaining = Enemy->Health->Current;
	Enemy->InitializeCombatScale(FPFGameRules::ScaleFor(4));
	TestEqual(TEXT("Existing health unchanged on join"), Enemy->Health->Current, Remaining);
	TestTrue(TEXT("Existing max remains frozen"), FMath::IsNearlyEqual(Enemy->Health->Maximum, 115.f)); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFRewardTest, "ProjectF.Progression.RewardsExactlyOnce", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFRewardTest::RunTest(const FString& Parameters)
{
	FPFTestWorld Fixture; APFPlayerState* Player = Fixture.World->SpawnActor<APFPlayerState>();
	TestTrue(TEXT("First reward applies"), Player->GrantReward(TEXT("Outpost.Radio"), 400, 10, 5));
	TestFalse(TEXT("Replay is ignored"), Player->GrantReward(TEXT("Outpost.Radio"), 400, 10, 5));
	TestEqual(TEXT("One experience reward"), Player->Profile.Experience, 400);
	TestEqual(TEXT("One parts reward"), Player->Profile.Parts, 10);
	TestEqual(TEXT("One supply reward"), Player->Profile.Supplies, 5);
	TestFalse(TEXT("Negative spending rejected"), Player->SpendSupplies(-10)); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFSaveTest, "ProjectF.Save.AtomicWriteAndCorruptionFallback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFSaveTest::RunTest(const FString& Parameters)
{
	UGameInstance* Instance = NewObject<UGameInstance>(); UPFSaveSubsystem* Saves = NewObject<UPFSaveSubsystem>(Instance);
	UPFSaveGame* Save = NewObject<UPFSaveGame>(); Save->WorldId = FGuid::NewGuid();
	const FString Slot = TEXT("Automation_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	TestTrue(TEXT("Initial write"), Saves->Write(Slot, Save));
	const FGuid Original = Save->WorldId; Save->WorldId = FGuid::NewGuid();
	TestTrue(TEXT("Second write retains backup"), Saves->Write(Slot, Save));
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), Slot + TEXT(".pfs"));
	TArray<uint8> Corruption = {1, 2, 3}; FFileHelper::SaveArrayToFile(Corruption, *Path);
	UPFSaveGame* Restored = Saves->Read(Slot);
	TestNotNull(TEXT("Backup restored after corruption"), Restored);
	if (Restored) TestEqual(TEXT("Previous normal world id restored"), Restored->WorldId, Original);
	TArray<uint8> Decoded; TestFalse(TEXT("Truncated payload rejected"), UPFSaveSubsystem::Decode(Corruption, Decoded));
	IFileManager::Get().Delete(*Path); IFileManager::Get().Delete(*(Path + TEXT(".bak"))); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFAmmoTest, "ProjectF.Combat.SessionInventoryDoesNotRefillOnRestore", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFAmmoTest::RunTest(const FString& Parameters)
{
	FPFTestWorld Fixture; APFPlayerCharacter* Player = Fixture.Player(FVector(0, 0, 96)); Player->DispatchBeginPlay();
	Player->Weapon->State.Magazine = 2; Player->Weapon->State.Reserve = 0;
	const auto Snapshot = Player->Weapon->Snapshot(); APFPlayerCharacter* Respawn = Fixture.Player(FVector(500, 0, 96)); Respawn->DispatchBeginPlay();
	Respawn->Weapon->Restore(Snapshot, EPFWeaponType::Rifle);
	TestEqual(TEXT("Remaining magazine retained"), Respawn->Weapon->State.Magazine, 2);
	TestEqual(TEXT("Empty reserve remains empty"), Respawn->Weapon->State.Reserve, 0); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFRearGeometryTest, "ProjectF.Takedown.RearConeBoundary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFRearGeometryTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Rear accepted"), FPFGameRules::IsRearApproach(FVector::ForwardVector, FVector(-1, 0, 0)));
	TestFalse(TEXT("Front rejected"), FPFGameRules::IsRearApproach(FVector::ForwardVector, FVector(1, 0, 0)));
	TestFalse(TEXT("Side rejected"), FPFGameRules::IsRearApproach(FVector::ForwardVector, FVector(0, 1, 0)));
	TestFalse(TEXT("Overlapping zero direction rejected"), FPFGameRules::IsRearApproach(FVector::ForwardVector, FVector::ZeroVector)); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFWeakPointTest, "ProjectF.Combat.WeakPointAndShotgunException", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFWeakPointTest::RunTest(const FString& Parameters)
{
	FPFTestWorld Fixture; APFPlayerCharacter* Shooter = Fixture.Player(FVector(-100, 0, 96));
	APFEnemyCharacter* RifleTarget = Fixture.Enemy(FVector(100, 0, 96)); RifleTarget->InitializeCombatScale(FPFGameRules::ScaleFor(4));
	RifleTarget->ReceiveShot(30.f, TEXT("head"), 1500.f, EPFWeaponType::Rifle, Shooter);
	TestEqual(TEXT("Valid unarmored headshot ignores co-op health"), RifleTarget->Health->Life, EPFLifeState::Dead);
	APFEnemyCharacter* ShotgunTarget = Fixture.Enemy(FVector(500, 0, 96)); ShotgunTarget->InitializeCombatScale(FPFGameRules::ScaleFor(4));
	ShotgunTarget->ReceiveShot(14.f, TEXT("head"), 500.f, EPFWeaponType::Shotgun, Shooter);
	TestTrue(TEXT("Single shotgun pellet cannot use lethal weak point"), ShotgunTarget->Health->IsAlive());
	APFEnemyCharacter* Armored = Fixture.Enemy(FVector(900, 0, 96)); Armored->EnemyRole = EPFEnemyRole::Heavy; Armored->bHelmetIntact = true; Armored->Health->InitializeHealth(200.f);
	Armored->ReceiveShot(30.f, TEXT("head"), 500.f, EPFWeaponType::Rifle, Shooter);
	TestTrue(TEXT("Helmet absorbs first impact"), Armored->Health->IsAlive()); TestFalse(TEXT("Helmet breaks"), Armored->bHelmetIntact); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFActionCancellationTest, "ProjectF.Combat.HealingAndReloadCancelWithoutResourceLoss", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFActionCancellationTest::RunTest(const FString& Parameters)
{
	FPFTestWorld Fixture; APFPlayerCharacter* Player = Fixture.Player(FVector(0, 0, 96)); Player->DispatchBeginPlay();
	Player->Health->Current = 40.f; Player->Medkits = 1; Player->ServerHeal(1); Fixture.Advance(1.f);
	Player->Health->ApplyDamage(5.f, nullptr); Fixture.Advance(2.f);
	TestEqual(TEXT("Interrupted heal does not consume a kit"), Player->Medkits, 1);
	TestEqual(TEXT("Interrupted heal does not restore health"), Player->Health->Current, 35.f);
	Player->Weapon->State.Magazine = 3; Player->Weapon->State.Reserve = 10;
	Player->Weapon->ServerReload(2); Fixture.Advance(1.f); Player->Weapon->Cancel(); Fixture.Advance(2.f);
	TestEqual(TEXT("Canceled magazine unchanged"), Player->Weapon->State.Magazine, 3);
	TestEqual(TEXT("Canceled reserve unchanged"), Player->Weapon->State.Reserve, 10);
	Player->Weapon->ServerReload(3); Player->Weapon->ServerReload(3); Fixture.Advance(2.5f);
	TestEqual(TEXT("Reload transfers available bullets once"), Player->Weapon->State.Magazine, 13);
	TestEqual(TEXT("Reload cannot duplicate reserve"), Player->Weapon->State.Reserve, 0); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFOccludedExecutionTest, "ProjectF.Takedown.WallBlocksEligibleRearApproach", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFOccludedExecutionTest::RunTest(const FString& Parameters)
{
	FPFTestWorld Fixture; APFPlayerCharacter* Player = Fixture.Player(FVector(-130, 0, 96));
	APFEnemyCharacter* Enemy = Fixture.Enemy(FVector(0, 0, 96));
	TestTrue(TEXT("Unobstructed rear approach eligible"), Player->Takedown->IsEligible(Enemy, EPFTakedownType::Rear));
	AActor* Wall = Fixture.World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Wall); Wall->SetRootComponent(Box); Box->SetBoxExtent(FVector(5, 100, 100));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent(); Wall->SetActorLocation(FVector(-65, 0, 100));
	TestFalse(TEXT("Solid cover blocks execution"), Player->Takedown->IsEligible(Enemy, EPFTakedownType::Rear));
	TestTrue(TEXT("Denied request leaves enemy alive"), Enemy->Health->IsAlive()); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFThreatRosterTest, "ProjectF.World.UnloadingDoesNotResolveThreats", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFThreatRosterTest::RunTest(const FString& Parameters)
{
	FPFTestWorld Fixture; APFOutpost* Outpost = Fixture.World->SpawnActor<APFOutpost>();
	APFEnemyCharacter* Enemy = Fixture.Enemy(FVector(0, 0, 96));
	TestTrue(TEXT("Threat registers"), Outpost->RegisterEnemy(Enemy, TEXT("Guard01")));
	TestEqual(TEXT("One unresolved threat"), Outpost->RemainingThreats, 1);
	Enemy->Destroy(); TestEqual(TEXT("Unloaded living actor is not a kill"), Outpost->RemainingThreats, 1);
	APFEnemyCharacter* Reloaded = Fixture.Enemy(FVector(500, 0, 96));
	TestTrue(TEXT("Same stable threat can reload"), Outpost->RegisterEnemy(Reloaded, TEXT("Guard01")));
	TestEqual(TEXT("Reload does not double count"), Outpost->RemainingThreats, 1);
	Outpost->ResolveEnemy(TEXT("Guard01"), Reloaded); Outpost->ResolveEnemy(TEXT("Guard01"), Reloaded);
	TestEqual(TEXT("Explicit death resolves exactly once"), Outpost->RemainingThreats, 0); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFSafeSpawnTest, "ProjectF.World.SafeSpawnRejectsEnemySight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFSafeSpawnTest::RunTest(const FString& Parameters)
{
	FPFTestWorld Fixture; APFEnemyCharacter* Enemy = Fixture.Enemy(FVector(0, 0, 96));
	UPFWorldStateSubsystem* World = Fixture.World->GetSubsystem<UPFWorldStateSubsystem>();
	TestFalse(TEXT("Nearby enemy blocks spawn even from behind"), World->IsSafeLocation(FVector(-1000, 0, 96)));
	TestFalse(TEXT("Distant but visible position rejected"), World->IsSafeLocation(FVector(3000, 0, 96)));
	TestTrue(TEXT("Outside enemy sight may be safe"), World->IsSafeLocation(FVector(-3000, 0, 96)));
	AActor* Wall = Fixture.World->SpawnActor<AActor>(); UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Box); Box->SetBoxExtent(FVector(10, 500, 300)); Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent(); Wall->SetActorLocation(FVector(1500, 0, 150));
	TestTrue(TEXT("Solid cover can protect a distant spawn"), World->IsSafeLocation(FVector(3000, 0, 96)));
	Enemy->Health->Execute(); TestTrue(TEXT("Dead enemy is not a spawn threat"), World->IsSafeLocation(FVector(-1000, 0, 96))); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFDamageInputTest, "ProjectF.Combat.NonlethalDamagePreservesFireAndDownClearsMovement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFDamageInputTest::RunTest(const FString& Parameters)
{
	FPFTestWorld Fixture; APFPlayerCharacter* Player = Fixture.Player(FVector(0, 0, 96)); Player->DispatchBeginPlay();
	Player->Weapon->ServerSetTrigger(true, 1);
	TestTrue(TEXT("Trigger held before hit"), Player->Weapon->HasTriggerHeld());
	Player->Health->ApplyDamage(1.f, nullptr);
	TestTrue(TEXT("A nonlethal hit does not silently release automatic fire"), Player->Weapon->HasTriggerHeld());
	Player->bSprinting = true; Player->bAiming = true; Player->Health->ApplyDamage(1000.f, nullptr);
	TestFalse(TEXT("Down cancels fire"), Player->Weapon->HasTriggerHeld());
	TestFalse(TEXT("Down clears stale sprint input"), Player->bSprinting);
	TestFalse(TEXT("Down clears stale aim input"), Player->bAiming); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFFirstDamageTest, "ProjectF.Combat.FirstDamageAppliesAfterBattleScaling", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFFirstDamageTest::RunTest(const FString& Parameters)
{
	FPFTestWorld Fixture;
	APFEnemyCharacter* Target = Fixture.Enemy(FVector(0, 0, 96));
	Target->Health->OnBeforeDamaged.AddWeakLambda(Target, [Target](float Amount, AActor* Source) { Target->InitializeCombatScale(FPFGameRules::ScaleFor(4)); });
	Target->Health->ApplyDamage(30.f, nullptr);
	TestTrue(TEXT("First damage is retained after scaling"), FMath::IsNearlyEqual(Target->Health->Current, 115.f));
	TestTrue(TEXT("Scaled maximum is frozen"), FMath::IsNearlyEqual(Target->Health->Maximum, 145.f));
	APFEnemyCharacter* Lethal = Fixture.Enemy(FVector(500, 0, 96));
	Lethal->Health->OnBeforeDamaged.AddWeakLambda(Lethal, [Lethal](float Amount, AActor* Source) { Lethal->InitializeCombatScale(FPFGameRules::ScaleFor(4)); });
	Lethal->Health->ApplyDamage(200.f, nullptr);
	TestEqual(TEXT("A lethal first explosion is not healed by scaling"), Lethal->Health->Life, EPFLifeState::Dead); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFDetectionTest, "ProjectF.Stealth.StanceDistanceAndFrameIndependentDetection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFDetectionTest::RunTest(const FString& Parameters)
{
	const float Standing = FPFGameRules::DetectionGain(.2f, 2500.f, false, false);
	TestTrue(TEXT("Crouching allows more response time"), FPFGameRules::DetectionGain(.2f, 2500.f, true, false) < Standing);
	TestTrue(TEXT("Sprinting exposes the player faster"), FPFGameRules::DetectionGain(.2f, 2500.f, false, true) > Standing);
	TestTrue(TEXT("Close exposure confirms faster"), FPFGameRules::DetectionGain(.2f, 300.f, false, false) > Standing);
	TestTrue(TEXT("Time partition does not change detection"), FMath::IsNearlyEqual(Standing * 10.f, FPFGameRules::DetectionGain(2.f, 2500.f, false, false)));
	TestEqual(TEXT("Negative elapsed time rejected"), FPFGameRules::DetectionGain(-1.f, 100.f, false, false), 0.f); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFCoverReservationTest, "ProjectF.AI.CoverOwnershipAndDeathCleanup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFCoverReservationTest::RunTest(const FString& Parameters)
{
	FPFTestWorld Fixture; APFEnemyCharacter* One = Fixture.Enemy(FVector(0, 0, 96)); APFEnemyCharacter* Two = Fixture.Enemy(FVector(500, 0, 96));
	APFCoverPoint* Cover = Fixture.World->SpawnActor<APFCoverPoint>();
	TestTrue(TEXT("First valid enemy reserves cover"), Cover->Reserve(One));
	TestFalse(TEXT("Second cannot take occupied cover"), Cover->Reserve(Two));
	Cover->Release(Two); TestTrue(TEXT("Wrong owner cannot release cover"), Cover->IsReservedBy(One));
	One->Health->Execute(); TestTrue(TEXT("Death cannot permanently lock cover"), Cover->Reserve(Two));
	Cover->Release(Two); TestFalse(TEXT("Exit releases cover"), Cover->IsReservedBy(Two)); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFAlarmInterruptionTest, "ProjectF.World.DamageInterruptsAlarmCall", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFAlarmInterruptionTest::RunTest(const FString& Parameters)
{
	FPFTestWorld Fixture; APFOutpost* Outpost = Fixture.World->SpawnActor<APFOutpost>();
	APFInteractable* Alarm = Fixture.World->SpawnActor<APFInteractable>(); Alarm->Kind = EPFInteractableKind::Alarm;
	Outpost->Alarm = Alarm; Outpost->Phase = EPFOutpostPhase::Engaged;
	APFEnemyCharacter* Enemy = Fixture.Enemy(FVector(0, 0, 96)); Enemy->Outpost = Outpost;
	TestTrue(TEXT("Caller reserves device"), Outpost->ReserveAlarm(Enemy));
	TestTrue(TEXT("Caller starts timed operation"), Outpost->BeginAlarm(Enemy));
	Enemy->Health->ApplyDamage(1.f, nullptr);
	TestNull(TEXT("A hit interrupts the caller before the call completes"), Outpost->AlarmCaller.Get());
	TestFalse(TEXT("An interrupted call is not marked successful"), Outpost->bAlarmCalled);
	TestTrue(TEXT("Reservation becomes available again"), Outpost->ReserveAlarm(Enemy)); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFReinforcementArrivalTest, "ProjectF.World.ReinforcementMustEnterBeforeThreatRegistration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFReinforcementArrivalTest::RunTest(const FString& Parameters)
{
	FPFTestWorld Fixture; APFOutpost* Outpost = Fixture.World->SpawnActor<APFOutpost>(); Outpost->Phase = EPFOutpostPhase::Engaged;
	APFEnemyCharacter* Enemy = Fixture.Enemy(FVector(5000, 0, 96)); Enemy->Outpost = Outpost; Enemy->ThreatId = TEXT("Reinforcement.0"); Enemy->bReinforcement = true; Enemy->bHasArrived = false;
	TestFalse(TEXT("Distant entrant has not arrived"), Enemy->TryArrive()); TestEqual(TEXT("Unarrived soldier is not an arrived threat"), Outpost->RemainingThreats, 0);
	Enemy->SetActorLocation(FVector(1000, 0, 96)); TestTrue(TEXT("Entering the site registers arrival"), Enemy->TryArrive());
	Enemy->TryArrive(); TestEqual(TEXT("Repeated arrival registers only once"), Outpost->RemainingThreats, 1);
	Enemy->Health->Execute(); TestEqual(TEXT("Arrived reinforcement death resolves the roster"), Outpost->RemainingThreats, 0); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFEquipmentProfileTest, "ProjectF.Progression.BlueprintAndEquipmentPersistenceRules", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFEquipmentProfileTest::RunTest(const FString& Parameters)
{
	FPFProfile Profile; Profile.PlayerId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	TestTrue(TEXT("Old profile with no equipment remains valid"), FPFGameRules::ValidateProfile(Profile));
	Profile.Equipment.Add(TEXT("Shotgun")); TestFalse(TEXT("Crafted equipment requires its blueprint"), FPFGameRules::ValidateProfile(Profile));
	Profile.Blueprints.Add(TEXT("Shotgun")); Profile.PrimaryWeapon = EPFWeaponType::Shotgun;
	TestTrue(TEXT("Prepared shotgun survives validation"), FPFGameRules::ValidateProfile(Profile));
	Profile.PrimaryWeapon = EPFWeaponType::Pistol; TestFalse(TEXT("Sidearm cannot replace primary slot"), FPFGameRules::ValidateProfile(Profile)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFVaultSpaceTest, "ProjectF.Movement.VaultRequiresClearArcAndSafeLanding", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFVaultSpaceTest::RunTest(const FString& Parameters)
{
    FPFTestWorld Fixture;
    auto Solid = [&Fixture](FVector Location, FVector Extent)
    {
        AActor* Actor = Fixture.World->SpawnActor<AActor>(); UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Box); Box->SetBoxExtent(Extent); Actor->SetActorLocation(Location); Box->SetMobility(EComponentMobility::Static);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Box->SetCollisionResponseToAllChannels(ECR_Block);
        Box->RegisterComponent(); return Actor;
    };
    Solid(FVector(0, 0, -20), FVector(1000, 1000, 20));
    Solid(FVector(90, 0, 50), FVector(15, 90, 50));
    APFPlayerCharacter* Player = Fixture.Player(FVector(0, 0, 96));
    Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    float Distance = 0.f, Height = 0.f;
    FName Failure;
    const bool bVault = Player->Traversal->FindVault(Distance, Height, &Failure);
    TestTrue(FString::Printf(TEXT("A low obstacle and clear landing permit vault; reason=%s"), *Failure.ToString()), bVault);
    Solid(FVector(100, 0, 260), FVector(200, 150, 10));
    TestFalse(TEXT("Overhead collision rejects the whole arc"), Player->Traversal->FindVault(Distance, Height));
    Player->Traversal->ServerVault(1); TestFalse(TEXT("Denied vault never locks input"), Player->Traversal->bVaulting);
    TestTrue(TEXT("Denied vault preserves ordinary actions"), Player->ActionName.IsNone()); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFLegacyBlueprintTest, "ProjectF.Save.LegacyExplorationKeepsNewBlueprintEntitlements", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFLegacyBlueprintTest::RunTest(const FString& Parameters)
{
    FPFProfile Profile; Profile.PlayerId = FGuid::NewGuid().ToString(EGuidFormats::Digits); Profile.Revision = 10;
    Profile.ClaimedRewards = {TEXT("Exploration.0"), TEXT("Exploration.1")};
    TestTrue(TEXT("An old collected cache restores its new blueprint"), FPFGameRules::UpgradeProfile(Profile));
    TestTrue(TEXT("Suppressor entitlement preserved"), Profile.Blueprints.Contains(TEXT("Suppressor")));
    TestTrue(TEXT("Shotgun entitlement preserved"), Profile.Blueprints.Contains(TEXT("Shotgun")));
    TestFalse(TEXT("Uncollected cache remains unexplored"), Profile.Blueprints.Contains(TEXT("Optic")));
    TestEqual(TEXT("Migration advances revision once"), Profile.Revision, 11LL);
    TestFalse(TEXT("Repeated loads cannot repeat the migration"), FPFGameRules::UpgradeProfile(Profile));
    TestEqual(TEXT("Repeated loads preserve revision"), Profile.Revision, 11LL);
    TestTrue(TEXT("Migrated profile is valid"), FPFGameRules::ValidateProfile(Profile)); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFFirstPersonBindingsTest, "ProjectF.Presentation.CameraRelativeArmsAndWeaponSpecificAnimation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFFirstPersonBindingsTest::RunTest(const FString& Parameters)
{
    UClass* PlayerClass = LoadClass<APFPlayerCharacter>(nullptr, TEXT("/Game/ProjectF/Player/BP_PFPlayer.BP_PFPlayer_C"));
    if (!TestNotNull(TEXT("Authored player class loads"), PlayerClass)) return false;
    FPFTestWorld Fixture;
    FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APFPlayerCharacter* Player = Fixture.World->SpawnActor<APFPlayerCharacter>(PlayerClass, FVector(0, 0, 96), FRotator::ZeroRotator, Spawn);
    Player->bProfileReady = true; Player->DispatchBeginPlay();
    APlayerController* Controller = Fixture.World->SpawnActor<APlayerController>();
    UGameInstance* Instance = NewObject<UGameInstance>(); Fixture.World->SetGameInstance(Instance);
    Controller->SetPlayer(NewObject<ULocalPlayer>(GEngine)); Controller->Possess(Player); Player->PawnClientRestart();
    TestTrue(TEXT("View pose copies its source body before camera warping"), Player->FirstPersonMesh->GetAttachParent() == Player->GetMesh());
    TestTrue(TEXT("Weapon follows the displayed hand"), Player->ViewWeapon->GetAttachParent() == Player->FirstPersonMesh);
    TestTrue(TEXT("Remote weapon follows the body hand"), Player->RemoteWeapon->GetAttachParent() == Player->GetMesh() && Player->RemoteWeapon->GetAttachSocketName() == TEXT("HandGrip_R"));
    TestFalse(TEXT("View head hiding never hides remote body head"), Player->GetMesh()->IsBoneHiddenByName(TEXT("head")));
    TestNotNull(TEXT("Rifle view animation initialized"), Player->FirstPersonMesh->GetAnimInstance());
    Player->FirstPersonMesh->TickAnimation(.1f, false); Player->FirstPersonMesh->RefreshBoneTransforms();
    const FVector HandInView = Player->Camera->GetComponentTransform().InverseTransformPosition(Player->FirstPersonMesh->GetSocketLocation(TEXT("HandGrip_R")));
    TestTrue(FString::Printf(TEXT("Idle weapon must be in front of view, hand=%s"), *HandInView.ToString()), HandInView.X > 10.f);
    TestTrue(TEXT("Idle weapon stays at a usable eye-line height"), HandInView.Z > -55.f && HandInView.Z < 10.f);
    Player->Weapon->ServerEquip(EPFWeaponType::Pistol, 1);
    TestTrue(TEXT("Pistol mesh is selected"), Player->ViewWeapon->GetStaticMesh() == Player->PistolMesh);
    TestTrue(TEXT("Remote pistol mesh follows the authoritative equipped type"), Player->RemoteWeapon->GetStaticMesh() == Player->PistolMesh);
    TestTrue(TEXT("Pistol has its own first-person graph"), Player->FirstPersonMesh->GetAnimClass() == Player->PistolViewAnimation);
    TestTrue(TEXT("Remote body uses the matching pistol graph"), Player->GetMesh()->GetAnimClass() == Player->PistolBodyAnimation);
    const float PreviousViewFOV = Player->Camera->FirstPersonFieldOfView;
    Player->PlayRearExecution(.3f);
    TestTrue(TEXT("Rear strike bypasses the aim-warp view graph"), Player->FirstPersonMesh->GetAnimClass() == Player->PistolBodyAnimation);
    TestTrue(TEXT("Only the view head is hidden during the strike"), Player->FirstPersonMesh->IsBoneHiddenByName(TEXT("neck_01")) && !Player->GetMesh()->IsBoneHiddenByName(TEXT("neck_01")));
    Player->StopRearExecution();
    TestTrue(TEXT("Normal first-person aiming graph is restored"), Player->FirstPersonMesh->GetAnimClass() == Player->PistolViewAnimation);
    TestTrue(TEXT("View FOV is restored"), FMath::IsNearlyEqual(Player->Camera->FirstPersonFieldOfView, PreviousViewFOV));
    TestFalse(TEXT("Temporary view head hiding is cleaned up"), Player->FirstPersonMesh->IsBoneHiddenByName(TEXT("neck_01")));
    Player->PlayRearExecution(.3f);
    Player->Health->ApplyDamage(1000.f, nullptr);
    TestEqual(TEXT("Presentation enters the downed state"), Player->Health->Life, EPFLifeState::Downed);
    TestFalse(TEXT("A downed player does not present an active remote weapon"), Player->RemoteWeapon->IsVisible());
    TestFalse(TEXT("Downing interrupts the temporary first-person pose"), Player->FirstPersonMesh->IsBoneHiddenByName(TEXT("neck_01")));
    TestTrue(TEXT("Revival succeeds"), Player->Health->Revive());
    TestEqual(TEXT("Revival restores the body graph instead of leaving a death pose"), Player->GetMesh()->GetAnimationMode(), EAnimationMode::AnimationBlueprint);
    TestNotNull(TEXT("Revived body animation instance exists"), Player->GetMesh()->GetAnimInstance());
    TestTrue(TEXT("Revived remote weapon is visible"), Player->RemoteWeapon->IsVisible());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFGuardRigTraceTest, "ProjectF.Presentation.GuardRigHeadTraceAndScaledWeakPoint", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFGuardRigTraceTest::RunTest(const FString& Parameters)
{
    UClass* GuardClass = LoadClass<APFEnemyCharacter>(nullptr, TEXT("/Game/ProjectF/AI/BP_Guard.BP_Guard_C"));
    if (!TestNotNull(TEXT("Authored guard class loads"), GuardClass)) return false;
    FPFTestWorld Fixture; const FTransform Transform(FRotator::ZeroRotator, FVector(0, 0, 96));
    APFEnemyCharacter* Enemy = Fixture.World->SpawnActorDeferred<APFEnemyCharacter>(GuardClass, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Enemy->AutoPossessAI = EAutoPossessAI::Disabled; Enemy->FinishSpawning(Transform); Enemy->DispatchBeginPlay();
    Enemy->InitializeCombatScale(FPFGameRules::ScaleFor(4));
    Enemy->GetMesh()->TickAnimation(.1f, false); Enemy->GetMesh()->RefreshBoneTransforms(); Enemy->GetMesh()->UpdateKinematicBonesToAnim(Enemy->GetMesh()->GetComponentSpaceTransforms(), ETeleportType::TeleportPhysics, true);
    TestTrue(TEXT("Guard has a stable rifle grip socket"), Enemy->GetMesh()->DoesSocketExist(TEXT("HandGrip_R")));
    const FVector Head = Enemy->GetMesh()->GetSocketLocation(TEXT("head")) + FVector(0, 0, 5);
    TestTrue(FString::Printf(TEXT("Head remains at humanoid height: %s"), *Head.ToString()), Head.Z > 120.f && Head.Z < 230.f);
    APFPlayerCharacter* Shooter = Fixture.Player(Head - FVector(500, 0, 0));
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(PFGuardHeadTest), true, Shooter);
    const bool bHit = Fixture.World->LineTraceSingleByChannel(Hit, Head - FVector(400, 0, 0), Head + FVector(40, 0, 0), ECC_Visibility, Query);
    TestTrue(FString::Printf(TEXT("Physics trace identifies visible head: actor=%s bone=%s"), *GetNameSafe(Hit.GetActor()), *Hit.BoneName.ToString()), bHit && Hit.GetActor() == Enemy && Hit.BoneName.ToString().Contains(TEXT("head")));
    if (bHit && Hit.GetActor() == Enemy) Enemy->ReceiveShot(30.f, Hit.BoneName, 500.f, EPFWeaponType::Rifle, Shooter);
    TestEqual(TEXT("Valid traced headshot stays lethal with four-player scale"), Enemy->Health->Life, EPFLifeState::Dead);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFRebindingTest, "ProjectF.Input.RebindingRejectsDuplicatesAndRestoresDefaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFRebindingTest::RunTest(const FString& Parameters)
{
    UPFUserSettings* Settings = NewObject<UPFUserSettings>(); Settings->ResetBindings();
    TestTrue(TEXT("Movement can move off WASD"), Settings->RebindAction(TEXT("MoveForward"), EKeys::I));
    TestEqual(TEXT("New movement key is used"), Settings->GetBinding(TEXT("MoveForward")), EKeys::I);
    TestFalse(TEXT("Duplicate action keys are rejected"), Settings->RebindAction(TEXT("ReloadKey"), EKeys::I));
    TestEqual(TEXT("Rejected change keeps the previous binding"), Settings->GetBinding(TEXT("ReloadKey")), EKeys::R);
    TestFalse(TEXT("Escape stays available for closing menus"), Settings->RebindAction(TEXT("Jump"), EKeys::Escape));
    TestFalse(TEXT("Unknown actions cannot be injected"), Settings->RebindAction(TEXT("Unknown"), EKeys::J));
    TestFalse(TEXT("Mouse axes cannot replace a button"), Settings->RebindAction(TEXT("FireKey"), EKeys::MouseX));
    TestFalse(TEXT("Gamepad defaults are not consumed by keyboard rebinding"), Settings->RebindAction(TEXT("Jump"), EKeys::Gamepad_FaceButton_Bottom));
    Settings->ResetBindings();
    for (const auto& Entry : UPFUserSettings::DefaultBindings()) TestEqual(*Entry.Key.ToString(), Settings->GetBinding(Entry.Key), Entry.Value);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFShotLedgerTest, "ProjectF.Combat.CosmeticPredictionReconcilesReorderedShots", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFShotLedgerTest::RunTest(const FString& Parameters)
{
    FPFShotEffectLedger Ledger;
    TestTrue(TEXT("Local first shot plays immediately"), Ledger.Record(100, 0, EPFWeaponType::Rifle, 0));
    TestFalse(TEXT("Its server acknowledgement cannot replay the effect"), Ledger.Record(100, 0, EPFWeaponType::Rifle, .15));
    TestTrue(TEXT("An early authoritative automatic shot can play"), Ledger.Record(100, 2, EPFWeaponType::Rifle, .2));
    TestTrue(TEXT("The out-of-order middle shot remains independent"), Ledger.Record(100, 1, EPFWeaponType::Rifle, .25));
    TestFalse(TEXT("A later local timer cannot replay an early authoritative shot"), Ledger.Record(100, 2, EPFWeaponType::Rifle, .3));
    TestFalse(TEXT("Repeated acknowledgements stay idempotent"), Ledger.Record(100, 0, EPFWeaponType::Rifle, .4));
    TestTrue(TEXT("A different trigger request is independent"), Ledger.Record(101, 0, EPFWeaponType::Rifle, .5));
    TestTrue(TEXT("Weapon identity prevents collisions"), Ledger.Record(100, 0, EPFWeaponType::Pistol, .6));
    for (uint32 Id = 0; Id < 128; ++Id) Ledger.Record(Id, 0, EPFWeaponType::Shotgun, 1 + Id * .01);
    TestTrue(TEXT("Cosmetic history stays bounded"), Ledger.Num() <= 64);
    TestTrue(TEXT("Old sequence identities expire"), Ledger.Record(101, 0, EPFWeaponType::Rifle, 10));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFAutomaticCadenceTest, "ProjectF.Combat.AutomaticCadenceHandlesBoundedHitches", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFAutomaticCadenceTest::RunTest(const FString& Parameters)
{
    FPFTestWorld Fixture; APFPlayerCharacter* Player = Fixture.Player(FVector(0, 0, 96)); Player->DispatchBeginPlay();
    ++GFrameCounter; Fixture.World->Tick(LEVELTICK_All, .001f);
    Player->Weapon->ServerSetTrigger(true, 1);
    TestEqual(TEXT("First legal round is immediate"), Player->Weapon->State.Magazine, 29);
    ++GFrameCounter; Fixture.World->Tick(LEVELTICK_All, .25f);
    TestEqual(TEXT("A quarter-second frame still accounts for the two scheduled rounds"), Player->Weapon->State.Magazine, 27);
    const int32 BeforeStall = Player->Weapon->State.Magazine;
    ++GFrameCounter; Fixture.World->Tick(LEVELTICK_All, 1.f);
    TestTrue(TEXT("A long stall cannot release more than three catch-up rounds"), BeforeStall - Player->Weapon->State.Magazine <= 3);
    Player->Weapon->ServerSetTrigger(false, 2);
    const int32 BeforeRelease = Player->Weapon->State.Magazine; Fixture.Advance(.4f);
    TestEqual(TEXT("Release stops further authoritative firing"), Player->Weapon->State.Magazine, BeforeRelease);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFRearExecutionPresentationTest, "ProjectF.Presentation.RearExecutionRestoresCrouchAndCommitsIndependently", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFRearExecutionPresentationTest::RunTest(const FString& Parameters)
{
    UClass* PlayerClass = LoadClass<APFPlayerCharacter>(nullptr, TEXT("/Game/ProjectF/Player/BP_PFPlayer.BP_PFPlayer_C"));
    UClass* GuardClass = LoadClass<APFEnemyCharacter>(nullptr, TEXT("/Game/ProjectF/AI/BP_Guard.BP_Guard_C"));
    FPFTestWorld Fixture; FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APFPlayerCharacter* Player = Fixture.World->SpawnActor<APFPlayerCharacter>(PlayerClass, FVector(-100, 0, 96), FRotator::ZeroRotator, Spawn);
    Player->bProfileReady = true; Player->DispatchBeginPlay();
    APFEnemyCharacter* Enemy = Fixture.World->SpawnActorDeferred<APFEnemyCharacter>(GuardClass, FTransform(FVector(0, 0, 96)), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    Enemy->AutoPossessAI = EAutoPossessAI::Disabled; Enemy->FinishSpawning(FTransform(FVector(0, 0, 96))); Enemy->DispatchBeginPlay();
    Enemy->InitializeCombatScale(FPFGameRules::ScaleFor(4)); Enemy->Health->InitializeHealth(1000000.f);
    TestTrue(TEXT("Authored rear strike is available"), Player->HasRearExecutionMotion());
    Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Player->Crouch(); Player->GetCharacterMovement()->Crouch(false);
    TestTrue(TEXT("Test begins crouched"), Player->bIsCrouched);
    Player->ServerSetMovementState(false, true);
    Player->Takedown->ServerRequest(Enemy, EPFTakedownType::Rear, 1);
    TestEqual(TEXT("Presentation never delays authority execution of a million-health target"), Enemy->Health->Life, EPFLifeState::Dead);
    TestFalse(TEXT("Paired strike stands only after space validation"), Player->bIsCrouched);
    TestTrue(TEXT("Rear presentation is active"), Player->Takedown->IsPresenting());
    TestFalse(TEXT("Paired presentation releases aiming"), Player->bAiming);
    Player->ServerSetMovementState(false, true);
    TestFalse(TEXT("Aiming cannot interrupt the paired camera"), Player->bAiming);
    TestTrue(TEXT("A body montage exists"), Player->GetMesh()->GetAnimInstance() && Player->GetMesh()->GetAnimInstance()->GetCurrentActiveMontage());
    TestTrue(TEXT("Victim waits for contact before the fall"), Enemy->GetMesh()->bPauseAnims);
    Fixture.Advance(.4f);
    TestFalse(TEXT("Victim begins falling on the agreed timeline"), Enemy->GetMesh()->bPauseAnims);
    Fixture.Advance(1.f);
    TestTrue(TEXT("Crouch is restored after the action"), Player->bIsCrouched);
    TestTrue(TEXT("Input action is cleaned up"), Player->ActionName.IsNone());
    TestFalse(TEXT("No presentation input lock remains"), Player->Takedown->IsPresenting());
    TestEqual(TEXT("Authority kill remains stable after cleanup"), Enemy->Health->Life, EPFLifeState::Dead);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPFRearExecutionCeilingTest, "ProjectF.Presentation.RearExecutionRejectsBlockedStandingSpace", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPFRearExecutionCeilingTest::RunTest(const FString& Parameters)
{
    UClass* PlayerClass = LoadClass<APFPlayerCharacter>(nullptr, TEXT("/Game/ProjectF/Player/BP_PFPlayer.BP_PFPlayer_C"));
    FPFTestWorld Fixture; FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APFPlayerCharacter* Player = Fixture.World->SpawnActor<APFPlayerCharacter>(PlayerClass, FVector(-100, 0, 96), FRotator::ZeroRotator, Spawn);
    Player->bProfileReady = true; Player->DispatchBeginPlay();
    APFEnemyCharacter* Enemy = Fixture.Enemy(FVector(0, 0, 96));
    Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Player->Crouch(); Player->GetCharacterMovement()->Crouch(false);
    AActor* Ceiling = Fixture.World->SpawnActor<AActor>();
    UBoxComponent* Box = NewObject<UBoxComponent>(Ceiling); Ceiling->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(200, 200, 15)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Ceiling->SetActorLocation(FVector(-50, 0, 155));
    Player->Takedown->ServerRequest(Enemy, EPFTakedownType::Rear, 1);
    TestTrue(TEXT("Blocked paired alignment does not kill the target"), Enemy->Health->IsAlive());
    TestFalse(TEXT("A rejected alignment releases the target reservation"), Enemy->IsExecutionReserved());
    TestTrue(TEXT("Rejected action preserves crouch"), Player->bIsCrouched);
    TestTrue(TEXT("Rejected action does not leave an input lock"), Player->ActionName.IsNone());
    return true;
}

#endif
