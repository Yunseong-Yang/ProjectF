#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PFEnvironmentCluster.generated.h"

class UHierarchicalInstancedStaticMeshComponent;

/** Authored, spatially bounded instance batches; no runtime scatter or tick. */
UCLASS()
class PROJECTF_API APFEnvironmentCluster : public AActor
{
	GENERATED_BODY()
public:
	APFEnvironmentCluster();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Instances;
};
