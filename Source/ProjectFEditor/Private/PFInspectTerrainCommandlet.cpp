#include "PFInspectTerrainCommandlet.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "FileHelpers.h"
#include "Misc/PackageName.h"

UPFInspectTerrainCommandlet::UPFInspectTerrainCommandlet()
{
	IsClient = false; IsEditor = true; LogToConsole = true;
}

int32 UPFInspectTerrainCommandlet::Main(const FString& Params)
{
	UWorld* World = LoadObject<UWorld>(nullptr, TEXT("/Game/Slay/Envs/Temple/temple_P.temple_P"));
	if (!World) return 1;
	TArray<TSharedPtr<FJsonValue>> Levels;
	for (ULevelStreaming* Streaming : World->GetStreamingLevels())
	{
		if (!Streaming) continue;
		TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("package"), Streaming->GetWorldAssetPackageName());
		Row->SetStringField(TEXT("class"), Streaming->GetClass()->GetName());
		Row->SetStringField(TEXT("transform"), Streaming->LevelTransform.ToString());
		Row->SetBoolField(TEXT("visible_in_editor"), Streaming->GetShouldBeVisibleInEditor());
		Row->SetBoolField(TEXT("runtime_visible_flag"), Streaming->GetShouldBeVisibleFlag());
		Row->SetBoolField(TEXT("should_load"), Streaming->ShouldBeLoaded());
		Levels.Add(MakeShared<FJsonValueObject>(Row));
	}
	TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
	Record->SetStringField(TEXT("world"), World->GetPathName()); Record->SetArrayField(TEXT("levels"), Levels);
	FString Text; FJsonSerializer::Serialize(Record, TJsonWriterFactory<>::Create(&Text));
	const FString Filename = FPaths::ProjectDir() / TEXT("ArtSource/Environment/TerrainReview/original-level-composition.json");
	if (!FFileHelper::SaveStringToFile(Text, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return 2;
	if (FParse::Param(*Params, TEXT("RepairReviewComposition")))
	{
		const FString ReviewPath(TEXT("/Game/ProjectF/Maps/Tests/L_TerrainArtReview"));
		UWorld* Review = UEditorLoadingAndSavingUtils::LoadMap(ReviewPath); if (!Review) return 3;
		TArray<ULevelStreaming*> Remove;
		for (ULevelStreaming* Streaming : Review->GetStreamingLevels())
			if (Streaming && Streaming->GetWorldAssetPackageName().EndsWith(TEXT("/Terrain_TF0010"))) Remove.Add(Streaming);
		for (ULevelStreaming* Streaming : Remove) Review->RemoveStreamingLevel(Streaming);
		Review->FlushLevelStreaming(EFlushLevelStreamingType::Full);
		if (!FEditorFileUtils::SaveLevel(Review->PersistentLevel, *FPackageName::LongPackageNameToFilename(ReviewPath, FPackageName::GetMapPackageExtension()))) return 4;
		UE_LOG(LogTemp, Display, TEXT("PF_TERRAIN_REVIEW_COMPOSITION removed_hidden_variants=%d"), Remove.Num());
	}
	UE_LOG(LogTemp, Display, TEXT("PF_ORIGINAL_TERRAIN_LEVELS count=%d"), Levels.Num()); return 0;
}
