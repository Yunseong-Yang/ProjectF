#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameFramework/SaveGame.h"
#include "Core/PFTypes.h"
#include "PFSaveSubsystem.generated.h"

USTRUCT()
struct PROJECTF_API FPFPendingReward
{
	GENERATED_BODY()
	UPROPERTY(SaveGame) FName RewardId;
	UPROPERTY(SaveGame) TArray<FString> Participants;
	UPROPERTY(SaveGame) int32 Experience = 400;
	UPROPERTY(SaveGame) int32 Parts = 10;
	UPROPERTY(SaveGame) int32 Supplies = 5;
};

UCLASS()
class PROJECTF_API UPFSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame) int32 FormatVersion = FPFGameRules::SaveVersion;
	UPROPERTY(SaveGame) FGuid WorldId;
	UPROPERTY(SaveGame) TArray<FPFOutpostRecord> Outposts;
	UPROPERTY(SaveGame) TArray<FPFProfile> Profiles;
	UPROPERTY(SaveGame) TArray<FPFPendingReward> Rewards;
	UPROPERTY(SaveGame) TArray<FPFSessionRecord> Sessions;
	UPROPERTY(SaveGame) FPFProfile LocalProfile;
};

/** Local files only. CRC-checked temporary writes, previous valid copy, no network backend. */
UCLASS()
class PROJECTF_API UPFSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	UPROPERTY(Transient) TObjectPtr<UPFSaveGame> WorldSave;
	UPROPERTY(Transient) TObjectPtr<UPFSaveGame> PersonalSave;
	UPROPERTY(BlueprintReadOnly) FText LastError;
	bool SaveWorld();
	bool SavePersonal(const FPFProfile& Profile);
	UPFSaveGame* Read(const FString& Slot);
	bool Write(const FString& Slot, UPFSaveGame* Save);
	static bool Decode(const TArray<uint8>& Bytes, TArray<uint8>& Payload);
private:
	bool bWorldWritable = true;
	bool bPersonalWritable = true;
};
