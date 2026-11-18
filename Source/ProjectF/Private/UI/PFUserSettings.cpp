#include "UI/PFUserSettings.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Engine/UserInterfaceSettings.h"

UPFUserSettings* UPFUserSettings::Get() { return GEngine ? Cast<UPFUserSettings>(GEngine->GetGameUserSettings()) : nullptr; }

const TArray<TPair<FName, FKey>>& UPFUserSettings::DefaultBindings()
{
	static const TArray<TPair<FName, FKey>> Defaults = {
		{TEXT("MoveForward"), EKeys::W}, {TEXT("MoveBack"), EKeys::S}, {TEXT("MoveLeft"), EKeys::A}, {TEXT("MoveRight"), EKeys::D},
		{TEXT("FireKey"), EKeys::LeftMouseButton}, {TEXT("Aim"), EKeys::RightMouseButton}, {TEXT("Sprint"), EKeys::LeftShift},
		{TEXT("InteractKey"), EKeys::E}, {TEXT("ReloadKey"), EKeys::R}, {TEXT("TakedownKey"), EKeys::F},
		{TEXT("Crouch"), EKeys::C}, {TEXT("Heal"), EKeys::H}, {TEXT("Grenade"), EKeys::G},
		{TEXT("SwapWeapon"), EKeys::Q}, {TEXT("Recon"), EKeys::B}, {TEXT("Jump"), EKeys::SpaceBar}};
	return Defaults;
}

FKey UPFUserSettings::GetBinding(FName Action) const
{
	if (Action == TEXT("FireKey")) return FireKey;
	if (Action == TEXT("InteractKey")) return InteractKey;
	if (Action == TEXT("ReloadKey")) return ReloadKey;
	if (Action == TEXT("TakedownKey")) return TakedownKey;
	if (const FKey* Key = AdditionalBindings.Find(Action)) return *Key;
	for (const auto& Entry : DefaultBindings()) if (Entry.Key == Action) return Entry.Value;
	return FKey();
}

bool UPFUserSettings::RebindAction(FName Action, FKey Key)
{
	if (!Key.IsValid() || Key == EKeys::Escape || Key.IsGamepadKey() || Key.IsAxis1D() || Key.IsAxis2D() || Key.IsAxis3D()) return false;
	bool bKnown = false;
	for (const auto& Entry : DefaultBindings())
	{
		bKnown |= Entry.Key == Action;
		if (Entry.Key != Action && GetBinding(Entry.Key) == Key) return false;
	}
	if (!bKnown) return false;
	if (Action == TEXT("FireKey")) FireKey = Key;
	else if (Action == TEXT("InteractKey")) InteractKey = Key;
	else if (Action == TEXT("ReloadKey")) ReloadKey = Key;
	else if (Action == TEXT("TakedownKey")) TakedownKey = Key;
	else AdditionalBindings.Add(Action, Key);
	return true;
}

void UPFUserSettings::ResetBindings()
{
	AdditionalBindings.Empty(); FireKey = EKeys::LeftMouseButton; InteractKey = EKeys::E; ReloadKey = EKeys::R; TakedownKey = EKeys::F;
}

void UPFUserSettings::ApplySettings(bool CheckCommandLine)
{
	LookSensitivity = FMath::Clamp(LookSensitivity, .1f, 5.f); AimSensitivity = FMath::Clamp(AimSensitivity, .1f, 2.f);
	FieldOfView = FMath::Clamp(FieldOfView, 70.f, 110.f); UIScale = FMath::Clamp(UIScale, .8f, 1.5f); ShakeScale = FMath::Clamp(ShakeScale, 0.f, 1.f);
	Super::ApplySettings(CheckCommandLine);
	SaveSettings();
}

void UPFUserSettings::ApplyNonResolutionSettings()
{
	Super::ApplyNonResolutionSettings();
	GetMutableDefault<UUserInterfaceSettings>()->ApplicationScale = FMath::IsFinite(UIScale) ? FMath::Clamp(UIScale, .8f, 1.5f) : 1.f;
	if (IConsoleVariable* Blur = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality"))) Blur->Set(bMotionBlur ? 3 : 0, ECVF_SetByGameSetting);
	if (IConsoleVariable* Feature = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.MotionBlur"))) Feature->Set(bMotionBlur ? 1 : 0, ECVF_SetByGameOverride);
}
