#include "Framework/PFGameInstance.h"
#include "Framework/PFPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

void UPFGameInstance::Init()
{
	Super::Init();
	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::OnNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::OnTravelFailure);
	}
}

void UPFGameInstance::Shutdown()
{
	if (GEngine) { GEngine->OnNetworkFailure().Remove(NetworkFailureHandle); GEngine->OnTravelFailure().Remove(TravelFailureHandle); }
	Super::Shutdown();
}

void UPFGameInstance::OnNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error)
{
	if (World && World->GetGameInstance() != this) return;
	ConnectionMessage = FText::FromString(Type == ENetworkFailure::ConnectionLost || Type == ENetworkFailure::ConnectionTimeout
		? TEXT("연결이 끊겼습니다. 호스트의 마지막 정상 저장 이후 진행은 복원되지 않을 수 있습니다.")
		: TEXT("방에 연결하지 못했습니다. 주소·방의 상태·게임 버전을 확인하세요. ") + Error);
	if (APFPlayerController* PC = Cast<APFPlayerController>(GetFirstLocalPlayerController())) PC->StatusMessage = ConnectionMessage;
}

void UPFGameInstance::OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error)
{
	if (World && World->GetGameInstance() != this) return;
	ConnectionMessage = FText::FromString(TEXT("월드를 불러오지 못했습니다. ") + Error);
	if (APFPlayerController* PC = Cast<APFPlayerController>(GetFirstLocalPlayerController())) PC->StatusMessage = ConnectionMessage;
}
