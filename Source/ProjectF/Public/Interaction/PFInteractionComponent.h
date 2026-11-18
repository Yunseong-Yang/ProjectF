#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PFInteractionComponent.generated.h"

class APFPlayerCharacter;

UCLASS(ClassGroup=(ProjectF), meta=(BlueprintSpawnableComponent))
class PROJECTF_API UPFInteractionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UPFInteractionComponent();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(Replicated, BlueprintReadOnly) TObjectPtr<AActor> ActiveTarget;
	UPROPERTY(Replicated, BlueprintReadOnly) float StartTime = 0.f;
	UPROPERTY(Replicated, BlueprintReadOnly) float EndTime = 0.f;
	UFUNCTION(Server, Reliable) void ServerBegin(AActor* Target, uint32 Sequence);
	UFUNCTION(Server, Reliable) void ServerCancel(uint32 Sequence);
	UFUNCTION(Server, Reliable) void ServerObserve(AActor* Target, uint32 Sequence);
	AActor* FindTarget() const;
	void Cancel();
private:
	FTimerHandle ProgressTimer;
	TWeakObjectPtr<AActor> ObservedTarget;
	float ObservationStart = 0.f;
	uint32 LastSequence = 0;
	uint32 OwnerDamageRevision = 0;
	uint32 TargetDamageRevision = 0;
	FVector StartLocation = FVector::ZeroVector;
	float LastObserveRequest = -100.f;
	bool IsValidTarget(AActor* Target) const;
	void UpdateProgress();
};
