#include "Combat/PFWeaponComponent.h"
#include "Combat/PFHealthComponent.h"
#include "Player/PFPlayerCharacter.h"
#include "AI/PFEnemyCharacter.h"
#include "World/PFInteractable.h"
#include "Progression/PFPlayerState.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Perception/AISense_Hearing.h"
#include "TimerManager.h"
#include "UI/PFUserSettings.h"
#include "GameFramework/GameStateBase.h"

UPFWeaponComponent::UPFWeaponComponent() { PrimaryComponentTick.bCanEverTick = false; SetIsReplicatedByDefault(true); }

void UPFWeaponComponent::BeginPlay()
{
	Super::BeginPlay(); Character = Cast<APFPlayerCharacter>(GetOwner());
	for (EPFWeaponType Type : {EPFWeaponType::Rifle, EPFWeaponType::Pistol, EPFWeaponType::Shotgun})
	{
		const FPFWeaponDefinition Definition = FPFGameRules::Weapon(Type);
		FPFWeaponState Entry; Entry.Type = Type; Entry.Magazine = Definition.Magazine; Entry.Reserve = Definition.Reserve; StoredWeapons.Add(Type, Entry);
	}
}

void UPFWeaponComponent::EndPlay(const EEndPlayReason::Type Reason) { Cancel(); Super::EndPlay(Reason); }

void UPFWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UPFWeaponComponent, State, COND_OwnerOnly); DOREPLIFETIME(UPFWeaponComponent, EquippedType);
	DOREPLIFETIME(UPFWeaponComponent, PresentationReloadDuration); DOREPLIFETIME(UPFWeaponComponent, PresentationReloadEnd);
}

void UPFWeaponComponent::ServerSetTrigger_Implementation(bool Pressed, uint32 Sequence)
{
	if (!Character.IsValid() || !FPFGameRules::IsNewSequence(Sequence, LastSequence)) return;
	LastSequence = Sequence;
	if (!Pressed) { bTriggerHeld = false; GetWorld()->GetTimerManager().ClearTimer(FireTimer); return; }
	if (!Character->CanAct() || Character->bSprinting || GetWorld()->GetTimeSeconds() < Character->WeaponReadyTime || bTriggerHeld) return;
	if (State.bReloading && State.Type == EPFWeaponType::Shotgun && State.Magazine > 0) Cancel();
	if (State.bReloading) return;
	ActiveTriggerRequest = Sequence; TriggerShotOrdinal = 0;
	const float Now = GetWorld()->GetTimeSeconds(), Interval = FPFGameRules::Weapon(State.Type).Interval;
	if (Now + .001f >= LastShotTime + Interval) LastShotTime = Now - Interval;
	bTriggerHeld = true; Fire();
	if (State.Type == EPFWeaponType::Rifle && bTriggerHeld)
		GetWorld()->GetTimerManager().SetTimer(FireTimer, this, &ThisClass::Fire, FPFGameRules::Weapon(State.Type).Interval, true);
}

void UPFWeaponComponent::Fire()
{
	if (!Character.IsValid() || !Character->CanAct() || Character->bSprinting || State.bReloading || State.Magazine <= 0) { Cancel(); return; }
	const float Now = GetWorld()->GetTimeSeconds();
	const FPFWeaponDefinition Definition = FPFGameRules::Weapon(State.Type);
	if (Now + .001f < LastShotTime + Definition.Interval || Now < Character->WeaponReadyTime) return;
	// Preserve scheduled cadence through ordinary hitches. Limit catch-up to
	// three due rounds so a long stall cannot release an unbounded burst.
	LastShotTime = FMath::Max(LastShotTime + Definition.Interval, Now - 2.f * Definition.Interval); --State.Magazine; ++State.ShotSerial;
	const FVector Eye = Character->GetAimOrigin(), Forward = Character->GetAimDirection();
	const FVector Muzzle = Eye + Forward * 55.f + Character->GetActorRightVector() * 12.f - FVector(0.f, 0.f, 12.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PFHitscan), true, Character.Get());
	FHitResult MuzzleBlock, CameraHit;
	const bool bMuzzleBlocked = GetWorld()->LineTraceSingleByChannel(MuzzleBlock, Eye, Muzzle, ECC_Visibility, Params);
	GetWorld()->LineTraceSingleByChannel(CameraHit, Eye, Eye + Forward * 15000.f, ECC_Visibility, Params);
	const FVector Aim = CameraHit.bBlockingHit ? CameraHit.ImpactPoint : Eye + Forward * 15000.f;
	const FVector Origin = bMuzzleBlocked ? Eye : Muzzle;
	const FVector BaseDirection = (Aim - Origin).GetSafeNormal(UE_SMALL_NUMBER, Forward);
	FRandomStream Random(State.ShotSerial * 196613 + GetOwner()->GetUniqueID());
	const APFPlayerState* PlayerState = Character->GetPlayerState<APFPlayerState>();
	const float OpticFactor = Character->bAiming && PlayerState && PlayerState->Profile.Equipment.Contains(TEXT("Optic")) ? .65f : 1.f;
	FVector LastEnd = Aim;
	uint8 HitKind = 0;
	for (int32 Pellet = 0; Pellet < Definition.Pellets; ++Pellet)
	{
		const FVector Direction = Random.VRandCone(BaseDirection, FMath::DegreesToRadians(Definition.SpreadDegrees * (Character->bAiming ? .5f : 1.f) * OpticFactor));
		FHitResult Hit;
		if (bMuzzleBlocked) Hit = MuzzleBlock;
		else GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * 15000.f, ECC_Visibility, Params);
		LastEnd = Hit.bBlockingHit ? Hit.ImpactPoint : Origin + Direction * 15000.f;
		if (APFEnemyCharacter* Enemy = Cast<APFEnemyCharacter>(Hit.GetActor()))
		{
			const bool bHelmet = Enemy->bHelmetIntact;
			if (Enemy->Health->IsAlive() && Enemy->ReceiveShot(Definition.Damage, Hit.BoneName, FVector::Distance(Eye, Hit.ImpactPoint), State.Type, Character.Get()) > 0.f)
				HitKind = FMath::Max<uint8>(HitKind, !Enemy->Health->IsAlive() ? 3 : bHelmet != Enemy->bHelmetIntact ? 2 : 1);
		}
		else if (APFInteractable* Device = Cast<APFInteractable>(Hit.GetActor()))
			UGameplayStatics::ApplyPointDamage(Device, Definition.Damage, Direction, Hit, Character->GetController(), Character.Get(), UDamageType::StaticClass());
	}
	UAISense_Hearing::ReportNoiseEvent(GetWorld(), Muzzle, 1.f, Character.Get(), IsSuppressed() ? 2000.f : 10000.f, TEXT("Gunshot"));
	MulticastShot(LastEnd, State.ShotSerial, IsSuppressed(), State.Type, ActiveTriggerRequest, TriggerShotOrdinal++); GetOwner()->ForceNetUpdate();
	if (HitKind) ClientConfirmHit(HitKind);
}

void UPFWeaponComponent::ClientConfirmHit_Implementation(uint8 Kind)
{
	LastHitKind = Kind; LastConfirmedHitTime = GetWorld()->GetTimeSeconds();
	if (HitConfirmSound) UGameplayStatics::PlaySound2D(this, HitConfirmSound, Kind == 3 ? .5f : .3f, Kind == 2 ? 1.4f : Kind == 3 ? .8f : 1.f);
}

void UPFWeaponComponent::ServerReload_Implementation(uint32 Sequence)
{
	if (!FPFGameRules::IsNewSequence(Sequence, LastSequence)) return; LastSequence = Sequence;
	if (!Character.IsValid() || !Character->CanAct() || State.bReloading || State.Reserve <= 0) return;
	const FPFWeaponDefinition Definition = FPFGameRules::Weapon(State.Type);
	if (State.Magazine >= Definition.Magazine) return;
	Cancel(); State.bReloading = true;
	const float Duration = Definition.ReloadSeconds * (Character->HasSkill(TEXT("Reload")) ? .8f : 1.f);
	State.ReloadEndTime = GetWorld()->GetTimeSeconds() + Duration;
	PresentationReloadDuration = Duration; PresentationReloadEnd = State.ReloadEndTime; OnRep_Reloading();
	GetWorld()->GetTimerManager().SetTimer(ReloadTimer, this, &ThisClass::FinishReload, Duration); GetOwner()->ForceNetUpdate();
}

void UPFWeaponComponent::FinishReload()
{
	if (!State.bReloading || !Character.IsValid() || !Character->Health->IsAlive()) { Cancel(); return; }
	const FPFWeaponDefinition Definition = FPFGameRules::Weapon(State.Type);
	const int32 Amount = FMath::Min(State.Reserve, State.Type == EPFWeaponType::Shotgun ? 1 : Definition.Magazine - State.Magazine);
	State.Magazine += Amount; State.Reserve -= Amount;
	if (State.Type == EPFWeaponType::Shotgun && State.Magazine < Definition.Magazine && State.Reserve > 0)
	{
		const float Duration = Definition.ReloadSeconds * (Character->HasSkill(TEXT("Reload")) ? .8f : 1.f);
		State.ReloadEndTime = GetWorld()->GetTimeSeconds() + Duration;
		PresentationReloadDuration = Duration; PresentationReloadEnd = State.ReloadEndTime; OnRep_Reloading();
		GetWorld()->GetTimerManager().SetTimer(ReloadTimer, this, &ThisClass::FinishReload, Duration);
	}
	else { State.bReloading = false; State.ReloadEndTime = 0.f; PresentationReloadEnd = 0.f; OnRep_Reloading(); }
	GetOwner()->ForceNetUpdate();
}

void UPFWeaponComponent::ServerEquip_Implementation(EPFWeaponType Type, uint32 Sequence)
{
	if (!FPFGameRules::IsNewSequence(Sequence, LastSequence)) return; LastSequence = Sequence;
	const float Now = GetWorld()->GetTimeSeconds();
	if (!Character.IsValid() || !Character->CanAct() || !StoredWeapons.Contains(Type) || Type == State.Type || Now - LastEquipTime < .3f) return;
	const APFPlayerState* PlayerState = Character->GetPlayerState<APFPlayerState>();
	const EPFWeaponType Primary = PlayerState ? PlayerState->Profile.PrimaryWeapon : EPFWeaponType::Rifle;
	if (Type != EPFWeaponType::Pistol && Type != Primary) return;
	Cancel(); LastEquipTime = Now; StoredWeapons.Add(State.Type, State); State = StoredWeapons[Type]; EquippedType = Type;
	Character->WeaponReadyTime = Now + (Character->HasSkill(TEXT("Handling")) ? .25f : .5f); GetOwner()->ForceNetUpdate();
	OnRep_EquippedType();
}

void UPFWeaponComponent::Cancel()
{
	StopLocalShotEffects();
	GetWorld()->GetTimerManager().ClearTimer(FireTimer); GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);
	bTriggerHeld = false;
	if (GetOwner()->HasAuthority()) { State.bReloading = false; State.ReloadEndTime = 0.f; PresentationReloadEnd = 0.f; OnRep_Reloading(); }
}

void UPFWeaponComponent::EquipPreparedPrimary()
{
	if (!GetOwner()->HasAuthority() || !Character.IsValid()) return;
	const APFPlayerState* PlayerState = Character->GetPlayerState<APFPlayerState>(); if (!PlayerState) return;
	Cancel(); StoredWeapons.Add(State.Type, State);
	if (const FPFWeaponState* Entry = StoredWeapons.Find(PlayerState->Profile.PrimaryWeapon)) { State = *Entry; EquippedType = State.Type; }
	Character->WeaponReadyTime = GetWorld()->GetTimeSeconds() + .5f; GetOwner()->ForceNetUpdate();
	OnRep_EquippedType();
}

bool UPFWeaponComponent::IsSuppressed() const
{
	const APFPlayerState* PlayerState = Character.IsValid() ? Character->GetPlayerState<APFPlayerState>() : nullptr;
	return State.Type != EPFWeaponType::Shotgun && PlayerState && PlayerState->Profile.Equipment.Contains(TEXT("Suppressor"));
}

void UPFWeaponComponent::AddAmmo(int32 Amount)
{
	if (!GetOwner()->HasAuthority() || Amount <= 0) return;
	State.Reserve = FMath::Min(FPFGameRules::Weapon(State.Type).Reserve, State.Reserve + Amount); GetOwner()->ForceNetUpdate();
}

TArray<FPFWeaponState> UPFWeaponComponent::Snapshot() const
{
	TArray<FPFWeaponState> Result;
	for (const auto& Pair : StoredWeapons)
	{
		FPFWeaponState Entry = Pair.Key == State.Type ? State : Pair.Value;
		Entry.bReloading = false; Entry.ReloadEndTime = 0.f; Result.Add(Entry);
	}
	return Result;
}

void UPFWeaponComponent::Restore(const TArray<FPFWeaponState>& Weapons, EPFWeaponType Equipped)
{
	if (!GetOwner()->HasAuthority()) return;
	Cancel();
	for (FPFWeaponState Entry : Weapons)
	{
		if (!StoredWeapons.Contains(Entry.Type)) continue;
		const FPFWeaponDefinition Definition = FPFGameRules::Weapon(Entry.Type);
		Entry.Magazine = FMath::Clamp(Entry.Magazine, 0, Definition.Magazine); Entry.Reserve = FMath::Clamp(Entry.Reserve, 0, Definition.Reserve);
		Entry.bReloading = false; Entry.ReloadEndTime = 0.f; StoredWeapons.Add(Entry.Type, Entry);
	}
	if (StoredWeapons.Contains(Equipped)) { State = StoredWeapons[Equipped]; EquippedType = Equipped; OnRep_EquippedType(); }
}

void UPFWeaponComponent::OnRep_EquippedType()
{
	UE_LOG(LogTemp, Verbose, TEXT("PF_SHOT_EQUIP type=%d state=%d active=%d request=%u"), int32(EquippedType), int32(State.Type), bLocalTriggerHeld, LocalTriggerRequest);
	StopLocalShotEffects(); if (Character.IsValid()) Character->UpdateWeaponPresentation();
}
void UPFWeaponComponent::OnRep_Reloading()
{
	UE_LOG(LogTemp, Verbose, TEXT("PF_SHOT_RELOAD end=%.3f active=%d request=%u"), PresentationReloadEnd, bLocalTriggerHeld, LocalTriggerRequest);
	if (PresentationReloadEnd > 0.f) StopLocalShotEffects();
	if (Character.IsValid()) Character->UpdateReloadPresentation(PresentationReloadEnd, PresentationReloadDuration);
}

void UPFWeaponComponent::BeginLocalShotEffects(uint32 Request)
{
	if (!Character.IsValid() || Character->HasAuthority() || !Character->IsLocallyControlled()) return;
	StopLocalShotEffects();
	if (GetWorld()->GetTimeSeconds() + .001f < NextLocalShotEffectTime)
	{
		UE_LOG(LogTemp, Verbose, TEXT("PF_SHOT_LOCAL_COOLDOWN request=%u remaining=%.4f"), Request, NextLocalShotEffectTime - GetWorld()->GetTimeSeconds());
		return;
	}
	LocalTriggerRequest = Request; LocalShotOrdinal = 0; LocalStartSerial = State.ShotSerial;
	LocalEffectWeapon = State.Type; bLocalTriggerHeld = true; PredictShotEffect();
	if (bLocalTriggerHeld && State.Type == EPFWeaponType::Rifle)
		GetWorld()->GetTimerManager().SetTimer(LocalEffectTimer, this, &ThisClass::PredictShotEffect, FPFGameRules::Weapon(State.Type).Interval, true);
}

void UPFWeaponComponent::StopLocalShotEffects()
{
	bLocalTriggerHeld = false; GetWorld()->GetTimerManager().ClearTimer(LocalEffectTimer);
}

void UPFWeaponComponent::ApplyLocalRecoil()
{
	if (!Character.IsValid() || !Character->IsLocallyControlled()) return;
	const UPFUserSettings* Settings = UPFUserSettings::Get();
	Character->AddControllerPitchInput((Character->HasSkill(TEXT("Recoil")) ? -.3f : -.4f) * (Settings ? Settings->ShakeScale : 1.f));
}

void UPFWeaponComponent::PredictShotEffect()
{
	if (!bLocalTriggerHeld || !Character.IsValid()) return;
	if (LocalEffectFrame != GFrameCounter) { LocalEffectFrame = GFrameCounter; EffectsThisFrame = 0; }
	if (EffectsThisFrame >= 3) return;
	++EffectsThisFrame;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const float ServerNow = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const int32 Outstanding = int32(LocalShotOrdinal) - FMath::Clamp(State.ShotSerial - LocalStartSerial, 0, int32(LocalShotOrdinal));
	if (!Character->CanAct() || Character->bSprinting || State.bReloading || State.Type != LocalEffectWeapon || EquippedType != LocalEffectWeapon
		|| State.Magazine <= Outstanding || ServerNow < Character->WeaponReadyTime || LocalShotOrdinal >= 64)
	{
		UE_LOG(LogTemp, Verbose, TEXT("PF_SHOT_LOCAL_REJECT request=%u ordinal=%u type=%d equipped=%d effect=%d can_act=%d sprint=%d reload=%d ammo=%d outstanding=%d ready_remaining=%.4f"),
			LocalTriggerRequest, LocalShotOrdinal, int32(State.Type), int32(EquippedType), int32(LocalEffectWeapon), Character->CanAct(), Character->bSprinting,
			State.bReloading, State.Magazine, Outstanding, Character->WeaponReadyTime - ServerNow);
		StopLocalShotEffects(); return;
	}
	if (!ShotEffects.Record(LocalTriggerRequest, LocalShotOrdinal++, LocalEffectWeapon, GetWorld()->GetTimeSeconds()))
	{
		UE_LOG(LogTemp, Verbose, TEXT("PF_SHOT_LOCAL_ALREADY_PLAYED request=%u ordinal=%u"), LocalTriggerRequest, LocalShotOrdinal - 1);
		return;
	}
	NextLocalShotEffectTime = GetWorld()->GetTimeSeconds() + FPFGameRules::Weapon(LocalEffectWeapon).Interval;
	Character->PlayShotPresentation(Character->GetAimOrigin() + Character->GetAimDirection() * 15000.f, IsSuppressed());
	ApplyLocalRecoil(); ++LocalPredictedEffects; ++LocalPlayedEffects;
	UE_LOG(LogTemp, Verbose, TEXT("PF_SHOT_LOCAL_PLAY request=%u ordinal=%u predicted=%d played=%d"), LocalTriggerRequest, LocalShotOrdinal - 1, LocalPredictedEffects, LocalPlayedEffects);
}

void UPFWeaponComponent::MulticastShot_Implementation(FVector_NetQuantize End, int32 Serial, bool bQuiet, EPFWeaponType ShotWeapon, uint32 Request, uint32 Ordinal)
{
	OnShotEffect(End, Serial);
	if (Character.IsValid())
	{
		if (Character->IsLocallyControlled() && !Character->HasAuthority())
		{
			if (!ShotEffects.Record(Request, Ordinal, ShotWeapon, GetWorld()->GetTimeSeconds())) return;
			++LocalPlayedEffects;
		}
		Character->PlayShotPresentation(End, bQuiet);
		ApplyLocalRecoil();
	}
}
