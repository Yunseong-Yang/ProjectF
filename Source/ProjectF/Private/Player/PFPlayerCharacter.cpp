#include "Player/PFPlayerCharacter.h"
#include "Player/PFTraversalComponent.h"
#include "Combat/PFHealthComponent.h"
#include "Combat/PFWeaponComponent.h"
#include "Combat/PFTakedownComponent.h"
#include "Combat/PFGrenade.h"
#include "Interaction/PFInteractionComponent.h"
#include "Progression/PFPlayerState.h"
#include "Framework/PFGameMode.h"
#include "Framework/PFPlayerController.h"
#include "UI/PFUserSettings.h"
#include "AI/PFEnemyCharacter.h"
#include "World/PFWorldStateSubsystem.h"
#include "GameFramework/GameStateBase.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Components/StaticMeshComponent.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Perception/AISense_Hearing.h"
#include "TimerManager.h"

APFPlayerCharacter::APFPlayerCharacter()
{
	bReplicates = true; PrimaryActorTick.bCanEverTick = false; SetNetUpdateFrequency(30.f);
	Tags.Add(TEXT("PF.Player"));
	GetCapsuleComponent()->InitCapsuleSize(34.f, 96.f); GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	GetMesh()->bCastHiddenShadow = true;
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;
	RemoteWeapon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RemoteWeapon")); RemoteWeapon->SetupAttachment(GetMesh());
	RemoteWeapon->SetOwnerNoSee(true); RemoteWeapon->SetCollisionEnabled(ECollisionEnabled::NoCollision); RemoteWeapon->bCastHiddenShadow = true;
	RemoteWeapon->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonMesh"));
	FirstPersonMesh->SetOnlyOwnerSee(true); FirstPersonMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson; FirstPersonMesh->SetCastShadow(false);
	GetCharacterMovement()->MaxWalkSpeed = 350.f; GetCharacterMovement()->MaxWalkSpeedCrouched = 180.f;
	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera")); Camera->SetupAttachment(GetRootComponent());
	Camera->SetRelativeLocation(FVector(0.f, 0.f, 64.f)); Camera->bUsePawnControlRotation = true;
	Camera->bEnableFirstPersonFieldOfView = true; Camera->bEnableFirstPersonScale = true; Camera->FirstPersonFieldOfView = 70.f; Camera->FirstPersonScale = .6f;
	FirstPersonMesh->SetupAttachment(GetMesh());
	ViewWeapon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ViewWeapon")); ViewWeapon->SetupAttachment(Camera);
	ViewWeapon->SetRelativeLocation(FVector(35.f, 14.f, -14.f)); ViewWeapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ViewWeapon->SetRelativeRotation(FRotator(0.f, -90.f, 0.f)); ViewWeapon->SetRelativeScale3D(FVector(.8f));
	ViewWeapon->SetOnlyOwnerSee(true); ViewWeapon->SetCastShadow(false);
	Health = CreateDefaultSubobject<UPFHealthComponent>(TEXT("Health")); Health->bCanBeDowned = true;
	Weapon = CreateDefaultSubobject<UPFWeaponComponent>(TEXT("Weapon"));
	Takedown = CreateDefaultSubobject<UPFTakedownComponent>(TEXT("Takedown"));
	Interaction = CreateDefaultSubobject<UPFInteractionComponent>(TEXT("Interaction"));
	Traversal = CreateDefaultSubobject<UPFTraversalComponent>(TEXT("Traversal"));
}

void APFPlayerCharacter::BeginPlay()
{
	Super::BeginPlay(); Health->OnLifeChanged.AddDynamic(this, &ThisClass::OnLifeChanged);
	if (GetMesh()->DoesSocketExist(TEXT("HandGrip_R"))) RemoteWeapon->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("HandGrip_R"));
	UpdateWeaponPresentation();
	if (FirstPersonMesh->GetSkeletalMeshAsset() && FirstPersonMesh->DoesSocketExist(TEXT("HandGrip_R")))
	{
		ViewWeapon->AttachToComponent(FirstPersonMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("HandGrip_R"));
		ViewWeapon->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator); ViewWeapon->SetRelativeScale3D(FVector(1.f));
		ViewWeapon->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	}
	if (HasAuthority())
	{
		Health->OnDamaged.AddWeakLambda(this, [this](float Amount, AActor* Source)
		{
			MarkThreat();
			LastDamageTime = GetWorld()->GetTimeSeconds(); DamageSource = IsValid(Source) ? Source->GetActorLocation() : GetActorLocation();
			if (ActionName == TEXT("Healing")) { GetWorldTimerManager().ClearTimer(HealingTimer); ClearAction(); }
			Interaction->Cancel();
		});
		GetWorldTimerManager().SetTimer(StatusTimer, this, &ThisClass::UpdateStatus, .2f, true);
		GetWorldTimerManager().SetTimer(FootstepTimer, this, &ThisClass::EmitFootstep, .1f, true);
	}
}

void APFPlayerCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
	CancelActions(); GetWorldTimerManager().ClearTimer(StatusTimer); GetWorldTimerManager().ClearTimer(FootstepTimer); GetWorldTimerManager().ClearTimer(RespawnTimer); GetWorldTimerManager().ClearTimer(ReconTimer);
	GetWorldTimerManager().ClearTimer(ViewTimer);
	Health->OnLifeChanged.RemoveDynamic(this, &ThisClass::OnLifeChanged);
	if (IsLocallyControlled()) if (APlayerController* PC = Cast<APlayerController>(Controller))
		if (PC->GetLocalPlayer()) if (auto* Subsystem = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) Subsystem->RemoveMappingContext(Mapping);
	Super::EndPlay(Reason);
}

void APFPlayerCharacter::PawnClientRestart()
{
	Super::PawnClientRestart(); InstallMapping();
	if (IsLocallyControlled()) UpdateWeaponPresentation();
}

void APFPlayerCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust); Camera->SetRelativeLocation(FVector(0.f, 0.f, 30.f));
}

void APFPlayerCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust); Camera->SetRelativeLocation(FVector(0.f, 0.f, 64.f));
}

void APFPlayerCharacter::InstallMapping()
{
	APlayerController* PC = Cast<APlayerController>(Controller); if (!PC || !PC->GetLocalPlayer() || !Mapping) return;
	if (const APFPlayerController* GameController = Cast<APFPlayerController>(PC)) if (GameController->bMenuOpen) return;
	if (auto* Subsystem = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) Subsystem->AddMappingContext(Mapping, 10);
}

void APFPlayerCharacter::SetLocalGameplayInputEnabled(bool bEnabled)
{
	if (!IsLocallyControlled()) return;
	if (!bEnabled) ReleaseLocalInput();
	if (APlayerController* PC = Cast<APlayerController>(Controller)) if (PC->GetLocalPlayer())
		if (auto* Subsystem = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) Subsystem->RemoveMappingContext(Mapping);
	if (bEnabled) InstallMapping();
}

void APFPlayerCharacter::RebuildLocalMapping()
{
	if (!IsLocallyControlled() || !InputComponent) return;
	ReleaseLocalInput();
	if (APlayerController* PC = Cast<APlayerController>(Controller)) if (PC->GetLocalPlayer())
		if (auto* Subsystem = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) Subsystem->RemoveMappingContext(Mapping);
	if (UEnhancedInputComponent* Enhanced = Cast<UEnhancedInputComponent>(InputComponent)) Enhanced->ClearActionEventBindings();
	SetupPlayerInputComponent(InputComponent);
}

void APFPlayerCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
	Super::SetupPlayerInputComponent(Input);
	UEnhancedInputComponent* Enhanced = Cast<UEnhancedInputComponent>(Input); if (!Enhanced) return;
	Mapping = NewObject<UInputMappingContext>(this); Actions.Reset();
	const UPFUserSettings* Settings = UPFUserSettings::Get();
	auto ReadKey = [Settings](FName Id, FKey Fallback) { return Settings ? Settings->GetBinding(Id) : Fallback; };
	auto Make = [this](EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(this); Action->ValueType = Type; Actions.Add(Action); return Action;
	};
	UInputAction* MoveAction = Make(EInputActionValueType::Axis2D);
	auto Axis = [this, MoveAction](FKey Key, bool bVertical, bool bNegative)
	{
		FEnhancedActionKeyMapping& Entry = Mapping->MapKey(MoveAction, Key);
		if (bNegative) Entry.Modifiers.Add(NewObject<UInputModifierNegate>(Mapping));
		if (bVertical) { auto* Swizzle = NewObject<UInputModifierSwizzleAxis>(Mapping); Swizzle->Order = EInputAxisSwizzle::YXZ; Entry.Modifiers.Add(Swizzle); }
	};
	Axis(ReadKey(TEXT("MoveForward"), EKeys::W), true, false); Axis(ReadKey(TEXT("MoveBack"), EKeys::S), true, true);
	Axis(ReadKey(TEXT("MoveLeft"), EKeys::A), false, true); Axis(ReadKey(TEXT("MoveRight"), EKeys::D), false, false);
	Mapping->MapKey(MoveAction, EKeys::Gamepad_Left2D);
	Enhanced->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ThisClass::Move);
	UInputAction* LookAction = Make(EInputActionValueType::Axis2D); Mapping->MapKey(LookAction, EKeys::Mouse2D);
	Enhanced->BindAction(LookAction, ETriggerEvent::Triggered, this, &ThisClass::Look);
	UInputAction* GamepadLook = Make(EInputActionValueType::Axis2D);
	Mapping->MapKey(GamepadLook, EKeys::Gamepad_Right2D).Modifiers.Add(NewObject<UInputModifierDeadZone>(Mapping));
	Enhanced->BindAction(GamepadLook, ETriggerEvent::Triggered, this, &ThisClass::LookGamepad);
	auto Button = [this, Enhanced, &Make](FKey Key, auto Function)
	{
		UInputAction* Action = Make(EInputActionValueType::Boolean); Mapping->MapKey(Action, Key);
		Enhanced->BindAction(Action, ETriggerEvent::Started, this, Function); return Action;
	};
	UInputAction* Fire = Button(Settings ? Settings->FireKey : EKeys::LeftMouseButton, &ThisClass::StartFire); Mapping->MapKey(Fire, EKeys::Gamepad_RightTrigger);
	Enhanced->BindAction(Fire, ETriggerEvent::Completed, this, &ThisClass::StopFire); Enhanced->BindAction(Fire, ETriggerEvent::Canceled, this, &ThisClass::StopFire);
	UInputAction* Aim = Button(ReadKey(TEXT("Aim"), EKeys::RightMouseButton), &ThisClass::AimStart); Mapping->MapKey(Aim, EKeys::Gamepad_LeftTrigger);
	Enhanced->BindAction(Aim, ETriggerEvent::Completed, this, &ThisClass::AimEnd);
	UInputAction* Sprint = Button(ReadKey(TEXT("Sprint"), EKeys::LeftShift), &ThisClass::SprintStart); Mapping->MapKey(Sprint, EKeys::Gamepad_LeftThumbstick);
	Enhanced->BindAction(Sprint, ETriggerEvent::Completed, this, &ThisClass::SprintEnd);
	UInputAction* Interact = Button(Settings ? Settings->InteractKey : EKeys::E, &ThisClass::Use); Mapping->MapKey(Interact, EKeys::Gamepad_DPad_Up);
	Enhanced->BindAction(Interact, ETriggerEvent::Completed, this, &ThisClass::StopUse); Enhanced->BindAction(Interact, ETriggerEvent::Canceled, this, &ThisClass::StopUse);
	Mapping->MapKey(Button(Settings ? Settings->ReloadKey : EKeys::R, &ThisClass::Reload), EKeys::Gamepad_FaceButton_Left);
	Mapping->MapKey(Button(Settings ? Settings->TakedownKey : EKeys::F, &ThisClass::ExecuteTakedown), EKeys::Gamepad_RightThumbstick);
	UInputAction* CrouchAction = Button(ReadKey(TEXT("Crouch"), EKeys::C), &ThisClass::ToggleCrouch); Mapping->MapKey(CrouchAction, EKeys::Gamepad_FaceButton_Right);
	Enhanced->BindAction(CrouchAction, ETriggerEvent::Completed, this, &ThisClass::EndCrouch);
	Mapping->MapKey(Button(ReadKey(TEXT("Heal"), EKeys::H), &ThisClass::HealInput), EKeys::Gamepad_DPad_Down);
	Mapping->MapKey(Button(ReadKey(TEXT("Grenade"), EKeys::G), &ThisClass::ThrowInput), EKeys::Gamepad_RightShoulder);
	Mapping->MapKey(Button(ReadKey(TEXT("SwapWeapon"), EKeys::Q), &ThisClass::SwapWeapon), EKeys::Gamepad_FaceButton_Top);
	UInputAction* ReconAction = Button(ReadKey(TEXT("Recon"), EKeys::B), &ThisClass::StartRecon);
	Mapping->MapKey(ReconAction, EKeys::Gamepad_LeftShoulder);
	Enhanced->BindAction(ReconAction, ETriggerEvent::Completed, this, &ThisClass::StopRecon);
	Enhanced->BindAction(ReconAction, ETriggerEvent::Canceled, this, &ThisClass::StopRecon);
	UInputAction* JumpAction = Button(ReadKey(TEXT("Jump"), EKeys::SpaceBar), &ThisClass::JumpInput); Mapping->MapKey(JumpAction, EKeys::Gamepad_FaceButton_Bottom);
	Enhanced->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	InstallMapping();
}

void APFPlayerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APFPlayerCharacter, bSprinting); DOREPLIFETIME(APFPlayerCharacter, bAiming); DOREPLIFETIME(APFPlayerCharacter, bProfileReady);
	DOREPLIFETIME(APFPlayerCharacter, ActionEndTime); DOREPLIFETIME(APFPlayerCharacter, ActionName); DOREPLIFETIME(APFPlayerCharacter, ActionStartTime); DOREPLIFETIME(APFPlayerCharacter, ActionSerial);
	DOREPLIFETIME(APFPlayerCharacter, bInCombat); DOREPLIFETIME(APFPlayerCharacter, LastThreatTime);
	DOREPLIFETIME_CONDITION(APFPlayerCharacter, WeaponReadyTime, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(APFPlayerCharacter, Medkits, COND_OwnerOnly); DOREPLIFETIME_CONDITION(APFPlayerCharacter, Grenades, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(APFPlayerCharacter, DetectionLevel, COND_OwnerOnly); DOREPLIFETIME_CONDITION(APFPlayerCharacter, DetectionSource, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(APFPlayerCharacter, LastDamageTime, COND_OwnerOnly); DOREPLIFETIME_CONDITION(APFPlayerCharacter, DamageSource, COND_OwnerOnly);
}

bool APFPlayerCharacter::CanAct() const { return bProfileReady && Health->IsAlive() && ActionName.IsNone(); }
bool APFPlayerCharacter::HasSkill(FName Id) const { const APFPlayerState* State = GetPlayerState<APFPlayerState>(); return State && State->HasSkill(Id); }
bool APFPlayerCharacter::IsSafe() const
{
	const AGameStateBase* State = GetWorld()->GetGameState();
	const float Now = State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	return Health->IsAlive() && !bInCombat && Now - LastThreatTime > 35.f
		&& GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->IsSafeLocation(GetActorLocation());
}
FVector APFPlayerCharacter::GetAimOrigin() const { return GetActorLocation() + FVector(0.f, 0.f, bIsCrouched ? 30.f : 64.f); }
FVector APFPlayerCharacter::GetAimDirection() const { return GetControlRotation().Vector(); }

void APFPlayerCharacter::Move(const FInputActionValue& Value)
{
	if (!bProfileReady || !Health->IsAlive() || ActionName == TEXT("Takedown") || ActionName == TEXT("Vault")) return;
	const FVector2D Axis = Value.Get<FVector2D>(); const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	AddMovementInput(Yaw.Vector(), Axis.Y); AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Axis.X);
}

void APFPlayerCharacter::Look(const FInputActionValue& Value)
{
	if (Takedown->IsPresenting()) return;
	const FVector2D Axis = Value.Get<FVector2D>(); const UPFUserSettings* Settings = UPFUserSettings::Get();
	const float Sensitivity = Settings ? (WantsViewAim() ? Settings->AimSensitivity : Settings->LookSensitivity) : 1.f;
	AddControllerYawInput(Axis.X * Sensitivity); AddControllerPitchInput(-Axis.Y * Sensitivity);
	if (Settings && !FMath::IsNearlyEqual(Camera->FieldOfView, Settings->FieldOfView * (WantsViewAim() ? .8f : 1.f), .05f)) RefreshLocalView();
}

void APFPlayerCharacter::StartFire()
{
	if (const APFPlayerController* PC = Cast<APFPlayerController>(Controller)) if (PC->bMenuOpen) return;
	const uint32 Request = ++LocalSequence; Weapon->BeginLocalShotEffects(Request); Weapon->ServerSetTrigger(true, Request);
}

void APFPlayerCharacter::LookGamepad(const FInputActionValue& Value)
{
	if (Takedown->IsPresenting()) return;
	const FVector2D Axis = Value.Get<FVector2D>(); const UPFUserSettings* Settings = UPFUserSettings::Get();
	const float Rate = 120.f * GetWorld()->GetDeltaSeconds() * (Settings ? (WantsViewAim() ? Settings->AimSensitivity : Settings->LookSensitivity) : 1.f);
	AddControllerYawInput(Axis.X * Rate); AddControllerPitchInput(-Axis.Y * Rate);
}
void APFPlayerCharacter::StopFire() { Weapon->StopLocalShotEffects(); Weapon->ServerSetTrigger(false, ++LocalSequence); }
void APFPlayerCharacter::ReleaseLocalInput() { StopFire(); StopUse(); StopRecon(); PredictViewAim(false); ServerSetMovementState(false, false); }
void APFPlayerCharacter::Reload() { Weapon->StopLocalShotEffects(); Weapon->ServerReload(++LocalSequence); }
void APFPlayerCharacter::Use() { Interaction->ServerBegin(Interaction->FindTarget(), ++LocalSequence); }
void APFPlayerCharacter::StopUse() { Interaction->ServerCancel(++LocalSequence); }
void APFPlayerCharacter::ExecuteTakedown() { if (APFEnemyCharacter* Enemy = Takedown->FindCandidate()) Takedown->ServerRequest(Enemy, Takedown->ChooseType(Enemy), ++LocalSequence); }
void APFPlayerCharacter::HealInput() { ServerHeal(++LocalSequence); }
void APFPlayerCharacter::ThrowInput()
{
	if (APFEnemyCharacter* Target = Takedown->FindThrowCandidate()) Takedown->ServerRequest(Target, EPFTakedownType::Throw, ++LocalSequence);
	else ServerThrowGrenade(++LocalSequence);
}
void APFPlayerCharacter::SwapWeapon()
{
	Weapon->StopLocalShotEffects();
	const APFPlayerState* State = GetPlayerState<APFPlayerState>();
	Weapon->ServerEquip(Weapon->State.Type == EPFWeaponType::Pistol && State ? State->Profile.PrimaryWeapon : EPFWeaponType::Pistol, ++LocalSequence);
}
void APFPlayerCharacter::JumpInput()
{
	if (!CanAct()) return;
	float Distance = 0.f, Height = 0.f;
	if (Traversal->FindVault(Distance, Height)) Traversal->ServerVault(++LocalSequence); else Jump();
}
void APFPlayerCharacter::ToggleCrouch() { if (!CanAct()) return; if (bIsCrouched) UnCrouch(); else { ServerSetMovementState(false, bAiming); Crouch(); } }
void APFPlayerCharacter::EndCrouch() { const auto* S = UPFUserSettings::Get(); if (S && !S->bToggleCrouch) UnCrouch(); }
void APFPlayerCharacter::SprintStart() { const auto* S = UPFUserSettings::Get(); ServerSetMovementState(S && S->bToggleSprint ? !bSprinting : true, false); }
void APFPlayerCharacter::SprintEnd() { const auto* S = UPFUserSettings::Get(); if (!S || !S->bToggleSprint) ServerSetMovementState(false, bAiming); }
void APFPlayerCharacter::AimStart()
{
	if (!CanAct() || Takedown->IsPresenting()) return;
	const auto* S = UPFUserSettings::Get();
	const bool bRequested = bHasViewAimPrediction && GetWorld()->GetTimeSeconds() < ViewAimPredictionDeadline ? bPredictedViewAim : bAiming;
	const bool bAim = S && S->bToggleAim ? !bRequested : true;
	PredictViewAim(bAim); ServerSetMovementState(false, bAim);
}
void APFPlayerCharacter::AimEnd() { const auto* S = UPFUserSettings::Get(); if (!S || !S->bToggleAim) { PredictViewAim(false); ServerSetMovementState(bSprinting, false); } }

void APFPlayerCharacter::Recon()
{
	FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(PFReconInput), false, this);
	GetWorld()->LineTraceSingleByChannel(Hit, GetAimOrigin(), GetAimOrigin() + GetAimDirection() * 10000.f, ECC_Visibility, Params);
	Interaction->ServerObserve(Hit.GetActor(), ++LocalSequence);
}

void APFPlayerCharacter::StartRecon() { Recon(); GetWorldTimerManager().SetTimer(ReconTimer, this, &ThisClass::Recon, .2f, true); }
void APFPlayerCharacter::StopRecon() { GetWorldTimerManager().ClearTimer(ReconTimer); }

void APFPlayerCharacter::ServerSetMovementState_Implementation(bool Sprint, bool Aim)
{
	if (!Health->IsAlive() || !bProfileReady) return;
	if (ActionName == TEXT("Takedown")) return;
	const bool bWasSprinting = bSprinting;
	bSprinting = Sprint && CanAct() && !bIsCrouched; bAiming = Aim && !bSprinting;
	if (bWasSprinting && !bSprinting) WeaponReadyTime = FMath::Max(WeaponReadyTime, GetWorld()->GetTimeSeconds() + .25f);
	if (bSprinting) Weapon->Cancel(); OnRep_MovementState(); ForceNetUpdate();
}

void APFPlayerCharacter::OnRep_MovementState()
{
	GetCharacterMovement()->MaxWalkSpeed = bSprinting ? 600.f : bAiming ? 220.f : 350.f;
	if (IsLocallyControlled())
	{
		if (bHasViewAimPrediction && bAiming == bPredictedViewAim) bHasViewAimPrediction = false;
		RefreshLocalView();
	}
}

bool APFPlayerCharacter::WantsViewAim() const
{
	const bool bRequested = bHasViewAimPrediction && GetWorld()->GetTimeSeconds() < ViewAimPredictionDeadline ? bPredictedViewAim : bAiming;
	return bRequested && CanAct() && !bSprinting && !Weapon->State.bReloading && !bExecutionViewActive;
}

void APFPlayerCharacter::PredictViewAim(bool bAim)
{
	if (!IsLocallyControlled() || HasAuthority()) return;
	bHasViewAimPrediction = true; bPredictedViewAim = bAim; ViewAimPredictionDeadline = GetWorld()->GetTimeSeconds() + .75f;
	RefreshLocalView();
}

FVector APFPlayerCharacter::GetViewSightLocation() const
{
	return ViewWeapon->GetComponentTransform().TransformPosition(Weapon->EquippedType == EPFWeaponType::Pistol ? PistolSightPoint : RifleSightPoint);
}

void APFPlayerCharacter::RefreshLocalView()
{
	if (!IsLocallyControlled() || IsActorBeingDestroyed() || bExecutionViewActive || GetWorldTimerManager().IsTimerActive(ViewTimer)) return;
	LastViewUpdateTime = GetWorld()->GetTimeSeconds();
	FTimerManagerTimerParameters Params; Params.bLoop = true; Params.bMaxOncePerFrame = true;
	GetWorldTimerManager().SetTimer(ViewTimer, this, &ThisClass::UpdateLocalView, 1.f / 60.f, Params);
}

void APFPlayerCharacter::UpdateLocalView()
{
	if (!IsLocallyControlled() || IsActorBeingDestroyed() || bExecutionViewActive)
	{ GetWorldTimerManager().ClearTimer(ViewTimer); return; }
	const float Now = GetWorld()->GetTimeSeconds(), Delta = FMath::Clamp(Now - LastViewUpdateTime, 0.f, .1f); LastViewUpdateTime = Now;
	if (bHasViewAimPrediction && Now >= ViewAimPredictionDeadline) bHasViewAimPrediction = false;
	const bool bViewAim = WantsViewAim();
	const APFPlayerCharacter* Defaults = GetClass()->GetDefaultObject<APFPlayerCharacter>();
	const FVector Base = Defaults->FirstPersonMesh->GetRelativeLocation();
	FVector Target = Base;
	if (bViewAim && ViewWeapon->GetStaticMesh() && FirstPersonMesh->GetAttachParent())
	{
		const FVector Desired = Camera->GetComponentLocation() + Camera->GetForwardVector() * SightEyeDistance;
		const FVector Correction = FirstPersonMesh->GetAttachParent()->GetComponentTransform().InverseTransformVectorNoScale(Desired - GetViewSightLocation());
		Target = Base + (FirstPersonMesh->GetRelativeLocation() + Correction - Base).GetClampedToMaxSize(110.f);
	}
	FirstPersonMesh->SetRelativeLocation(FMath::VInterpTo(FirstPersonMesh->GetRelativeLocation(), Target, Delta, 18.f));
	const UPFUserSettings* Settings = UPFUserSettings::Get();
	const float TargetFOV = (Settings ? Settings->FieldOfView : 90.f) * (bViewAim ? .8f : 1.f);
	Camera->SetFieldOfView(FMath::FInterpTo(Camera->FieldOfView, TargetFOV, Delta, 18.f));
	if (!bViewAim && !bAiming && !bHasViewAimPrediction && FirstPersonMesh->GetRelativeLocation().Equals(Base, .01f) && FMath::IsNearlyEqual(Camera->FieldOfView, TargetFOV, .01f))
	{
		FirstPersonMesh->SetRelativeLocation(Base); Camera->SetFieldOfView(TargetFOV); GetWorldTimerManager().ClearTimer(ViewTimer);
	}
}

void APFPlayerCharacter::ClearWeaponStance()
{
	if (!HasAuthority()) return;
	bAiming = false; bSprinting = false; OnRep_MovementState(); ForceNetUpdate();
}

void APFPlayerCharacter::SetAction(FName Name, float Duration)
{
	if (!HasAuthority()) return;
	ActionName = Name; ActionStartTime = GetWorld()->GetTimeSeconds(); ActionEndTime = ActionStartTime + Duration; ++ActionSerial; ForceNetUpdate();
}

void APFPlayerCharacter::ClearAction() { if (HasAuthority()) { ActionName = NAME_None; ActionStartTime = 0.f; ActionEndTime = 0.f; ForceNetUpdate(); } }
void APFPlayerCharacter::MarkThreat(float ObservedTime)
{
	if (!HasAuthority()) return;
	const float Now = GetWorld()->GetTimeSeconds();
	LastThreatTime = FMath::Max(LastThreatTime, ObservedTime < 0.f ? Now : FMath::Min(ObservedTime, Now));
	bInCombat = Now - LastThreatTime <= 35.f;
}

void APFPlayerCharacter::CancelActions()
{
	GetWorldTimerManager().ClearTimer(HealingTimer); Weapon->Cancel(); Interaction->Cancel(); Takedown->Cancel(); Traversal->Cancel(); ClearAction();
}

void APFPlayerCharacter::ServerHeal_Implementation(uint32 Sequence)
{
	if (!FPFGameRules::IsNewSequence(Sequence, LastConsumableSequence)) return; LastConsumableSequence = Sequence;
	if (!CanAct() || Medkits <= 0 || Health->Current >= Health->Maximum) return;
	Weapon->Cancel(); HealingRevision = Health->DamageRevision;
	const float Duration = HasSkill(TEXT("Healing")) ? 2.f : 2.5f; SetAction(TEXT("Healing"), Duration);
	GetWorldTimerManager().SetTimer(HealingTimer, this, &ThisClass::FinishHeal, Duration);
}

void APFPlayerCharacter::FinishHeal()
{
	if (ActionName == TEXT("Healing") && Health->DamageRevision == HealingRevision && Medkits > 0 && Health->Heal(60.f)) --Medkits;
	ClearAction();
}

void APFPlayerCharacter::ServerThrowGrenade_Implementation(uint32 Sequence)
{
	if (!FPFGameRules::IsNewSequence(Sequence, LastConsumableSequence)) return; LastConsumableSequence = Sequence;
	if (!CanAct() || Grenades <= 0) return;
	const FVector Origin = GetAimOrigin(); FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(PFGrenadeStart), false, this);
	if (GetWorld()->SweepSingleByChannel(Hit, Origin, Origin + GetAimDirection() * 70.f, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(10.f), Params)) return;
	FActorSpawnParameters Spawn; Spawn.Owner = this; Spawn.Instigator = this;
	if (APFGrenade* Grenade = GetWorld()->SpawnActor<APFGrenade>(APFGrenade::StaticClass(), Origin + GetAimDirection() * 70.f, GetControlRotation(), Spawn))
	{
		Grenade->Movement->Velocity = GetAimDirection() * 1400.f; --Grenades; WeaponReadyTime = GetWorld()->GetTimeSeconds() + .5f; ForceNetUpdate();
	}
}

float APFPlayerCharacter::TakeDamage(float Damage, const FDamageEvent& Event, AController* InstigatorController, AActor* Causer)
{
	return Health->ApplyDamage(Damage, InstigatorController ? InstigatorController->GetPawn() : Causer);
}

void APFPlayerCharacter::OnLifeChanged(EPFLifeState Previous, EPFLifeState Current)
{
	if (Current != EPFLifeState::Alive)
	{
		CancelActions(); GetCharacterMovement()->DisableMovement();
		if (HasAuthority()) { bSprinting = false; bAiming = false; ForceNetUpdate(); }
	}
	else { GetCharacterMovement()->SetMovementMode(MOVE_Walking); OnRep_MovementState(); }
	FirstPersonMesh->SetVisibility(Current == EPFLifeState::Alive); ViewWeapon->SetVisibility(Current == EPFLifeState::Alive); RemoteWeapon->SetVisibility(Current == EPFLifeState::Alive);
	Camera->SetRelativeLocation(FVector(0, 0, Current == EPFLifeState::Alive ? (bIsCrouched ? 30.f : 64.f) : -30.f));
	if (Current != EPFLifeState::Alive && DeathAnimation) GetMesh()->PlayAnimation(DeathAnimation, false);
	else if (Current == EPFLifeState::Alive) UpdateWeaponPresentation();
	if (HasAuthority() && Current == EPFLifeState::Dead) GetWorldTimerManager().SetTimer(RespawnTimer, this, &ThisClass::RequestRespawn, 15.f);
}

void APFPlayerCharacter::RequestRespawn()
{
	if (APFGameMode* Mode = GetWorld()->GetAuthGameMode<APFGameMode>()) if (APFPlayerController* PC = Cast<APFPlayerController>(Controller)) Mode->Respawn(PC);
}

void APFPlayerCharacter::ReportDetection(const FVector& Source, float Awareness)
{
	if (!HasAuthority()) return;
	if (Awareness > .05f && GetWorld()->GetTimeSeconds() - LastDetectionCueTime > 8.f) { LastDetectionCueTime = GetWorld()->GetTimeSeconds(); ClientDetectionCue(); }
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - DetectionWindowStart >= .19f) { DetectionWindowStart = Now; DetectionLevel = Awareness; DetectionSource = Source; }
	else if (Awareness >= DetectionLevel) { DetectionLevel = Awareness; DetectionSource = Source; }
	LastDetectionTime = GetWorld()->GetTimeSeconds();
}

void APFPlayerCharacter::UpdateStatus()
{
	const float Now = GetWorld()->GetTimeSeconds(); bInCombat = Now - LastThreatTime <= 35.f;
	if (APFPlayerState* State = GetPlayerState<APFPlayerState>())
	{
		State->bJoinedWorld = bProfileReady; State->TeamLife = Health->Life; State->TeamLocation = GetActorLocation();
		State->TeamHealth = FMath::Clamp(FMath::RoundToInt(Health->Current), 0, 100);
	}
	if (Now - LastDetectionTime > .4f) DetectionLevel = FMath::Max(0.f, DetectionLevel - .2f);
}
void APFPlayerCharacter::EmitFootstep()
{
	if (Health->IsAlive() && GetCharacterMovement()->IsMovingOnGround() && GetVelocity().SizeSquared2D() > 2500.f && GetWorld()->GetTimeSeconds() >= NextFootstepTime)
	{
		NextFootstepTime = GetWorld()->GetTimeSeconds() + (bSprinting ? .32f : bIsCrouched ? .65f : .48f);
		UAISense_Hearing::ReportNoiseEvent(GetWorld(), GetActorLocation(), 1.f, this, bIsCrouched ? 300.f : bSprinting ? 1800.f : 800.f, TEXT("Footstep"));
		MulticastFootstep();
	}
}

void APFPlayerCharacter::MulticastFootstep_Implementation() { if (FootstepSound) UGameplayStatics::PlaySoundAtLocation(this, FootstepSound, GetActorLocation(), bIsCrouched ? .12f : .4f); }
void APFPlayerCharacter::ClientDetectionCue_Implementation() { if (DetectionSound) UGameplayStatics::PlaySound2D(this, DetectionSound, .4f); }

void APFPlayerCharacter::PlayShotPresentation(FVector Impact, bool bQuiet)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	USoundBase* Sound = bQuiet && SuppressedSound ? SuppressedSound.Get() : FireSound.Get();
	if (Sound) UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation(), bQuiet ? .3f : .65f);
	UAnimSequence* Fire = Weapon->EquippedType == EPFWeaponType::Pistol ? PistolFireAnimation.Get() : RifleFireAnimation.Get();
	if (Fire) for (USkeletalMeshComponent* Part : {GetMesh(), FirstPersonMesh.Get()})
		if (UAnimInstance* Anim = Part->GetAnimInstance())
			Anim->PlaySlotAnimationAsDynamicMontage(Fire, Part == FirstPersonMesh && !bExecutionViewActive ? TEXT("Arms") : TEXT("DefaultSlot"), .02f, .05f);
}

void APFPlayerCharacter::UpdateWeaponPresentation()
{
	UStaticMesh* WeaponMesh = Weapon->EquippedType == EPFWeaponType::Pistol ? PistolMesh.Get() : RifleMesh.Get();
	if (WeaponMesh) { ViewWeapon->SetStaticMesh(WeaponMesh); RemoteWeapon->SetStaticMesh(WeaponMesh); }
	const bool bPistol = Weapon->EquippedType == EPFWeaponType::Pistol;
	const TSubclassOf<UAnimInstance> Body = bPistol ? PistolBodyAnimation : RifleBodyAnimation;
	const TSubclassOf<UAnimInstance> View = bPistol ? PistolViewAnimation : RifleViewAnimation;
	if (Body && (GetMesh()->GetAnimationMode() != EAnimationMode::AnimationBlueprint || GetMesh()->GetAnimClass() != Body))
	{
		GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint); GetMesh()->SetAnimInstanceClass(Body);
	}
	if (IsLocallyControlled() && !bExecutionViewActive && View && FirstPersonMesh->GetAnimClass() != View) FirstPersonMesh->SetAnimInstanceClass(View);
	RefreshLocalView();
}

bool APFPlayerCharacter::HasRearExecutionMotion() const
{
	return Weapon->EquippedType == EPFWeaponType::Pistol ? PistolRearExecution != nullptr : RifleRearExecution != nullptr;
}

void APFPlayerCharacter::PlayRearExecution(float Elapsed)
{
	StopRearExecution(); Weapon->StopLocalShotEffects();
	UAnimSequence* Motion = Weapon->EquippedType == EPFWeaponType::Pistol ? PistolRearExecution.Get() : RifleRearExecution.Get();
	if (!Motion || Elapsed >= FPFGameRules::TakedownSeconds || !Health->IsAlive()) return;
	if (Elapsed < .15f) PlayExecutionSound(false);
	if (IsLocallyControlled())
	{
		bExecutionViewActive = true; PreviousExecutionViewFOV = Camera->FirstPersonFieldOfView;
		bHasViewAimPrediction = false; GetWorldTimerManager().ClearTimer(ViewTimer);
		PreviousExecutionViewLocation = FirstPersonMesh->GetRelativeLocation();
		FirstPersonMesh->SetRelativeLocation(FVector::ZeroVector);
		Camera->FirstPersonFieldOfView = Camera->FieldOfView;
		FirstPersonMesh->SetAnimInstanceClass(Weapon->EquippedType == EPFWeaponType::Pistol ? PistolBodyAnimation : RifleBodyAnimation);
		FirstPersonMesh->HideBoneByName(TEXT("neck_01"), PBO_None);
	}
	auto Play = [&](USkeletalMeshComponent* Part, TObjectPtr<UAnimMontage>& Montage)
	{
		if (UAnimInstance* Anim = Part->GetAnimInstance())
		{
			const float Rate = Motion->GetPlayLength() / FPFGameRules::TakedownSeconds;
			Montage = Anim->PlaySlotAnimationAsDynamicMontage(Motion, TEXT("DefaultSlot"), .04f, .1f, Rate);
			if (Montage) Anim->Montage_SetPosition(Montage, FMath::Clamp(Elapsed * Rate, 0.f, Motion->GetPlayLength()));
		}
	};
	Play(GetMesh(), ExecutionMontage); if (IsLocallyControlled()) Play(FirstPersonMesh, ViewExecutionMontage);
}

void APFPlayerCharacter::PlayExecutionSound(bool bImpact)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	USoundBase* Sound = bImpact ? ExecutionImpactSound.Get() : ExecutionWhooshSound.Get();
	const FVector Location = bImpact && Takedown->PresentationTarget ? Takedown->PresentationTarget->GetActorLocation() : GetActorLocation();
	if (Sound) UGameplayStatics::PlaySoundAtLocation(this, Sound, Location, bImpact ? .55f : .35f);
}

void APFPlayerCharacter::StopRearExecution()
{
	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance()) if (ExecutionMontage) Anim->Montage_Stop(.08f, ExecutionMontage);
	if (UAnimInstance* Anim = FirstPersonMesh->GetAnimInstance()) if (ViewExecutionMontage) Anim->Montage_Stop(.08f, ViewExecutionMontage);
	ExecutionMontage = nullptr; ViewExecutionMontage = nullptr;
	if (bExecutionViewActive)
	{
		bExecutionViewActive = false; FirstPersonMesh->UnHideBoneByName(TEXT("neck_01")); Camera->FirstPersonFieldOfView = PreviousExecutionViewFOV;
		FirstPersonMesh->SetRelativeLocation(PreviousExecutionViewLocation);
		if (!IsActorBeingDestroyed() && Health->IsAlive()) UpdateWeaponPresentation();
	}
}

void APFPlayerCharacter::UpdateReloadPresentation(float EndTime, float Duration)
{
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const float Now = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	UAnimSequence* Reload = Weapon->EquippedType == EPFWeaponType::Pistol ? PistolReloadAnimation.Get() : RifleReloadAnimation.Get();
	auto Update = [&](USkeletalMeshComponent* Part, TObjectPtr<UAnimMontage>& Montage)
	{
		UAnimInstance* Anim = Part->GetAnimInstance(); if (!Anim) return;
		if (Montage) Anim->Montage_Stop(.08f, Montage); Montage = nullptr;
		if (!Reload || EndTime <= Now || Duration <= 0.f) return;
		Montage = Anim->PlaySlotAnimationAsDynamicMontage(Reload, Part == FirstPersonMesh && !bExecutionViewActive ? TEXT("Arms") : TEXT("DefaultSlot"), .06f, .1f, Reload->GetPlayLength() / Duration);
		if (Montage) Anim->Montage_SetPosition(Montage, FMath::Clamp(1.f - (EndTime - Now) / Duration, 0.f, 1.f) * Reload->GetPlayLength());
	};
	Update(GetMesh(), ReloadMontage); Update(FirstPersonMesh, ViewReloadMontage);
	RefreshLocalView();
}
