#include "PFHumanGuardBuilder.h"
#include "AI/PFEnemyCharacter.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AssetToolsModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	bool Save(UObject* Asset)
	{
		FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Asset->GetOutermost(), Asset, *FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), Args);
	}
}

bool BuildPFHumanGuard(UBlueprint* EnemyBlueprint)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/ProjectF/Characters/Guard/SKM_Guard_Rooted"));
	if (!Mesh || !Mesh->GetSkeleton() || !EnemyBlueprint) return false;
	auto Clip = [](const TCHAR* Name)
	{
		return LoadObject<UAnimSequence>(nullptr, *(FString(TEXT("/Game/ProjectF/Characters/Guard/Animations/Guard_")) + Name));
	};
	UAnimSequence* Idle = Clip(TEXT("MF_Rifle_Idle_ADS")); UAnimSequence* Walk = Clip(TEXT("MF_Rifle_Walk_Fwd")); UAnimSequence* Jog = Clip(TEXT("MF_Rifle_Jog_Fwd"));
	UAnimSequence* Jump = Clip(TEXT("MM_Rifle_Jump_Start")); UAnimSequence* Fall = Clip(TEXT("MM_Rifle_Jump_Fall_Loop")); UAnimSequence* Land = Clip(TEXT("MM_Rifle_Jump_Fall_Land"));
	UAnimSequence* Death = Clip(TEXT("MM_Death_Back_01"));
	if (!Idle || !Walk || !Jog || !Jump || !Fall || !Land || !Death) return false;
	IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UBlendSpace* Blend = LoadObject<UBlendSpace>(nullptr, TEXT("/Game/ProjectF/AI/BS_HumanGuard"));
	if (!Blend) Blend = Cast<UBlendSpace>(Tools.DuplicateAsset(TEXT("BS_HumanGuard"), TEXT("/Game/ProjectF/AI"), LoadObject<UBlendSpace>(nullptr, TEXT("/Game/ProjectF/AI/BS_GuardRifle"))));
	UAnimBlueprint* Animation = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/ProjectF/AI/ABP_HumanGuard"));
	if (!Animation) Animation = Cast<UAnimBlueprint>(Tools.DuplicateAsset(TEXT("ABP_HumanGuard"), TEXT("/Game/ProjectF/AI"), LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/ProjectF/AI/ABP_GuardRifle"))));
	if (!Blend || !Animation) return false;
	Blend->SetSkeleton(Mesh->GetSkeleton());
	for (int32 Index = 0; Index < Blend->GetBlendSamples().Num(); ++Index)
	{
		const float Speed = Blend->GetBlendSamples()[Index].SampleValue.X;
		if (!Blend->ReplaceSampleAnimation(Index, Speed < 50.f ? Idle : Speed < 300.f ? Walk : Jog)) return false;
	}
	Blend->ValidateSampleData(); Blend->ResampleData(); if (!Save(Blend)) return false;
	Animation->TargetSkeleton = Mesh->GetSkeleton();
	TArray<UEdGraph*> Graphs; Animation->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		const TArray<TObjectPtr<UEdGraphNode>> Nodes = Graph->Nodes;
		for (UEdGraphNode* Node : Nodes)
		{
			if (UAnimGraphNode_BlendSpacePlayer* Player = Cast<UAnimGraphNode_BlendSpacePlayer>(Node)) Player->SetAnimationAsset(Blend);
			if (UAnimGraphNode_SequencePlayer* Player = Cast<UAnimGraphNode_SequencePlayer>(Node))
			{
				const FString Name = GetNameSafe(Player->GetAnimationAsset());
				Player->SetAnimationAsset(Name.Contains(TEXT("Land")) ? Land : Name.Contains(TEXT("Fall")) ? Fall : Name.Contains(TEXT("Jump")) ? Jump : Idle);
			}
			if (Node->GetClass()->GetFName() == TEXT("AnimGraphNode_ControlRig"))
			{
				UEdGraphPin* Input = nullptr; UEdGraphPin* Output = nullptr;
				for (UEdGraphPin* Pin : Node->Pins) if (Pin->PinType.PinSubCategoryObject.IsValid() && Pin->PinType.PinSubCategoryObject->GetName().Contains(TEXT("PoseLink")))
					if (Pin->Direction == EGPD_Input) Input = Pin; else Output = Pin;
				if (!Input || !Output || Input->LinkedTo.Num() != 1) return false;
				UEdGraphPin* Source = Input->LinkedTo[0]; const TArray<UEdGraphPin*> Targets = Output->LinkedTo;
				Input->BreakAllPinLinks(); Output->BreakAllPinLinks();
				for (UEdGraphPin* Target : Targets) if (!Graph->GetSchema()->TryCreateConnection(Source, Target)) return false;
				FBlueprintEditorUtils::RemoveNode(Animation, Node, true);
			}
		}
	}
	FKismetEditorUtilities::CompileBlueprint(Animation); if (Animation->Status == BS_Error || !Save(Animation)) return false;
	USkeleton* Skeleton = Mesh->GetSkeleton();
	if (!Skeleton->FindSocket(TEXT("HandGrip_R")))
	{
		USkeletalMesh* Source = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple"));
		const USkeletalMeshSocket* Original = Source ? Source->FindSocket(TEXT("HandGrip_R")) : nullptr;
		if (!Original) return false;
		USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(Skeleton); Socket->SocketName = TEXT("HandGrip_R"); Socket->BoneName = TEXT("hand_r");
		Socket->RelativeLocation = Original->RelativeLocation; Socket->RelativeRotation = Original->RelativeRotation; Socket->RelativeScale = Original->RelativeScale;
		Skeleton->Sockets.Add(Socket); if (!Save(Skeleton)) return false;
	}
	APFEnemyCharacter* Enemy = CastChecked<APFEnemyCharacter>(EnemyBlueprint->GeneratedClass->GetDefaultObject());
	Enemy->GetMesh()->SetSkeletalMesh(Mesh); Enemy->GetMesh()->SetAnimInstanceClass(Animation->GeneratedClass); Enemy->DeathAnimation = Death;
	const FSkeletalMeshModel* Model = Mesh->GetImportedModel(); if (!Model || Model->LODModels.IsEmpty()) return false;
	TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
	Record->SetStringField(TEXT("mesh"), Mesh->GetPathName());
	Record->SetNumberField(TEXT("materials"), Mesh->GetMaterials().Num());
	Record->SetNumberField(TEXT("bones"), Mesh->GetRefSkeleton().GetNum());
	Record->SetBoolField(TEXT("physics_asset"), Mesh->GetPhysicsAsset() != nullptr);
	Record->SetBoolField(TEXT("production_approved"), false);
	Record->SetStringField(TEXT("material_budget_status"), TEXT("12 slots exceed the initial 2-4 target; consolidation or a measured exception and visual acceptance remain required"));
	TArray<TSharedPtr<FJsonValue>> LODs;
	for (int32 Index = 0; Index < Model->LODModels.Num(); ++Index)
	{
		const FSkeletalMeshLODModel& LOD = Model->LODModels[Index]; uint32 Triangles = 0;
		for (const FSkelMeshSection& Section : LOD.Sections) Triangles += Section.NumTriangles;
		TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetNumberField(TEXT("lod"), Index); Data->SetNumberField(TEXT("vertices"), LOD.NumVertices);
		Data->SetNumberField(TEXT("triangles"), Triangles); Data->SetNumberField(TEXT("sections"), LOD.Sections.Num());
		LODs.Add(MakeShared<FJsonValueObject>(Data));
	}
	Record->SetArrayField(TEXT("imported_lods"), LODs);
	FString Encoded; FJsonSerializer::Serialize(Record, TJsonWriterFactory<>::Create(&Encoded));
	if (!FFileHelper::SaveStringToFile(Encoded, *(FPaths::ProjectDir() / TEXT("ArtSource/Characters/Guard/unreal-validation.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return false;
	UE_LOG(LogTemp, Display, TEXT("PF_HUMAN_GUARD: candidate mesh, baked locomotion, weapon socket and death bound; production approval remains external."));
	return true;
}
