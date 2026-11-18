#include "UI/PFHUDWidget.h"
#include "UI/PFUserSettings.h"
#include "UI/PFMapWidget.h"
#include "Camera/CameraComponent.h"
#include "Framework/PFPlayerController.h"
#include "Framework/PFGameState.h"
#include "Player/PFPlayerCharacter.h"
#include "Progression/PFPlayerState.h"
#include "Combat/PFHealthComponent.h"
#include "Combat/PFWeaponComponent.h"
#include "Combat/PFTakedownComponent.h"
#include "Interaction/PFInteractionComponent.h"
#include "World/PFOutpost.h"
#include "World/PFInteractable.h"
#include "Save/PFSaveSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/EditableTextBox.h"
#include "Components/InputKeySelector.h"
#include "Components/SizeBox.h"
#include "Components/WidgetSwitcher.h"
#include "EngineUtils.h"
#include "Rendering/DrawElements.h"
#include "GameFramework/PlayerState.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"

namespace
{
	const FLinearColor Accent(.7f, .85f, .3f, 1.f);
	const FLinearColor Muted(.65f, .7f, .7f, 1.f);
}

void UPFMenuAction::Execute() { if (Widget) Widget->Dispatch(Id); }

void UPFMenuAction::Rebind(FInputChord Chord)
{
	UPFUserSettings* Settings = UPFUserSettings::Get(); if (!Settings || !Chord.Key.IsValid()) return;
	if (!Settings->RebindAction(Id, Chord.Key))
	{
		if (Selector) Selector->SetSelectedKey(FInputChord(Settings->GetBinding(Id)));
		if (Widget) if (APFPlayerController* PC = Widget->GetOwningPlayer<APFPlayerController>()) PC->StatusMessage = FText::FromString(TEXT("이미 사용 중이거나 지정할 수 없는 키입니다. 다른 키를 선택하세요."));
		return;
	}
	Settings->SaveSettings();
	if (Widget) if (APlayerController* PC = Widget->GetOwningPlayer()) if (APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(PC->GetPawn())) Player->RebuildLocalMapping();
}

UTextBlock* UPFHUDWidget::AddText(UVerticalBox* Parent, const FString& Text, int32 Size, FLinearColor Color)
{
	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(); Label->SetText(FText::FromString(Text));
	FSlateFontInfo Font = Label->GetFont(); Font.Size = Size; Label->SetFont(Font); Label->SetColorAndOpacity(Color); Label->SetAutoWrapText(true);
	Label->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, .85f)); Label->SetShadowOffset(FVector2D(1.f, 1.f));
	Parent->AddChildToVerticalBox(Label); return Label;
}

UButton* UPFHUDWidget::AddButton(UVerticalBox* Parent, const FString& Label, FName Id)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(); Button->SetBackgroundColor(FLinearColor(.12f, .17f, .17f, 1.f));
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(); Text->SetText(FText::FromString(Label));
	FSlateFontInfo Font = Text->GetFont(); Font.Size = 17; Text->SetFont(Font); Text->SetMargin(FMargin(12.f, 9.f));
	Text->SetColorAndOpacity(FLinearColor::White); Button->AddChild(Text); Parent->AddChildToVerticalBox(Button);
	UPFMenuAction* Action = NewObject<UPFMenuAction>(this); Action->Widget = this; Action->Id = Id; MenuActions.Add(Action);
	Button->OnClicked.AddDynamic(Action, &UPFMenuAction::Execute); return Button;
}

void UPFHUDWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget = Root;
	auto Panel = [this, Root](FVector2D Position, FVector2D Size, FAnchors Anchors, FVector2D Alignment)
	{
		UBorder* Border = WidgetTree->ConstructWidget<UBorder>(); Border->SetBrushColor(FLinearColor(.018f, .03f, .03f, .52f)); Border->SetPadding(FMargin(12.f, 9.f));
		UCanvasPanelSlot* Slot = Root->AddChildToCanvas(Border); Slot->SetAnchors(Anchors); Slot->SetAlignment(Alignment); Slot->SetPosition(Position); Slot->SetAutoSize(true);
		USizeBox* Sizing = WidgetTree->ConstructWidget<USizeBox>(); Sizing->SetWidthOverride(Size.X); Border->AddChild(Sizing);
		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(); Sizing->AddChild(Box); return Box;
	};
	UVerticalBox* Vitals = Panel(FVector2D(24.f, -24.f), FVector2D(320.f, 120.f), FAnchors(0.f, 1.f), FVector2D(0.f, 1.f));
	AddText(Vitals, TEXT("상태"), 12, Accent); VitalText = AddText(Vitals, TEXT(""), 19);
	HealthBar = WidgetTree->ConstructWidget<UProgressBar>(); HealthBar->SetFillColorAndOpacity(Accent); Vitals->AddChildToVerticalBox(HealthBar);
	AmmoText = AddText(Vitals, TEXT(""), 17);
	UVerticalBox* Objectives = Panel(FVector2D(24.f, 24.f), FVector2D(330.f, 140.f), FAnchors(0.f), FVector2D::ZeroVector);
	AddText(Objectives, TEXT("작전 목표"), 12, Accent); ObjectiveText = AddText(Objectives, TEXT(""), 15);
	UVerticalBox* Team = Panel(FVector2D(-24.f, 24.f), FVector2D(350.f, 140.f), FAnchors(1.f, 0.f), FVector2D(1.f, 0.f));
	AddText(Team, TEXT("분대"), 12, Accent); TeamText = AddText(Team, TEXT(""), 14);
	UVerticalBox* Detection = Panel(FVector2D(0.f, 30.f), FVector2D(380.f, 55.f), FAnchors(.5f, 0.f), FVector2D(.5f, 0.f));
	DetectionText = AddText(Detection, TEXT(""), 16, Accent);
	UVerticalBox* Prompt = Panel(FVector2D(0.f, -135.f), FVector2D(520.f, 90.f), FAnchors(.5f, 1.f), FVector2D(.5f, 1.f));
	PromptText = AddText(Prompt, TEXT(""), 16); ActionBar = WidgetTree->ConstructWidget<UProgressBar>(); ActionBar->SetFillColorAndOpacity(Accent); Prompt->AddChildToVerticalBox(ActionBar);
	NoticeText = AddText(Prompt, TEXT(""), 14, Muted);
	UTextBlock* Reticle = WidgetTree->ConstructWidget<UTextBlock>(); Reticle->SetText(FText::FromString(TEXT("·")));
	UCanvasPanelSlot* CrossSlot = Root->AddChildToCanvas(Reticle); CrossSlot->SetAnchors(FAnchors(.5f)); CrossSlot->SetAlignment(FVector2D(.5f)); CrossSlot->SetAutoSize(true);
	Menu = WidgetTree->ConstructWidget<UBorder>(); Menu->SetBrushColor(FLinearColor(.02f, .035f, .035f, .98f)); Menu->SetPadding(FMargin(28.f));
	UCanvasPanelSlot* MenuSlot = Root->AddChildToCanvas(Menu); MenuSlot->SetAnchors(FAnchors(.5f)); MenuSlot->SetAlignment(FVector2D(.5f)); MenuSlot->SetSize(FVector2D(950.f, 680.f));
	UVerticalBox* Shell = WidgetTree->ConstructWidget<UVerticalBox>(); Menu->AddChild(Shell);
	AddText(Shell, TEXT("작전 준비"), 30, Accent); AddText(Shell, TEXT("협동 메뉴를 열어도 월드는 계속 진행됩니다.  ·  ESC 닫기"), 14, Muted);
	ProgressionText = AddText(Shell, TEXT(""), 16);
	UHorizontalBox* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>(); Shell->AddChildToVerticalBox(Tabs);
	Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>(); Shell->AddChildToVerticalBox(Pages);
	TArray<UVerticalBox*> PageContent;
	const TArray<FString> TabNames = {TEXT("작전 지도"), TEXT("장비·보급"), TEXT("스킬"), TEXT("화면·조작"), TEXT("협동·저장")};
	for (int32 Index = 0; Index < TabNames.Num(); ++Index)
	{
		UVerticalBox* TabBox = WidgetTree->ConstructWidget<UVerticalBox>(); Tabs->AddChildToHorizontalBox(TabBox);
		UButton* Tab = AddButton(TabBox, TabNames[Index], FName(*FString::Printf(TEXT("Page.%d"), Index))); if (Index == 0) FirstTab = Tab;
		USizeBox* PageSize = WidgetTree->ConstructWidget<USizeBox>(); PageSize->SetHeightOverride(510.f); Pages->AddChild(PageSize);
		UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(); PageSize->AddChild(Scroll);
		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Box); PageContent.Add(Box);
	}
	UVerticalBox* Content = PageContent[0];
	USizeBox* MapSize = WidgetTree->ConstructWidget<USizeBox>(); MapSize->SetHeightOverride(330.f); Content->AddChildToVerticalBox(MapSize);
	UPFMapWidget* Map = WidgetTree->ConstructWidget<UPFMapWidget>(); Map->SetOwningPlayer(GetOwningPlayer()); MapSize->AddChild(Map);
	Map->MapTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/ProjectF/UI/T_FrontierMap"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	AddText(Content, TEXT("빠른 이동 · 확보한 거점이 안전하고 추적 중이 아닐 때 이용할 수 있습니다."), 15, Muted);
	AddButton(Content, TEXT("안전 캠프로 복귀"), TEXT("Travel.Safe.Camp"));
	AddButton(Content, TEXT("통신소로 이동"), TEXT("Travel.RadioStation")); AddButton(Content, TEXT("주요 거점으로 이동"), TEXT("Travel.Stronghold"));
	Content = PageContent[1]; EquipmentText = AddText(Content, TEXT(""), 16);
	AddText(Content, TEXT("설계도는 탐험 지점에서 발견합니다. 제작·장비 변경은 안전 지점에서 가능합니다."), 15, Muted);
	AddButton(Content, TEXT("소음기 제작 · 부품 10"), TEXT("Equipment.Suppressor"));
	AddButton(Content, TEXT("조준경 제작 · 부품 8"), TEXT("Equipment.Optic"));
	AddButton(Content, TEXT("산탄총 제작 / 장비 · 부품 15"), TEXT("Equipment.Shotgun"));
	AddButton(Content, TEXT("주무기: 소총 선택"), TEXT("Equipment.Rifle"));
	AddButton(Content, TEXT("현재 무기 탄약 보급 · 보급품 2"), TEXT("Equipment.Ammunition"));
	AddButton(Content, TEXT("회복 도구 제작"), TEXT("CraftMedkit")); AddButton(Content, TEXT("투척 도구 제작"), TEXT("CraftGrenade"));
	Content = PageContent[2];
	for (const FPFSkillDefinition& Skill : FPFGameRules::Skills())
	{
		UButton* Button = AddButton(Content, Skill.Name.ToString(), Skill.Id); SkillButtons.Add(Button); SkillTexts.Add(Cast<UTextBlock>(Button->GetContent()));
	}
	AddButton(Content, TEXT("안전 지점에서 스킬 재분배"), TEXT("Reset"));
	Content = PageContent[4];
	AddText(Content, TEXT("협동 연결"), 24, Accent);
	Address = WidgetTree->ConstructWidget<UEditableTextBox>(); Address->SetHintText(FText::FromString(TEXT("호스트 IP 주소 (예: 192.168.0.10)"))); Content->AddChildToVerticalBox(Address);
	AddButton(Content, TEXT("현재 월드로 LAN 방 열기 · 최대 4명"), TEXT("Host")); AddButton(Content, TEXT("주소로 참가"), TEXT("Join"));
	AddButton(Content, TEXT("저장 후 나가기"), TEXT("Leave"));
	Content = PageContent[3]; AddText(Content, TEXT("화면·조작"), 24, Accent); SettingsText = AddText(Content, TEXT(""), 15, Muted);
	const UPFUserSettings* InputSettings = UPFUserSettings::Get();
	const TArray<TPair<FName, FString>> Bindings = {
		{TEXT("MoveForward"), TEXT("앞으로")}, {TEXT("MoveBack"), TEXT("뒤로")}, {TEXT("MoveLeft"), TEXT("왼쪽")}, {TEXT("MoveRight"), TEXT("오른쪽")},
		{TEXT("FireKey"), TEXT("사격")}, {TEXT("Aim"), TEXT("조준")}, {TEXT("Sprint"), TEXT("질주")},
		{TEXT("InteractKey"), TEXT("상호작용")}, {TEXT("ReloadKey"), TEXT("재장전")}, {TEXT("TakedownKey"), TEXT("테이크다운")},
		{TEXT("Crouch"), TEXT("앉기")}, {TEXT("Heal"), TEXT("회복 도구")}, {TEXT("Grenade"), TEXT("투척 도구")},
		{TEXT("SwapWeapon"), TEXT("무기 교체")}, {TEXT("Recon"), TEXT("정찰")}, {TEXT("Jump"), TEXT("점프 / 장애물 넘기")}};
	for (const auto& Binding : Bindings)
	{
		AddText(Content, Binding.Value, 15, Muted);
		UInputKeySelector* Selector = WidgetTree->ConstructWidget<UInputKeySelector>(); Content->AddChildToVerticalBox(Selector);
		if (InputSettings)
		{
			Selector->SetSelectedKey(FInputChord(InputSettings->GetBinding(Binding.Key)));
		}
		UPFMenuAction* BindingAction = NewObject<UPFMenuAction>(this); BindingAction->Widget = this; BindingAction->Id = Binding.Key; BindingAction->Selector = Selector; MenuActions.Add(BindingAction);
		Selector->OnKeySelected.AddDynamic(BindingAction, &UPFMenuAction::Rebind);
	}
	AddButton(Content, TEXT("조작 키 기본값 복원"), TEXT("ResetBindings"));
	AddButton(Content, TEXT("시야각 변경"), TEXT("FOV")); AddButton(Content, TEXT("일반 감도 변경"), TEXT("Sensitivity"));
	AddButton(Content, TEXT("조준 감도 변경"), TEXT("AimSensitivity")); AddButton(Content, TEXT("UI 크기 변경"), TEXT("UIScale"));
	AddButton(Content, TEXT("조준 누르기 / 전환"), TEXT("ToggleAim")); AddButton(Content, TEXT("질주 누르기 / 전환"), TEXT("ToggleSprint"));
	AddButton(Content, TEXT("앉기 누르기 / 전환"), TEXT("ToggleCrouch"));
	AddButton(Content, TEXT("그래픽 품질 · 낮음 / 보통 / 높음 / 매우 높음"), TEXT("GraphicsQuality"));
	AddButton(Content, TEXT("카메라 반동 완화"), TEXT("Shake")); AddButton(Content, TEXT("모션 블러 켜기 / 끄기"), TEXT("MotionBlur"));
	Menu->SetVisibility(ESlateVisibility::Collapsed);
}

void UPFHUDWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime); RefreshElapsed += DeltaTime;
	if (RefreshElapsed >= .1f) { RefreshElapsed = 0.f; Refresh(); }
}

void UPFHUDWidget::Refresh()
{
	APFPlayerController* PC = GetOwningPlayer<APFPlayerController>(); if (!PC) return;
	Menu->SetVisibility(PC->bMenuOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	NoticeText->SetText(PC->StatusMessage);
	APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(PC->GetPawn());
	if (Player)
	{
		const UPFUserSettings* InputSettings = UPFUserSettings::Get();
		const FString UseKey = InputSettings ? InputSettings->InteractKey.GetDisplayName().ToString() : TEXT("E");
		const FString ExecuteKey = InputSettings ? InputSettings->TakedownKey.GetDisplayName().ToString() : TEXT("F");
		const FString ReconKey = InputSettings ? InputSettings->GetBinding(TEXT("Recon")).GetDisplayName().ToString() : TEXT("B");
		const FString ReloadKey = InputSettings ? InputSettings->ReloadKey.GetDisplayName().ToString() : TEXT("R");
		VitalText->SetText(FText::FromString(FString::Printf(TEXT("%s  %.0f / %.0f"), Player->Health->Life == EPFLifeState::Downed ? TEXT("[!] 소생 대기") : Player->Health->Life == EPFLifeState::Dead ? TEXT("[×] 재등장 대기") : TEXT("[+] 체력"), Player->Health->Current, Player->Health->Maximum)));
		HealthBar->SetPercent(Player->Health->Current / FMath::Max(1.f, Player->Health->Maximum));
		AmmoText->SetText(FText::FromString(FString::Printf(TEXT("%d / %d    회복 %d · 투척 %d"), Player->Weapon->State.Magazine, Player->Weapon->State.Reserve, Player->Medkits, Player->Grenades)));
		const TCHAR* WeaponName = Player->Weapon->State.Type == EPFWeaponType::Pistol ? TEXT("권총") : Player->Weapon->State.Type == EPFWeaponType::Shotgun ? TEXT("산탄총") : TEXT("소총");
		AmmoText->SetText(FText::FromString(FString::Printf(TEXT("%s  %02d / %d   회복 %d · 투척 %d"), WeaponName, Player->Weapon->State.Magazine, Player->Weapon->State.Reserve, Player->Medkits, Player->Grenades)));
		DetectionText->SetText(FText::FromString(Player->DetectionLevel > .01f ? FString::Printf(TEXT("%s  ·  탐지 %.0f%%"), Player->DetectionLevel >= 1.f ? TEXT("[!] 위치 노출") : TEXT("[◇] 시선을 피하세요"), Player->DetectionLevel * 100.f) : Player->bInCombat ? TEXT("[!] 추적 중 · 엄폐하고 적의 시야를 벗어나세요") : FString::Printf(TEXT("은밀 이동 가능 · %s 관찰"), *ReconKey)));
		FString Prompt;
		if (Player->Health->Life == EPFLifeState::Downed) Prompt = FString::Printf(TEXT("동료가 %s를 길게 눌러 소생할 수 있습니다."), *UseKey);
		else if (!Player->ActionName.IsNone()) Prompt = Player->ActionName == TEXT("Vault") ? TEXT("장애물 넘기") : Player->ActionName == TEXT("Healing") ? TEXT("회복 중") : Player->ActionName == TEXT("Takedown") ? TEXT("테이크다운") : TEXT("상호작용 중 · 이동/피해로 취소");
		else if (Player->Weapon->State.bReloading) Prompt = TEXT("재장전 중");
		else if (Player->Takedown->FindCandidate()) Prompt = FString::Printf(TEXT("[%s] 테이크다운"), *ExecuteKey);
		else if (AActor* Target = Player->Interaction->FindTarget())
		{
			if (APFOutpost* Outpost = Cast<APFOutpost>(Target)) Prompt = Outpost->RemainingThreats > 0 ? FString::Printf(TEXT("잔여 위협 %d · 안전 확보 필요"), Outpost->RemainingThreats) : FString::Printf(TEXT("[%s 길게] 거점 확보"), *UseKey);
			else if (APFInteractable* Device = Cast<APFInteractable>(Target)) Prompt = FString::Printf(TEXT("[%s 길게] "), *UseKey) + Device->DisplayName.ToString();
			else Prompt = FString::Printf(TEXT("[%s 길게] 동료 소생"), *UseKey);
		}
		else Prompt = FString::Printf(TEXT("%s 재장전 · %s 정찰 · %s 상호작용 · ESC 메뉴"), *ReloadKey, *ReconKey, *UseKey);
		PromptText->SetText(FText::FromString(Prompt));
		const float Now = GetWorld()->GetGameState() ? GetWorld()->GetGameState()->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
		ActionBar->SetPercent(Player->ActionEndTime > Player->ActionStartTime ? FMath::Clamp((Now - Player->ActionStartTime) / (Player->ActionEndTime - Player->ActionStartTime), 0.f, 1.f) : 0.f);
		ActionBar->SetVisibility(Player->ActionName.IsNone() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (const APFGameState* State = GetWorld()->GetGameState<APFGameState>())
	{
		FString Objectives;
		for (const FPFOutpostRecord& Outpost : State->Outposts)
		{
			const TCHAR* Phase = Outpost.Phase == EPFOutpostPhase::Captured ? TEXT("[✓] 확보") : Outpost.Phase == EPFOutpostPhase::Engaged ? TEXT("[!] 교전") : Outpost.Phase == EPFOutpostPhase::Discovered ? TEXT("[◇] 발견") : TEXT("[?] 미확인");
			const FString Distance = Player && Outpost.Phase != EPFOutpostPhase::Undiscovered ? FString::Printf(TEXT(" · %.0fm"), FVector::Distance(Player->GetActorLocation(), Outpost.MapLocation) / 100.f) : TEXT("");
			Objectives += FString::Printf(TEXT("%s  %s%s\n"), Phase, Outpost.Phase == EPFOutpostPhase::Undiscovered ? TEXT("미발견 시설") : *(Outpost.Title.IsEmpty() ? Outpost.Id.ToString() : Outpost.Title.ToString()), *Distance);
		}
		if (Player) for (TActorIterator<APFOutpost> It(GetWorld()); It; ++It)
		{
			if (It->Phase != EPFOutpostPhase::Engaged || Player->GetDistanceTo(*It) > 8500.f) continue;
			const float Now = State->GetServerWorldTimeSeconds();
			Objectives += FString::Printf(TEXT("잔여 위협 %d  ·  %s\n"), It->RemainingThreats, It->RemainingThreats == 0 ? TEXT("시설을 조작해 확보") : TEXT("거점 위협 제거"));
			if (It->AlarmStartTime > 0.f) Objectives += FString::Printf(TEXT("[!] 경보 호출 중 %.1fs · 호출자를 저지하세요\n"), FMath::Max(0.f, 4.f - Now + It->AlarmStartTime));
			else if (It->ReinforcementTime > Now) Objectives += FString::Printf(TEXT("[!] 경보 전송됨 · 증원 진입까지 %.0fs\n"), It->ReinforcementTime - Now);
			else if (It->Alarm && It->Alarm->bDestroyed) Objectives += TEXT("[✓] 경보 장치 무력화\n");
		}
		ObjectiveText->SetText(FText::FromString(Objectives));
		FString Team;
		for (int32 Index = 0; Index < State->PlayerArray.Num(); ++Index)
		{
			const APlayerState* Mate = State->PlayerArray[Index];
			const APFPlayerState* SquadMate = Cast<APFPlayerState>(Mate);
			const TCHAR* Status = !SquadMate || !SquadMate->bJoinedWorld ? TEXT("접속 중") : SquadMate->TeamLife == EPFLifeState::Downed ? TEXT("[!] 소생 필요") : SquadMate->TeamLife == EPFLifeState::Dead ? TEXT("[×] 재등장 대기") : TEXT("[+] 활동");
			Team += FString::Printf(TEXT("%d · %s  %s  %.0fms\n"), Index + 1, *Mate->GetPlayerName().Left(12), Status, Mate->GetPingInMilliseconds());
		}
		TeamText->SetText(FText::FromString(Team));
	}
	if (APFPlayerState* State = PC->GetPlayerState<APFPlayerState>())
	{
		ProgressionText->SetText(FText::FromString(FString::Printf(TEXT("레벨 %d  ·  경험치 %d  ·  스킬 포인트 %d  ·  부품 %d  ·  보급품 %d"), FPFGameRules::LevelForExperience(State->Profile.Experience), State->Profile.Experience, State->GetSkillPoints(), State->Profile.Parts, State->Profile.Supplies)));
		FString Gear = TEXT("설계도 / 장비\n");
		for (const TPair<FName, FString>& Entry : TArray<TPair<FName, FString>>{{TEXT("Suppressor"), TEXT("소음기")}, {TEXT("Optic"), TEXT("조준경")}, {TEXT("Shotgun"), TEXT("산탄총")}})
			Gear += Entry.Value + (State->Profile.Equipment.Contains(Entry.Key) ? TEXT("  [✓] 제작 완료\n") : State->Profile.Blueprints.Contains(Entry.Key) ? TEXT("  [◇] 제작 가능\n") : TEXT("  [?] 설계도 미발견\n"));
		EquipmentText->SetText(FText::FromString(Gear));
		const auto& Skills = FPFGameRules::Skills();
		for (int32 Index = 0; Index < Skills.Num(); ++Index)
		{
			const bool Owned = State->HasSkill(Skills[Index].Id);
			SkillTexts[Index]->SetText(FText::FromString(FString::Printf(TEXT("%s %s  ·  %d점  /  %s"), Owned ? TEXT("[✓]") : TEXT("[+]") , *Skills[Index].Name.ToString(), Skills[Index].Cost, *Skills[Index].Description.ToString())));
			SkillButtons[Index]->SetIsEnabled(FPFGameRules::CanUnlock(State->Profile, Skills[Index].Id));
		}
	}
	if (const UPFUserSettings* Settings = UPFUserSettings::Get())
	{
		const TArray<FString> Quality = {TEXT("낮음"), TEXT("보통"), TEXT("높음"), TEXT("매우 높음"), TEXT("최상")};
		const int32 Preset = Settings->GetOverallScalabilityLevel();
		SettingsText->SetText(FText::FromString(FString::Printf(TEXT("시야각 %.0f · 일반 감도 %.1f · 조준 감도 %.1f · UI %.0f%% · 카메라 %.0f%% · 그래픽 %s"), Settings->FieldOfView, Settings->LookSensitivity, Settings->AimSensitivity, Settings->UIScale * 100.f, Settings->ShakeScale * 100.f, Preset >= 0 && Preset < Quality.Num() ? *Quality[Preset] : TEXT("사용자 설정"))));
	}
}

void UPFHUDWidget::Dispatch(FName Id)
{
	APFPlayerController* PC = GetOwningPlayer<APFPlayerController>(); if (!PC) return;
	if (Id.ToString().StartsWith(TEXT("Page."))) Pages->SetActiveWidgetIndex(FCString::Atoi(*Id.ToString().RightChop(5)));
	else if (Id.ToString().StartsWith(TEXT("Equipment."))) PC->ServerPrepareEquipment(FName(*Id.ToString().RightChop(10)));
	else if (FPFGameRules::FindSkill(Id)) PC->ServerUnlockSkill(Id);
	else if (Id == TEXT("Reset")) PC->ServerResetSkills();
	else if (Id == TEXT("CraftMedkit")) PC->ServerCraft(true);
	else if (Id == TEXT("CraftGrenade")) PC->ServerCraft(false);
	else if (Id == TEXT("Host")) PC->PFHost();
	else if (Id == TEXT("Join")) PC->PFJoin(Address->GetText().ToString());
	else if (Id == TEXT("Leave")) PC->ServerSaveAndLeave();
	else if (Id.ToString().StartsWith(TEXT("Travel."))) PC->ServerFastTravel(FName(*Id.ToString().RightChop(7)));
	else if (UPFUserSettings* Settings = UPFUserSettings::Get())
	{
		if (Id == TEXT("ResetBindings"))
		{
			Settings->ResetBindings();
			for (UPFMenuAction* Action : MenuActions) if (Action->Selector) Action->Selector->SetSelectedKey(FInputChord(Settings->GetBinding(Action->Id)));
			if (APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(PC->GetPawn())) Player->RebuildLocalMapping();
		}
		if (Id == TEXT("FOV")) Settings->FieldOfView = Settings->FieldOfView >= 110.f ? 70.f : Settings->FieldOfView + 10.f;
		if (Id == TEXT("Sensitivity")) Settings->LookSensitivity = Settings->LookSensitivity >= 2.f ? .5f : Settings->LookSensitivity + .25f;
		if (Id == TEXT("AimSensitivity")) Settings->AimSensitivity = Settings->AimSensitivity >= 1.5f ? .25f : Settings->AimSensitivity + .25f;
		if (Id == TEXT("UIScale")) Settings->UIScale = Settings->UIScale >= 1.2f ? .8f : Settings->UIScale + .1f;
		if (Id == TEXT("ToggleAim")) Settings->bToggleAim = !Settings->bToggleAim;
		if (Id == TEXT("ToggleSprint")) Settings->bToggleSprint = !Settings->bToggleSprint;
		if (Id == TEXT("ToggleCrouch")) Settings->bToggleCrouch = !Settings->bToggleCrouch;
		if (Id == TEXT("Shake")) Settings->ShakeScale = Settings->ShakeScale > 0.f ? 0.f : 1.f;
		if (Id == TEXT("MotionBlur")) Settings->bMotionBlur = !Settings->bMotionBlur;
		if (Id == TEXT("GraphicsQuality")) Settings->SetOverallScalabilityLevel((FMath::Max(0, Settings->GetOverallScalabilityLevel()) + 1) % 4);
		Settings->ApplySettings(false);
		if (APFPlayerCharacter* Player = Cast<APFPlayerCharacter>(PC->GetPawn())) Player->Camera->SetFieldOfView(Settings->FieldOfView * (Player->bAiming ? .8f : 1.f));
	}
	Refresh();
}

void UPFHUDWidget::FocusMenu()
{
	SetKeyboardFocus();
	if (FirstTab) FirstTab->SetKeyboardFocus();
}

int32 UPFHUDWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style, bool bEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, Geometry, CullingRect, Elements, LayerId, Style, bEnabled) + 1;
	const APFPlayerController* PC = GetOwningPlayer<APFPlayerController>();
	const APFPlayerCharacter* Player = PC ? Cast<APFPlayerCharacter>(PC->GetPawn()) : nullptr;
	if (!Player || PC->bMenuOpen) return Layer;
	const FVector2D Center = Geometry.GetLocalSize() * .5f;
	const float Now = GetWorld()->GetGameState() ? GetWorld()->GetGameState()->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	auto DirectionArc = [&](const FVector& Source, float Strength, FLinearColor Color, float Radius)
	{
		const float Angle = FMath::DegreesToRadians((Source - Player->GetActorLocation()).Rotation().Yaw - PC->GetControlRotation().Yaw - 90.f);
		TArray<FVector2D> Points;
		for (int32 Step = 0; Step <= 12; ++Step)
		{
			const float A = Angle + FMath::DegreesToRadians(-18.f + Step * 3.f);
			Points.Add(Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius);
		}
		Color.A = FMath::Clamp(Strength, 0.f, 1.f);
		FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, 4.f);
	};
	if (Player->DetectionLevel > .01f) DirectionArc(Player->DetectionSource, Player->DetectionLevel, Player->DetectionLevel >= 1.f ? FLinearColor(1.f, .3f, .13f) : FLinearColor(.95f, .85f, .48f), 95.f);
	if (Now - Player->LastDamageTime < 1.2f) DirectionArc(Player->DamageSource, 1.f - (Now - Player->LastDamageTime) / 1.2f, FLinearColor(1.f, .14f, .1f), 122.f);
	if (GetWorld()->GetTimeSeconds() - Player->Weapon->LastConfirmedHitTime < .2f)
	{
		const FLinearColor Color = Player->Weapon->LastHitKind == 3 ? Accent : Player->Weapon->LastHitKind == 2 ? FLinearColor(.5f, .8f, 1.f) : FLinearColor::White;
		for (int32 X : {-1, 1}) for (int32 Y : {-1, 1})
			FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), {Center + FVector2D(X * 6.f, Y * 6.f), Center + FVector2D(X * 12.f, Y * 12.f)}, ESlateDrawEffect::None, Color, true, 2.f);
	}
	return Layer;
}

FReply UPFHUDWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape || Event.GetKey() == EKeys::Gamepad_FaceButton_Right)
	{
		if (APFPlayerController* PC = GetOwningPlayer<APFPlayerController>()) { PC->ToggleMenu(); return FReply::Handled(); }
	}
	return Super::NativeOnKeyDown(Geometry, Event);
}
