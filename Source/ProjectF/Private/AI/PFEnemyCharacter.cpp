#include "AI/PFEnemyCharacter.h"
#include "AI/PFAIController.h"
#include "Combat/PFHealthComponent.h"
#include "Player/PFPlayerCharacter.h"
#include "Progression/PFPlayerState.h"
#include "World/PFOutpost.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Animation/AnimSequence.h"
#include "GameFramework/GameStateBase.h"
#include "TimerManager.h"

APFEnemyCharacter::APFEnemyCharacter()
{
	bReplicates = true; PrimaryActorTick.bCanEverTick = false;
	SetNetUpdateFrequency(15.f); SetMinNetUpdateFrequency(3.f); SetNetCullDistanceSquared(FMath::Square(18000.f));
	Health = CreateDefaultSubobject<UPFHealthComponent>(TEXT("Health"));
	HeldWeapon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeldWeapon")); HeldWeapon->SetupAttachment(GetMesh());
	HeldWeapon->SetCollisionEnabled(ECollisionEnabled::NoCollision); HeldWeapon->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	AIControllerClass = APFAIController::StaticClass(); AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	GetCapsuleComponent()->InitCapsuleSize(34.f, 96.f);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	GetCharacterMovement()->MaxWalkSpeed = 250.f;
	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
	Tags.Add(TEXT("PF.Enemy"));
}

void APFEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();
	EntryOrigin = GetActorLocation();
	if (GetMesh()->GetSkeletalMeshAsset() && GetMesh()->DoesSocketExist(TEXT("HandGrip_R")))
	{
		HeldWeapon->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("HandGrip_R")); HeldWeapon->SetRelativeRotation(FRotator::ZeroRotator);
	}
	Health->OnLifeChanged.AddDynamic(this, &ThisClass::OnLifeChanged);
	if (HasAuthority())
	{
		GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		bHelmetIntact = EnemyRole == EPFEnemyRole::Heavy;
		Health->InitializeHealth(EnemyRole == EPFEnemyRole::Heavy ? 200.f : 100.f);
		bHasArrived = !bReinforcement;
		if (Outpost && bHasArrived && !Outpost->RegisterEnemy(this, ThreatId)) { Destroy(); return; }
		Health->OnBeforeDamaged.AddWeakLambda(this, [this](float Amount, AActor* Source)
		{
			if (Outpost) Outpost->ReleaseAlarm(this);
			if (APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(Source)) RecordDamageContributor(Player);
			else if (Outpost) Outpost->Engage(nullptr);
		});
	}
}

void APFEnemyCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(ExecutionPoseTimer);
	ExecutionOwner.Reset();
	Health->OnLifeChanged.RemoveDynamic(this, &ThisClass::OnLifeChanged);
	// An unloaded living enemy is deliberately not reported as a kill.
	Super::EndPlay(Reason);
}

bool APFEnemyCharacter::TryArrive()
{
	if (!HasAuthority() || !Health->IsAlive()) return false;
	if (bHasArrived) return true;
	if (!Outpost || Outpost->Phase == EPFOutpostPhase::Captured)
	{
		Outpost = nullptr; bHasArrived = true; EntryDestination = EntryOrigin;
		PatrolPoints = {EntryOrigin, EntryOrigin + GetActorForwardVector() * 450.f}; return false;
	}
	if (GetDistanceTo(Outpost) > 4500.f) return false;
	if (!Outpost->RegisterEnemy(this, ThreatId)) return false;
	bHasArrived = true; return true;
}

void APFEnemyCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APFEnemyCharacter, EnemyRole); DOREPLIFETIME(APFEnemyCharacter, Alert);
	DOREPLIFETIME(APFEnemyCharacter, bHelmetIntact); DOREPLIFETIME(APFEnemyCharacter, KillId);
	DOREPLIFETIME(APFEnemyCharacter, DamageMultiplier);
	DOREPLIFETIME(APFEnemyCharacter, ExecutionPresentationTime);
}

void APFEnemyCharacter::InitializeCombatScale(const FPFCombatScale& Scale)
{
	if (!HasAuthority() || bScaleInitialized || !Health->IsAlive()) return;
	bScaleInitialized = true; FrozenScale = Scale; DamageMultiplier = Scale.Damage;
	Health->InitializeHealth((EnemyRole == EPFEnemyRole::Heavy ? 200.f : 100.f) * Scale.Health);
}

void APFEnemyCharacter::RecordDamageContributor(APFPlayerCharacter* Player)
{
	if (!HasAuthority() || !IsValid(Player)) return;
	Contributors.Add(Player); if (Outpost) { Outpost->Engage(Player); Outpost->RecordContribution(Player); }
	if (APFAIController* AI = Cast<APFAIController>(GetController())) AI->ReceiveReport(Player, Player->GetActorLocation(), GetWorld()->GetTimeSeconds());
}

float APFEnemyCharacter::ReceiveShot(float BaseDamage, FName Bone, float Distance, EPFWeaponType WeaponType, APFPlayerCharacter* Shooter)
{
	if (!HasAuthority() || !IsValid(Shooter) || !Shooter->Health->IsAlive() || !Health->IsAlive()) return 0.f;
	RecordDamageContributor(Shooter);
	const FPFWeaponDefinition Definition = FPFGameRules::Weapon(WeaponType);
	const bool bHead = Bone.ToString().Contains(TEXT("head"), ESearchCase::IgnoreCase);
	if (bHead && bHelmetIntact)
	{
		bHelmetIntact = false; return Health->ApplyDamage(BaseDamage * .5f, Shooter);
	}
	if (bHead && EnemyRole != EPFEnemyRole::Heavy && WeaponType != EPFWeaponType::Shotgun && Distance <= Definition.EffectiveRange)
	{
		// An eligible weak point is a separate lethal result, independent of co-op scaling.
		const float Previous = Health->Current; Health->Execute(); return Previous;
	}
	const float Falloff = FMath::GetMappedRangeValueClamped(FVector2D(Definition.EffectiveRange, Definition.EffectiveRange * 2.f), FVector2D(1.f, .4f), Distance);
	const bool bLimb = Bone.ToString().Contains(TEXT("arm")) || Bone.ToString().Contains(TEXT("leg"));
	return Health->ApplyDamage(BaseDamage * Falloff * (bHead ? 4.f : bLimb ? .7f : 1.f) * (EnemyRole == EPFEnemyRole::Heavy ? .65f : 1.f), Shooter);
}

float APFEnemyCharacter::TakeDamage(float Damage, const FDamageEvent& Event, AController* InstigatorController, AActor* Causer)
{
	return Health->ApplyDamage(Damage, InstigatorController ? InstigatorController->GetPawn() : Causer);
}

bool APFEnemyCharacter::ReserveExecution(APFPlayerCharacter* Attacker)
{
	if (!HasAuthority() || !IsValid(Attacker) || !Health->IsAlive() || ExecutionOwner.IsValid()) return false;
	ExecutionOwner = Attacker; return true;
}

void APFEnemyCharacter::ReleaseExecution(APFPlayerCharacter* Attacker)
{
	if (HasAuthority() && ExecutionOwner.Get() == Attacker) ExecutionOwner.Reset();
}

bool APFEnemyCharacter::CommitExecution(APFPlayerCharacter* Attacker, float PresentationTime)
{
	if (!HasAuthority() || ExecutionOwner.Get() != Attacker || !Health->IsAlive()) return false;
	RecordDamageContributor(Attacker);
	ExecutionPresentationTime = PresentationTime;
	const bool bCommitted = Health->Execute(); ExecutionOwner.Reset(); return bCommitted;
}

void APFEnemyCharacter::OnLifeChanged(EPFLifeState Previous, EPFLifeState Current)
{
	if (Current != EPFLifeState::Dead) return;
	Alert = EPFAlertState::Dead;
	PlayDeathPresentation();
	GetCharacterMovement()->DisableMovement(); GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (!HasAuthority() || bKillProcessed) return;
	bKillProcessed = true; KillId = FGuid::NewGuid(); ExecutionOwner.Reset();
	if (APFAIController* AI = Cast<APFAIController>(GetController())) { AI->Shutdown(); AI->UnPossess(); AI->Destroy(); }
	if (Outpost) Outpost->ResolveEnemy(ThreatId, this);
	for (const TWeakObjectPtr<APFPlayerCharacter>& Contributor : Contributors)
		if (Contributor.IsValid()) if (APFPlayerState* Player = Contributor->GetPlayerState<APFPlayerState>()) Player->AddCombatExperience(35);
	SetLifeSpan(45.f); ForceNetUpdate();
}

void APFEnemyCharacter::OnRep_ExecutionPresentation()
{
	if (Health->Life == EPFLifeState::Dead) PlayDeathPresentation();
}

void APFEnemyCharacter::PlayDeathPresentation()
{
	if (!DeathAnimation) return;
	GetWorldTimerManager().ClearTimer(ExecutionPoseTimer);
	GetMesh()->bPauseAnims = false; GetMesh()->PlayAnimation(DeathAnimation, false);
	if (ExecutionPresentationTime < 0.f) return;
	const AGameStateBase* State = GetWorld()->GetGameState();
	const float Now = State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const float Elapsed = FMath::Max(0.f, Now - ExecutionPresentationTime);
	const float Rate = DeathAnimation->GetPlayLength() / (FPFGameRules::TakedownSeconds - FPFGameRules::TakedownContactSeconds);
	GetMesh()->SetPlayRate(Rate);
	GetMesh()->SetPosition(FMath::Clamp((Elapsed - FPFGameRules::TakedownContactSeconds) * Rate, 0.f, DeathAnimation->GetPlayLength()), false);
	GetMesh()->bPauseAnims = Elapsed < FPFGameRules::TakedownContactSeconds || Elapsed >= FPFGameRules::TakedownSeconds;
	if (Elapsed < FPFGameRules::TakedownContactSeconds)
		GetWorldTimerManager().SetTimer(ExecutionPoseTimer, this, &ThisClass::PlayDeathPresentation, FPFGameRules::TakedownContactSeconds - Elapsed, false);
}

void APFEnemyCharacter::FireAt(APFPlayerCharacter* Target)
{
	if (!HasAuthority() || !Health->IsAlive() || IsExecutionReserved() || !IsValid(Target) || !Target->Health->IsAlive()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextShotTime) return;
	const int32 BurstSize = EnemyRole == EPFEnemyRole::Heavy ? 5 : 3;
	NextShotTime = Now + (++BurstShots >= BurstSize ? (EnemyRole == EPFEnemyRole::Sentry ? 1.3f : 1.f) : .18f);
	if (BurstShots >= BurstSize) BurstShots = 0;
	const FVector Origin = GetPawnViewLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PFEnemyFire), true, this);
	FHitResult Check;
	if (GetWorld()->LineTraceSingleByChannel(Check, Origin, Target->GetActorLocation(), ECC_Visibility, Params) && Check.GetActor() != Target) return;
	FRandomStream Random(GetUniqueID() ^ FMath::RoundToInt(Now * 100.f));
	const FVector Direction = Random.VRandCone((Target->GetActorLocation() - Origin).GetSafeNormal(), FMath::DegreesToRadians(3.5f));
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * 8000.f, ECC_Visibility, Params))
		if (APFPlayerCharacter* Victim = Cast<APFPlayerCharacter>(Hit.GetActor())) { Victim->MarkThreat(); Victim->Health->ApplyDamage(18.f * DamageMultiplier, this); }
	MulticastFireEffect();
}

void APFEnemyCharacter::MulticastFireEffect_Implementation() { if (GunshotSound) UGameplayStatics::PlaySoundAtLocation(this, GunshotSound, GetActorLocation(), .5f); }
