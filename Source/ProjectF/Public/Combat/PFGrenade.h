#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PFGrenade.generated.h"

class USphereComponent;
class UProjectileMovementComponent;

UCLASS()
class PROJECTF_API APFGrenade : public AActor
{
	GENERATED_BODY()
public:
	APFGrenade();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Collision;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> Movement;
	UFUNCTION(BlueprintImplementableEvent) void OnExplosion();
private:
	FTimerHandle FuseTimer;
	void Explode();
	UFUNCTION(NetMulticast, Unreliable) void MulticastExplosion();
};
