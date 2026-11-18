#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Core/PFTypes.h"
#include "Perception/AIPerceptionTypes.h"
#include "PFAIController.generated.h"

class UStateTreeAIComponent;
class UAIPerceptionComponent;
class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;
class UStateTree;
class APFPlayerCharacter;
class APFCoverPoint;

struct FPFTargetMemory
{
	TWeakObjectPtr<APFPlayerCharacter> Target;
	FVector LastSeen = FVector::ZeroVector;
	float LastSeenTime = -100.f;
	float LastHeardTime = -100.f;
	FVector LastHeard = FVector::ZeroVector;
	bool bVisible = false;
	float Awareness = 0.f;
	FName Source;
};

UCLASS(Blueprintable)
class PROJECTF_API APFAIController : public AAIController
{
	GENERATED_BODY()
public:
	APFAIController();
	virtual void OnPossess(APawn* Pawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStateTreeAIComponent> StateTree;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UAIPerceptionComponent> Perception;
	UPROPERTY() TObjectPtr<UAISenseConfig_Sight> SightConfig;
	UPROPERTY() TObjectPtr<UAISenseConfig_Hearing> HearingConfig;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TObjectPtr<UStateTree> Behavior;
	UPROPERTY(BlueprintReadOnly) EPFAlertState DesiredState = EPFAlertState::Routine;
	UPROPERTY(BlueprintReadOnly) FVector InvestigationPoint = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<APFPlayerCharacter> VisibleTarget;
	void UpdateMemory();
	void PerformBehavior(EPFAlertState State, float DeltaTime);
	void ExitBehavior();
	void Shutdown();
	void ReceiveReport(APFPlayerCharacter* Target, const FVector& Location, float Time);
	const TMap<TWeakObjectPtr<APFPlayerCharacter>, FPFTargetMemory>& GetMemories() const { return Memories; }
private:
	FTimerHandle MemoryTimer;
	TMap<TWeakObjectPtr<APFPlayerCharacter>, FPFTargetMemory> Memories;
	FVector PostLocation = FVector::ZeroVector;
	FVector LastMoveGoal = FVector(FLT_MAX);
	float NextMoveTime = 0.f;
	float SearchUntil = 0.f;
	float AlarmAttemptTime = 0.f;
	int32 PatrolIndex = 0;
	float LastReportTime = -100.f;
	float LastBodyCheckTime = -100.f;
	float NextTacticTime = 0.f;
	float PeekUntil = 0.f;
	TWeakObjectPtr<APFCoverPoint> Cover;
	TSet<TWeakObjectPtr<AActor>> KnownBodies;
	void UpdateTactics();
	void ReleaseCover();
	void ShareReport();
	bool MoveToKnownPoint(const FVector& Location, float Acceptance = 100.f);
	UFUNCTION() void OnPerceived(AActor* Actor, FAIStimulus Stimulus);
};
