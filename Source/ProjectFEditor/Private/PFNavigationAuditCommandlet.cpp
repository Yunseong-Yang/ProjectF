#include "PFNavigationAuditCommandlet.h"
#include "FileHelpers.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavMesh/RecastNavMesh.h"
#include "World/PFOutpost.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"

namespace
{
TArray<TSharedPtr<FJsonValue>> VectorValues(const FVector& Point)
{
	return {MakeShared<FJsonValueNumber>(Point.X), MakeShared<FJsonValueNumber>(Point.Y), MakeShared<FJsonValueNumber>(Point.Z)};
}
TSharedPtr<FJsonValue> VectorValue(const FVector& Point) { return MakeShared<FJsonValueArray>(VectorValues(Point)); }
}

UPFNavigationAuditCommandlet::UPFNavigationAuditCommandlet()
{
	IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true;
}

int32 UPFNavigationAuditCommandlet::Main(const FString& Params)
{
	FString Map(TEXT("/Game/ProjectF/Maps/Tests/L_FrontierTemple")), Output;
	FParse::Value(*Params, TEXT("Map="), Map); FParse::Value(*Params, TEXT("Output="), Output);
	const FString AllowedOutput = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Saved/Implementation"));
	Output = FPaths::ConvertRelativePathToFull(Output);
	if (!Map.StartsWith(TEXT("/Game/ProjectF/Maps/")) || !FPackageName::IsValidLongPackageName(Map)
		|| !Output.StartsWith(AllowedOutput + TEXT("/")) || !Output.EndsWith(TEXT(".json"))) return 1;
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
	if (!World) return 2;
	World->FlushLevelStreaming(EFlushLevelStreamingType::Full); FlushAsyncLoading();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	ARecastNavMesh* Mesh = Nav ? Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) : nullptr;
	if (!Mesh) return 3;
	const FBox Region(FVector(11000, 4500, 4000), FVector(22000, 15500, 6200));
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("map"), Map); Root->SetBoolField(TEXT("map_saved"), false);
	Root->SetNumberField(TEXT("agent_radius"), Mesh->GetConfig().AgentRadius);
	Root->SetNumberField(TEXT("agent_height"), Mesh->GetConfig().AgentHeight);
	TArray<TSharedPtr<FJsonValue>> EntryRows;
	for (TActorIterator<APFOutpost> Post(World); Post; ++Post)
	{
		if (Post->OutpostId != TEXT("Stronghold")) continue;
		for (int32 Index = 0; Index < Post->ReinforcementSpawns.Num(); ++Index)
		{
			const FVector Start = Post->ReinforcementSpawns[Index].GetLocation(), Goal = Post->GetActorLocation();
			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetNumberField(TEXT("index"), Index); Row->SetArrayField(TEXT("start"), VectorValues(Start));
			Row->SetArrayField(TEXT("goal"), VectorValues(Goal));
			FNavLocation From;
			const bool bFrom = Nav->ProjectPointToNavigation(Start, From, FVector(80, 80, 160));
			Row->SetBoolField(TEXT("start_projected"), bFrom);
			if (bFrom) Row->SetArrayField(TEXT("projected_start"), VectorValues(From.Location));
			for (const TPair<FString, FVector>& Query : {TPair<FString, FVector>(TEXT("default"), Mesh->GetConfig().DefaultQueryExtent), TPair<FString, FVector>(TEXT("bounded"), FVector(300, 300, 160))})
			{
				FNavLocation To; const bool bTo = Nav->ProjectPointToNavigation(Goal, To, Query.Value);
				Row->SetBoolField(Query.Key + TEXT("_goal_projected"), bTo);
				if (bTo) Row->SetArrayField(Query.Key + TEXT("_goal"), VectorValues(To.Location));
				UNavigationPath* Path = bFrom && bTo ? UNavigationSystemV1::FindPathToLocationSynchronously(World, From.Location, To.Location) : nullptr;
				Row->SetBoolField(Query.Key + TEXT("_complete_path"), Path && Path->IsValid() && !Path->IsPartial());
				if (Path)
				{
					Row->SetNumberField(Query.Key + TEXT("_length_cm"), Path->GetPathLength());
					TArray<TSharedPtr<FJsonValue>> Points; for (const FVector& Point : Path->PathPoints) Points.Add(VectorValue(Point));
					Row->SetArrayField(Query.Key + TEXT("_path"), Points);
				}
			}
			EntryRows.Add(MakeShared<FJsonValueObject>(Row));
		}
	}
	Root->SetArrayField(TEXT("reinforcement_queries"), EntryRows);
	// Recast box queries have a bounded result buffer; query small cells and
	// deduplicate so a dense roof does not silently hide lower corridor polygons.
	TMap<NavNodeRef, FNavPoly> UniquePolys;
	for (float X = Region.Min.X; X < Region.Max.X; X += 800.f)
		for (float Y = Region.Min.Y; Y < Region.Max.Y; Y += 800.f)
		{
			TArray<FNavPoly> Cell;
			Mesh->GetPolysInBox(FBox(FVector(X, Y, Region.Min.Z), FVector(FMath::Min(X + 800.f, Region.Max.X), FMath::Min(Y + 800.f, Region.Max.Y), Region.Max.Z)), Cell);
			for (const FNavPoly& Poly : Cell) UniquePolys.Add(Poly.Ref, Poly);
		}
	TArray<FNavPoly> Polys; UniquePolys.GenerateValueArray(Polys);
	if (Polys.IsEmpty()) { UE_LOG(LogTemp, Error, TEXT("PF_NAV_AUDIT: no polygons in the inspected region")); return 5; }
	TArray<TSharedPtr<FJsonValue>> PolyRows;
	for (const FNavPoly& Poly : Polys)
	{
		TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("id"), LexToString(Poly.Ref)); Row->SetArrayField(TEXT("center"), VectorValues(Poly.Center));
		TArray<FVector> Verts; Mesh->GetPolyVerts(Poly.Ref, Verts);
		TArray<TSharedPtr<FJsonValue>> VertRows; for (const FVector& Point : Verts) VertRows.Add(VectorValue(Point));
		Row->SetArrayField(TEXT("vertices"), VertRows);
		TArray<NavNodeRef> Neighbors; Mesh->GetPolyNeighbors(Poly.Ref, Neighbors);
		TArray<TSharedPtr<FJsonValue>> Links; for (NavNodeRef Ref : Neighbors) Links.Add(MakeShared<FJsonValueString>(LexToString(Ref)));
		Row->SetArrayField(TEXT("neighbors"), Links); PolyRows.Add(MakeShared<FJsonValueObject>(Row));
	}
	Root->SetArrayField(TEXT("polygons"), PolyRows);
	TArray<TSharedPtr<FJsonValue>> Actors;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		FVector Center, Extent; It->GetActorBounds(false, Center, Extent);
		if (!FBox(Center - Extent, Center + Extent).Intersect(Region)) continue;
		TInlineComponentArray<UStaticMeshComponent*> Components; It->GetComponents(Components);
		for (const UStaticMeshComponent* Component : Components)
		{
			const UStaticMesh* Asset = Component->GetStaticMesh(); if (!Asset) continue;
			TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("actor"), It->GetActorLabel()); Row->SetStringField(TEXT("component"), Component->GetName());
			Row->SetStringField(TEXT("mesh"), Asset->GetPathName()); Row->SetStringField(TEXT("level"), It->GetLevel()->GetPathName());
			Row->SetArrayField(TEXT("center"), VectorValues(Component->Bounds.Origin)); Row->SetArrayField(TEXT("extent"), VectorValues(Component->Bounds.BoxExtent));
			Row->SetBoolField(TEXT("actor_collision"), It->GetActorEnableCollision()); Row->SetStringField(TEXT("profile"), Component->GetCollisionProfileName().ToString());
			Row->SetBoolField(TEXT("navigation_relevant"), Component->CanEverAffectNavigation());
			if (const UBodySetup* Body = Asset->GetBodySetup())
			{
				Row->SetNumberField(TEXT("collision_complexity"), static_cast<int32>(Body->CollisionTraceFlag));
				Row->SetNumberField(TEXT("convex_hulls"), Body->AggGeom.ConvexElems.Num());
			}
			Actors.Add(MakeShared<FJsonValueObject>(Row));
		}
	}
	Root->SetArrayField(TEXT("mesh_components"), Actors);
	TArray<TSharedPtr<FJsonValue>> Samples;
	for (float Y : {9600.f, 10000.f, 10400.f, 10550.f}) for (int32 X = 13700; X <= 16500; X += 50)
	{
		const float ExpectedZ = FMath::Lerp(4200.f, 4605.f, FMath::Clamp((X - 15000.f) / 800.f, 0.f, 1.f));
		FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(PFNavAuditFloor), false);
		const bool bHit = World->LineTraceSingleByChannel(Hit, FVector(X, Y, ExpectedZ + 100), FVector(X, Y, ExpectedZ - 250), ECC_Visibility, Query);
		TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>(); Row->SetArrayField(TEXT("sample"), VectorValues(FVector(X, Y, ExpectedZ)));
		if (bHit)
		{
			Row->SetArrayField(TEXT("floor"), VectorValues(Hit.ImpactPoint)); Row->SetStringField(TEXT("actor"), Hit.GetActor() ? Hit.GetActor()->GetActorLabel() : TEXT("none"));
			Row->SetBoolField(TEXT("start_penetrating"), Hit.bStartPenetrating);
			FNavLocation Location;
			if (Nav->ProjectPointToNavigation(Hit.ImpactPoint, Location, FVector(60, 60, 80)))
			{ Row->SetArrayField(TEXT("nav"), VectorValues(Location.Location)); Row->SetStringField(TEXT("poly"), LexToString(Location.NodeRef)); }
		}
		Samples.Add(MakeShared<FJsonValueObject>(Row));
	}
	Root->SetArrayField(TEXT("floor_samples"), Samples);
	FString Json; FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
	if (!FFileHelper::SaveStringToFile(Json, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return 4;
	UE_LOG(LogTemp, Display, TEXT("PF_NAV_AUDIT_COMPLETE polygons=%d components=%d samples=%d saved_map=0"), Polys.Num(), Actors.Num(), Samples.Num());
	return 0;
}
