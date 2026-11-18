#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Core/PFTypes.h"
#include "Engine/NetSerialization.h"
#include "PFGameState.generated.h"

USTRUCT(BlueprintType)
struct PROJECTF_API FPFReconMark
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 Id = 0;
	UPROPERTY(BlueprintReadOnly) FVector_NetQuantize Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) float ObservedTime = 0.f;
	UPROPERTY(BlueprintReadOnly) EPFEnemyRole Role = EPFEnemyRole::Soldier;
};

UCLASS()
class PROJECTF_API APFGameState : public AGameStateBase
{
	GENERATED_BODY()
public:
	APFGameState();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(Replicated, BlueprintReadOnly) TArray<FPFOutpostRecord> Outposts;
	UPROPERTY(Replicated, BlueprintReadOnly) TArray<FPFReconMark> ReconMarks;
	UPROPERTY(Replicated, BlueprintReadOnly) FGuid WorldId;
};
