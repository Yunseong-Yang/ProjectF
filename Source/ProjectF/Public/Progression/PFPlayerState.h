#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Core/PFTypes.h"
#include "PFPlayerState.generated.h"

UCLASS()
class PROJECTF_API APFPlayerState : public APlayerState
{
	GENERATED_BODY()
public:
	APFPlayerState();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual void CopyProperties(APlayerState* PlayerState) override;
	UPROPERTY(Replicated, BlueprintReadOnly) FPFProfile Profile;
	UPROPERTY(Replicated, BlueprintReadOnly) bool bJoinedWorld = false;
	UPROPERTY(Replicated, BlueprintReadOnly) EPFLifeState TeamLife = EPFLifeState::Alive;
	UPROPERTY(Replicated, BlueprintReadOnly) FVector TeamLocation = FVector::ZeroVector;
	UPROPERTY(Replicated, BlueprintReadOnly) uint8 TeamHealth = 100;
	UFUNCTION(BlueprintPure) bool HasSkill(FName Id) const { return Profile.Skills.Contains(Id); }
	UFUNCTION(BlueprintPure) int32 GetSkillPoints() const { return FPFGameRules::AvailablePoints(Profile); }
	bool UnlockSkill(FName Id);
	bool ResetSkills();
	bool GrantReward(FName Id, int32 Experience, int32 Parts, int32 Supplies);
	void AddCombatExperience(int32 Amount);
	bool SpendSupplies(int32 Amount);
};
