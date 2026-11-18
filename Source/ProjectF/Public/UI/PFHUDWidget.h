#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Framework/Commands/InputChord.h"
#include "PFHUDWidget.generated.h"

class UTextBlock;
class UProgressBar;
class UBorder;
class UVerticalBox;
class UEditableTextBox;
class UButton;
class UWidgetSwitcher;
class UInputKeySelector;
class UPFHUDWidget;

UCLASS()
class PROJECTF_API UPFMenuAction : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY() TObjectPtr<UPFHUDWidget> Widget;
	UPROPERTY() TObjectPtr<UInputKeySelector> Selector;
	FName Id;
	UFUNCTION() void Execute();
	UFUNCTION() void Rebind(FInputChord Chord);
};

UCLASS(Blueprintable)
class PROJECTF_API UPFHUDWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	void Dispatch(FName Id);
	void Refresh();
	void FocusMenu();
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style, bool bEnabled) const override;
private:
	UPROPERTY() TObjectPtr<UTextBlock> VitalText;
	UPROPERTY() TObjectPtr<UTextBlock> AmmoText;
	UPROPERTY() TObjectPtr<UTextBlock> ObjectiveText;
	UPROPERTY() TObjectPtr<UTextBlock> TeamText;
	UPROPERTY() TObjectPtr<UTextBlock> PromptText;
	UPROPERTY() TObjectPtr<UTextBlock> NoticeText;
	UPROPERTY() TObjectPtr<UTextBlock> ProgressionText;
	UPROPERTY() TObjectPtr<UTextBlock> SettingsText;
	UPROPERTY() TObjectPtr<UTextBlock> DetectionText;
	UPROPERTY() TObjectPtr<UTextBlock> EquipmentText;
	UPROPERTY() TObjectPtr<UWidgetSwitcher> Pages;
	UPROPERTY() TObjectPtr<UButton> FirstTab;
	UPROPERTY() TObjectPtr<UProgressBar> HealthBar;
	UPROPERTY() TObjectPtr<UProgressBar> ActionBar;
	UPROPERTY() TObjectPtr<UBorder> Menu;
	UPROPERTY() TObjectPtr<UEditableTextBox> Address;
	UPROPERTY() TArray<TObjectPtr<UPFMenuAction>> MenuActions;
	UPROPERTY() TArray<TObjectPtr<UButton>> SkillButtons;
	UPROPERTY() TArray<TObjectPtr<UTextBlock>> SkillTexts;
	float RefreshElapsed = 0.f;
	UTextBlock* AddText(UVerticalBox* Parent, const FString& Text, int32 Size, FLinearColor Color = FLinearColor::White);
	UButton* AddButton(UVerticalBox* Parent, const FString& Label, FName Id);
};
