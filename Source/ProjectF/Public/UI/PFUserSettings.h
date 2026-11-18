#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "InputCoreTypes.h"
#include "PFUserSettings.generated.h"

UCLASS(Config=GameUserSettings)
class PROJECTF_API UPFUserSettings : public UGameUserSettings
{
	GENERATED_BODY()
public:
	UPROPERTY(Config, BlueprintReadWrite, Category="Accessibility") float LookSensitivity = 1.f;
	UPROPERTY(Config, BlueprintReadWrite, Category="Accessibility") float AimSensitivity = .6f;
	UPROPERTY(Config, BlueprintReadWrite, Category="Accessibility") float FieldOfView = 90.f;
	UPROPERTY(Config, BlueprintReadWrite, Category="Accessibility") float UIScale = 1.f;
	UPROPERTY(Config, BlueprintReadWrite, Category="Accessibility") float ShakeScale = 1.f;
	UPROPERTY(Config, BlueprintReadWrite, Category="Accessibility") bool bToggleAim = false;
	UPROPERTY(Config, BlueprintReadWrite, Category="Accessibility") bool bToggleSprint = false;
	UPROPERTY(Config, BlueprintReadWrite, Category="Accessibility") bool bToggleCrouch = true;
	UPROPERTY(Config, BlueprintReadWrite, Category="Accessibility") bool bMotionBlur = false;
	UPROPERTY(Config, BlueprintReadWrite, Category="Input") FKey FireKey = EKeys::LeftMouseButton;
	UPROPERTY(Config, BlueprintReadWrite, Category="Input") FKey InteractKey = EKeys::E;
	UPROPERTY(Config, BlueprintReadWrite, Category="Input") FKey ReloadKey = EKeys::R;
	UPROPERTY(Config, BlueprintReadWrite, Category="Input") FKey TakedownKey = EKeys::F;
	UPROPERTY(Config) TMap<FName, FKey> AdditionalBindings;
	static const TArray<TPair<FName, FKey>>& DefaultBindings();
	FKey GetBinding(FName Action) const;
	bool RebindAction(FName Action, FKey Key);
	void ResetBindings();
	virtual void ApplySettings(bool CheckCommandLine) override;
	virtual void ApplyNonResolutionSettings() override;
	static UPFUserSettings* Get();
};
