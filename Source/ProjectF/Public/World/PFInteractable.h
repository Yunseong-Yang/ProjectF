#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PFInteractable.generated.h"

class APFPlayerCharacter;
class APFOutpost;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EPFInteractableKind : uint8 { Supply, Alarm, Exploration, SafePoint };

UCLASS(Blueprintable)
class PROJECTF_API APFInteractable : public AActor
{
	GENERATED_BODY()
public:
	APFInteractable();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual float TakeDamage(float Damage, const FDamageEvent& Event, AController* InstigatorController, AActor* Causer) override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EPFInteractableKind Kind = EPFInteractableKind::Supply;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName PersistentId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<APFOutpost> Outpost;
	UPROPERTY(Replicated, BlueprintReadOnly) bool bDestroyed = false;
	UPROPERTY(Replicated, BlueprintReadOnly) float Durability = 60.f;
	bool CanUse(APFPlayerCharacter* Player) const;
	bool Use(APFPlayerCharacter* Player);
};
