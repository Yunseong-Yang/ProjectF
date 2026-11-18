#include "Framework/PFGameState.h"
#include "Net/UnrealNetwork.h"

APFGameState::APFGameState() { SetNetUpdateFrequency(10.f); }

void APFGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APFGameState, Outposts); DOREPLIFETIME(APFGameState, ReconMarks); DOREPLIFETIME(APFGameState, WorldId);
}
