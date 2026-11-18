#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Core/PFTypes.h"
#include "PFPlayerController.generated.h"

class UPFHUDWidget;
class UPFNetValidationComponent;
class UPFFrontEndWidget;
class ACameraActor;

UCLASS()
class PROJECTF_API APFPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void SetupInputComponent() override;
	virtual void ClientReturnToMainMenuWithTextReason_Implementation(const FText& ReturnReason) override;
	UPROPERTY(BlueprintReadOnly) bool bMenuOpen = false;
	UPROPERTY(BlueprintReadOnly) FText StatusMessage;
	UPROPERTY(Transient) TObjectPtr<UPFHUDWidget> HUDWidget;
	UPROPERTY(Transient) TObjectPtr<UPFFrontEndWidget> FrontEndWidget;
	UPROPERTY(Transient) TObjectPtr<ACameraActor> FrontEndCamera;
	bool bFrontEndOpen = false;
	void StartExpedition();
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPFNetValidationComponent> NetValidation;
	APFPlayerController();
	bool bInitializedProfile = false;
	void CaptureSession();
	void RestoreSession();
	UFUNCTION(Server, Reliable) void ServerInitializeProfile(const FPFProfile& Profile, int32 Protocol);
	UFUNCTION(Server, Reliable) void ServerUnlockSkill(FName Id);
	UFUNCTION(Server, Reliable) void ServerResetSkills();
	UFUNCTION(Server, Reliable) void ServerCraft(bool Medkit);
	UFUNCTION(Server, Reliable) void ServerPrepareEquipment(FName Id);
	bool IsAtSafePoint() const;
	UFUNCTION(Server, Reliable) void ServerFastTravel(FName Id);
	UFUNCTION(Server, Reliable) void ServerSaveAndLeave();
	UFUNCTION(Client, Reliable) void ClientPersistProfile(const FPFProfile& Profile);
	UFUNCTION(Client, Reliable) void ClientNotify(const FText& Message);
	UFUNCTION(Client, Reliable) void ClientOpenPreparation();
	UFUNCTION(Exec) void PFHost();
	UFUNCTION(Exec) void PFJoin(const FString& Address);
	void ToggleMenu();
private:
	float LastMenuRequest = -100.f;
};
