#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/EngineBaseTypes.h"
#include "PFGameInstance.generated.h"

UCLASS()
class PROJECTF_API UPFGameInstance : public UGameInstance
{
	GENERATED_BODY()
public:
	virtual void Init() override;
	virtual void Shutdown() override;
	UPROPERTY(BlueprintReadOnly) FText ConnectionMessage;
private:
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
	void OnNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);
	void OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error);
};
