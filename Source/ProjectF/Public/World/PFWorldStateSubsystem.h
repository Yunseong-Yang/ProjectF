#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Core/PFTypes.h"
#include "PFWorldStateSubsystem.generated.h"

class APFOutpost;
class APFPlayerState;

UCLASS()
class PROJECTF_API UPFWorldStateSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	bool RegisterOutpost(APFOutpost* Outpost);
	void UnregisterOutpost(APFOutpost* Outpost);
	const FPFOutpostRecord* Find(FName Id) const;
	bool CompleteOutpost(FName Id, const TArray<APFPlayerState*>& Participants, const TSet<FString>& EligibleIds);
	void Discover(FName Id);
	void SetEngaged(FName Id);
	bool IsCommunicationsDisrupted(FName Target) const;
	bool IsSafeLocation(const FVector& Location) const;
	bool ApplyPendingRewards(APFPlayerState* Player);
	bool Save();
	void Publish();
	const TMap<FName, TWeakObjectPtr<APFOutpost>>& GetOutposts() const { return LoadedOutposts; }
private:
	bool bLoaded = false;
	void Load();
	TArray<FPFOutpostRecord> Records;
	TMap<FName, TWeakObjectPtr<APFOutpost>> LoadedOutposts;
};
