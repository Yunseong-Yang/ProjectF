#include "PFBuildContentCommandlet.h"
#include "PFHumanGuardBuilder.h"
#include "AI/PFAIController.h"
#include "AI/PFEnemyCharacter.h"
#include "AI/StateTree/PFStateTreeNodes.h"
#include "Framework/PFGameMode.h"
#include "Player/PFPlayerCharacter.h"
#include "Combat/PFWeaponComponent.h"
#include "World/PFOutpost.h"
#include "World/PFInteractable.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "Components/StateTreeAIComponentSchema.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/Blueprint.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimSequence.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_Root.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetToolsModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_VariableGet.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Sound/SoundBase.h"
#include "Components/BrushComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Builders/CubeBuilder.h"
#include "ActorFactories/ActorFactory.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "StaticMeshCompiler.h"
#include "Navigation/NavLinkProxy.h"
#include "NavAreas/NavArea_Default.h"
#include "NavMesh/RecastNavMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavModifierVolume.h"
#include "NavAreas/NavArea_Null.h"

namespace
{
	bool SaveAsset(UObject* Asset)
	{
		FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Asset->GetOutermost(), Asset,
			*FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), Args);
	}

	UBlueprint* CreateBP(UClass* Parent, const TCHAR* Path)
	{
		if (UBlueprint* Existing = LoadObject<UBlueprint>(nullptr, Path)) return Existing;
		UPackage* Package = CreatePackage(Path);
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent, Package, FName(*FPackageName::GetLongPackageAssetName(Path)), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		FAssetRegistryModule::AssetCreated(Blueprint); return Blueprint;
	}

	bool RemoveFootIK(UAnimBlueprint* Blueprint)
	{
		TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			const TArray<TObjectPtr<UEdGraphNode>> Nodes = Graph->Nodes;
			for (UEdGraphNode* Node : Nodes)
			{
				if (Node->GetClass()->GetFName() != TEXT("AnimGraphNode_ControlRig")) continue;
				UEdGraphPin* Input = nullptr; UEdGraphPin* Output = nullptr;
				for (UEdGraphPin* Pin : Node->Pins)
					if (Pin->PinType.PinSubCategoryObject.IsValid() && Pin->PinType.PinSubCategoryObject->GetName().Contains(TEXT("PoseLink")))
					{
						if (Pin->Direction == EGPD_Input) Input = Pin; else Output = Pin;
					}
				if (!Input || !Output || Input->LinkedTo.Num() != 1) return false;
				const TArray<UEdGraphPin*> Sources = Input->LinkedTo, Targets = Output->LinkedTo;
				Input->BreakAllPinLinks(); Output->BreakAllPinLinks();
				for (UEdGraphPin* Target : Targets) if (!Graph->GetSchema()->TryCreateConnection(Sources[0], Target)) return false;
				FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
			}
		}
		FKismetEditorUtilities::CompileBlueprint(Blueprint); return Blueprint->Status != BS_Error && SaveAsset(Blueprint);
	}

	void SetWeaponSequences(UAnimBlueprint* Blueprint, bool bPistol)
	{
		const FString Folder = bPistol ? TEXT("/Game/Characters/Mannequins/Anims/Pistol/") : TEXT("/Game/Characters/Mannequins/Anims/Rifle/");
		TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes)
			if (UAnimGraphNode_SequencePlayer* Player = Cast<UAnimGraphNode_SequencePlayer>(Node))
			{
				const UAnimationAsset* Asset = Player->GetAnimationAsset(); if (!Asset) continue;
				const FString Name = Asset->GetName(); FString Replacement;
				if (Name.Contains(TEXT("Idle"))) Replacement = Folder + (bPistol ? TEXT("MF_Pistol_Idle_ADS") : TEXT("MF_Rifle_Idle_ADS"));
				else if (Name.Contains(TEXT("Fall"))) Replacement = Folder + (bPistol ? TEXT("Jump/MM_Pistol_Jump_Fall_Loop") : TEXT("Jump/MM_Rifle_Jump_Fall_Loop"));
				else if (Name.Contains(TEXT("Land")) && !bPistol) Replacement = Folder + TEXT("Jump/MM_Rifle_Jump_Fall_Land");
				else if (Name.Contains(TEXT("Jump"))) Replacement = Folder + (bPistol ? TEXT("Jump/MM_Pistol_Jump_Start") : TEXT("Jump/MM_Rifle_Jump_Start"));
				if (!Replacement.IsEmpty()) if (UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *Replacement)) Player->SetAnimationAsset(Sequence);
			}
	}

	bool AdaptViewCamera(UAnimBlueprint* Blueprint)
	{
		TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			UK2Node_DynamicCast* CharacterCast = nullptr;
			for (UEdGraphNode* Node : Graph->Nodes) if (UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
				if (CastNode->TargetType && (CastNode->TargetType->GetName().Contains(TEXT("ShooterCharacter")) || CastNode->TargetType->GetName().Contains(TEXT("ProjectFCharacter")) || CastNode->TargetType == APFPlayerCharacter::StaticClass()))
				{
					const TArray<UEdGraphPin*> Targets = CastNode->GetCastResultPin()->LinkedTo;
					CastNode->GetCastResultPin()->BreakAllPinLinks();
					for (UEdGraphPin* Pin : CastNode->Pins) if (Pin->bOrphanedPin) Pin->BreakAllPinLinks();
					CastNode->TargetType = APFPlayerCharacter::StaticClass(); CastNode->ReconstructNode(); CharacterCast = CastNode;
					for (UEdGraphPin* Target : Targets) Graph->GetSchema()->TryCreateConnection(CastNode->GetCastResultPin(), Target);
				}
			for (UEdGraphNode* Node : Graph->Nodes) if (UK2Node_VariableGet* Getter = Cast<UK2Node_VariableGet>(Node))
				if (Getter->VariableReference.GetMemberName() == TEXT("FirstPersonCameraComponent") || Getter->VariableReference.GetMemberName() == TEXT("Camera") || Getter->VariableReference.GetMemberName() == TEXT("FirstPersonMesh"))
				{
					const FName Member = Getter->VariableReference.GetMemberName() == TEXT("FirstPersonMesh") ? TEXT("FirstPersonMesh") : TEXT("Camera");
					TArray<UEdGraphPin*> Targets;
					for (UEdGraphPin* Pin : Getter->Pins) if (Pin->Direction == EGPD_Output) Targets.Append(Pin->LinkedTo);
					for (UEdGraphPin* Pin : Getter->Pins) Pin->BreakAllPinLinks();
					Getter->VariableReference.SetExternalMember(Member, APFPlayerCharacter::StaticClass()); Getter->ReconstructNode();
					UEdGraphPin* CameraPin = Getter->FindPin(Member); UEdGraphPin* TargetPin = Getter->FindPin(UEdGraphSchema_K2::PN_Self);
					if (!CameraPin || !TargetPin || !CharacterCast) return false;
					for (UEdGraphPin* Target : Targets) if (!Graph->GetSchema()->TryCreateConnection(CameraPin, Target)) return false;
					if (!Graph->GetSchema()->TryCreateConnection(CharacterCast->GetCastResultPin(), TargetPin)) return false;
				}
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return Blueprint->Status != BS_Error && SaveAsset(Blueprint);
	}
}

UPFBuildContentCommandlet::UPFBuildContentCommandlet() { IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true; }

namespace { int32 BuildFrontierNavigationOnly(const FString& Params); }

int32 UPFBuildContentCommandlet::Main(const FString& Params)
{
	if (FParse::Param(*Params, TEXT("InspectViewSlots")))
	{
		for (const TCHAR* Path : {TEXT("/Game/ProjectF/Player/ABP_WarpedRifle"), TEXT("/Game/ProjectF/Player/ABP_WarpedPistol")})
		{
			UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, Path); if (!Blueprint) return 50;
			TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
			for (UEdGraph* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node->GetClass()->GetName().Contains(TEXT("AnimGraphNode"))) continue;
				UE_LOG(LogTemp, Display, TEXT("PF_VIEW_GRAPH blueprint=%s graph=%s node=%s class=%s"), Path, *Graph->GetName(), *Node->GetName(), *Node->GetClass()->GetName());
				if (const UAnimGraphNode_Slot* Slot = Cast<UAnimGraphNode_Slot>(Node)) UE_LOG(LogTemp, Display, TEXT("PF_VIEW_SLOT name=%s"), *Slot->Node.SlotName.ToString());
				for (const UEdGraphPin* Pin : Node->Pins)
					if (Pin->PinType.PinSubCategoryObject.IsValid() && Pin->PinType.PinSubCategoryObject->GetName().Contains(TEXT("PoseLink")))
						for (const UEdGraphPin* Link : Pin->LinkedTo)
							UE_LOG(LogTemp, Display, TEXT("PF_VIEW_POSE node=%s pin=%s direction=%d linked=%s.%s"), *Node->GetName(), *Pin->PinName.ToString(), int32(Pin->Direction), *Link->GetOwningNode()->GetName(), *Link->PinName.ToString());
			}
		}
		return 0;
	}
	if (FParse::Param(*Params, TEXT("BuildFrontierNavigation"))) return BuildFrontierNavigationOnly(Params);

	const TCHAR* TreePath = TEXT("/Game/ProjectF/AI/ST_Guard");
	UStateTree* Tree = LoadObject<UStateTree>(nullptr, TreePath);
	if (!Tree)
	{
		UPackage* Package = CreatePackage(TreePath); Tree = NewObject<UStateTree>(Package, TEXT("ST_Guard"), RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Tree);
	}
	UStateTreeEditorData* Data = NewObject<UStateTreeEditorData>(Tree); Tree->EditorData = Data;
	Data->Schema = NewObject<UStateTreeAIComponentSchema>(Data);
	UStateTreeState& Root = Data->AddRootState();
	const TArray<TPair<FName, EPFAlertState>> States = {
		{TEXT("Dead"), EPFAlertState::Dead}, {TEXT("Combat"), EPFAlertState::Combat},
		{TEXT("Search"), EPFAlertState::Search}, {TEXT("Investigate"), EPFAlertState::Investigate}, {TEXT("Routine"), EPFAlertState::Routine}
	};
	for (const auto& Pair : States)
	{
		UStateTreeState& State = Root.AddChildState(Pair.Key);
		State.AddEnterCondition<FPFStateCondition>().GetNode().State = Pair.Value;
		State.AddTask<FPFBehaviorTask>().GetNode().State = Pair.Value;
		State.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, &Root);
	}
	FStateTreeCompilerLog CompileLog; FStateTreeCompiler Compiler(CompileLog);
	if (!Compiler.Compile(*Tree)) { CompileLog.DumpToLog(LogTemp); return 1; }
	if (!SaveAsset(Tree)) return 2;
	UBlueprint* AIBP = CreateBP(APFAIController::StaticClass(), TEXT("/Game/ProjectF/AI/BP_PFAIController"));
	CastChecked<APFAIController>(AIBP->GeneratedClass->GetDefaultObject())->Behavior = Tree;
	if (!SaveAsset(AIBP)) return 3;
	USkeletalMesh* CharacterMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple"));
	UAnimBlueprint* Animation = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
	if (!CharacterMesh || !Animation) return 4;
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UBlendSpace* RifleBlend = LoadObject<UBlendSpace>(nullptr, TEXT("/Game/ProjectF/AI/BS_GuardRifle"));
	if (!RifleBlend) RifleBlend = Cast<UBlendSpace>(AssetTools.DuplicateAsset(TEXT("BS_GuardRifle"), TEXT("/Game/ProjectF/AI"), LoadObject<UBlendSpace>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run"))));
	UAnimBlueprint* GuardAnimation = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/ProjectF/AI/ABP_GuardRifle"));
	if (!GuardAnimation) GuardAnimation = Cast<UAnimBlueprint>(AssetTools.DuplicateAsset(TEXT("ABP_GuardRifle"), TEXT("/Game/ProjectF/AI"), Animation));
	if (!RifleBlend || !GuardAnimation) return 15;
	for (int32 Sample = 0; Sample < RifleBlend->GetBlendSamples().Num(); ++Sample)
	{
		const float Speed = RifleBlend->GetBlendSamples()[Sample].SampleValue.X;
		const TCHAR* AnimPath = Speed < 50.f ? TEXT("/Game/Characters/Mannequins/Anims/Rifle/MF_Rifle_Idle_ADS") : Speed < 300.f ? TEXT("/Game/Characters/Mannequins/Anims/Rifle/Walk/MF_Rifle_Walk_Fwd") : TEXT("/Game/Characters/Mannequins/Anims/Rifle/Jog/MF_Rifle_Jog_Fwd");
		if (!RifleBlend->ReplaceSampleAnimation(Sample, LoadObject<UAnimSequence>(nullptr, AnimPath))) return 16;
	}
	RifleBlend->ValidateSampleData(); RifleBlend->ResampleData(); if (!SaveAsset(RifleBlend)) return 17;
	TArray<UEdGraph*> AnimationGraphs; GuardAnimation->GetAllGraphs(AnimationGraphs);
	for (UEdGraph* Graph : AnimationGraphs) for (UEdGraphNode* Node : Graph->Nodes)
		if (UAnimGraphNode_BlendSpacePlayer* BlendNode = Cast<UAnimGraphNode_BlendSpacePlayer>(Node)) BlendNode->SetAnimationAsset(RifleBlend);
	SetWeaponSequences(GuardAnimation, false); FKismetEditorUtilities::CompileBlueprint(GuardAnimation);
	if (GuardAnimation->Status == BS_Error || !SaveAsset(GuardAnimation)) return 18;
	UBlendSpace* PistolBlend = LoadObject<UBlendSpace>(nullptr, TEXT("/Game/ProjectF/Player/BS_Pistol"));
	if (!PistolBlend) PistolBlend = Cast<UBlendSpace>(AssetTools.DuplicateAsset(TEXT("BS_Pistol"), TEXT("/Game/ProjectF/Player"), RifleBlend));
	UAnimBlueprint* PistolAnimation = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/ProjectF/Player/ABP_Pistol"));
	if (!PistolAnimation) PistolAnimation = Cast<UAnimBlueprint>(AssetTools.DuplicateAsset(TEXT("ABP_Pistol"), TEXT("/Game/ProjectF/Player"), GuardAnimation));
	if (!PistolBlend || !PistolAnimation) return 24;
	for (int32 Sample = 0; Sample < PistolBlend->GetBlendSamples().Num(); ++Sample)
	{
		const float Speed = PistolBlend->GetBlendSamples()[Sample].SampleValue.X;
		const TCHAR* AnimPath = Speed < 50.f ? TEXT("/Game/Characters/Mannequins/Anims/Pistol/MF_Pistol_Idle_ADS") : Speed < 300.f ? TEXT("/Game/Characters/Mannequins/Anims/Pistol/Walk/MF_Pistol_Walk_Fwd") : TEXT("/Game/Characters/Mannequins/Anims/Pistol/Jog/MF_Pistol_Jog_Fwd");
		if (!PistolBlend->ReplaceSampleAnimation(Sample, LoadObject<UAnimSequence>(nullptr, AnimPath))) return 25;
	}
	PistolBlend->ValidateSampleData(); PistolBlend->ResampleData(); if (!SaveAsset(PistolBlend)) return 26;
	AnimationGraphs.Reset(); PistolAnimation->GetAllGraphs(AnimationGraphs);
	for (UEdGraph* Graph : AnimationGraphs) for (UEdGraphNode* Node : Graph->Nodes)
		if (UAnimGraphNode_BlendSpacePlayer* BlendNode = Cast<UAnimGraphNode_BlendSpacePlayer>(Node)) BlendNode->SetAnimationAsset(PistolBlend);
	SetWeaponSequences(PistolAnimation, true); FKismetEditorUtilities::CompileBlueprint(PistolAnimation); if (PistolAnimation->Status == BS_Error || !SaveAsset(PistolAnimation)) return 27;
	UAnimBlueprint* RifleView = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/ProjectF/Player/ABP_ViewRifle"));
	if (!RifleView) RifleView = Cast<UAnimBlueprint>(AssetTools.DuplicateAsset(TEXT("ABP_ViewRifle"), TEXT("/Game/ProjectF/Player"), GuardAnimation));
	UAnimBlueprint* PistolView = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/ProjectF/Player/ABP_ViewPistol"));
	if (!PistolView) PistolView = Cast<UAnimBlueprint>(AssetTools.DuplicateAsset(TEXT("ABP_ViewPistol"), TEXT("/Game/ProjectF/Player"), PistolAnimation));
	if (!RifleView || !PistolView) return 28;
	SetWeaponSequences(RifleView, false); SetWeaponSequences(PistolView, true);
	if (!RemoveFootIK(RifleView) || !RemoveFootIK(PistolView)) return 28;
	UAnimBlueprint* WarpedRifle = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/ProjectF/Player/ABP_WarpedRifle"));
	if (!WarpedRifle) WarpedRifle = Cast<UAnimBlueprint>(AssetTools.DuplicateAsset(TEXT("ABP_WarpedRifle"), TEXT("/Game/ProjectF/Player"), LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Variant_Shooter/Anims/ABP_FP_Weapon"))));
	UAnimBlueprint* WarpedPistol = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/ProjectF/Player/ABP_WarpedPistol"));
	if (!WarpedPistol) WarpedPistol = Cast<UAnimBlueprint>(AssetTools.DuplicateAsset(TEXT("ABP_WarpedPistol"), TEXT("/Game/ProjectF/Player"), LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Variant_Shooter/Anims/ABP_FP_Pistol"))));
	if (!WarpedRifle || !WarpedPistol || !AdaptViewCamera(WarpedRifle) || !AdaptViewCamera(WarpedPistol)) return 29;
	UBlueprint* EnemyBP = CreateBP(APFEnemyCharacter::StaticClass(), TEXT("/Game/ProjectF/AI/BP_Guard"));
	APFEnemyCharacter* EnemyCDO = CastChecked<APFEnemyCharacter>(EnemyBP->GeneratedClass->GetDefaultObject());
	UStaticMesh* FieldRifle = FPackageName::DoesPackageExist(TEXT("/Game/ProjectF/Weapons/SM_FieldRifle")) ? LoadObject<UStaticMesh>(nullptr, TEXT("/Game/ProjectF/Weapons/SM_FieldRifle")) : LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Weapons/Rifle/Meshes/SM_Rifle"));
	EnemyCDO->AIControllerClass = AIBP->GeneratedClass;
	EnemyCDO->DeathAnimation = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Back_01"));
	EnemyCDO->GunshotSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/ProjectF/Audio/S_RifleReport"));
	EnemyCDO->HeldWeapon->SetStaticMesh(FieldRifle);
	EnemyCDO->GetMesh()->SetSkeletalMesh(CharacterMesh);
	EnemyCDO->GetMesh()->SetRelativeLocationAndRotation(FVector(0, 0, -96), FRotator(0, -90, 0));
	EnemyCDO->GetMesh()->SetAnimInstanceClass(GuardAnimation->GeneratedClass);
	EnemyCDO->GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	EnemyCDO->GetMesh()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	EnemyCDO->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	if (FParse::Param(*Params, TEXT("UseHumanGuards")) && !BuildPFHumanGuard(EnemyBP)) return 30;
	if (!SaveAsset(EnemyBP)) return 5;
	UBlueprint* PlayerBP = CreateBP(APFPlayerCharacter::StaticClass(), TEXT("/Game/ProjectF/Player/BP_PFPlayer"));
	APFPlayerCharacter* PlayerCDO = CastChecked<APFPlayerCharacter>(PlayerBP->GeneratedClass->GetDefaultObject());
	PlayerCDO->GetMesh()->SetSkeletalMesh(CharacterMesh); PlayerCDO->GetMesh()->SetRelativeLocationAndRotation(FVector(0, 0, -96), FRotator(0, -90, 0));
	PlayerCDO->GetMesh()->SetAnimInstanceClass(GuardAnimation->GeneratedClass);
	const bool bPreserveView = PlayerCDO->FirstPersonMesh->GetSkeletalMeshAsset() && !FParse::Param(*Params, TEXT("ResetPlayerPresentation"));
	if (!bPreserveView) PlayerCDO->FirstPersonMesh->SetSkeletalMesh(CharacterMesh);
	PlayerCDO->Camera->FirstPersonFieldOfView = 80.f;
	PlayerCDO->RifleRearExecution = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/ProjectF/Player/Animations/AS_Rifle_RearStrike"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	PlayerCDO->PistolRearExecution = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/ProjectF/Player/Animations/AS_Pistol_RearStrike"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	PlayerCDO->ExecutionWhooshSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/ProjectF/Audio/S_ExecutionWhoosh"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	PlayerCDO->ExecutionImpactSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/ProjectF/Audio/S_ExecutionImpact"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	PlayerCDO->RemoteWeapon->SetStaticMesh(FieldRifle);
	PlayerCDO->FirstPersonMesh->SetupAttachment(PlayerCDO->GetMesh());
	if (!bPreserveView) PlayerCDO->FirstPersonMesh->SetRelativeLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	// The view graph reads its owning controller; initialize it after possession.
	PlayerCDO->FirstPersonMesh->SetAnimInstanceClass(nullptr);
	PlayerCDO->RifleBodyAnimation = GuardAnimation->GeneratedClass; PlayerCDO->PistolBodyAnimation = PistolAnimation->GeneratedClass;
	PlayerCDO->RifleViewAnimation = WarpedRifle->GeneratedClass; PlayerCDO->PistolViewAnimation = WarpedPistol->GeneratedClass;
	PlayerCDO->RifleMesh = FieldRifle;
	PlayerCDO->PistolMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Weapons/Pistol/Meshes/SM_Pistol"));
	PlayerCDO->DeathAnimation = EnemyCDO->DeathAnimation;
	PlayerCDO->RifleFireAnimation = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Fire"));
	PlayerCDO->PistolFireAnimation = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Pistol/MM_Pistol_Fire"));
	PlayerCDO->RifleReloadAnimation = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Reload"));
	PlayerCDO->PistolReloadAnimation = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Pistol/MM_Pistol_Reload"));
	PlayerCDO->ViewWeapon->SetStaticMesh(FieldRifle);
	PlayerCDO->ViewWeapon->SetRelativeRotation(FRotator(0.f, -90.f, 0.f)); PlayerCDO->ViewWeapon->SetRelativeScale3D(FVector(.8f));
	PlayerCDO->FireSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/ProjectF/Audio/S_RifleReport"));
	PlayerCDO->SuppressedSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/ProjectF/Audio/S_SuppressedReport"));
	PlayerCDO->FootstepSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/ProjectF/Audio/S_DirtFootstep"));
	PlayerCDO->DetectionSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/ProjectF/Audio/S_Detection"));
	PlayerCDO->Weapon->HitConfirmSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/ProjectF/Audio/S_HitConfirm"));
	if (!SaveAsset(PlayerBP)) return 6;
	UBlueprint* ModeBP = CreateBP(APFGameMode::StaticClass(), TEXT("/Game/ProjectF/Core/BP_PFGameMode"));
	CastChecked<APFGameMode>(ModeBP->GeneratedClass->GetDefaultObject())->DefaultPawnClass = PlayerBP->GeneratedClass;
	if (!SaveAsset(ModeBP)) return 7;
	if (FPackageName::DoesPackageExist(TEXT("/Game/ProjectF/Environment/Frontier/Meshes/SM_Elm08")))
	{
		UStaticMesh* TreeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/ProjectF/Environment/Frontier/Meshes/SM_Elm08"));
		UBodySetup* Body = TreeMesh ? TreeMesh->GetBodySetup() : nullptr;
		if (!Body) return 22;
		Body->AggGeom.EmptyElements(); FKSphylElem Trunk; Trunk.Center = FVector(0, 0, 270); Trunk.Radius = 18.f; Trunk.Length = 500.f;
		Body->AggGeom.SphylElems.Add(Trunk); Body->CollisionTraceFlag = CTF_UseSimpleAsComplex; Body->InvalidatePhysicsData(); Body->CreatePhysicsMeshes();
		if (!SaveAsset(TreeMesh)) return 23;
		UE_LOG(LogTemp, Display, TEXT("PF_TREE_COLLISION: trunk capsule radius18 length500 centerZ270; leaves do not block sight or movement"));
	}
	if (!GEditor) return 8;
	const FString MapPath = TEXT("/Game/ProjectF/Maps/Tests/L_CoopSystems");
	if (FPackageName::DoesPackageExist(MapPath) && !FParse::Param(*Params, TEXT("RebuildTestMap")))
	{
		UE_LOG(LogTemp, Display, TEXT("PF_CONTENT: StateTree and Blueprint assets updated; existing map preserved.")); return 0;
	}
	UWorld* World = GEditor->NewMap(); if (!World) return 9;
	World->GetWorldSettings()->DefaultGameMode = ModeBP->GeneratedClass;
	USkeletalMesh* Mesh = CharacterMesh; (void)Mesh;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube"));
	auto Block = [World, Cube](FVector Location, FVector Scale, const FString& Label)
	{
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
		Actor->GetStaticMeshComponent()->SetStaticMesh(Cube); Actor->SetActorScale3D(Scale); Actor->SetActorLabel(Label);
		Actor->Tags.Add(TEXT("PF.Unapproved")); return Actor;
	};
	Block(FVector(0, 0, -100), FVector(1000, 1000, 2), TEXT("TestGround_Unapproved"));
	ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector(0, 0, 2000), FRotator(-45, -35, 0));
	CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent())->SetIntensity(3.f);
	CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent())->bAtmosphereSunLight = true;
	World->SpawnActor<ASkyAtmosphere>();
	AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(); Fog->GetComponent()->SetFogDensity(.003f);
	ASkyLight* Sky = World->SpawnActor<ASkyLight>(); Sky->GetLightComponent()->SetIntensity(.7f); Sky->GetLightComponent()->SetRealTimeCaptureEnabled(true);
	APlayerStart* Start = World->SpawnActor<APlayerStart>(FVector(-34000, -16000, 110), FRotator(0, 30, 0)); Start->Tags.Add(TEXT("PF.SafeSpawn"));
	APFInteractable* SafePoint = World->SpawnActor<APFInteractable>(FVector(-33700, -16000, 60), FRotator::ZeroRotator);
	SafePoint->Kind = EPFInteractableKind::SafePoint; SafePoint->PersistentId = TEXT("Safe.Camp"); SafePoint->DisplayName = FText::FromString(TEXT("안전 캠프"));
	SafePoint->Mesh->SetStaticMesh(Cube); SafePoint->SetActorScale3D(FVector(1.2, 1.2, 1.2)); SafePoint->Tags.Add(TEXT("PF.Unapproved"));
	TArray<APFOutpost*> Outposts;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FVector Center = Index == 0 ? FVector(-14000, 0, 75) : FVector(17000, 10000, 75);
		APFOutpost* Outpost = World->SpawnActor<APFOutpost>(Center, FRotator::ZeroRotator); Outposts.Add(Outpost);
		Outpost->OutpostId = Index == 0 ? TEXT("RadioStation") : TEXT("Stronghold");
		Outpost->DisplayName = FText::FromString(Index == 0 ? TEXT("통신소") : TEXT("주요 거점"));
		Outpost->Facility = Index == 0 ? EPFFacilityType::Communications : EPFFacilityType::Stronghold;
		if (Index == 0) Outpost->Supports.Add(TEXT("Stronghold"));
		Outpost->EnemyClass = EnemyBP->GeneratedClass; Outpost->Terminal->SetStaticMesh(Cube); Outpost->SetActorScale3D(FVector(1, 1, 1.5)); Outpost->Tags.Add(TEXT("PF.Unapproved"));
		for (int32 SpawnIndex = 0; SpawnIndex < 8; ++SpawnIndex)
		{
			const float Angle = SpawnIndex * 45.f; const FVector Offset = FRotator(0, Angle, 0).Vector() * 1400.f;
			Outpost->GarrisonSpawns.Add(FTransform(FRotator(0, Angle, 0), FVector(Center.X + Offset.X, Center.Y + Offset.Y, 100)));
		}
		for (int32 SpawnIndex = 0; SpawnIndex < 8; ++SpawnIndex)
			Outpost->ReinforcementSpawns.Add(FTransform(FRotator(0, 180, 0), Center + FVector(4800 + SpawnIndex * 110, 3000, 25)));
		APFInteractable* Alarm = World->SpawnActor<APFInteractable>(Center + FVector(450, 0, 0), FRotator::ZeroRotator);
		Alarm->Kind = EPFInteractableKind::Alarm; Alarm->PersistentId = FName(*(Outpost->OutpostId.ToString() + TEXT(".Alarm")));
		Alarm->DisplayName = FText::FromString(TEXT("경보 장치 해제")); Alarm->Outpost = Outpost; Alarm->Mesh->SetStaticMesh(Cube);
		Alarm->SetActorScale3D(FVector(.5, .5, 1.5)); Alarm->Tags.Add(TEXT("PF.Unapproved")); Outpost->Alarm = Alarm;
		for (int32 Side = 0; Side < 4; ++Side)
		{
			const FVector Offset = FRotator(0, Side * 90.f, 0).Vector() * 2100.f;
			AStaticMeshActor* Wall = Block(Center + Offset + FVector(0, 0, 100), FVector(24, 1, 3.5), FString::Printf(TEXT("Outpost%d_Cover%d"), Index, Side));
			Wall->SetActorRotation(FRotator(0, Side * 90.f + 90.f, 0));
		}
		for (int32 Cover = 0; Cover < 6; ++Cover) Block(Center + FVector((Cover % 3 - 1) * 650.f, (Cover / 3 ? 1 : -1) * 700.f, 0), FVector(2, 2, 1.5), TEXT("TestCover_Unapproved"));
		Block(Center + FVector(5200, 3000, 150), FVector(2, 30, 6), TEXT("ReinforcementScreen_Unapproved"));
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		APFInteractable* Cache = World->SpawnActor<APFInteractable>(FVector(-23000 + Index * 13000, -7000 + Index * 1800, 50), FRotator::ZeroRotator);
		Cache->Kind = EPFInteractableKind::Exploration; Cache->PersistentId = FName(*FString::Printf(TEXT("Exploration.%d"), Index));
		Cache->DisplayName = FText::FromString(TEXT("탐험 보급품")); Cache->Mesh->SetStaticMesh(Cube); Cache->Tags.Add(TEXT("PF.Unapproved"));
	}
	ANavMeshBoundsVolume* Bounds = World->SpawnActor<ANavMeshBoundsVolume>();
	UCubeBuilder* Builder = NewObject<UCubeBuilder>(); Builder->X = 100000.f; Builder->Y = 100000.f; Builder->Z = 4000.f;
	UActorFactory::CreateBrushForVolumeActor(Bounds, Builder);
	Bounds->SetActorLocation(FVector(0, 0, 1000)); Bounds->GetBrushComponent()->SetMobility(EComponentMobility::Static);
	FlushAsyncLoading();
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
		Nav->ReleaseInitialBuildingLock(); Nav->OnNavigationBoundsUpdated(Bounds);
		Nav->OnWorldInitDone(FNavigationSystemRunMode::EditorMode); Nav->Build();
		FNavLocation Projected;
		if (!Nav->ProjectPointToNavigation(Outposts[0]->GarrisonSpawns[0].GetLocation(), Projected, FVector(200, 200, 300)))
		{
			UE_LOG(LogTemp, Error, TEXT("PF_CONTENT: navigation projection failed; map not accepted.")); return 11;
		}
	}
	const FString Filename = FPackageName::LongPackageNameToFilename(MapPath, FPackageName::GetMapPackageExtension());
	if (!FEditorFileUtils::SaveLevel(World->PersistentLevel, Filename)) return 10;
	UE_LOG(LogTemp, Display, TEXT("PF_CONTENT: compiled StateTree, four Blueprint classes, two-outpost TEST map authored locally. Art approval is pending."));
	return 0;
}

namespace
{
int32 BuildFrontierNavigationOnly(const FString& Params)
{
	if (!GEditor) return 8;
	FString FrontierMap(TEXT("/Game/ProjectF/Maps/L_MountainFrontier"));
	FParse::Value(*Params, TEXT("FrontierMap="), FrontierMap);
	if (!FrontierMap.StartsWith(TEXT("/Game/ProjectF/Maps/")) || !FPackageName::IsValidLongPackageName(FrontierMap)) return 24;
	const bool bTempleLayout = FParse::Param(*Params, TEXT("ValidateTempleLayout"));
	UWorld* Frontier = UEditorLoadingAndSavingUtils::LoadMap(FrontierMap);
	if (!Frontier) return 12;
	if (FParse::Param(*Params, TEXT("BlockTempleUnderfloorNavigation")))
	{
		if (!FrontierMap.EndsWith(TEXT("/L_FrontierTemple"))) return 34;
		for (TActorIterator<ANavModifierVolume> It(Frontier); It; ++It) if (It->ActorHasTag(TEXT("PF.TempleUnderfloorNav"))) It->Destroy();
		FActorSpawnParameters Spawn; Spawn.OverrideLevel = Frontier->PersistentLevel;
		ANavModifierVolume* Exclusion = Frontier->SpawnActor<ANavModifierVolume>(Spawn);
		if (!Exclusion) return 35;
		UCubeBuilder* Shape = NewObject<UCubeBuilder>(); Shape->X = 5600.f; Shape->Y = 8000.f; Shape->Z = 200.f;
		UActorFactory::CreateBrushForVolumeActor(Exclusion, Shape); Exclusion->SetActorLocation(FVector(18200, 10000, 4200));
		Exclusion->SetAreaClass(UNavArea_Null::StaticClass()); Exclusion->Tags.Add(TEXT("PF.TempleUnderfloorNav"));
		Exclusion->SetActorLabel(TEXT("Exclude non-playable terrain below temple floors"));
	}
	if (FParse::Param(*Params, TEXT("BuildTempleLinks")))
	{
		if (!FrontierMap.EndsWith(TEXT("/L_FrontierTemple"))) return 27;
		for (TActorIterator<ANavLinkProxy> It(Frontier); It; ++It) if (It->ActorHasTag(TEXT("PF.TempleTraversal"))) It->Destroy();
		const TArray<TPair<FVector, FVector>> Portals = {
			{FVector(13800, 10000, 4210), FVector(14300, 10000, 4210)},
			{FVector(14900, 10000, 4210), FVector(15900, 10000, 4630)}};
		for (int32 Index = 0; Index < Portals.Num(); ++Index)
		{
			const FVector Origin = (Portals[Index].Key + Portals[Index].Value) * .5;
			FActorSpawnParameters Spawn; Spawn.OverrideLevel = Frontier->PersistentLevel;
			ANavLinkProxy* Proxy = Frontier->SpawnActor<ANavLinkProxy>(Origin, FRotator::ZeroRotator, Spawn);
			if (!Proxy) return 29;
			FNavigationLink Link; Link.Left = Portals[Index].Key - Origin; Link.Right = Portals[Index].Value - Origin;
			Link.Direction = ENavLinkDirection::BothWays; Link.SetAreaClass(UNavArea_Default::StaticClass());
			Link.bUseSnapHeight = true; Link.SnapHeight = 60.f; Link.SnapRadius = 120.f;
			Link.bSnapToCheapestArea = false; Link.MaxFallDownLength = 0.f;
			Proxy->PointLinks = {Link}; Proxy->Tags.Add(TEXT("PF.TempleTraversal")); Proxy->Tags.Add(TEXT("PF.Unapproved"));
			Proxy->SetActorLabel(Index == 0 ? TEXT("Temple entrance walkable doorway link") : TEXT("Temple courtyard walkable stair link"));
			FNavigationSystem::UpdateActorData(*Proxy);
		}
	}
	if (FParse::Param(*Params, TEXT("RebuildTemplePhysics")))
	{
		FString Match; FParse::Value(*Params, TEXT("TemplePhysicsMatch="), Match);
		TSet<UStaticMesh*> Rebuilt;
		for (TActorIterator<AActor> Actor(Frontier); Actor; ++Actor)
		{
			TInlineComponentArray<UStaticMeshComponent*> Components; Actor->GetComponents(Components);
			for (UStaticMeshComponent* Component : Components)
			{
				UStaticMesh* Mesh = Component->GetStaticMesh();
				if (!Mesh || Rebuilt.Contains(Mesh) || !Mesh->GetPathName().StartsWith(TEXT("/Game/ProjectF/Environment/Temple/Meshes/"))) continue;
				if (!Match.IsEmpty() && !Mesh->GetName().Contains(Match)) continue;
				UBodySetup* Body = Mesh->GetBodySetup();
				if (!Body || Body->CollisionTraceFlag != CTF_UseComplexAsSimple) continue;
				TArray<UStaticMesh*> WaitFor = {Mesh}; FStaticMeshCompilingManager::Get().FinishCompilation(WaitFor);
				const int32 Before = Body->TriMeshGeometries.Num();
				const bool bPreviouslySkipped = Body->bNeverNeedsCookedCollisionData;
				const int32 PreviousDefaultCollision = static_cast<int32>(Body->DefaultInstance.GetCollisionEnabled());
				Body->DefaultInstance.SetCollisionProfileName(TEXT("BlockAll"));
				Body->DefaultInstance.SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Body->DefaultInstance.SetResponseToAllChannels(ECR_Block);
				Body->bNeverNeedsCookedCollisionData = false; Body->InvalidatePhysicsData(); Body->CreatePhysicsMeshes();
				Mesh->CreateNavCollision(true);
				if (Body->TriMeshGeometries.IsEmpty() || !SaveAsset(Mesh)) return 28;
				UE_LOG(LogTemp, Display, TEXT("PF_TEMPLE_PHYSICS mesh=%s before=%d after=%d previously_skipped=%d default_collision_before=%d"), *Mesh->GetName(), Before, Body->TriMeshGeometries.Num(), bPreviouslySkipped, PreviousDefaultCollision);
				Rebuilt.Add(Mesh);
			}
		}
		for (TActorIterator<AActor> Actor(Frontier); Actor; ++Actor)
		{
			TInlineComponentArray<UStaticMeshComponent*> Components; Actor->GetComponents(Components);
			for (UStaticMeshComponent* Component : Components) if (Rebuilt.Contains(Component->GetStaticMesh())) Component->RecreatePhysicsState();
		}
		if (!Match.IsEmpty() && Rebuilt.IsEmpty()) return 37;
	}
	ANavMeshBoundsVolume* Bounds = nullptr;
	for (TActorIterator<ANavMeshBoundsVolume> It(Frontier); It; ++It) if (It->ActorHasTag(TEXT("PF.FrontierGenerated")))
	{
		if (!Bounds) Bounds = *It; else It->Destroy();
	}
	if (!Bounds) Bounds = Frontier->SpawnActor<ANavMeshBoundsVolume>();
	UCubeBuilder* Builder = NewObject<UCubeBuilder>(); Builder->X = 100000.f; Builder->Y = 100000.f; Builder->Z = 50000.f;
	UActorFactory::CreateBrushForVolumeActor(Bounds, Builder); Bounds->SetActorLocation(FVector(0, 0, 18000));
	Bounds->Tags.Add(TEXT("PF.FrontierGenerated"));
	FVector Camp( -34000, -16000, 1302 );
	for (TActorIterator<APlayerStart> It(Frontier); It; ++It) if (It->ActorHasTag(TEXT("PF.SafeSpawn"))) { Camp = It->GetActorLocation(); break; }
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Frontier))
	{
			if (FParse::Param(*Params, TEXT("ResetTempleNavigation")))
			{
				if (!FrontierMap.EndsWith(TEXT("/L_FrontierTemple"))) return 32;
				TArray<ARecastNavMesh*> Previous;
				for (TActorIterator<ARecastNavMesh> Mesh(Frontier); Mesh; ++Mesh) Previous.Add(*Mesh);
				for (ARecastNavMesh* Mesh : Previous) { Nav->UnregisterNavData(Mesh); Mesh->Destroy(); }
				if (!Nav->GetDefaultNavDataInstance(FNavigationSystem::Create)) return 33;
				UE_LOG(LogTemp, Display, TEXT("PF_TEMPLE_NAV reset_previous_data=%d"), Previous.Num());
			}
			if (FrontierMap.EndsWith(TEXT("/L_FrontierTemple")))
			{
				UClass* GuardClass = LoadClass<APFEnemyCharacter>(nullptr, TEXT("/Game/ProjectF/AI/BP_Guard.BP_Guard_C"));
				const APFEnemyCharacter* Guard = GuardClass ? GuardClass->GetDefaultObject<APFEnemyCharacter>() : nullptr;
				if (!Guard) return 31;
				const float StepHeight = Guard->GetCharacterMovement()->MaxStepHeight;
				for (TActorIterator<ARecastNavMesh> Mesh(Frontier); Mesh; ++Mesh)
					for (uint8 Index = 0; Index < static_cast<uint8>(ENavigationDataResolution::MAX); ++Index)
					{
						Mesh->SetAgentMaxStepHeight(static_cast<ENavigationDataResolution>(Index), StepHeight);
						Mesh->SetCellHeight(static_cast<ENavigationDataResolution>(Index), 5.f);
					}
				UE_LOG(LogTemp, Display, TEXT("PF_TEMPLE_NAV character_step_cm=%.1f voxel_height_cm=5"), StepHeight);
			}
		Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
			Nav->ReleaseInitialBuildingLock(); Nav->OnNavigationBoundsUpdated(Bounds); Nav->OnWorldInitDone(FNavigationSystemRunMode::EditorMode);
			for (TActorIterator<ANavLinkProxy> Link(Frontier); Link; ++Link) if (Link->ActorHasTag(TEXT("PF.TempleTraversal")))
			{
				UNavigationSystemV1::OnActorRegistered(*Link);
				FNavigationSystem::UpdateActorAndComponentData(**Link);
			}
			Nav->Tick(0.f); Nav->Build(); Nav->Tick(0.f);
			if (FParse::Param(*Params, TEXT("InspectTempleLinks")))
			{
				if (ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)))
				{
					FRecastDebugGeometry AllGeometry; Recast->GetDebugGeometryForTile(AllGeometry, INDEX_NONE);
					UE_LOG(LogTemp, Display, TEXT("PF_TEMPLE_LINK_SUMMARY vertices=%d links=%d forbidden=%d tiles=%d"), AllGeometry.MeshVerts.Num(), AllGeometry.OffMeshLinks.Num(), AllGeometry.ForbiddenLinks.Num(), Recast->GetNavMeshTilesCount());
					for (const FRecastDebugGeometry::FOffMeshLink& Link : AllGeometry.OffMeshLinks)
						UE_LOG(LogTemp, Display, TEXT("PF_TEMPLE_LINK_ALL left=%s right=%s valid_ends=%d radius=%.1f area=%d direction=%d generated=%d"), *Link.Left.ToString(), *Link.Right.ToString(), Link.ValidEnds, Link.Radius, Link.AreaID, Link.Direction, Link.bIsGenerated);
					for (int32 Tile = 0; Tile < Recast->GetNavMeshTilesCount(); ++Tile)
					{
						const FBox TileBounds = Recast->GetNavMeshTileBounds(Tile);
						if (!TileBounds.IsValid || TileBounds.Max.X < 13000 || TileBounds.Min.X > 17000 || TileBounds.Max.Y < 9000 || TileBounds.Min.Y > 11000) continue;
						FRecastDebugGeometry Geometry; Recast->GetDebugGeometryForTile(Geometry, Tile);
						for (const FRecastDebugGeometry::FOffMeshLink& Link : Geometry.OffMeshLinks)
							UE_LOG(LogTemp, Display, TEXT("PF_TEMPLE_LINK left=%s right=%s valid_ends=%d radius=%.1f height=%.1f"), *Link.Left.ToString(), *Link.Right.ToString(), Link.ValidEnds, Link.Radius, Link.Height);
					}
				}
			}
			if (FParse::Param(*Params, TEXT("BuildNavigationOnly")))
			{
				UE_LOG(LogTemp, Display, TEXT("PF_FRONTIER_NAV: build-only stage; gameplay layout is not yet validated"));
				return FEditorFileUtils::SaveLevel(Frontier->PersistentLevel, *FPackageName::LongPackageNameToFilename(FrontierMap, FPackageName::GetMapPackageExtension())) ? 0 : 14;
			}
		FNavLocation Point;
		if (!Nav->ProjectPointToNavigation(FVector(-14000, 0, 2700), Point, FVector(300, 300, 500))) return 13;
		TArray<FVector> Route = {Camp, FVector(-23500, -5800, 3000), FVector(-14000, -2500, 2700), FVector(3000, -800, 3000), FVector(17000, 7500, 4300)};
		if (bTempleLayout)
		{
			Route.Last() = FVector(12500, 10000, 4300);
			bool bFoundStronghold = false;
			for (TActorIterator<APFOutpost> It(Frontier); It; ++It)
			{
				if (It->OutpostId != TEXT("Stronghold")) continue;
				bFoundStronghold = true;
				Route.Add(It->GetActorLocation());
				if (!IsValid(It->Alarm) || It->GarrisonSpawns.Num() < 14) return 26;
				Route.Add(It->Alarm->GetActorLocation());
				for (const FTransform& Spawn : It->GarrisonSpawns)
				{
					FNavLocation SpawnNav;
					const FVector Location = Spawn.GetLocation();
					FCollisionQueryParams Query(SCENE_QUERY_STAT(PFTempleSpawn), false);
					if (!Nav->ProjectPointToNavigation(Location, SpawnNav, FVector(80, 80, 160))
						|| Frontier->OverlapBlockingTestByChannel(Location, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(42.f, 96.f), Query))
					{
						UE_LOG(LogTemp, Error, TEXT("PF_TEMPLE_NAV: blocked/off-nav garrison spawn %s"), *Location.ToString()); return 25;
					}
					Route.Add(Location);
				}
				TSet<int32> PatrolSlots;
				for (const FPFGarrisonPatrolRoute& Patrol : It->GarrisonPatrolRoutes)
				{
					if (!It->GarrisonSpawns.IsValidIndex(Patrol.SpawnIndex) || PatrolSlots.Contains(Patrol.SpawnIndex) || Patrol.Waypoints.Num() < 2) return 36;
					PatrolSlots.Add(Patrol.SpawnIndex);
					for (const FVector& Location : Patrol.Waypoints)
					{
						FNavLocation PatrolNav; FCollisionQueryParams Query(SCENE_QUERY_STAT(PFTemplePatrol), false);
						if (Location.ContainsNaN() || !Nav->ProjectPointToNavigation(Location, PatrolNav, FVector(80, 80, 160))
							|| Frontier->OverlapBlockingTestByChannel(Location, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(42.f, 96.f), Query)) return 36;
						Route.Add(Location);
					}
					Route.Add(Patrol.Waypoints[0]);
				}
			}
			if (!bFoundStronghold) return 26;
		}
		auto ProjectRoutePoint = [Nav, bTempleLayout](const FVector& Location, FNavLocation& Projected)
		{
			// The imported cinematic set also has surfaces below the terrain.
			// Keep temple checks near their authored floor instead of selecting a lower story.
			const bool bTemplePoint = bTempleLayout && Location.X >= 11000.f && Location.X <= 25000.f && Location.Y >= 4500.f && Location.Y <= 15500.f;
			return Nav->ProjectPointToNavigation(Location, Projected, FVector(300, 300, bTemplePoint ? 160.f : 3000.f));
		};
		for (int32 Index = 1; Index < Route.Num(); ++Index)
		{
			FNavLocation From, To;
			if (!ProjectRoutePoint(Route[Index - 1], From) || !ProjectRoutePoint(Route[Index], To)) return 19;
			UNavigationPath* RoutePath = UNavigationSystemV1::FindPathToLocationSynchronously(Frontier, From.Location, To.Location);
			if (!RoutePath || !RoutePath->IsValid() || RoutePath->IsPartial()) { UE_LOG(LogTemp, Error, TEXT("PF_FRONTIER_NAV: route %d is disconnected from=%s projected_from=%s to=%s projected_to=%s"), Index,
				*Route[Index - 1].ToString(), *From.Location.ToString(), *Route[Index].ToString(), *To.Location.ToString()); return 20; }
			UE_LOG(LogTemp, Display, TEXT("PF_FRONTIER_NAV: route %d connected, length %.1fm"), Index, RoutePath->GetPathLength() / 100.f);
		}
	}
	else return 22;
	TArray<FVector> Grounds = {Camp - FVector(0, 0, 102), FVector(-14000, -2500, 2600), FVector(17000, 7500, 4200)};
	if (bTempleLayout) Grounds.Pop();
	for (const FVector& Ground : Grounds)
	{
		FHitResult Hit;
		if (!Frontier->LineTraceSingleByChannel(Hit, Ground + FVector(0, 0, 10000), Ground - FVector(0, 0, 2000), ECC_Visibility) || FMath::Abs(Hit.ImpactPoint.Z - Ground.Z) > 35.f)
		{
			UE_LOG(LogTemp, Error, TEXT("PF_FRONTIER_NAV: expected ground %s, actual %s"), *Ground.ToString(), *Hit.ImpactPoint.ToString()); return 21;
		}
	}
	return FEditorFileUtils::SaveLevel(Frontier->PersistentLevel, *FPackageName::LongPackageNameToFilename(FrontierMap, FPackageName::GetMapPackageExtension())) ? 0 : 14;

}
}
