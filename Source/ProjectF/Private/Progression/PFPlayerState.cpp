#include "Progression/PFPlayerState.h"
#include "Net/UnrealNetwork.h"

APFPlayerState::APFPlayerState() { SetNetUpdateFrequency(5.f); }

void APFPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(APFPlayerState, Profile, COND_OwnerOnly);
	DOREPLIFETIME(APFPlayerState, bJoinedWorld); DOREPLIFETIME(APFPlayerState, TeamLife);
	DOREPLIFETIME(APFPlayerState, TeamLocation); DOREPLIFETIME(APFPlayerState, TeamHealth);
}

void APFPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);
	if (APFPlayerState* Other = Cast<APFPlayerState>(PlayerState)) Other->Profile = Profile;
}

bool APFPlayerState::UnlockSkill(FName Id)
{
	if (!HasAuthority() || !FPFGameRules::CanUnlock(Profile, Id)) return false;
	Profile.Skills.Add(Id); Profile.SpentPoints += FPFGameRules::FindSkill(Id)->Cost; ++Profile.Revision; ForceNetUpdate(); return true;
}

bool APFPlayerState::ResetSkills()
{
	if (!HasAuthority()) return false;
	Profile.Skills.Reset(); Profile.SpentPoints = 0; ++Profile.Revision; ForceNetUpdate(); return true;
}

bool APFPlayerState::GrantReward(FName Id, int32 Experience, int32 Parts, int32 Supplies)
{
	if (!HasAuthority() || Id.IsNone() || Profile.ClaimedRewards.Contains(Id)) return false;
	Profile.ClaimedRewards.Add(Id);
	++Profile.Revision;
	Profile.Experience = FMath::Clamp<int64>(static_cast<int64>(Profile.Experience) + FMath::Max(0, Experience), 0, 10000000);
	Profile.Parts = FMath::Clamp<int64>(static_cast<int64>(Profile.Parts) + FMath::Max(0, Parts), 0, 1000000);
	Profile.Supplies = FMath::Clamp<int64>(static_cast<int64>(Profile.Supplies) + FMath::Max(0, Supplies), 0, 1000000);
	ForceNetUpdate(); return true;
}

void APFPlayerState::AddCombatExperience(int32 Amount)
{
	if (!HasAuthority()) return;
	++Profile.Revision;
	Profile.Experience = FMath::Clamp<int64>(static_cast<int64>(Profile.Experience) + FMath::Max(0, Amount), 0, 10000000); ForceNetUpdate();
}

bool APFPlayerState::SpendSupplies(int32 Amount)
{
	if (!HasAuthority() || Amount < 0 || Profile.Supplies < Amount) return false;
	Profile.Supplies -= Amount; ++Profile.Revision; ForceNetUpdate(); return true;
}
