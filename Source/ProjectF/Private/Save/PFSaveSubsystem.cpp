#include "Save/PFSaveSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Crc.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
	constexpr uint32 SaveMagic = 0x50465356;
	constexpr int32 MaximumSaveBytes = 16 * 1024 * 1024;
	FString SavePath(const FString& Slot) { return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), Slot + TEXT(".pfs")); }
}

void UPFSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	WorldSave = Read(TEXT("World"));
	if (!WorldSave) { WorldSave = NewObject<UPFSaveGame>(this); WorldSave->WorldId = FGuid::NewGuid(); }
	PersonalSave = Read(TEXT("Personal"));
	if (!PersonalSave)
	{
		PersonalSave = NewObject<UPFSaveGame>(this);
		PersonalSave->LocalProfile.PlayerId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	}
}

bool UPFSaveSubsystem::Decode(const TArray<uint8>& Bytes, TArray<uint8>& Payload)
{
	Payload.Reset();
	if (Bytes.Num() < 16 || Bytes.Num() > MaximumSaveBytes) return false;
	FMemoryReader Reader(Bytes, true);
	uint32 Magic = 0, Crc = 0; int32 Version = 0, Size = 0;
	Reader << Magic << Version << Size << Crc;
	if (Magic != SaveMagic || Version != FPFGameRules::SaveVersion || Size <= 0 || Size != Bytes.Num() - Reader.Tell()) return false;
	Payload.SetNumUninitialized(Size); Reader.Serialize(Payload.GetData(), Size);
	if (Reader.IsError() || FCrc::MemCrc32(Payload.GetData(), Size) != Crc) { Payload.Reset(); return false; }
	return true;
}

UPFSaveGame* UPFSaveSubsystem::Read(const FString& Slot)
{
	const FString Path = SavePath(Slot);
	const bool bExists = IFileManager::Get().FileExists(*Path) || IFileManager::Get().FileExists(*(Path + TEXT(".bak")));
	for (const FString& Candidate : {Path, Path + TEXT(".bak")})
	{
		const int64 FileSize = IFileManager::Get().FileSize(*Candidate);
		if (FileSize <= 0 || FileSize > MaximumSaveBytes) continue;
		TArray<uint8> Bytes, Payload;
		if (!FFileHelper::LoadFileToArray(Bytes, *Candidate) || !Decode(Bytes, Payload)) continue;
		UPFSaveGame* Result = Cast<UPFSaveGame>(UGameplayStatics::LoadGameFromMemory(Payload));
		if (!Result || Result->FormatVersion != FPFGameRules::SaveVersion || Result->Outposts.Num() > 10000 || Result->Profiles.Num() > 1000) continue;
		bool bValid = true;
		for (FPFProfile& Profile : Result->Profiles) { FPFGameRules::UpgradeProfile(Profile); bValid &= FPFGameRules::ValidateProfile(Profile); }
		if (Slot == TEXT("Personal")) FPFGameRules::UpgradeProfile(Result->LocalProfile);
		if (Slot == TEXT("Personal")) bValid &= FPFGameRules::ValidateProfile(Result->LocalProfile);
		if (!bValid) continue;
		if (Candidate.EndsWith(TEXT(".bak"))) LastError = FText::FromString(TEXT("최근 저장을 읽지 못해 이전 정상 저장을 복원했습니다."));
		return Result;
	}
	if (bExists)
	{
		LastError = FText::FromString(TEXT("저장이 손상되었거나 호환되지 않습니다. 원본을 보존하며 새 진행의 덮어쓰기를 막았습니다."));
		if (Slot == TEXT("World")) bWorldWritable = false; else bPersonalWritable = false;
	}
	return nullptr;
}

bool UPFSaveSubsystem::Write(const FString& Slot, UPFSaveGame* Save)
{
	if (!Save || Save->FormatVersion != FPFGameRules::SaveVersion) return false;
	TArray<uint8> Payload;
	if (!UGameplayStatics::SaveGameToMemory(Save, Payload) || Payload.Num() > MaximumSaveBytes - 16) return false;
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes, true);
	uint32 Magic = SaveMagic, Crc = FCrc::MemCrc32(Payload.GetData(), Payload.Num());
	int32 Version = FPFGameRules::SaveVersion, Size = Payload.Num();
	Writer << Magic << Version << Size << Crc; Writer.Serialize(Payload.GetData(), Size); Writer.Close();
	const FString Path = SavePath(Slot), Temp = Path + TEXT(".tmp"), Backup = Path + TEXT(".bak");
	IFileManager& Files = IFileManager::Get();
	Files.MakeDirectory(*FPaths::GetPath(Path), true);
	if (!FFileHelper::SaveArrayToFile(Bytes, *Temp)) { LastError = FText::FromString(TEXT("저장 파일을 기록하지 못했습니다.")); return false; }
	TArray<uint8> Check, Decoded;
	if (!FFileHelper::LoadFileToArray(Check, *Temp) || !Decode(Check, Decoded)) return false;
	TArray<uint8> Previous;
	if (Files.FileExists(*Path) && FFileHelper::LoadFileToArray(Previous, *Path) && Decode(Previous, Decoded))
	{
		if (Files.Copy(*Backup, *Path, true, true) != COPY_OK) return false;
	}
	if (!Files.Move(*Path, *Temp, true, true)) { LastError = FText::FromString(TEXT("저장 교체에 실패했습니다. 이전 저장은 보존했습니다.")); return false; }
	return true;
}

bool UPFSaveSubsystem::SaveWorld() { return bWorldWritable && Write(TEXT("World"), WorldSave); }

bool UPFSaveSubsystem::SavePersonal(const FPFProfile& Profile)
{
	if (!bPersonalWritable || !FPFGameRules::ValidateProfile(Profile)) return false;
	PersonalSave->LocalProfile = Profile; return Write(TEXT("Personal"), PersonalSave);
}
