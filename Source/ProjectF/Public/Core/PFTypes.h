#pragma once

#include "CoreMinimal.h"
#include "PFTypes.generated.h"

UENUM(BlueprintType)
enum class EPFLifeState : uint8 { Alive, Downed, Dead };

UENUM(BlueprintType)
enum class EPFWeaponType : uint8 { Rifle, Pistol, Shotgun };

UENUM(BlueprintType)
enum class EPFEnemyRole : uint8 { Soldier, Sentry, Heavy };

UENUM(BlueprintType)
enum class EPFAlertState : uint8 { Routine, Investigate, Search, Combat, Dead };

UENUM(BlueprintType)
enum class EPFTakedownType : uint8 { Rear, Aerial, Cover, Chain, Heavy, Throw };

UENUM(BlueprintType)
enum class EPFOutpostPhase : uint8 { Undiscovered, Discovered, Engaged, Captured };

UENUM(BlueprintType)
enum class EPFFacilityType : uint8 { Stronghold, Communications, Ammunition, VehicleDepot };

USTRUCT(BlueprintType)
struct PROJECTF_API FPFCombatScale
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 Participants = 1;
	UPROPERTY(BlueprintReadOnly) float Health = 1.f;
	UPROPERTY(BlueprintReadOnly) float Damage = 1.f;
	UPROPERTY(BlueprintReadOnly) int32 Garrison = 8;
};

USTRUCT(BlueprintType)
struct PROJECTF_API FPFWeaponDefinition
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Damage = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Magazine = 30;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Reserve = 150;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Interval = .1f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ReloadSeconds = 2.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float EffectiveRange = 6000.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Pellets = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float SpreadDegrees = .25f;
};

USTRUCT(BlueprintType)
struct PROJECTF_API FPFWeaponState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) EPFWeaponType Type = EPFWeaponType::Rifle;
	UPROPERTY(BlueprintReadOnly) int32 Magazine = 30;
	UPROPERTY(BlueprintReadOnly) int32 Reserve = 150;
	UPROPERTY(BlueprintReadOnly) bool bReloading = false;
	UPROPERTY(BlueprintReadOnly) float ReloadEndTime = 0.f;
	UPROPERTY(BlueprintReadOnly) int32 ShotSerial = 0;
};

USTRUCT(BlueprintType)
struct PROJECTF_API FPFSkillDefinition
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FName Id;
	UPROPERTY(BlueprintReadOnly) FText Name;
	UPROPERTY(BlueprintReadOnly) FText Description;
	UPROPERTY(BlueprintReadOnly) FName Prerequisite;
	UPROPERTY(BlueprintReadOnly) int32 Cost = 1;
	UPROPERTY(BlueprintReadOnly) int32 Branch = 0;
};

USTRUCT(BlueprintType)
struct PROJECTF_API FPFProfile
{
	GENERATED_BODY()
	UPROPERTY(SaveGame, BlueprintReadOnly) FString PlayerId;
	UPROPERTY(SaveGame, BlueprintReadOnly) int64 Revision = 0;
	UPROPERTY(SaveGame, BlueprintReadOnly) int32 Experience = 0;
	UPROPERTY(SaveGame, BlueprintReadOnly) int32 SpentPoints = 0;
	UPROPERTY(SaveGame, BlueprintReadOnly) TArray<FName> Skills;
	UPROPERTY(SaveGame, BlueprintReadOnly) int32 Parts = 0;
	UPROPERTY(SaveGame, BlueprintReadOnly) int32 Supplies = 0;
	UPROPERTY(SaveGame, BlueprintReadOnly) TArray<FName> ClaimedRewards;
	UPROPERTY(SaveGame, BlueprintReadOnly) TArray<FName> Blueprints;
	UPROPERTY(SaveGame, BlueprintReadOnly) TArray<FName> Equipment;
	UPROPERTY(SaveGame, BlueprintReadOnly) EPFWeaponType PrimaryWeapon = EPFWeaponType::Rifle;
};

USTRUCT()
struct PROJECTF_API FPFSessionRecord
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FString PlayerId;
	UPROPERTY(SaveGame) TArray<FPFWeaponState> Weapons;
	UPROPERTY(SaveGame) EPFWeaponType Equipped = EPFWeaponType::Rifle;
	UPROPERTY(SaveGame) int32 Medkits = 3;
	UPROPERTY(SaveGame) int32 Grenades = 2;
	UPROPERTY(SaveGame) float Health = 100.f;
};

USTRUCT(BlueprintType)
struct PROJECTF_API FPFOutpostRecord
{
	GENERATED_BODY()
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadOnly) FName Id;
	UPROPERTY(SaveGame, BlueprintReadOnly) FText Title;
	UPROPERTY(SaveGame, BlueprintReadOnly) FVector MapLocation = FVector::ZeroVector;
	UPROPERTY(SaveGame, BlueprintReadOnly) EPFOutpostPhase Phase = EPFOutpostPhase::Undiscovered;
	UPROPERTY(SaveGame, BlueprintReadOnly) EPFFacilityType Facility = EPFFacilityType::Stronghold;
	UPROPERTY(SaveGame, BlueprintReadOnly) TArray<FName> Supports;
	UPROPERTY(SaveGame, BlueprintReadOnly) FGuid CompletionId;
};

/** Pure rules are shared by authority code and automation tests; clients never write outcomes. */
struct PROJECTF_API FPFGameRules
{
	static constexpr int32 SaveVersion = 1;
	static constexpr int32 ProtocolVersion = 4;
	static constexpr float TakedownSeconds = 1.2f;
	static constexpr float TakedownContactSeconds = .32f;
	static constexpr int32 MaxPlayers = 4;
	static FPFCombatScale ScaleFor(int32 Participants);
	static FPFWeaponDefinition Weapon(EPFWeaponType Type);
	static const TArray<FPFSkillDefinition>& Skills();
	static const FPFSkillDefinition* FindSkill(FName Id);
	static int32 LevelForExperience(int32 Experience);
	static int32 AvailablePoints(const FPFProfile& Profile);
	static bool CanUnlock(const FPFProfile& Profile, FName Skill);
	static bool IsRearApproach(const FVector& TargetForward, const FVector& TargetToAttacker);
	static bool IsNewSequence(uint32 Incoming, uint32 Previous);
	static bool ValidateProfile(const FPFProfile& Profile);
	static bool UpgradeProfile(FPFProfile& Profile);
	static float DetectionGain(float Seconds, float Distance, bool bCrouched, bool bSprinting);
};
