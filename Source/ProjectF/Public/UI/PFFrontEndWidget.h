#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PFFrontEndWidget.generated.h"

class UPFFrontEndWidget;
class UVerticalBox;
class UButton;
class UTextBlock;
class UEditableTextBox;
class UWidgetSwitcher;

UCLASS()
class PROJECTF_API UPFFrontEndAction : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY() TObjectPtr<UPFFrontEndWidget> Widget;
	FName Id;
	UFUNCTION() void Execute();
};

UCLASS()
class PROJECTF_API UPFFrontEndWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	void Dispatch(FName Id);
	FName GetFocusedAction() const;
private:
	UPROPERTY() TObjectPtr<UWidgetSwitcher> Pages;
	UPROPERTY() TObjectPtr<UEditableTextBox> Address;
	UPROPERTY() TObjectPtr<UTextBlock> Status;
	UPROPERTY() TObjectPtr<UTextBlock> SettingsLabel;
	UPROPERTY() TArray<TObjectPtr<UPFFrontEndAction>> Actions;
	UPROPERTY() TArray<TObjectPtr<UButton>> Buttons;
	UPROPERTY() TArray<TObjectPtr<UButton>> PageFocus;
	float RefreshTime = 0.f;
	UTextBlock* Text(UVerticalBox* Box, const FString& Label, int32 Size, FLinearColor Color);
	UButton* Button(UVerticalBox* Box, const FString& Label, FName Id);
	void ShowPage(int32 Page);
	void Refresh();
};
