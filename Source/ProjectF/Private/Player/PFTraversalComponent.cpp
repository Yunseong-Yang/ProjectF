#include "Player/PFTraversalComponent.h"
#include "Player/PFPlayerCharacter.h"
#include "Combat/PFHealthComponent.h"
#include "Combat/PFWeaponComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UPFTraversalComponent::UPFTraversalComponent() { PrimaryComponentTick.bCanEverTick = false; SetIsReplicatedByDefault(true); }
void UPFTraversalComponent::EndPlay(const EEndPlayReason::Type Reason) { Cancel(); Super::EndPlay(Reason); }
void UPFTraversalComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UPFTraversalComponent, bVaulting);
}

bool UPFTraversalComponent::FindVault(float& Distance, float& Height, FName* Failure) const
{
	auto Fail = [Failure](FName Reason) { if (Failure) *Failure = Reason; return false; };
	const APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(GetOwner());
	if (!Player || !Player->CanAct() || Player->bIsCrouched || !Player->GetCharacterMovement()->IsMovingOnGround()) return Fail(TEXT("MovementState"));
	const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
	const FVector Start = Player->GetActorLocation(), Feet = Start - FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight());
	const FVector Forward = FRotator(0, Player->GetControlRotation().Yaw, 0).Vector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PFVault), false, Player);
	FHitResult Obstacle, Top, Landing;
	if (!GetWorld()->LineTraceSingleByChannel(Obstacle, Feet + FVector(0, 0, 45), Feet + FVector(0, 0, 45) + Forward * 115.f, ECC_Visibility, Params)) return Fail(TEXT("NoObstacle"));
	if (!Obstacle.GetComponent() || Obstacle.GetComponent()->Mobility != EComponentMobility::Static) return Fail(TEXT("MovingObstacle"));
	const FVector Probe = Feet + Forward * (Obstacle.Distance + 15.f);
	if (!GetWorld()->LineTraceSingleByChannel(Top, Probe + FVector(0, 0, 150), Probe + FVector(0, 0, 25), ECC_Visibility, Params) || Top.ImpactNormal.Z < .7f) return Fail(TEXT("ObstacleTop"));
	const float ObstacleHeight = Top.ImpactPoint.Z - Feet.Z;
	if (ObstacleHeight < 30.f || ObstacleHeight > 115.f) return Fail(TEXT("ObstacleHeight"));
	Distance = 240.f; Height = ObstacleHeight * 1.8f + 30.f;
	const FVector EndFeet = Feet + Forward * Distance;
	if (!GetWorld()->LineTraceSingleByChannel(Landing, EndFeet + FVector(0, 0, 140), EndFeet - FVector(0, 0, 50), ECC_Visibility, Params)
		|| Landing.ImpactNormal.Z < .7f || FMath::Abs(Landing.ImpactPoint.Z - Feet.Z) > 10.f) return Fail(TEXT("UnsafeLanding"));
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight() - 2.f);
	FVector Previous = Start;
	for (int32 Step = 1; Step <= 20; ++Step)
	{
		const float T = Step / 20.f;
		const FVector Point = Start + Forward * (Distance * T) + FVector(0, 0, 4.f * Height * T * (1.f - T));
		FHitResult Hit;
		if (GetWorld()->SweepSingleByChannel(Hit, Previous, Point, FQuat::Identity, ECC_Pawn, Shape, Params)) return Fail(TEXT("BlockedArc"));
		Previous = Point;
	}
	return true;
}

void UPFTraversalComponent::ServerVault_Implementation(uint32 Sequence)
{
	if (!FPFGameRules::IsNewSequence(Sequence, LastSequence)) return; LastSequence = Sequence;
	APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(GetOwner()); float Distance = 0.f, Height = 0.f;
	if (!Player || !FindVault(Distance, Height)) return;
	Player->Weapon->Cancel(); Player->ServerSetMovementState(false, false); Player->SetAction(TEXT("Vault"), .8f);
	TSharedPtr<FRootMotionSource_JumpForce> Motion = MakeShared<FRootMotionSource_JumpForce>();
	Motion->InstanceName = TEXT("PFVault"); Motion->AccumulateMode = ERootMotionAccumulateMode::Override; Motion->Priority = 700;
	Motion->Rotation = FRotator(0, Player->GetControlRotation().Yaw, 0); Motion->Distance = Distance; Motion->Height = Height; Motion->Duration = .8f;
	Motion->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity; Motion->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	MotionId = Player->GetCharacterMovement()->ApplyRootMotionSource(Motion);
	bVaulting = true; GetOwner()->ForceNetUpdate();
	GetWorld()->GetTimerManager().SetTimer(FinishTimer, this, &ThisClass::Cancel, .8f);
}

void UPFTraversalComponent::OnRep_Vaulting()
{
	if (APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(GetOwner()))
		if (Player->Health->IsAlive()) Player->GetCharacterMovement()->SetMovementMode(bVaulting ? MOVE_Flying : MOVE_Falling);
}

void UPFTraversalComponent::Cancel()
{
	GetWorld()->GetTimerManager().ClearTimer(FinishTimer);
	if (!GetOwner()->HasAuthority()) return;
	if (APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(GetOwner()))
	{
		if (MotionId) Player->GetCharacterMovement()->RemoveRootMotionSourceByID(MotionId);
		MotionId = 0;
		if (bVaulting) { bVaulting = false; OnRep_Vaulting(); Player->ForceNetUpdate(); }
		if (Player->ActionName == TEXT("Vault")) Player->ClearAction();
	}
}
