#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/PFTypes.h"
#include "PFTakedownComponent.generated.h"

class APFEnemyCharacter;
class APFPlayerCharacter;

UCLASS(ClassGroup=(ProjectF), meta=(BlueprintSpawnableComponent))
class PROJECTF_API UPFTakedownComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UPFTakedownComponent();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(ReplicatedUsing=OnRep_Presentation, BlueprintReadOnly) int32 ActionId = 0;
	UPROPERTY(ReplicatedUsing=OnRep_Presentation) TObjectPtr<APFEnemyCharacter> PresentationTarget;
	UPROPERTY(Replicated) bool bCrouchedBeforeExecution = false;
	UPROPERTY(Replicated, BlueprintReadOnly) float StartTime = 0.f;
	UPROPERTY(Replicated, BlueprintReadOnly) EPFTakedownType ActiveType = EPFTakedownType::Rear;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UFUNCTION(Server, Reliable) void ServerRequest(APFEnemyCharacter* Target, EPFTakedownType Type, uint32 Sequence);
	APFEnemyCharacter* FindCandidate() const;
	APFEnemyCharacter* FindThrowCandidate() const;
	EPFTakedownType ChooseType(APFEnemyCharacter* Enemy) const;
	bool IsEligible(APFEnemyCharacter* Enemy, EPFTakedownType Type) const;
	void Cancel();
	bool IsPresenting() const;
private:
	uint32 LastSequence = 0;
	float GetServerTime() const;
	bool HasChainWindow() const;
	TWeakObjectPtr<APFEnemyCharacter> ReservedEnemy;
	FTimerHandle CleanupTimer;
	FTimerHandle ImpactTimer;
	void PlayImpactCue();
	int32 LastPresentedAction = 0;
	bool bRestoreCrouch = false;
	void RestoreStance(APFPlayerCharacter* Player);
	UFUNCTION() void OnRep_Presentation();
};
