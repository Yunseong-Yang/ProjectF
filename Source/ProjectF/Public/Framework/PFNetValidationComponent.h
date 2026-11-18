#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PFNetValidationComponent.generated.h"

class APFEnemyCharacter;
class APFOutpost;
class APFPlayerController;
class AAIController;

/** Explicit development smoke, visual and frame checks. Inert without an opt-in flag; inert in Shipping. */
UCLASS()
class PROJECTF_API UPFNetValidationComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UPFNetValidationComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick) override;
	UFUNCTION(Client, Reliable) void ClientBeginAmmoCheck();
	UFUNCTION(Server, Reliable) void ServerReportAmmo(bool Passed, int32 Magazine, int32 Reserve);
	UFUNCTION(Server, Reliable) void ServerTestBurst();
	UFUNCTION(Client, Reliable) void ClientExecute(APFEnemyCharacter* Enemy, uint32 Sequence);
	UFUNCTION(Client, Reliable) void ClientCapture(APFOutpost* Outpost);
	UFUNCTION(Client, Reliable) void ClientVerifyWorld(FName OutpostId, FGuid CompletionId);
	UFUNCTION(Server, Reliable) void ServerReportWorld(FName OutpostId, FGuid CompletionId, bool Passed);
	UFUNCTION(Client, Reliable) void ClientReconnect(const FString& Address);
private:
	bool bTempleTraversal = false;
	int32 TempleTraversalStage = 0;
	float TempleTraversalLogTime = -10.f;
	TWeakObjectPtr<APFEnemyCharacter> TempleTraversalPawn;
	TWeakObjectPtr<AAIController> TempleTraversalController;
	void CheckTempleTraversal(float Now);
	bool bEnabled = false;
	bool bVisualCapture = false;
	bool bFrontEndCheck = false;
	bool bExecutionCapture = false;
	bool bExecutionFirstPerson = false;
	int32 ExecutionCaptureStage = 0;
	float ExecutionCaptureTime = 0.f;
	TWeakObjectPtr<APFEnemyCharacter> ExecutionReviewTarget;
	void CaptureExecution(APFPlayerController* PC, float Elapsed);
	int32 FrontEndStage = 0;
	void CheckFrontEnd(APFPlayerController* PC, float Elapsed);
	bool bFrameProbe = false;
	bool bReviewCameraReady = false;
	bool bVisualAssetsReady = false;
	float LastVisualAssetLog = -100.f;
	bool bStress = false;
	bool bStressSplit = false;
	int32 StressPhase = 0;
	float StressTime = 0.f;
	float StressSyntheticHealth = 100000.f;
	float LastStressAuditTime = 0.f;
	float LastStressGunfireTime = -100.f;
	TArray<TWeakObjectPtr<APFOutpost>> StressOutposts;
	void UpdateStress(float Now);
	void AuditStress(float Now);
	TArray<float> FrameSamples;
	TArray<float> GameSamples;
	TArray<float> RenderSamples;
	TArray<float> GPUSamples;
	void SamplePerformance(float Elapsed);
	int32 VisualStage = 0;
	bool bAmmoReported = false;
	bool bAmmoPassed = false;
	bool bWorldReported = false;
	bool bWorldPassed = false;
	bool bWaitingForWorld = false;
	bool bUsedRearExecutionCheck = false;
	int32 LocalStage = 0;
	int32 ServerStage = 0;
	int32 ExpectedPlayers = 4;
	float StageTime = 0.f;
	float LocalTime = 0.f;
	float LastServerUpdate = -1.f;
	FTimerHandle BurstStopTimer;
	float WorldCheckTime = 0.f;
	FName ExpectedOutpostId;
	FGuid ExpectedCompletion;
	FVector StartPosition = FVector::ZeroVector;
	FVector LocalMovementDirection = FVector::RightVector;
	TWeakObjectPtr<APFEnemyCharacter> ExecutionTarget;
	TWeakObjectPtr<APFOutpost> CaptureTarget;
	TArray<TWeakObjectPtr<APFPlayerController>> Players;
	TMap<TWeakObjectPtr<APFPlayerController>, FVector> StartingPositions;
	TMap<TWeakObjectPtr<APFEnemyCharacter>, FVector> InitialEnemyPositions;
	int32 BeforeExperience = 0;
	float MaximumPatrolDisplacement = 0.f;
	void TickLocal(float Now);
	void TickServer(float Now);
	void Finish(bool Passed, const TCHAR* Reason);
};
