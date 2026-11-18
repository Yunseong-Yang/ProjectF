#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/PFTypes.h"
#include "PFHealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPFLifeChanged, EPFLifeState, Previous, EPFLifeState, Current);
DECLARE_MULTICAST_DELEGATE_TwoParams(FPFHealthDamaged, float, AActor*);

UCLASS(ClassGroup=(ProjectF), meta=(BlueprintSpawnableComponent))
class PROJECTF_API UPFHealthComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UPFHealthComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(BlueprintAssignable) FPFLifeChanged OnLifeChanged;
	FPFHealthDamaged OnDamaged;
	FPFHealthDamaged OnBeforeDamaged;
	UPROPERTY(EditDefaultsOnly, Category="Health") bool bCanBeDowned = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Health") float Maximum = 100.f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Health") float Current = 100.f;
	UPROPERTY(ReplicatedUsing=OnRep_Life, BlueprintReadOnly, Category="Health") EPFLifeState Life = EPFLifeState::Alive;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Health") float BleedOutTime = 0.f;
	uint32 DamageRevision = 0;
	UFUNCTION(BlueprintPure) bool IsAlive() const { return Life == EPFLifeState::Alive; }
	/** Authority-only. Returns the applied damage. Teammate damage is rejected. */
	float ApplyDamage(float Amount, AActor* Source);
	bool Heal(float Amount);
	bool Revive();
	bool Execute();
	void InitializeHealth(float Value);
private:
	FTimerHandle BleedTimer;
	void SetLife(EPFLifeState NewLife);
	void BleedOut();
	UFUNCTION() void OnRep_Life(EPFLifeState Previous);
};
