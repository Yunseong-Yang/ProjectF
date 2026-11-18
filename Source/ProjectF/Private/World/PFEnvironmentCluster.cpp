#include "World/PFEnvironmentCluster.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

APFEnvironmentCluster::APFEnvironmentCluster()
{
	PrimaryActorTick.bCanEverTick = false;
	Instances = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("Instances")); SetRootComponent(Instances);
	Instances->SetMobility(EComponentMobility::Static);
}
