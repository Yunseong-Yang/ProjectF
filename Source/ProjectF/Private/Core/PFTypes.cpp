#include "Core/PFTypes.h"

float FPFGameRules::DetectionGain(float Seconds, float Distance, bool bCrouched, bool bSprinting)
{
	if (!FMath::IsFinite(Seconds) || !FMath::IsFinite(Distance) || Seconds <= 0.f) return 0.f;
	const float RangeFactor = FMath::GetMappedRangeValueClamped(FVector2D(300.f, 5000.f), FVector2D(.45f, 1.6f), Distance);
	return Seconds / (RangeFactor * (bCrouched ? 1.8f : bSprinting ? .65f : 1.f));
}

FPFCombatScale FPFGameRules::ScaleFor(int32 Participants)
{
	FPFCombatScale Result;
	Result.Participants = FMath::Clamp(Participants, 1, MaxPlayers);
	Result.Health = 1.f + .15f * (Result.Participants - 1);
	Result.Damage = 1.f + .05f * (Result.Participants - 1);
	Result.Garrison = 8 + 2 * (Result.Participants - 1);
	return Result;
}

FPFWeaponDefinition FPFGameRules::Weapon(EPFWeaponType Type)
{
	FPFWeaponDefinition Result;
	if (Type == EPFWeaponType::Pistol)
	{
		Result.Damage = 28.f; Result.Magazine = 12; Result.Reserve = 48;
		Result.Interval = .2f; Result.ReloadSeconds = 1.6f; Result.EffectiveRange = 2000.f;
	}
	else if (Type == EPFWeaponType::Shotgun)
	{
		Result.Damage = 14.f; Result.Magazine = 6; Result.Reserve = 30;
		Result.Interval = .75f; Result.ReloadSeconds = .6f; Result.EffectiveRange = 1000.f;
		Result.Pellets = 8; Result.SpreadDegrees = 4.f;
	}
	return Result;
}

const TArray<FPFSkillDefinition>& FPFGameRules::Skills()
{
	static const TArray<FPFSkillDefinition> Definitions = []
	{
		TArray<FPFSkillDefinition> Nodes;
		auto Add = [&Nodes](const TCHAR* Id, const TCHAR* Name, const TCHAR* Description,
			const TCHAR* Parent, int32 Cost, int32 Branch)
		{
			FPFSkillDefinition Node;
			Node.Id = Id; Node.Name = FText::FromString(Name); Node.Description = FText::FromString(Description);
			Node.Prerequisite = Parent; Node.Cost = Cost; Node.Branch = Branch; Nodes.Add(Node);
		};
		Add(TEXT("Aerial"), TEXT("공중 처형"), TEXT("위에서 접근한 적을 처형합니다."), TEXT(""), 1, 0);
		Add(TEXT("Cover"), TEXT("엄폐 처형"), TEXT("엄폐 모서리의 적을 처형합니다."), TEXT("Aerial"), 1, 0);
		Add(TEXT("Chain"), TEXT("연속 처형"), TEXT("처형 직후 가까운 다음 적에게 연결합니다."), TEXT("Cover"), 2, 0);
		Add(TEXT("ExecutionMaster"), TEXT("처형 숙련"), TEXT("중화기병 처형과 투척 연계를 해금합니다."), TEXT("Chain"), 2, 0);
		Add(TEXT("Handling"), TEXT("무기 전환 숙련"), TEXT("무기 전환 준비 시간이 줄어듭니다."), TEXT(""), 1, 1);
		Add(TEXT("Reload"), TEXT("재장전 숙련"), TEXT("재장전 시간이 20% 줄어듭니다."), TEXT("Handling"), 1, 1);
		Add(TEXT("Recoil"), TEXT("반동 제어"), TEXT("사격 반동이 25% 줄어듭니다."), TEXT("Reload"), 2, 1);
		Add(TEXT("Throwing"), TEXT("투척 운용"), TEXT("투척 도구를 하나 더 휴대합니다."), TEXT("Recoil"), 2, 1);
		Add(TEXT("Medicine"), TEXT("회복 도구 휴대"), TEXT("회복 도구를 하나 더 휴대합니다."), TEXT(""), 1, 2);
		Add(TEXT("Healing"), TEXT("회복 숙련"), TEXT("회복 도구 사용 시간이 20% 줄어듭니다."), TEXT("Medicine"), 1, 2);
		Add(TEXT("Revive"), TEXT("신속 소생"), TEXT("동료 소생 시간이 20% 줄어듭니다."), TEXT("Healing"), 2, 2);
		Add(TEXT("Supply"), TEXT("보급 효율"), TEXT("안전 지점 제작의 보급품 비용이 줄어듭니다."), TEXT("Revive"), 2, 2);
		return Nodes;
	}();
	return Definitions;
}

const FPFSkillDefinition* FPFGameRules::FindSkill(FName Id)
{
	return Skills().FindByPredicate([Id](const FPFSkillDefinition& Node) { return Node.Id == Id; });
}

int32 FPFGameRules::LevelForExperience(int32 Experience)
{
	int32 Level = 1;
	int64 Remaining = FMath::Max(0, Experience);
	while (Level < 100 && Remaining >= 250 + (Level - 1) * 100)
	{
		Remaining -= 250 + (Level - 1) * 100;
		++Level;
	}
	return Level;
}

int32 FPFGameRules::AvailablePoints(const FPFProfile& Profile)
{
	return FMath::Max(0, LevelForExperience(Profile.Experience) - 1 - Profile.SpentPoints);
}

bool FPFGameRules::CanUnlock(const FPFProfile& Profile, FName Skill)
{
	const FPFSkillDefinition* Node = FindSkill(Skill);
	return Node && !Profile.Skills.Contains(Skill) && AvailablePoints(Profile) >= Node->Cost
		&& (Node->Prerequisite.IsNone() || Profile.Skills.Contains(Node->Prerequisite));
}

bool FPFGameRules::IsRearApproach(const FVector& TargetForward, const FVector& TargetToAttacker)
{
	return !TargetToAttacker.IsNearlyZero() && FVector::DotProduct(TargetForward.GetSafeNormal2D(),
		TargetToAttacker.GetSafeNormal2D()) <= -.5f;
}

bool FPFGameRules::IsNewSequence(uint32 Incoming, uint32 Previous)
{
	return static_cast<int32>(Incoming - Previous) > 0;
}

bool FPFGameRules::ValidateProfile(const FPFProfile& Profile)
{
	if (Profile.Blueprints.Num() > 3 || Profile.Equipment.Num() > 3 || Profile.PrimaryWeapon == EPFWeaponType::Pistol) return false;
	const TSet<FName> Allowed = {TEXT("Shotgun"), TEXT("Suppressor"), TEXT("Optic")};
	TSet<FName> Known;
	for (FName Id : Profile.Blueprints) { if (!Allowed.Contains(Id) || Known.Contains(Id)) return false; Known.Add(Id); }
	Known.Reset();
	for (FName Id : Profile.Equipment) { if (!Profile.Blueprints.Contains(Id) || Known.Contains(Id)) return false; Known.Add(Id); }
	if (Profile.PrimaryWeapon != EPFWeaponType::Rifle && (Profile.PrimaryWeapon != EPFWeaponType::Shotgun || !Profile.Equipment.Contains(TEXT("Shotgun")))) return false;
	if (Profile.PlayerId.IsEmpty() || Profile.PlayerId.Len() > 64 || Profile.Revision < 0 || Profile.Revision > 1000000000000LL || Profile.Experience < 0
		|| Profile.Experience > 10000000 || Profile.Parts < 0 || Profile.Parts > 1000000
		|| Profile.Supplies < 0 || Profile.Supplies > 1000000 || Profile.Skills.Num() > Skills().Num()
		|| Profile.ClaimedRewards.Num() > 10000) return false;
	TSet<FName> Unique;
	int32 Cost = 0;
	for (FName Id : Profile.Skills)
	{
		const FPFSkillDefinition* Node = FindSkill(Id);
		if (!Node || Unique.Contains(Id) || (!Node->Prerequisite.IsNone() && !Profile.Skills.Contains(Node->Prerequisite))) return false;
		Unique.Add(Id); Cost += Node->Cost;
	}
	return Cost == Profile.SpentPoints && Cost <= LevelForExperience(Profile.Experience) - 1;
}

bool FPFGameRules::UpgradeProfile(FPFProfile& Profile)
{
	bool bChanged = false;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FName Reward(*FString::Printf(TEXT("Exploration.%d"), Index));
		const FName Blueprint = Index == 0 ? TEXT("Suppressor") : Index == 1 ? TEXT("Shotgun") : TEXT("Optic");
		if (Profile.ClaimedRewards.Contains(Reward) && !Profile.Blueprints.Contains(Blueprint)) { Profile.Blueprints.Add(Blueprint); bChanged = true; }
	}
	if (bChanged) Profile.Revision = FMath::Min<int64>(1000000000000LL, Profile.Revision + 1);
	return bChanged;
}
