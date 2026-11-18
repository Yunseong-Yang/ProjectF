#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PFTraversalComponent.generated.h"

/** Short, fully swept vaults use Character Movement root-motion sources. */
UCLASS(ClassGroup=(ProjectF), meta=(BlueprintSpawnableComponent))
class PROJECTF_API UPFTraversalComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UPFTraversalComponent();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool FindVault(float& Distance, float& Height, FName* Failure = nullptr) const;
	UFUNCTION(Server, Reliable) void ServerVault(uint32 Sequence);
	void Cancel();
	UPROPERTY(ReplicatedUsing=OnRep_Vaulting, BlueprintReadOnly) bool bVaulting = false;
private:
	uint32 LastSequence = 0;
	uint16 MotionId = 0;
	FTimerHandle FinishTimer;
	UFUNCTION() void OnRep_Vaulting();
};
