#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/PFTypes.h"
#include "PFOutpost.generated.h"

class APFPlayerCharacter;
class APFPlayerState;
class APFEnemyCharacter;
class APFInteractable;
class UStaticMeshComponent;
class UAudioComponent;
class USoundBase;

/** Server threat roster survives actor unloading. Only an explicit death resolves a threat. */
struct FPFThreatEntry
{
	TWeakObjectPtr<APFEnemyCharacter> Enemy;
	bool bResolved = false;
};

/** Authored server patrol for a specific initial garrison slot. */
USTRUCT(BlueprintType)
struct FPFGarrisonPatrolRoute
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 SpawnIndex = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FVector> Waypoints;
};

UCLASS(Blueprintable)
class PROJECTF_API APFOutpost : public AActor
{
	GENERATED_BODY()
	friend class UPFNetValidationComponent;
public:
	APFOutpost();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Terminal;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UAudioComponent> AlarmSpeaker;
	UPROPERTY(EditAnywhere, Category="Presentation") TObjectPtr<USoundBase> AlarmCallingSound;
	UPROPERTY(EditAnywhere, Category="Presentation") TObjectPtr<USoundBase> AlarmSentSound;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName OutpostId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EPFFacilityType Facility = EPFFacilityType::Stronghold;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FName> Supports;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ParticipationRadius = 12000.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FTransform> GarrisonSpawns;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FPFGarrisonPatrolRoute> GarrisonPatrolRoutes;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FTransform> ReinforcementSpawns;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSubclassOf<APFEnemyCharacter> EnemyClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<APFInteractable> Alarm;
	UPROPERTY(Replicated, BlueprintReadOnly) EPFOutpostPhase Phase = EPFOutpostPhase::Undiscovered;
	UPROPERTY(Replicated, BlueprintReadOnly) int32 RemainingThreats = 0;
	UPROPERTY(Replicated, BlueprintReadOnly) FPFCombatScale BattleScale;
	UPROPERTY(Replicated, BlueprintReadOnly) float ReinforcementTime = 0.f;
	UPROPERTY(ReplicatedUsing=OnRep_AlarmState, BlueprintReadOnly) float AlarmStartTime = 0.f;
	UPROPERTY(ReplicatedUsing=OnRep_AlarmState, BlueprintReadOnly) bool bAlarmCalled = false;
	UPROPERTY(Replicated) float AlarmSentTime = -100.f;
	UPROPERTY(Replicated, BlueprintReadOnly) TObjectPtr<APFEnemyCharacter> AlarmCaller;
	bool RegisterEnemy(APFEnemyCharacter* Enemy, FName ThreatId);
	void ResolveEnemy(FName ThreatId, APFEnemyCharacter* Enemy);
	void RecordContribution(APFPlayerCharacter* Player);
	void Engage(APFPlayerCharacter* Player);
	bool CanCapture(APFPlayerCharacter* Player) const;
	bool Capture(APFPlayerCharacter* Player);
	bool ReserveAlarm(APFEnemyCharacter* Enemy);
	void ReleaseAlarm(APFEnemyCharacter* Enemy);
	bool BeginAlarm(APFEnemyCharacter* Enemy);
	void CancelAlarm();
	bool IsSafeSpawn() const;
	FVector GetSafeSpawnLocation() const;
private:
	TMap<FName, FPFThreatEntry> Threats;
	TSet<FString> ContributorIds;
	FTimerHandle AlarmTimer;
	FTimerHandle ReinforcementTimer;
	bool bGarrisonSpawned = false;
	float AudibleCallStart = -100.f;
	bool bPlayedSentSound = false;
	int32 PendingReinforcements = 0;
	TSet<FName> SpawnedThreatIds;
	UFUNCTION() void OnRep_AlarmState();
	void SpawnGarrison();
	void SpawnReinforcements();
	void FinishAlarm();
	int32 SpawnGroup(const TArray<FTransform>& Spawns, int32 Count, const TCHAR* Prefix, const FPFCombatScale& Scale);
	int32 CountParticipants() const;
};
