#include "Framework/PFPlayerController.h"
#include "Framework/PFGameMode.h"
#include "Framework/PFGameInstance.h"
#include "Framework/PFNetValidationComponent.h"
#include "Player/PFPlayerCharacter.h"
#include "Progression/PFPlayerState.h"
#include "Combat/PFHealthComponent.h"
#include "Combat/PFWeaponComponent.h"
#include "World/PFWorldStateSubsystem.h"
#include "World/PFOutpost.h"
#include "World/PFInteractable.h"
#include "Save/PFSaveSubsystem.h"
#include "UI/PFHUDWidget.h"
#include "UI/PFFrontEndWidget.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Misc/CommandLine.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameSession.h"

APFPlayerController::APFPlayerController()
{
	NetValidation = CreateDefaultSubobject<UPFNetValidationComponent>(TEXT("DevelopmentValidation"));
}

void APFPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		UPFSaveSubsystem* Saves = GetGameInstance()->GetSubsystem<UPFSaveSubsystem>();
		if (!Saves->LastError.IsEmpty()) StatusMessage = Saves->LastError;
		if (UPFGameInstance* Instance = Cast<UPFGameInstance>(GetGameInstance())) if (!Instance->ConnectionMessage.IsEmpty()) StatusMessage = Instance->ConnectionMessage;
		const TCHAR* CommandLine = FCommandLine::Get();
		const bool bDevelopmentRun = FParse::Param(CommandLine, TEXT("PFNetSmoke")) || FParse::Param(CommandLine, TEXT("PFVisualCapture"))
			|| FParse::Param(CommandLine, TEXT("PFFrameProbe")) || FParse::Param(CommandLine, TEXT("PFStress")) || FParse::Param(CommandLine, TEXT("PFSkipFrontEnd")) || FParse::Param(CommandLine, TEXT("PFExecutionCapture"));
		bFrontEndOpen = GetWorld()->WorldType == EWorldType::Game && GetNetMode() == NM_Standalone
			&& GetWorld()->GetMapName().EndsWith(TEXT("L_MountainFrontier")) && !bDevelopmentRun;
		if (bFrontEndOpen)
		{
			FActorSpawnParameters Spawn; Spawn.Owner = this;
			FrontEndCamera = GetWorld()->SpawnActor<ACameraActor>(FVector(-35000, -17500, 8100), FRotator(-8, 42, 0), Spawn);
			if (FrontEndCamera) { FrontEndCamera->GetCameraComponent()->SetFieldOfView(80); SetViewTarget(FrontEndCamera); }
			FrontEndWidget = CreateWidget<UPFFrontEndWidget>(this, UPFFrontEndWidget::StaticClass());
			if (FrontEndWidget)
			{
				FrontEndWidget->AddToViewport(10); bShowMouseCursor = true;
				FInputModeUIOnly Mode; Mode.SetWidgetToFocus(FrontEndWidget->TakeWidget()); SetInputMode(Mode);
				FrontEndWidget->Dispatch(TEXT("Back"));
			}
			else StartExpedition();
		}
		else StartExpedition();
	}
}

void APFPlayerController::StartExpedition()
{
	if (!IsLocalController() || HUDWidget) return;
	bFrontEndOpen = false; bShowMouseCursor = false;
	if (FrontEndWidget) { FrontEndWidget->RemoveFromParent(); FrontEndWidget = nullptr; }
	SetViewTarget(this);
	if (FrontEndCamera) { FrontEndCamera->Destroy(); FrontEndCamera = nullptr; }
	FInputModeGameOnly Mode; SetInputMode(Mode);
	ServerInitializeProfile(GetGameInstance()->GetSubsystem<UPFSaveSubsystem>()->PersonalSave->LocalProfile, FPFGameRules::ProtocolVersion);
	HUDWidget = CreateWidget<UPFHUDWidget>(this, UPFHUDWidget::StaticClass());
	if (HUDWidget) HUDWidget->AddToViewport();
}

void APFPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (HUDWidget) { HUDWidget->RemoveFromParent(); HUDWidget = nullptr; }
	if (FrontEndWidget) { FrontEndWidget->RemoveFromParent(); FrontEndWidget = nullptr; }
	Super::EndPlay(Reason);
}

void APFPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent(); InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ThisClass::ToggleMenu);
	InputComponent->BindKey(EKeys::Gamepad_Special_Right, IE_Pressed, this, &ThisClass::ToggleMenu);
}

void APFPlayerController::ToggleMenu()
{
	if (bFrontEndOpen) { if (FrontEndWidget) FrontEndWidget->Dispatch(TEXT("Back")); return; }
	bMenuOpen = !bMenuOpen; bShowMouseCursor = bMenuOpen;
	if (APFPlayerCharacter* PlayerPawn = Cast<APFPlayerCharacter>(GetPawn())) PlayerPawn->SetLocalGameplayInputEnabled(!bMenuOpen);
	if (bMenuOpen) { FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false); SetInputMode(Mode); }
	else { FInputModeGameOnly Mode; SetInputMode(Mode); }
	if (HUDWidget) HUDWidget->Refresh();
	if (bMenuOpen && HUDWidget) HUDWidget->FocusMenu();
}

void APFPlayerController::ServerInitializeProfile_Implementation(const FPFProfile& Submitted, int32 Protocol)
{
	if (bInitializedProfile) return;
	FGuid Id;
	if (Protocol != FPFGameRules::ProtocolVersion || !FPFGameRules::ValidateProfile(Submitted) || !FGuid::ParseExact(Submitted.PlayerId, EGuidFormats::Digits, Id))
	{
		ClientReturnToMainMenuWithTextReason(FText::FromString(TEXT("빌드 또는 개인 프로필이 호환되지 않습니다."))); return;
	}
	for (TActorIterator<APFPlayerState> It(GetWorld()); It; ++It)
		if (*It != PlayerState && It->Profile.PlayerId == Submitted.PlayerId) { ClientReturnToMainMenuWithTextReason(FText::FromString(TEXT("동일한 프로필이 이미 접속해 있습니다."))); return; }
	APFPlayerState* State = GetPlayerState<APFPlayerState>(); if (!State) return;
	UPFSaveSubsystem* Saves = GetGameInstance()->GetSubsystem<UPFSaveSubsystem>();
	const FPFProfile* Stored = Saves->WorldSave->Profiles.FindByPredicate([&Submitted](const FPFProfile& Profile) { return Profile.PlayerId == Submitted.PlayerId; });
	// Personal progress can arrive from another friend's world; only a newer server snapshot wins.
	State->Profile = Stored && Stored->Revision > Submitted.Revision ? *Stored : Submitted;
	bInitializedProfile = true; GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->ApplyPendingRewards(State);
	if (APFGameMode* Mode = GetWorld()->GetAuthGameMode<APFGameMode>()) Mode->AdmitPlayer(this);
	ClientPersistProfile(State->Profile);
}

void APFPlayerController::ClientPersistProfile_Implementation(const FPFProfile& Profile)
{
	if (!GetGameInstance()->GetSubsystem<UPFSaveSubsystem>()->SavePersonal(Profile)) StatusMessage = FText::FromString(TEXT("개인 저장에 실패했습니다. 마지막 정상 저장은 보존됩니다."));
}

void APFPlayerController::ClientNotify_Implementation(const FText& Message) { StatusMessage = Message; }
void APFPlayerController::ClientOpenPreparation_Implementation()
{
	if (!bMenuOpen) ToggleMenu();
	if (HUDWidget) HUDWidget->Dispatch(TEXT("Page.1"));
}

void APFPlayerController::ClientReturnToMainMenuWithTextReason_Implementation(const FText& ReturnReason)
{
	if (UPFGameInstance* Instance = Cast<UPFGameInstance>(GetGameInstance())) Instance->ConnectionMessage = ReturnReason;
	Super::ClientReturnToMainMenuWithTextReason_Implementation(ReturnReason);
}

void APFPlayerController::ServerUnlockSkill_Implementation(FName Id)
{
	APFPlayerState* State = GetPlayerState<APFPlayerState>(); APFPlayerCharacter* PlayerPawn = Cast<APFPlayerCharacter>(GetPawn());
	if (!bInitializedProfile || !State || !PlayerPawn || !PlayerPawn->CanAct() || GetWorld()->GetTimeSeconds() - LastMenuRequest < .2f) return;
	LastMenuRequest = GetWorld()->GetTimeSeconds();
	if (!State->UnlockSkill(Id)) { ClientNotify(FText::FromString(TEXT("스킬 포인트 또는 선행 해금이 부족합니다."))); return; }
	GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->Save(); ClientPersistProfile(State->Profile);
}

void APFPlayerController::ServerResetSkills_Implementation()
{
	APFPlayerCharacter* PlayerPawn = Cast<APFPlayerCharacter>(GetPawn()); APFPlayerState* State = GetPlayerState<APFPlayerState>();
	if (!PlayerPawn || !State || !PlayerPawn->CanAct() || !PlayerPawn->IsSafe()) return;
	if (GetWorld()->GetTimeSeconds() - LastMenuRequest < .3f) return;
	LastMenuRequest = GetWorld()->GetTimeSeconds();
	bool bAtSafePoint = false;
	for (TActorIterator<APFInteractable> It(GetWorld()); It; ++It) if (It->Kind == EPFInteractableKind::SafePoint && PlayerPawn->GetDistanceTo(*It) <= 500.f) bAtSafePoint = true;
	for (TActorIterator<APFOutpost> It(GetWorld()); It; ++It) if (It->IsSafeSpawn() && PlayerPawn->GetDistanceTo(*It) <= 500.f) bAtSafePoint = true;
	if (!bAtSafePoint) { ClientNotify(FText::FromString(TEXT("스킬은 확보한 안전 지점에서 재분배할 수 있습니다."))); return; }
	State->ResetSkills(); GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->Save(); ClientPersistProfile(State->Profile);
}

void APFPlayerController::ServerCraft_Implementation(bool Medkit)
{
	APFPlayerCharacter* PlayerPawn = Cast<APFPlayerCharacter>(GetPawn()); APFPlayerState* State = GetPlayerState<APFPlayerState>();
	if (!PlayerPawn || !State || !PlayerPawn->CanAct() || !PlayerPawn->IsSafe() || GetWorld()->GetTimeSeconds() - LastMenuRequest < .3f) return;
	LastMenuRequest = GetWorld()->GetTimeSeconds();
	bool bAtSafePoint = false;
	for (TActorIterator<APFInteractable> It(GetWorld()); It; ++It) if (It->Kind == EPFInteractableKind::SafePoint && PlayerPawn->GetDistanceTo(*It) <= 500.f) bAtSafePoint = true;
	for (TActorIterator<APFOutpost> It(GetWorld()); It; ++It) if (It->IsSafeSpawn() && PlayerPawn->GetDistanceTo(*It) <= 500.f) bAtSafePoint = true;
	if (!bAtSafePoint) { ClientNotify(FText::FromString(TEXT("제작은 안전 지점에서 가능합니다."))); return; }
	const int32 Capacity = Medkit ? (State->HasSkill(TEXT("Medicine")) ? 4 : 3) : (State->HasSkill(TEXT("Throwing")) ? 3 : 2);
	int32& Count = Medkit ? PlayerPawn->Medkits : PlayerPawn->Grenades;
	if (Count >= Capacity || !State->SpendSupplies(State->HasSkill(TEXT("Supply")) ? 1 : 2)) return;
	++Count; CaptureSession(); GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->Save(); ClientPersistProfile(State->Profile);
}

bool APFPlayerController::IsAtSafePoint() const
{
	const APFPlayerCharacter* PlayerPawn = Cast<APFPlayerCharacter>(GetPawn());
	if (!PlayerPawn || !PlayerPawn->CanAct() || !PlayerPawn->IsSafe()) return false;
	for (TActorIterator<APFInteractable> It(GetWorld()); It; ++It)
		if (It->Kind == EPFInteractableKind::SafePoint && PlayerPawn->GetDistanceTo(*It) <= 500.f) return true;
	for (TActorIterator<APFOutpost> It(GetWorld()); It; ++It)
		if (It->IsSafeSpawn() && PlayerPawn->GetDistanceTo(*It) <= 500.f) return true;
	return false;
}

void APFPlayerController::ServerPrepareEquipment_Implementation(FName Id)
{
	APFPlayerState* State = GetPlayerState<APFPlayerState>(); APFPlayerCharacter* PlayerPawn = Cast<APFPlayerCharacter>(GetPawn());
	if (!State || !PlayerPawn || !bInitializedProfile || GetWorld()->GetTimeSeconds() - LastMenuRequest < .3f) return;
	LastMenuRequest = GetWorld()->GetTimeSeconds();
	if (!IsAtSafePoint()) { ClientNotify(FText::FromString(TEXT("장비 준비는 전투를 벗어난 안전 지점에서 가능합니다."))); return; }
	if (Id == TEXT("Rifle") || (Id == TEXT("Shotgun") && State->Profile.Equipment.Contains(Id)))
	{
		State->Profile.PrimaryWeapon = Id == TEXT("Rifle") ? EPFWeaponType::Rifle : EPFWeaponType::Shotgun;
		PlayerPawn->Weapon->EquipPreparedPrimary(); ++State->Profile.Revision;
	}
	else if (Id == TEXT("Ammunition"))
	{
		if (PlayerPawn->Weapon->State.Reserve >= FPFGameRules::Weapon(PlayerPawn->Weapon->State.Type).Reserve) { ClientNotify(FText::FromString(TEXT("예비 탄약이 충분합니다."))); return; }
		if (!State->SpendSupplies(2)) { ClientNotify(FText::FromString(TEXT("보급품 2개가 필요합니다."))); return; }
		PlayerPawn->Weapon->AddAmmo(PlayerPawn->Weapon->State.Type == EPFWeaponType::Shotgun ? 12 : 60);
	}
	else
	{
		const int32 Cost = Id == TEXT("Shotgun") ? 15 : Id == TEXT("Suppressor") ? 10 : Id == TEXT("Optic") ? 8 : 0;
		if (!Cost || !State->Profile.Blueprints.Contains(Id)) { ClientNotify(FText::FromString(TEXT("탐험 지점에서 이 장비의 설계도를 먼저 찾아야 합니다."))); return; }
		if (State->Profile.Equipment.Contains(Id)) return;
		if (State->Profile.Parts < Cost) { ClientNotify(FText::FromString(TEXT("장비 제작에 필요한 부품이 부족합니다."))); return; }
		State->Profile.Parts -= Cost; State->Profile.Equipment.Add(Id); ++State->Profile.Revision;
	}
	State->ForceNetUpdate(); CaptureSession(); GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->Save(); ClientPersistProfile(State->Profile);
	ClientNotify(FText::FromString(TEXT("장비 준비를 완료했습니다.")));
}

void APFPlayerController::ServerFastTravel_Implementation(FName Id)
{
	APFPlayerCharacter* PlayerPawn = Cast<APFPlayerCharacter>(GetPawn());
	if (!PlayerPawn || !PlayerPawn->CanAct() || !PlayerPawn->IsSafe()) return;
	if (Id == TEXT("Safe.Camp"))
	{
		for (TActorIterator<APFInteractable> It(GetWorld()); It; ++It) if (It->Kind == EPFInteractableKind::SafePoint && It->PersistentId == Id)
		{
			const FVector Destination = It->GetActorLocation() + FVector(0, 250, 110);
			if (GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->IsSafeLocation(Destination)) PlayerPawn->TeleportTo(Destination, It->GetActorRotation());
			return;
		}
	}
	APFOutpost* Outpost = GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->GetOutposts().FindRef(Id).Get();
	if (!Outpost || !Outpost->IsSafeSpawn()) { ClientNotify(FText::FromString(TEXT("목적지가 안전하지 않습니다."))); return; }
	PlayerPawn->TeleportTo(Outpost->GetSafeSpawnLocation(), Outpost->GetActorRotation());
}

void APFPlayerController::CaptureSession()
{
	if (!HasAuthority()) return;
	const APFPlayerCharacter* PlayerPawn = Cast<APFPlayerCharacter>(GetPawn()); const APFPlayerState* State = GetPlayerState<APFPlayerState>();
	if (!PlayerPawn || !State || State->Profile.PlayerId.IsEmpty()) return;
	UPFSaveGame* Save = GetGameInstance()->GetSubsystem<UPFSaveSubsystem>()->WorldSave;
	FPFSessionRecord* Record = Save->Sessions.FindByPredicate([State](const FPFSessionRecord& Entry) { return Entry.PlayerId == State->Profile.PlayerId; });
	if (!Record) Record = &Save->Sessions.AddDefaulted_GetRef();
	Record->PlayerId = State->Profile.PlayerId; Record->Weapons = PlayerPawn->Weapon->Snapshot(); Record->Equipped = PlayerPawn->Weapon->State.Type;
	Record->Medkits = PlayerPawn->Medkits; Record->Grenades = PlayerPawn->Grenades; Record->Health = PlayerPawn->Health->Current;
}

void APFPlayerController::RestoreSession()
{
	if (!HasAuthority()) return;
	APFPlayerCharacter* PlayerPawn = Cast<APFPlayerCharacter>(GetPawn()); const APFPlayerState* State = GetPlayerState<APFPlayerState>();
	if (!PlayerPawn || !State) return;
	const auto& Sessions = GetGameInstance()->GetSubsystem<UPFSaveSubsystem>()->WorldSave->Sessions;
	if (const FPFSessionRecord* Record = Sessions.FindByPredicate([State](const FPFSessionRecord& Entry) { return Entry.PlayerId == State->Profile.PlayerId; }))
	{
		PlayerPawn->Weapon->Restore(Record->Weapons, Record->Equipped); PlayerPawn->Medkits = FMath::Clamp(Record->Medkits, 0, State->HasSkill(TEXT("Medicine")) ? 4 : 3);
		PlayerPawn->Grenades = FMath::Clamp(Record->Grenades, 0, State->HasSkill(TEXT("Throwing")) ? 3 : 2);
		PlayerPawn->Health->Current = FMath::Clamp(Record->Health > 0.f ? Record->Health : 40.f, 1.f, PlayerPawn->Health->Maximum);
	}
}

void APFPlayerController::ServerSaveAndLeave_Implementation()
{
	CaptureSession(); const bool Saved = GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->Save();
	if (!Saved) { ClientNotify(FText::FromString(TEXT("저장에 실패하여 방을 유지합니다."))); return; }
	if (APFPlayerState* State = GetPlayerState<APFPlayerState>()) ClientPersistProfile(State->Profile);
	if (IsLocalController())
	{
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (APFPlayerController* PC = Cast<APFPlayerController>(It->Get()))
			{
				if (APFPlayerState* State = PC->GetPlayerState<APFPlayerState>()) PC->ClientPersistProfile(State->Profile);
				if (PC != this) PC->ClientReturnToMainMenuWithTextReason(FText::FromString(TEXT("호스트가 저장 후 방을 종료했습니다. 개인 성장은 보존됩니다.")));
			}
	}
	ClientReturnToMainMenuWithTextReason(FText::FromString(TEXT("저장 후 연결을 종료했습니다.")));
}

void APFPlayerController::PFHost()
{
	if (!IsLocalController()) return;
	if (GetWorld()->GetNetMode() != NM_Standalone) { StatusMessage = FText::FromString(TEXT("이미 협동 세션에 연결되어 있습니다.")); return; }
	CaptureSession();
	if (!GetWorld()->GetSubsystem<UPFWorldStateSubsystem>()->Save()) { StatusMessage = FText::FromString(TEXT("저장에 실패하여 방을 열지 못했습니다.")); return; }
	UGameplayStatics::OpenLevel(this, FName(*GetWorld()->GetMapName().Replace(*GetWorld()->StreamingLevelsPrefix, TEXT(""))), true, FString::Printf(TEXT("listen?PFBuild=%d"), FPFGameRules::ProtocolVersion));
}

void APFPlayerController::PFJoin(const FString& Address)
{
	if (!IsLocalController()) return;
	if (Address.IsEmpty() || Address.Len() > 128 || !Address.Contains(TEXT("."))) { StatusMessage = FText::FromString(TEXT("유효한 호스트 주소를 입력하세요.")); return; }
	for (TCHAR AddressCharacter : Address) if (!FChar::IsDigit(AddressCharacter) && AddressCharacter != TEXT('.') && AddressCharacter != TEXT(':')) { StatusMessage = FText::FromString(TEXT("IP 주소와 포트만 입력할 수 있습니다.")); return; }
	if (UPFGameInstance* Instance = Cast<UPFGameInstance>(GetGameInstance())) Instance->ConnectionMessage = FText::GetEmpty();
	ClientTravel(Address + FString::Printf(TEXT("?PFBuild=%d"), FPFGameRules::ProtocolVersion), TRAVEL_Absolute);
}
