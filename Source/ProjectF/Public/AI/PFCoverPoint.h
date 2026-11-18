#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PFCoverPoint.generated.h"

class APFEnemyCharacter;

/** Authored cover and a separate firing position; reservations exist only on authority. */
UCLASS(Blueprintable)
class PROJECTF_API APFCoverPoint : public AActor
{
	GENERATED_BODY()
public:
	APFCoverPoint();
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector PeekOffset = FVector(0.f, 140.f, 0.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bLowCover = true;
	FVector GetPeekLocation() const;
	bool CanProtectFrom(const FVector& Threat, const APFEnemyCharacter* Enemy) const;
	bool Reserve(APFEnemyCharacter* Enemy);
	void Release(APFEnemyCharacter* Enemy);
	bool IsReservedBy(const APFEnemyCharacter* Enemy) const;
private:
	TWeakObjectPtr<APFEnemyCharacter> Occupant;
};
