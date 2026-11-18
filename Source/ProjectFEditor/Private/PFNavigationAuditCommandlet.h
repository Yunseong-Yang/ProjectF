#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "PFNavigationAuditCommandlet.generated.h"

/** Read-only surface/topology evidence. Does not rebuild or save the loaded map. */
UCLASS()
class UPFNavigationAuditCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UPFNavigationAuditCommandlet();
	virtual int32 Main(const FString& Params) override;
};
