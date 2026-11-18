#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "PFBuildContentCommandlet.generated.h"

/** Deterministic local content authoring. Creates test content only, preserving template assets. */
UCLASS()
class UPFBuildContentCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UPFBuildContentCommandlet();
	virtual int32 Main(const FString& Params) override;
};
