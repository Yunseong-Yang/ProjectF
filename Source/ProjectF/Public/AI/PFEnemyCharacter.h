#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Core/PFTypes.h"
#include "PFEnemyCharacter.generated.h"

class UPFHealthComponent;
class APFOutpost;
class APFPlayerCharacter;
class UStaticMeshComponent;
class UAnimSequence;
class USoundBase;

UCLASS(Blueprintable)
class PROJECTF_API APFEnemyCharacter : public ACharacter
{
	GENERATED_BODY()
public:
	APFEnemyCharacter();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual float TakeDamage(float Damage, const FDamageEvent& Event, AController* InstigatorController, AActor* Causer) override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UPFHealthComponent> Health;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> HeldWeapon;
	UPROPERTY(EditDefaultsOnly, Category="Presentation") TObjectPtr<UAnimSequence> DeathAnimation;
	UPROPERTY(EditDefaultsOnly, Category="Presentation") TObjectPtr<USoundBase> GunshotSound;
	UFUNCTION(NetMulticast, Unreliable) void MulticastFireEffect();
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly) EPFEnemyRole EnemyRole = EPFEnemyRole::Soldier;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<APFOutpost> Outpost;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName ThreatId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FVector> PatrolPoints;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bAtCoverCorner = false;
	UPROPERTY(Replicated, BlueprintReadOnly) EPFAlertState Alert = EPFAlertState::Routine;
	UPROPERTY(Replicated, BlueprintReadOnly) bool bHelmetIntact = false;
	UPROPERTY(Replicated, BlueprintReadOnly) FGuid KillId;
	UPROPERTY(Replicated, BlueprintReadOnly) float DamageMultiplier = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bReinforcement = false;
	UPROPERTY(BlueprintReadOnly) bool bHasArrived = true;
	bool bAdvancingToPost = false;
	FVector EntryDestination = FVector::ZeroVector;
	FVector EntryOrigin = FVector::ZeroVector;
	bool TryArrive();
	FPFCombatScale FrozenScale;
	void InitializeCombatScale(const FPFCombatScale& Scale);
	float ReceiveShot(float BaseDamage, FName Bone, float Distance, EPFWeaponType Weapon, APFPlayerCharacter* Shooter);
	bool ReserveExecution(APFPlayerCharacter* Attacker);
	void ReleaseExecution(APFPlayerCharacter* Attacker);
	bool CommitExecution(APFPlayerCharacter* Attacker, float PresentationTime = -100.f);
	UPROPERTY(ReplicatedUsing=OnRep_ExecutionPresentation) float ExecutionPresentationTime = -100.f;
	bool IsExecutionReserved() const { return ExecutionOwner.IsValid(); }
	void FireAt(APFPlayerCharacter* Target);
	void RecordDamageContributor(APFPlayerCharacter* Player);
private:
	bool bScaleInitialized = false;
	bool bKillProcessed = false;
	float NextShotTime = 0.f;
	int32 BurstShots = 0;
	FTimerHandle ExecutionPoseTimer;
	UFUNCTION() void OnRep_ExecutionPresentation();
	void PlayDeathPresentation();
	TWeakObjectPtr<APFPlayerCharacter> ExecutionOwner;
	TSet<TWeakObjectPtr<APFPlayerCharacter>> Contributors;
	UFUNCTION() void OnLifeChanged(EPFLifeState Previous, EPFLifeState Current);
};
