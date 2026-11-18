#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/PFTypes.h"
#include "Engine/NetSerialization.h"
#include "Combat/PFShotEffectLedger.h"
#include "PFWeaponComponent.generated.h"

class APFPlayerCharacter;
class USoundBase;

UCLASS(ClassGroup=(ProjectF), meta=(BlueprintSpawnableComponent))
class PROJECTF_API UPFWeaponComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UPFWeaponComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(Replicated, BlueprintReadOnly) FPFWeaponState State;
	UPROPERTY(EditDefaultsOnly, Category="Presentation") TObjectPtr<USoundBase> HitConfirmSound;
	UPROPERTY(ReplicatedUsing=OnRep_EquippedType, BlueprintReadOnly) EPFWeaponType EquippedType = EPFWeaponType::Rifle;
	UPROPERTY(Replicated) float PresentationReloadDuration = 0.f;
	UPROPERTY(ReplicatedUsing=OnRep_Reloading) float PresentationReloadEnd = 0.f;
	UFUNCTION(Server, Reliable) void ServerSetTrigger(bool Pressed, uint32 Sequence);
	UFUNCTION(Server, Reliable) void ServerReload(uint32 Sequence);
	UFUNCTION(Server, Reliable) void ServerEquip(EPFWeaponType Type, uint32 Sequence);
	void Cancel();
	void BeginLocalShotEffects(uint32 Request);
	void StopLocalShotEffects();
	int32 LocalPredictedEffects = 0;
	int32 LocalPlayedEffects = 0;
	void AddAmmo(int32 Amount);
	void EquipPreparedPrimary();
	bool IsSuppressed() const;
	float LastConfirmedHitTime = -100.f;
	uint8 LastHitKind = 0;
	UFUNCTION(Client, Unreliable) void ClientConfirmHit(uint8 Kind);
	TArray<FPFWeaponState> Snapshot() const;
	void Restore(const TArray<FPFWeaponState>& Weapons, EPFWeaponType Equipped);
	bool HasTriggerHeld() const { return bTriggerHeld; }
	UFUNCTION(NetMulticast, Unreliable) void MulticastShot(FVector_NetQuantize End, int32 Serial, bool bQuiet, EPFWeaponType ShotWeapon, uint32 Request, uint32 Ordinal);
	UFUNCTION(BlueprintImplementableEvent, Category="Weapon") void OnShotEffect(FVector End, int32 Serial);
private:
	TWeakObjectPtr<APFPlayerCharacter> Character;
	TMap<EPFWeaponType, FPFWeaponState> StoredWeapons;
	FTimerHandle FireTimer;
	FTimerHandle ReloadTimer;
	FTimerHandle LocalEffectTimer;
	FPFShotEffectLedger ShotEffects;
	uint32 ActiveTriggerRequest = 0;
	uint32 TriggerShotOrdinal = 0;
	uint32 LocalTriggerRequest = 0;
	uint32 LocalShotOrdinal = 0;
	int32 LocalStartSerial = 0;
	EPFWeaponType LocalEffectWeapon = EPFWeaponType::Rifle;
	bool bLocalTriggerHeld = false;
	float NextLocalShotEffectTime = -100.f;
	uint64 LocalEffectFrame = MAX_uint64;
	uint8 EffectsThisFrame = 0;
	void PredictShotEffect();
	void ApplyLocalRecoil();
	bool bTriggerHeld = false;
	uint32 LastSequence = 0;
	float LastShotTime = -100.f;
	float LastEquipTime = -100.f;
	void Fire();
	void FinishReload();
	UFUNCTION() void OnRep_EquippedType();
	UFUNCTION() void OnRep_Reloading();
};
