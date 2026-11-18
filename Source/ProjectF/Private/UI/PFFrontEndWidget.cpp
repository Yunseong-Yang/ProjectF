#include "UI/PFFrontEndWidget.h"
#include "UI/PFUserSettings.h"
#include "Framework/PFPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Brushes/SlateColorBrush.h"

namespace
{
	const FLinearColor Ink(.018f, .032f, .024f, .96f);
	const FLinearColor Paper(.83f, .89f, .85f);
	const FLinearColor Ochre(.65f, .43f, .15f);
	const FLinearColor Quiet(.46f, .59f, .51f);
}

void UPFFrontEndAction::Execute() { if (Widget) Widget->Dispatch(Id); }

UTextBlock* UPFFrontEndWidget::Text(UVerticalBox* Box, const FString& Label, int32 Size, FLinearColor Color)
{
	UTextBlock* Entry = WidgetTree->ConstructWidget<UTextBlock>();
	Entry->SetText(FText::FromString(Label)); Entry->SetColorAndOpacity(Color); Entry->SetAutoWrapText(true);
	FSlateFontInfo Font = Entry->GetFont(); Font.Size = Size; Entry->SetFont(Font);
	Box->AddChildToVerticalBox(Entry)->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f)); return Entry;
}

UButton* UPFFrontEndWidget::Button(UVerticalBox* Box, const FString& Label, FName Id)
{
	UButton* Entry = WidgetTree->ConstructWidget<UButton>();
	FButtonStyle Style;
	Style.SetNormal(FSlateColorBrush(FLinearColor::White));
	Style.SetHovered(FSlateColorBrush(FLinearColor(.8f, .9f, .8f)));
	Style.SetPressed(FSlateColorBrush(FLinearColor(.6f, .7f, .6f)));
	Entry->SetStyle(Style);
	Entry->SetBackgroundColor(FLinearColor(.055f, .085f, .065f));
	UTextBlock* Caption = WidgetTree->ConstructWidget<UTextBlock>(); Caption->SetText(FText::FromString(Label));
	Caption->SetColorAndOpacity(Paper); Caption->SetJustification(ETextJustify::Left); Caption->SetMargin(FMargin(18.f, 8.f));
	FSlateFontInfo Font = Caption->GetFont(); Font.Size = 18; Caption->SetFont(Font); Entry->AddChild(Caption);
	Box->AddChildToVerticalBox(Entry)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f)); Buttons.Add(Entry);
	UPFFrontEndAction* Action = NewObject<UPFFrontEndAction>(this); Action->Widget = this; Action->Id = Id; Actions.Add(Action);
	Entry->OnClicked.AddDynamic(Action, &UPFFrontEndAction::Execute); return Entry;
}

void UPFFrontEndWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized(); SetIsFocusable(true);
	UScaleBox* Scale = WidgetTree->ConstructWidget<UScaleBox>(); Scale->SetStretch(EStretch::ScaleToFit); WidgetTree->RootWidget = Scale;
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(); Size->SetWidthOverride(1280); Size->SetHeightOverride(720); Scale->AddChild(Size);
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(); Size->AddChild(Canvas);
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(); Panel->SetBrushColor(Ink); Panel->SetPadding(FMargin(30.f));
	UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(Panel); CanvasSlot->SetPosition(FVector2D(64, 66)); CanvasSlot->SetSize(FVector2D(450, 590));
	UVerticalBox* Shell = WidgetTree->ConstructWidget<UVerticalBox>(); Panel->AddChild(Shell);
	Text(Shell, TEXT("Project F"), 54, Paper);
	Text(Shell, TEXT("산악 국경지대"), 20, Quiet);
	Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>(); Shell->AddChildToVerticalBox(Pages);
	TArray<UVerticalBox*> Content;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>(); Pages->AddChild(Page); Content.Add(Page);
	}
	Text(Content[0], TEXT("통신소를 정찰하고, 경보를 차단하고, 산악 보급로를 확보하세요."), 17, Paper);
	PageFocus.Add(Button(Content[0], TEXT("현장으로 · 이어하기"), TEXT("Continue")));
	Button(Content[0], TEXT("협동 방 열기"), TEXT("Host"));
	Button(Content[0], TEXT("협동 참가"), TEXT("JoinPage"));
	Button(Content[0], TEXT("화면 설정"), TEXT("SettingsPage"));
	Button(Content[0], TEXT("게임 종료"), TEXT("Quit"));
	Text(Content[1], TEXT("호스트 주소"), 22, Paper);
	Text(Content[1], TEXT("같은 네트워크의 방에 참가합니다. 인터넷 연결은 호스트의 네트워크 설정이 필요합니다."), 16, Quiet);
	Address = WidgetTree->ConstructWidget<UEditableTextBox>(); Address->SetHintText(FText::FromString(TEXT("예: 192.168.0.10:7777")));
	Content[1]->AddChildToVerticalBox(Address)->SetPadding(FMargin(0, 4, 0, 16));
	PageFocus.Add(Button(Content[1], TEXT("주소로 참가"), TEXT("Join")));
	Button(Content[1], TEXT("뒤로"), TEXT("Back"));
	SettingsLabel = Text(Content[2], TEXT(""), 17, Paper);
	PageFocus.Add(Button(Content[2], TEXT("그래픽 품질 변경"), TEXT("Quality")));
	Button(Content[2], TEXT("시야각 변경"), TEXT("FOV"));
	Button(Content[2], TEXT("모션 블러 켜기 / 끄기"), TEXT("Blur"));
	Button(Content[2], TEXT("뒤로"), TEXT("Back"));
	Status = Text(Shell, TEXT(""), 15, FLinearColor(.88f, .68f, .35f));
	Text(Shell, TEXT("↑↓ 선택   Enter 확인   Esc 뒤로"), 13, Quiet);
	Refresh();
}

void UPFFrontEndWidget::NativeConstruct() { Super::NativeConstruct(); ShowPage(0); }

void UPFFrontEndWidget::ShowPage(int32 Page)
{
	if (!Pages || !PageFocus.IsValidIndex(Page)) return;
	Pages->SetActiveWidgetIndex(Page); PageFocus[Page]->SetUserFocus(GetOwningPlayer());
}

FName UPFFrontEndWidget::GetFocusedAction() const
{
	for (int32 Index = 0; Index < Buttons.Num(); ++Index)
		if (Buttons[Index]->HasKeyboardFocus() || Buttons[Index]->HasUserFocus(GetOwningPlayer())) return Actions[Index]->Id;
	return NAME_None;
}

void UPFFrontEndWidget::Refresh()
{
	if (const APFPlayerController* PC = GetOwningPlayer<APFPlayerController>()) Status->SetText(PC->StatusMessage);
	if (const UPFUserSettings* Settings = UPFUserSettings::Get())
	{
		static const TCHAR* Names[] = {TEXT("낮음"), TEXT("보통"), TEXT("높음"), TEXT("매우 높음")};
		const int32 Quality = Settings->GetOverallScalabilityLevel();
		SettingsLabel->SetText(FText::FromString(FString::Printf(TEXT("그래픽 %s\n시야각 %.0f°\n모션 블러 %s"), Quality >= 0 && Quality < 4 ? Names[Quality] : TEXT("사용자 설정"), Settings->FieldOfView, Settings->bMotionBlur ? TEXT("켜짐") : TEXT("꺼짐"))));
	}
	for (UButton* Entry : Buttons)
	{
		const bool bFocused = Entry->HasKeyboardFocus() || Entry->HasUserFocus(GetOwningPlayer());
		Entry->SetBackgroundColor(bFocused ? Ochre : FLinearColor(.055f, .085f, .065f));
		if (UTextBlock* Caption = Cast<UTextBlock>(Entry->GetContent())) Caption->SetColorAndOpacity(bFocused ? Ink : Paper);
	}
}

void UPFFrontEndWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime); RefreshTime += DeltaTime;
	if (RefreshTime >= .1f) { RefreshTime = 0.f; Refresh(); }
}

void UPFFrontEndWidget::Dispatch(FName Id)
{
	APFPlayerController* PC = GetOwningPlayer<APFPlayerController>(); if (!PC) return;
	if (Id == TEXT("Continue")) PC->StartExpedition();
	else if (Id == TEXT("Host")) PC->PFHost();
	else if (Id == TEXT("JoinPage")) ShowPage(1);
	else if (Id == TEXT("SettingsPage")) ShowPage(2);
	else if (Id == TEXT("Back")) ShowPage(0);
	else if (Id == TEXT("Join")) PC->PFJoin(Address->GetText().ToString().TrimStartAndEnd());
	else if (Id == TEXT("Quit")) UKismetSystemLibrary::QuitGame(this, PC, EQuitPreference::Quit, false);
	else if (UPFUserSettings* Settings = UPFUserSettings::Get())
	{
		if (Id == TEXT("Quality")) Settings->SetOverallScalabilityLevel((FMath::Max(0, Settings->GetOverallScalabilityLevel()) + 1) % 4);
		if (Id == TEXT("FOV")) Settings->FieldOfView = Settings->FieldOfView >= 110 ? 70 : Settings->FieldOfView + 10;
		if (Id == TEXT("Blur")) Settings->bMotionBlur = !Settings->bMotionBlur;
		Settings->ApplySettings(false);
	}
	Refresh();
}

FReply UPFFrontEndWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
	if (Event.GetKey() == EKeys::Escape || Event.GetKey() == EKeys::Gamepad_FaceButton_Right)
	{
		ShowPage(0); return FReply::Handled();
	}
	return Super::NativeOnKeyDown(Geometry, Event);
}
