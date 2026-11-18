#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "PFInspectTerrainCommandlet.generated.h"

UCLASS()
class UPFInspectTerrainCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UPFInspectTerrainCommandlet();
	virtual int32 Main(const FString& Params) override;
};
