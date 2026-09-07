#include "EnemyBlueprintAutomationLibrary.h"

#include "../aether_test/ANS_FinisherCinematic.h"
#include "../aether_test/ANS_MeleeHitbox.h"
#include "../aether_test/ANS_TimedNiagaraEffect.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotify_PlayNiagaraEffect.h"
#include "Animation/AnimTypes.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_InputAction.h"
#include "K2Node_VariableGet.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "LevelEditor.h"
#include "IAssetViewport.h"
#include "Misc/PackageName.h"
#include "Materials/MaterialInterface.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "PlayInEditorDataTypes.h"
#include "Particles/ParticleSystem.h"
#include "ScopedTransaction.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRibbonRendererProperties.h"

FString UEnemyBlueprintAutomationLibrary::ApplyEnemyP0PlayerValidGuard()
{
	if (GEditor && GEditor->PlayWorld)
	{
		return TEXT("Refused: stop PIE/SIE before modifying BP_Enemy.");
	}

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Game/Combat/GAS/BP_Enemy.BP_Enemy"));
	UEdGraph* Graph = Blueprint && !Blueprint->UbergraphPages.IsEmpty() ? Blueprint->UbergraphPages[0] : nullptr;
	if (!Graph)
	{
		return TEXT("Refused: BP_Enemy EventGraph was not found.");
	}

	const UFunction* GetPlayerCharacterFunction = UGameplayStatics::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UGameplayStatics, GetPlayerCharacter));
	const UFunction* IsValidFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, IsValid));
	if (!GetPlayerCharacterFunction || !IsValidFunction)
	{
		return TEXT("Refused: Engine utility functions required for P0 were not found.");
	}

	UK2Node_CallFunction* GetPlayerCharacterNode = nullptr;
	TArray<UK2Node_CallFunction*> IsValidNodes;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
		if (!CallNode)
		{
			continue;
		}

		if (CallNode->GetTargetFunction() == GetPlayerCharacterFunction)
		{
			GetPlayerCharacterNode = CallNode;
		}

		if (CallNode->GetTargetFunction() == IsValidFunction)
		{
			IsValidNodes.Add(CallNode);
		}
	}

	if (!GetPlayerCharacterNode)
	{
		return TEXT("Refused: GetPlayerCharacter(0) node was not found in BP_Enemy EventGraph.");
	}
	for (UK2Node_CallFunction* IsValidNode : IsValidNodes)
	{
		UEdGraphPin* ObjectPin = IsValidNode ? IsValidNode->FindPin(TEXT("Object")) : nullptr;
		UEdGraphPin* ReturnPin = IsValidNode ? IsValidNode->GetReturnValuePin() : nullptr;
		if (!ObjectPin || !ReturnPin)
		{
			continue;
		}
		const bool bReadsPlayer = ObjectPin->LinkedTo.ContainsByPredicate([GetPlayerCharacterNode](const UEdGraphPin* Pin)
		{
			return Pin && Pin->GetOwningNode() == GetPlayerCharacterNode;
		});
		const bool bFeedsBranch = ReturnPin->LinkedTo.ContainsByPredicate([](const UEdGraphPin* Pin)
		{
			return Pin && Cast<UK2Node_IfThenElse>(Pin->GetOwningNode()) != nullptr;
		});
		if (bReadsPlayer && bFeedsBranch)
		{
			// Keep the existing reflected entry point useful during Live Coding: UE 5.4
			// does not expose a newly-added UFUNCTION until the editor restarts.
			return ApplyEnemyReactionMontageGuard();
		}
	}

	// This is the known range/attack branch in BP_Enemy. The GUID is used only after validating the node type and input wire.
	const FGuid RangeBranchGuid(0x777AC0A3, 0x456F9B45, 0xA0547B87, 0xAEF7CF81);
	UK2Node_IfThenElse* RangeBranch = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->NodeGuid == RangeBranchGuid)
		{
			RangeBranch = Cast<UK2Node_IfThenElse>(Node);
			break;
		}
	}
	if (!RangeBranch)
	{
		return TEXT("Refused: BP_Enemy's distance/attack branch changed since audit; no graph wire was modified.");
	}

	UEdGraphPin* RangeExecInput = RangeBranch->GetExecPin();
	if (!RangeExecInput || RangeExecInput->LinkedTo.Num() != 1)
	{
		return TEXT("Refused: distance/attack branch did not have exactly one input execution wire.");
	}
	UEdGraphPin* PreviousExec = RangeExecInput->LinkedTo[0];
	UEdGraphPin* PlayerReturn = GetPlayerCharacterNode->GetReturnValuePin();
	if (!PreviousExec || !PlayerReturn)
	{
		return TEXT("Refused: required BP_Enemy pins were unavailable.");
	}

	const FScopedTransaction Transaction(NSLOCTEXT("Combat", "AddEnemyP0PlayerGuard", "Add Enemy Player Validity Guard"));
	Blueprint->Modify();
	Graph->Modify();
	RangeBranch->Modify();

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	UK2Node_CallFunction* IsValidNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CallFunction>(
		Graph,
		FVector2D(RangeBranch->NodePosX - 180, RangeBranch->NodePosY - 165),
		EK2NewNodeFlags::None,
		[IsValidFunction](UK2Node_CallFunction* NewNode)
		{
			NewNode->SetFromFunction(IsValidFunction);
		});
	UK2Node_IfThenElse* GuardBranch = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_IfThenElse>(
		Graph,
		FVector2D(RangeBranch->NodePosX - 20, RangeBranch->NodePosY - 165), EK2NewNodeFlags::None);

	if (!IsValidNode || !GuardBranch)
	{
		return TEXT("Refused: could not create the P0 guard nodes.");
	}

	GuardBranch->NodeComment = TEXT("CODEX_P0_PLAYER_VALID_GUARD");
	UEdGraphPin* IsValidObject = IsValidNode->FindPin(TEXT("Object"));
	UEdGraphPin* IsValidReturn = IsValidNode->GetReturnValuePin();
	if (!IsValidObject || !IsValidReturn || !Schema->TryCreateConnection(PlayerReturn, IsValidObject))
	{
		return TEXT("Refused: P0 nodes were created but the player validity data link could not be completed; undo the transaction and inspect BP_Enemy.");
	}
	Schema->BreakSinglePinLink(PreviousExec, RangeExecInput);
	if (!Schema->TryCreateConnection(PreviousExec, GuardBranch->GetExecPin()) ||
		!Schema->TryCreateConnection(IsValidReturn, GuardBranch->GetConditionPin()) ||
		!Schema->TryCreateConnection(GuardBranch->GetThenPin(), RangeExecInput))
	{
		return TEXT("Refused: P0 nodes were created but their execution links could not be completed; undo the transaction and inspect BP_Enemy.");
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return TEXT("Applied: added Branch(IsValid(GetPlayerCharacter(0))) before BP_Enemy's distance/attack branch.");
}

FString UEnemyBlueprintAutomationLibrary::ApplyEnemyReactionMontageGuard()
{
	if (GEditor && GEditor->PlayWorld)
	{
		return TEXT("Refused: stop PIE/SIE before modifying BP_Enemy.");
	}

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Game/Combat/GAS/BP_Enemy.BP_Enemy"));
	UEdGraph* Graph = Blueprint && !Blueprint->UbergraphPages.IsEmpty() ? Blueprint->UbergraphPages[0] : nullptr;
	if (!Graph)
	{
		return TEXT("Refused: BP_Enemy EventGraph was not found.");
	}

	UK2Node_VariableGet* ExistingMeshNode = nullptr;
	UK2Node_IfThenElse* ExistingReactionGuard = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_VariableGet* VariableGet = Cast<UK2Node_VariableGet>(Node))
		{
			if (VariableGet->VariableReference.GetMemberName() == TEXT("Mesh") &&
				VariableGet->VariableReference.IsSelfContext())
			{
				ExistingMeshNode = VariableGet;
			}
		}
		if (Node && Node->NodeComment == TEXT("CODEX_REACTION_MONTAGE_GUARD"))
		{
			ExistingReactionGuard = Cast<UK2Node_IfThenElse>(Node);
		}
	}
	if (!ExistingMeshNode)
	{
		return TEXT("Refused: BP_Enemy's existing self-context Get Mesh node was not found.");
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	if (ExistingReactionGuard)
	{
		UEdGraphPin* Condition = ExistingReactionGuard->GetConditionPin();
		UK2Node_CallFunction* IsPlayingNode = Condition && Condition->LinkedTo.Num() == 1
			? Cast<UK2Node_CallFunction>(Condition->LinkedTo[0]->GetOwningNode()) : nullptr;
		UEdGraphPin* IsPlayingSelf = IsPlayingNode ? IsPlayingNode->FindPin(UEdGraphSchema_K2::PN_Self) : nullptr;
		UK2Node_CallFunction* GetAnimNode = IsPlayingSelf && IsPlayingSelf->LinkedTo.Num() == 1
			? Cast<UK2Node_CallFunction>(IsPlayingSelf->LinkedTo[0]->GetOwningNode()) : nullptr;
		UEdGraphPin* GetAnimSelf = GetAnimNode ? GetAnimNode->FindPin(UEdGraphSchema_K2::PN_Self) : nullptr;
		if (!GetAnimSelf)
		{
			return TEXT("Refused: existing reaction guard is incomplete and could not be repaired automatically.");
		}

		UEdGraphNode* InvalidMeshNode = GetAnimSelf->LinkedTo.Num() == 1 ? GetAnimSelf->LinkedTo[0]->GetOwningNode() : nullptr;
		Schema->BreakPinLinks(*GetAnimSelf, true);
		if (!Schema->TryCreateConnection(ExistingMeshNode->GetValuePin(), GetAnimSelf))
		{
			return TEXT("Refused: existing Get Mesh could not be connected to the reaction guard.");
		}
		if (InvalidMeshNode && InvalidMeshNode != ExistingMeshNode)
		{
			FBlueprintEditorUtils::RemoveNode(Blueprint, InvalidMeshNode, true);
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return TEXT("Repaired: reaction guard now reuses BP_Enemy's valid Get Mesh node.");
	}

	UK2Node_IfThenElse* AttackingBranch = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UK2Node_IfThenElse* Branch = Cast<UK2Node_IfThenElse>(Node);
		if (!Branch)
		{
			continue;
		}
		for (const UEdGraphPin* LinkedPin : Branch->GetConditionPin()->LinkedTo)
		{
			const UK2Node_VariableGet* VariableGet = LinkedPin ? Cast<UK2Node_VariableGet>(LinkedPin->GetOwningNode()) : nullptr;
			if (VariableGet && VariableGet->VariableReference.GetMemberName() == TEXT("bAttacking"))
			{
				AttackingBranch = Branch;
				break;
			}
		}
		if (AttackingBranch)
		{
			break;
		}
	}

	UEdGraphPin* IdleExec = AttackingBranch ? AttackingBranch->GetElsePin() : nullptr;
	if (!IdleExec || IdleExec->LinkedTo.Num() != 1)
	{
		return TEXT("Refused: the bAttacking=false execution wire was not found uniquely.");
	}
	UEdGraphPin* PreviousNextExec = IdleExec->LinkedTo[0];

	const UFunction* GetAnimInstanceFunction = USkeletalMeshComponent::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(USkeletalMeshComponent, GetAnimInstance));
	const UFunction* IsAnyMontagePlayingFunction = UAnimInstance::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UAnimInstance, IsAnyMontagePlaying));
	if (!GetAnimInstanceFunction || !IsAnyMontagePlayingFunction)
	{
		return TEXT("Refused: required animation functions were not found.");
	}

	const FScopedTransaction Transaction(NSLOCTEXT("Combat", "AddEnemyReactionMontageGuard", "Add Enemy Reaction Montage Guard"));
	Blueprint->Modify();
	Graph->Modify();
	AttackingBranch->Modify();

	UK2Node_CallFunction* GetAnimInstanceNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CallFunction>(
		Graph, FVector2D(AttackingBranch->NodePosX + 440, AttackingBranch->NodePosY + 160), EK2NewNodeFlags::None,
		[GetAnimInstanceFunction](UK2Node_CallFunction* NewNode)
		{
			NewNode->SetFromFunction(GetAnimInstanceFunction);
		});
	UK2Node_CallFunction* IsAnyMontagePlayingNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CallFunction>(
		Graph, FVector2D(AttackingBranch->NodePosX + 680, AttackingBranch->NodePosY + 160), EK2NewNodeFlags::None,
		[IsAnyMontagePlayingFunction](UK2Node_CallFunction* NewNode)
		{
			NewNode->SetFromFunction(IsAnyMontagePlayingFunction);
		});
	UK2Node_IfThenElse* ReactionGuard = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_IfThenElse>(
		Graph, FVector2D(AttackingBranch->NodePosX + 900, AttackingBranch->NodePosY), EK2NewNodeFlags::None);

	if (!GetAnimInstanceNode || !IsAnyMontagePlayingNode || !ReactionGuard)
	{
		return TEXT("Refused: could not create the reaction guard nodes; undo the transaction and inspect BP_Enemy.");
	}

	ReactionGuard->NodeComment = TEXT("CODEX_REACTION_MONTAGE_GUARD");
	UEdGraphPin* MeshValue = ExistingMeshNode->GetValuePin();
	UEdGraphPin* GetAnimSelf = GetAnimInstanceNode->FindPin(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* AnimInstanceValue = GetAnimInstanceNode->GetReturnValuePin();
	UEdGraphPin* IsPlayingSelf = IsAnyMontagePlayingNode->FindPin(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* IsPlayingValue = IsAnyMontagePlayingNode->GetReturnValuePin();
	if (!MeshValue || !GetAnimSelf || !AnimInstanceValue || !IsPlayingSelf || !IsPlayingValue ||
		!Schema->TryCreateConnection(MeshValue, GetAnimSelf) ||
		!Schema->TryCreateConnection(AnimInstanceValue, IsPlayingSelf) ||
		!Schema->TryCreateConnection(IsPlayingValue, ReactionGuard->GetConditionPin()))
	{
		return TEXT("Refused: reaction guard data links could not be completed; undo the transaction and inspect BP_Enemy.");
	}

	Schema->BreakSinglePinLink(IdleExec, PreviousNextExec);
	if (!Schema->TryCreateConnection(IdleExec, ReactionGuard->GetExecPin()) ||
		!Schema->TryCreateConnection(ReactionGuard->GetElsePin(), PreviousNextExec))
	{
		return TEXT("Refused: reaction guard execution links could not be completed; undo the transaction and inspect BP_Enemy.");
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return TEXT("Applied: BP_Enemy now waits until no montage is playing before chase/attack.");
}

FString UEnemyBlueprintAutomationLibrary::StartNormalPIE()
{
	if (!GEditor)
	{
		return TEXT("Refused: UnrealEd is unavailable.");
	}
	if (GEditor->PlayWorld)
	{
		return TEXT("Already running: PIE/SIE is active.");
	}

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	FRequestPlaySessionParams SessionParams;
	SessionParams.DestinationSlateViewport = LevelEditorModule.GetFirstActiveViewport();
	GEditor->RequestPlaySession(SessionParams);
	return TEXT("Requested: normal PIE session.");
}

FString UEnemyBlueprintAutomationLibrary::EndPlaySession()
{
	if (!GEditor || !GEditor->PlayWorld)
	{
		return TEXT("No PIE/SIE session is active.");
	}
	GEditor->RequestEndPlayMap();
	return TEXT("Requested: end PIE/SIE session.");
}

FString UEnemyBlueprintAutomationLibrary::ConfigureDiveD3HitWindows()
{
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Game/Combat/Montages/AM_Dive_D3.AM_Dive_D3"));
	if (!Montage)
	{
		return TEXT("Refused: AM_Dive_D3 could not be loaded.");
	}

	struct FDiveHitWindow
	{
		float Start;
		float Duration;
		float Damage;
		float Radius;
		float Knockback;
		float LiftZ;
		bool bPlayReaction;
	};

	// Keep all hit windows inside the authored 0.172-0.845s contact region.
	// The first three carry the victim along the aerial spin; the last is the impact.
	static constexpr FDiveHitWindow Windows[] = {
		{0.18f, 0.12f, 3.f, 85.f, 140.f, 20.f, false},
		{0.34f, 0.12f, 3.f, 90.f, 160.f, 25.f, false},
		{0.50f, 0.12f, 3.f, 95.f, 180.f, 35.f, false},
		{0.68f, 0.14f, 6.f, 130.f, 500.f, 100.f, true},
	};

	TArray<UANS_MeleeHitbox*> ExistingHitboxes;
	TSubclassOf<UGameplayEffect> PreservedEffect;
	bool bPreservedOnlyHitPlayers = false;
	float PreservedAttackCooldown = 1.25f;
	bool bPreservedDrawDebug = false;
	for (int32 Index = Montage->Notifies.Num() - 1; Index >= 0; --Index)
	{
		UANS_MeleeHitbox* Hitbox = Cast<UANS_MeleeHitbox>(Montage->Notifies[Index].NotifyStateClass.Get());
		if (!Hitbox)
		{
			continue;
		}
		if (ExistingHitboxes.IsEmpty())
		{
			PreservedEffect = Hitbox->ExtraEffectOnHit;
			bPreservedOnlyHitPlayers = Hitbox->bOnlyHitPlayers;
			PreservedAttackCooldown = Hitbox->AttackCooldown;
			bPreservedDrawDebug = Hitbox->bDrawDebug;
		}
		ExistingHitboxes.Insert(Hitbox, 0);
		Montage->Notifies.RemoveAt(Index);
	}

	Montage->Modify();
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Windows); ++Index)
	{
		UANS_MeleeHitbox* Hitbox = ExistingHitboxes.IsValidIndex(Index)
			? ExistingHitboxes[Index]
			: NewObject<UANS_MeleeHitbox>(Montage, UANS_MeleeHitbox::StaticClass(), NAME_None, RF_Transactional);
		if (!Hitbox)
		{
			return TEXT("Refused: could not create a D3 hitbox notify state.");
		}

		Hitbox->Modify();
		Hitbox->SocketName = TEXT("pelvis");
		Hitbox->Radius = Windows[Index].Radius;
		Hitbox->Damage = Windows[Index].Damage;
		Hitbox->LaunchZ = 0.f;
		Hitbox->HorizontalKnockback = Windows[Index].Knockback;
		Hitbox->KnockbackLiftZ = Windows[Index].LiftZ;
		Hitbox->KnockbackDirection = ECombatKnockbackDirection::AttackerForward;
		Hitbox->bPlayHitReaction = Windows[Index].bPlayReaction;
		Hitbox->ExtraEffectOnHit = PreservedEffect;
		Hitbox->bOnlyHitPlayers = bPreservedOnlyHitPlayers;
		Hitbox->AttackCooldown = PreservedAttackCooldown;
		Hitbox->bDrawDebug = bPreservedDrawDebug;

		FAnimNotifyEvent& Event = Montage->Notifies.AddDefaulted_GetRef();
		Event.NotifyName = FName(*Hitbox->GetNotifyName());
		Event.Link(Montage, Windows[Index].Start, 0);
		Event.TriggerTimeOffset = 0.f;
		Event.EndTriggerTimeOffset = 0.f;
		Event.TrackIndex = 1;
		Event.Notify = nullptr;
		Event.NotifyStateClass = Hitbox;
		Event.SetDuration(Windows[Index].Duration);
		Event.EndLink.Link(Montage, Event.EndLink.GetTime(), 0);
		Event.MontageTickType = EMontageNotifyTickType::Queued;
		Event.NotifyTriggerChance = 1.f;
#if WITH_EDITORONLY_DATA
		Event.DisplayTime_DEPRECATED = Windows[Index].Start;
		Event.Guid = FGuid::NewGuid();
#endif
	}

	Montage->SortNotifies();
	Montage->InitializeNotifyTrack();
	Montage->RefreshCacheData();
	Montage->MarkPackageDirty();

	UPackage* Package = Montage->GetOutermost();
	FString PackageFileName;
	if (!Package || !FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), PackageFileName, FPackageName::GetAssetPackageExtension()))
	{
		return TEXT("Applied in memory, but could not resolve AM_Dive_D3's package filename.");
	}

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	if (!UPackage::SavePackage(Package, Montage, *PackageFileName, SaveArgs))
	{
		return TEXT("Applied in memory, but saving AM_Dive_D3 failed.");
	}

	return FString::Printf(TEXT("Applied: AM_Dive_D3 now has %d hit windows (saved %s)."),
		UE_ARRAY_COUNT(Windows), *PackageFileName);
}

FString UEnemyBlueprintAutomationLibrary::DumpBlueprintEventGraph(const FString& BlueprintAssetPath)
{
	const FString ObjectPath = BlueprintAssetPath.EndsWith(TEXT("."))
		? BlueprintAssetPath
		: BlueprintAssetPath + TEXT(".") + FPackageName::GetShortName(BlueprintAssetPath);
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
	if (!Blueprint || Blueprint->UbergraphPages.IsEmpty())
	{
		return FString::Printf(TEXT("Refused: Blueprint not found or has no EventGraph (%s)."), *ObjectPath);
	}

	FString Dump;
	UEdGraph* Graph = Blueprint->UbergraphPages[0];
	Dump += FString::Printf(TEXT("Blueprint=%s Graph=%s Nodes=%d\n"), *ObjectPath, *Graph->GetName(), Graph->Nodes.Num());
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}
		Dump += FString::Printf(TEXT("NODE %s Guid=%s Comment=%s Title=%s\n"),
			*Node->GetClass()->GetName(), *Node->NodeGuid.ToString(), *Node->NodeComment,
			*Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
		{
			Dump += FString::Printf(TEXT("  FUNCTION %s\n"), *Call->GetFunctionName().ToString());
		}
		else if (UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
		{
			Dump += FString::Printf(TEXT("  EVENT %s\n"), *Event->EventReference.GetMemberName().ToString());
		}
		else if (UK2Node_InputAction* Input = Cast<UK2Node_InputAction>(Node))
		{
			Dump += FString::Printf(TEXT("  INPUT %s\n"), *Input->InputActionName.ToString());
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->LinkedTo.IsEmpty())
			{
				continue;
			}
			Dump += FString::Printf(TEXT("  PIN %s %s ->"), *Pin->PinName.ToString(),
				Pin->Direction == EGPD_Input ? TEXT("IN") : TEXT("OUT"));
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin && LinkedPin->GetOwningNode())
				{
					Dump += FString::Printf(TEXT(" %s.%s"),
						*LinkedPin->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView).ToString(),
						*LinkedPin->PinName.ToString());
				}
			}
			Dump += TEXT("\n");
		}
	}
	UE_LOG(LogTemp, Display, TEXT("%s"), *Dump);
	return Dump;
}

FString UEnemyBlueprintAutomationLibrary::ConfigurePlayerStaminaHitWindows()
{
	static const TCHAR* SkillPaths[] = {
		TEXT("/Game/Game/Combat/Montages/AM_Skill_S4.AM_Skill_S4"),
		TEXT("/Game/Game/Combat/Montages/AM_Skill_S5.AM_Skill_S5"),
		TEXT("/Game/Game/Combat/Montages/AM_Skill_S6.AM_Skill_S6"),
		TEXT("/Game/Game/Combat/Montages/AM_Skill_S7.AM_Skill_S7")
	};
	static const TCHAR* NormalPaths[] = {
		TEXT("/Game/Game/Combat/Montages/AM_Ground_A1.AM_Ground_A1"),
		TEXT("/Game/Game/Combat/Montages/AM_Ground_A2.AM_Ground_A2"),
		TEXT("/Game/Game/Combat/Montages/AM_Ground_A3.AM_Ground_A3"),
		TEXT("/Game/Game/Combat/Montages/AM_Ground_A4.AM_Ground_A4"),
		TEXT("/Game/Game/Combat/Montages/AM_Launcher_L2.AM_Launcher_L2"),
		TEXT("/Game/Game/Combat/Montages/AM_Dive_D3.AM_Dive_D3")
	};

	int32 ModifiedMontages = 0;
	int32 ModifiedHitboxes = 0;
	FString Missing;
	auto Configure = [&ModifiedMontages, &ModifiedHitboxes, &Missing](const TCHAR* Path, bool bSkill, int32 SkillIndex)
	{
		UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, Path);
		if (!Montage)
		{
			if (!Missing.IsEmpty())
			{
				Missing += TEXT(", ");
			}
			Missing += Path;
			return;
		}

		bool bChanged = false;
		for (FAnimNotifyEvent& Event : Montage->Notifies)
		{
			UANS_MeleeHitbox* Hitbox = Cast<UANS_MeleeHitbox>(Event.NotifyStateClass.Get());
			if (!Hitbox)
			{
				continue;
			}

			Hitbox->Modify();
			if (bSkill)
			{
				Hitbox->StaminaCostOnBegin = 12.5f;
				Hitbox->MinimumStaminaOnBegin = SkillIndex == 0 ? 50.f : 0.f;
				Hitbox->StaminaGainOnConfirmedHit = 0.f;
				Hitbox->HorizontalKnockback = SkillIndex == 2 ? 350.f : SkillIndex == 3 ? 600.f : 0.f;
				Hitbox->KnockbackLiftZ = SkillIndex == 2 ? 60.f : SkillIndex == 3 ? 120.f : 0.f;
				Hitbox->KnockbackDirection = ECombatKnockbackDirection::AttackerForward;
			}
			else
			{
				Hitbox->StaminaCostOnBegin = 0.f;
				Hitbox->MinimumStaminaOnBegin = 0.f;
				Hitbox->StaminaGainOnConfirmedHit = 8.f;
			}
			bChanged = true;
			++ModifiedHitboxes;
		}

		if (!bChanged)
		{
			return;
		}
		Montage->Modify();
		Montage->MarkPackageDirty();
		Montage->SortNotifies();
		Montage->InitializeNotifyTrack();
		Montage->RefreshCacheData();
		UPackage* Package = Montage->GetOutermost();
		FString PackageFileName;
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GError;
		if (Package && FPackageName::TryConvertLongPackageNameToFilename(
			Package->GetName(), PackageFileName, FPackageName::GetAssetPackageExtension()) &&
			UPackage::SavePackage(Package, Montage, *PackageFileName, SaveArgs))
		{
			++ModifiedMontages;
		}
	};

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SkillPaths); ++Index)
	{
		Configure(SkillPaths[Index], true, Index);
	}
	for (const TCHAR* Path : NormalPaths)
	{
		Configure(Path, false, INDEX_NONE);
	}

	FString Result = FString::Printf(TEXT("Applied: %d hitboxes across %d montages (E=%0.1f / %d hits, normal gain=%0.1f)."),
		ModifiedHitboxes, ModifiedMontages, 50.f / 4.f, 4, 8.f);
	if (!Missing.IsEmpty())
	{
		Result += FString::Printf(TEXT(" Missing: %s"), *Missing);
	}
	return Result;
}

FString UEnemyBlueprintAutomationLibrary::ConfigureConfirmedHitVFXTransformSample()
{
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Game/Combat/Montages/AM_Ground_A1.AM_Ground_A1"));
	UNiagaraSystem* SampleVFX = LoadObject<UNiagaraSystem>(nullptr,
		TEXT("/Game/Mixed_Magic_VFX_Pack/VFX/NS_Dark_Solo_Impact.NS_Dark_Solo_Impact"));
	if (!Montage || !SampleVFX)
	{
		return TEXT("Refused: AM_Ground_A1 or NS_Dark_Solo_Impact could not be loaded.");
	}

	UANS_MeleeHitbox* SampleHitbox = nullptr;
	for (FAnimNotifyEvent& Event : Montage->Notifies)
	{
		if (UANS_MeleeHitbox* Candidate = Cast<UANS_MeleeHitbox>(Event.NotifyStateClass.Get()))
		{
			SampleHitbox = Candidate;
			break;
		}
	}
	if (!SampleHitbox)
	{
		return TEXT("Refused: AM_Ground_A1 has no ANS_MeleeHitbox notify.");
	}

	SampleHitbox->Modify();
	SampleHitbox->ConfirmedHitVFXEntries.Reset();
	FConfirmedHitVFXEntry& Entry = SampleHitbox->ConfirmedHitVFXEntries.AddDefaulted_GetRef();
	Entry.Effect = SampleVFX;
	Entry.LocationOffset = FVector(0.f, 0.f, 8.f);
	Entry.RotationOffset = FRotator(0.f, 0.f, 90.f);
	Entry.Scale = FVector(0.35f, 0.35f, 0.35f);
	Entry.bAlignToHitNormal = true;
	SampleHitbox->ConfirmedHitVFXList.Reset();
	SampleHitbox->ConfirmedHitParticleVFXList.Reset();
	SampleHitbox->ConfirmedHitVFX = nullptr;
	SampleHitbox->ConfirmedHitParticleVFX = nullptr;
	SampleHitbox->ConfirmedHitVFXScale = 1.f;

	Montage->Modify();
	Montage->MarkPackageDirty();
	Montage->SortNotifies();
	Montage->InitializeNotifyTrack();
	Montage->RefreshCacheData();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	UPackage* Package = Montage->GetOutermost();
	FString PackageFileName;
	if (!Package || !FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), PackageFileName, FPackageName::GetAssetPackageExtension()) ||
		!UPackage::SavePackage(Package, Montage, *PackageFileName, SaveArgs))
	{
		return TEXT("Applied the transform sample in memory, but saving AM_Ground_A1 failed.");
	}

	return TEXT("Applied unified transform sample to AM_Ground_A1: NS_Dark_Solo_Impact, Z+8, Roll+90, Scale 0.35.");
}

FString UEnemyBlueprintAutomationLibrary::MigrateHitboxVFXToUnifiedEntries()
{
	if (GEditor && GEditor->PlayWorld)
	{
		return TEXT("Refused: stop PIE/SIE before migrating hitbox VFX.");
	}

	static const TCHAR* MontagePaths[] = {
		TEXT("/Game/Game/Combat/Montages/AM_Ground_A1.AM_Ground_A1"),
		TEXT("/Game/Game/Combat/Montages/AM_Ground_A2.AM_Ground_A2"),
		TEXT("/Game/Game/Combat/Montages/AM_Ground_A3.AM_Ground_A3"),
		TEXT("/Game/Game/Combat/Montages/AM_Ground_A4.AM_Ground_A4"),
		TEXT("/Game/Game/Combat/Montages/AM_Launcher_L2.AM_Launcher_L2"),
		TEXT("/Game/Game/Combat/Montages/AM_Skill_S4.AM_Skill_S4"),
		TEXT("/Game/Game/Combat/Montages/AM_Skill_S5.AM_Skill_S5"),
		TEXT("/Game/Game/Combat/Montages/AM_Skill_S6.AM_Skill_S6"),
		TEXT("/Game/Game/Combat/Montages/AM_Skill_S7.AM_Skill_S7"),
		TEXT("/Game/Game/Combat/Montages/AM_Dive_D3.AM_Dive_D3")
	};

	int32 MigratedHitboxes = 0;
	int32 ModifiedMontages = 0;
	int32 RemovedLegacySlots = 0;
	FString Missing;
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;

	auto AppendUniqueEffect = [](TArray<FConfirmedHitVFXEntry>& Entries, UFXSystemAsset* Effect, float Scale)
	{
		if (!Effect)
		{
			return false;
		}
		for (const FConfirmedHitVFXEntry& Existing : Entries)
		{
			if (Existing.Effect.Get() == Effect)
			{
				return false;
			}
		}
		FConfirmedHitVFXEntry& NewEntry = Entries.AddDefaulted_GetRef();
		NewEntry.Effect = Effect;
		NewEntry.Scale = FVector(FMath::Max(0.01f, Scale));
		NewEntry.bAlignToHitNormal = true;
		return true;
	};

	for (const TCHAR* Path : MontagePaths)
	{
		UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, Path);
		if (!Montage)
		{
			if (!Missing.IsEmpty())
			{
				Missing += TEXT(", ");
			}
			Missing += Path;
			continue;
		}

		bool bMontageChanged = false;
		for (FAnimNotifyEvent& Event : Montage->Notifies)
		{
			UANS_MeleeHitbox* Hitbox = Cast<UANS_MeleeHitbox>(Event.NotifyStateClass.Get());
			if (!Hitbox)
			{
				continue;
			}

			const float LegacyScale = FMath::Max(0.01f, Hitbox->ConfirmedHitVFXScale);
			TArray<FConfirmedHitVFXEntry> Entries = Hitbox->ConfirmedHitVFXEntries;
			for (FConfirmedHitVFXEntry& Entry : Entries)
			{
				Entry.Scale.X = FMath::Max(0.01f, Entry.Scale.X * LegacyScale);
				Entry.Scale.Y = FMath::Max(0.01f, Entry.Scale.Y * LegacyScale);
				Entry.Scale.Z = FMath::Max(0.01f, Entry.Scale.Z * LegacyScale);
			}

			for (const TObjectPtr<UNiagaraSystem>& Effect : Hitbox->ConfirmedHitVFXList)
			{
				if (AppendUniqueEffect(Entries, Effect.Get(), LegacyScale))
				{
					++RemovedLegacySlots;
				}
			}
			for (const TObjectPtr<UParticleSystem>& Effect : Hitbox->ConfirmedHitParticleVFXList)
			{
				if (AppendUniqueEffect(Entries, Effect.Get(), LegacyScale))
				{
					++RemovedLegacySlots;
				}
			}
			if (AppendUniqueEffect(Entries, Hitbox->ConfirmedHitVFX, LegacyScale))
			{
				++RemovedLegacySlots;
			}
			if (AppendUniqueEffect(Entries, Hitbox->ConfirmedHitParticleVFX, LegacyScale))
			{
				++RemovedLegacySlots;
			}

			Entries.RemoveAll([](const FConfirmedHitVFXEntry& Entry)
			{
				return Entry.Effect == nullptr;
			});

			const int32 LegacyCount = Hitbox->ConfirmedHitVFXList.Num()
				+ Hitbox->ConfirmedHitParticleVFXList.Num()
				+ (Hitbox->ConfirmedHitVFX ? 1 : 0)
				+ (Hitbox->ConfirmedHitParticleVFX ? 1 : 0);
			const bool bHadLegacyData = LegacyCount > 0 || !FMath::IsNearlyEqual(LegacyScale, 1.f);
			if (bHadLegacyData || Entries.Num() != Hitbox->ConfirmedHitVFXEntries.Num())
			{
				Hitbox->Modify();
				Hitbox->ConfirmedHitVFXEntries = MoveTemp(Entries);
				Hitbox->ConfirmedHitVFXList.Reset();
				Hitbox->ConfirmedHitParticleVFXList.Reset();
				Hitbox->ConfirmedHitVFX = nullptr;
				Hitbox->ConfirmedHitParticleVFX = nullptr;
				Hitbox->ConfirmedHitVFXScale = 1.f;
				bMontageChanged = true;
				++MigratedHitboxes;
			}
		}

		if (!bMontageChanged)
		{
			continue;
		}

		Montage->Modify();
		Montage->MarkPackageDirty();
		UPackage* Package = Montage->GetOutermost();
		FString PackageFileName;
		if (!Package || !FPackageName::TryConvertLongPackageNameToFilename(
			Package->GetName(), PackageFileName, FPackageName::GetAssetPackageExtension()) ||
			!UPackage::SavePackage(Package, Montage, *PackageFileName, SaveArgs))
		{
			return FString::Printf(TEXT("Migrated in memory, but saving %s failed."), Path);
		}
		++ModifiedMontages;
	}

	FString Result = FString::Printf(
		TEXT("Unified confirmed-hit VFX on %d hitboxes across %d montages; migrated %d legacy slots."),
		MigratedHitboxes, ModifiedMontages, RemovedLegacySlots);
	if (!Missing.IsEmpty())
	{
		Result += FString::Printf(TEXT(" Missing: %s"), *Missing);
	}
	return Result;
}

FString UEnemyBlueprintAutomationLibrary::DisablePlayerPoisonDebugInput()
{
	if (GEditor && GEditor->PlayWorld)
	{
		return TEXT("Refused: stop PIE/SIE before modifying BP_ThirdPersonCharacter.");
	}

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr,
		TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter.BP_ThirdPersonCharacter"));
	UEdGraph* Graph = Blueprint && !Blueprint->UbergraphPages.IsEmpty() ? Blueprint->UbergraphPages[0] : nullptr;
	if (!Graph)
	{
		return TEXT("Refused: BP_ThirdPersonCharacter EventGraph was not found.");
	}

	int32 RemovedNodes = 0;
	for (int32 Index = Graph->Nodes.Num() - 1; Index >= 0; --Index)
	{
		UEdGraphNode* Node = Graph->Nodes[Index];
		const UK2Node_InputAction* LegacyInputAction = Cast<UK2Node_InputAction>(Node);
		const bool bLegacyPoisonInput = LegacyInputAction && LegacyInputAction->InputActionName == TEXT("IA_Posion");
		const bool bEnhancedPoisonInput = Node &&
			Node->GetClass()->GetName() == TEXT("K2Node_EnhancedInputAction") &&
			Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Contains(TEXT("IA_Posion"));
		if (!bLegacyPoisonInput && !bEnhancedPoisonInput)
		{
			continue;
		}

		Blueprint->Modify();
		Graph->Modify();
		FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
		++RemovedNodes;
	}

	if (RemovedNodes == 0)
	{
		return TEXT("Already clean: no IA_Posion input event remains in BP_ThirdPersonCharacter.");
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	UPackage* Package = Blueprint->GetOutermost();
	FString PackageFileName;
	if (!Package || !FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), PackageFileName, FPackageName::GetAssetPackageExtension()))
	{
		return FString::Printf(TEXT("Removed %d IA_Posion node(s) in memory, but could not resolve the package filename."),
			RemovedNodes);
	}

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	if (!UPackage::SavePackage(Package, Blueprint, *PackageFileName, SaveArgs))
	{
		return FString::Printf(TEXT("Removed %d IA_Posion node(s) in memory, but saving BP_ThirdPersonCharacter failed."),
			RemovedNodes);
	}

	return FString::Printf(TEXT("Applied: removed %d IA_Posion debug event node(s) and saved %s."),
		RemovedNodes, *PackageFileName);
}

FString UEnemyBlueprintAutomationLibrary::FixPlayerSkillStaminaGraph()
{
	if (GEditor && GEditor->PlayWorld)
	{
		return TEXT("Refused: stop PIE/SIE before modifying BP_ThirdPersonCharacter.");
	}

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr,
		TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter.BP_ThirdPersonCharacter"));
	UEdGraph* Graph = Blueprint && !Blueprint->UbergraphPages.IsEmpty() ? Blueprint->UbergraphPages[0] : nullptr;
	if (!Graph)
	{
		return TEXT("Refused: BP_ThirdPersonCharacter EventGraph was not found.");
	}

	UK2Node_CallFunction* LegacyPayment = nullptr;
	UEdGraphPin* AdvanceSkillExec = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
		if (!Call || Call->GetFunctionName() != TEXT("TryPayStamina"))
		{
			continue;
		}

		UEdGraphPin* ExecInput = Call->GetExecPin();
		if (!ExecInput || ExecInput->LinkedTo.Num() != 1)
		{
			continue;
		}

		UEdGraphPin* CandidateSource = ExecInput->LinkedTo[0];
		UEdGraphNode* SourceNode = CandidateSource ? CandidateSource->GetOwningNode() : nullptr;
		if (SourceNode && SourceNode->GetNodeTitle(ENodeTitleType::ListView).ToString() == TEXT("AdvanceSkill"))
		{
			LegacyPayment = Call;
			AdvanceSkillExec = CandidateSource;
			break;
		}
	}

	if (!LegacyPayment)
	{
		return TEXT("Already clean: AdvanceSkill has no legacy TryPayStamina call.");
	}

	UEdGraphPin* PaymentThen = LegacyPayment->GetThenPin();
	UK2Node_IfThenElse* PaymentBranch = PaymentThen && PaymentThen->LinkedTo.Num() == 1
		? Cast<UK2Node_IfThenElse>(PaymentThen->LinkedTo[0]->GetOwningNode()) : nullptr;
	UEdGraphPin* BranchThen = PaymentBranch ? PaymentBranch->GetThenPin() : nullptr;
	UEdGraphPin* NextExecInput = BranchThen && BranchThen->LinkedTo.Num() == 1 ? BranchThen->LinkedTo[0] : nullptr;
	UEdGraphPin* ReturnValue = LegacyPayment->GetReturnValuePin();
	const bool bConditionReadsPayment = PaymentBranch && ReturnValue &&
		PaymentBranch->GetConditionPin()->LinkedTo.Contains(ReturnValue);
	if (!AdvanceSkillExec || !PaymentBranch || !NextExecInput || !bConditionReadsPayment)
	{
		return TEXT("Refused: AdvanceSkill's legacy stamina pattern changed; no graph wire was modified.");
	}

	const FScopedTransaction Transaction(NSLOCTEXT("Combat", "RemoveLegacySkillStaminaPayment",
		"Remove Legacy Skill Stamina Payment"));
	Blueprint->Modify();
	Graph->Modify();
	LegacyPayment->Modify();
	PaymentBranch->Modify();

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	Schema->BreakSinglePinLink(AdvanceSkillExec, LegacyPayment->GetExecPin());
	Schema->BreakSinglePinLink(BranchThen, NextExecInput);
	if (!Schema->TryCreateConnection(AdvanceSkillExec, NextExecInput))
	{
		return TEXT("Refused: could not reconnect AdvanceSkill to its combo branch; undo the transaction.");
	}

	FBlueprintEditorUtils::RemoveNode(Blueprint, PaymentBranch, true);
	FBlueprintEditorUtils::RemoveNode(Blueprint, LegacyPayment, true);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	UPackage* Package = Blueprint->GetOutermost();
	FString PackageFileName;
	if (!Package || !FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), PackageFileName, FPackageName::GetAssetPackageExtension()))
	{
		return TEXT("Applied in memory, but could not resolve BP_ThirdPersonCharacter's package filename.");
	}

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	if (!UPackage::SavePackage(Package, Blueprint, *PackageFileName, SaveArgs))
	{
		return TEXT("Applied in memory, but saving BP_ThirdPersonCharacter failed.");
	}

	return TEXT("Applied: AdvanceSkill now enters the E combo directly; its four hit windows exclusively pay 12.5 stamina each.");
}

FString UEnemyBlueprintAutomationLibrary::ConfigureEnemyAttackTelegraph()
{
	if (GEditor && GEditor->PlayWorld)
	{
		return TEXT("Refused: stop PIE/SIE before modifying AM_Enemy_Attack.");
	}

	UAnimMontage* EnemyMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Game/Enemy/AM_Enemy_Attack.AM_Enemy_Attack"));
	UAnimMontage* ReferenceMontage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/Game/Combat/Montages/AM_Ground_A1.AM_Ground_A1"));
	UBlueprint* EnemyBlueprint = LoadObject<UBlueprint>(nullptr,
		TEXT("/Game/Game/Combat/GAS/BP_Enemy.BP_Enemy"));
	UObject* EnemyDefaults = EnemyBlueprint && EnemyBlueprint->GeneratedClass
		? EnemyBlueprint->GeneratedClass->GetDefaultObject() : nullptr;
	FNumericProperty* AttackRangeProperty = EnemyBlueprint && EnemyBlueprint->GeneratedClass
		? CastField<FNumericProperty>(EnemyBlueprint->GeneratedClass->FindPropertyByName(TEXT("AttackRange"))) : nullptr;
	if (!EnemyMontage || !ReferenceMontage || ReferenceMontage->SlotAnimTracks.IsEmpty() ||
		!EnemyBlueprint || !EnemyDefaults || !AttackRangeProperty || !AttackRangeProperty->IsFloatingPoint())
	{
		return TEXT("Refused: enemy montage, reference montage, or BP_Enemy.AttackRange could not be loaded.");
	}

	FAnimNotifyEvent* EnemyHitEvent = nullptr;
	const FAnimNotifyEvent* ReferenceHitEvent = nullptr;
	for (FAnimNotifyEvent& Event : EnemyMontage->Notifies)
	{
		if (Cast<UANS_MeleeHitbox>(Event.NotifyStateClass.Get()))
		{
			EnemyHitEvent = &Event;
			break;
		}
	}
	for (const FAnimNotifyEvent& Event : ReferenceMontage->Notifies)
	{
		if (Cast<UANS_MeleeHitbox>(Event.NotifyStateClass.Get()))
		{
			ReferenceHitEvent = &Event;
			break;
		}
	}
	if (!EnemyHitEvent || !ReferenceHitEvent)
	{
		return TEXT("Refused: a melee hit window was not found on both montages.");
	}

	const float OldLength = EnemyMontage->GetPlayLength();
	const float OldHitTime = EnemyHitEvent->GetTriggerTime();
	const float NewHitTime = ReferenceHitEvent->GetTriggerTime();
	const float NewHitDuration = ReferenceHitEvent->GetDuration();
	void* AttackRangeValue = AttackRangeProperty->ContainerPtrToValuePtr<void>(EnemyDefaults);
	const double OldAttackRange = AttackRangeProperty->GetFloatingPointPropertyValue(AttackRangeValue);
	constexpr double NewAttackRange = 100.0;
	if (NewHitTime <= 0.f || NewHitDuration <= 0.f)
	{
		return TEXT("Refused: AM_Ground_A1 contains an invalid melee hit window.");
	}

	EnemyMontage->Modify();
	EnemyMontage->SlotAnimTracks = ReferenceMontage->SlotAnimTracks;
	EnemyMontage->RateScale = 1.f;
	EnemyMontage->SetCompositeLength(EnemyMontage->CalculateSequenceLength());

	EnemyHitEvent->Link(EnemyMontage, NewHitTime, 0);
	EnemyHitEvent->TriggerTimeOffset = 0.f;
	EnemyHitEvent->EndTriggerTimeOffset = 0.f;
	EnemyHitEvent->SetDuration(NewHitDuration);
	EnemyHitEvent->EndLink.Link(EnemyMontage, NewHitTime + NewHitDuration, 0);
#if WITH_EDITORONLY_DATA
	EnemyHitEvent->DisplayTime_DEPRECATED = NewHitTime;
#endif
	EnemyBlueprint->Modify();
	EnemyDefaults->Modify();
	AttackRangeProperty->SetFloatingPointPropertyValue(AttackRangeValue, NewAttackRange);
	EnemyDefaults->PostEditChange();
	FBlueprintEditorUtils::MarkBlueprintAsModified(EnemyBlueprint);

	EnemyMontage->SortNotifies();
	EnemyMontage->InitializeNotifyTrack();
	EnemyMontage->RefreshCacheData();
	EnemyMontage->MarkPackageDirty();

	UPackage* Package = EnemyMontage->GetOutermost();
	FString PackageFileName;
	if (!Package || !FPackageName::TryConvertLongPackageNameToFilename(
		Package->GetName(), PackageFileName, FPackageName::GetAssetPackageExtension()))
	{
		return TEXT("Applied in memory, but could not resolve AM_Enemy_Attack's package filename.");
	}

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	if (!UPackage::SavePackage(Package, EnemyMontage, *PackageFileName, SaveArgs))
	{
		return TEXT("Applied in memory, but saving AM_Enemy_Attack failed.");
	}
	UPackage* BlueprintPackage = EnemyBlueprint->GetOutermost();
	FString BlueprintPackageFileName;
	if (!BlueprintPackage || !FPackageName::TryConvertLongPackageNameToFilename(
		BlueprintPackage->GetName(), BlueprintPackageFileName, FPackageName::GetAssetPackageExtension()) ||
		!UPackage::SavePackage(BlueprintPackage, EnemyBlueprint, *BlueprintPackageFileName, SaveArgs))
	{
		return TEXT("Applied enemy attack timing in memory, but saving BP_Enemy.AttackRange failed.");
	}

	return FString::Printf(
		TEXT("Applied: enemy attack length %.3fs -> %.3fs; hit %.3fs -> %.3fs (duration %.3fs); range %.1f -> %.1f."),
		OldLength, EnemyMontage->GetPlayLength(), OldHitTime, NewHitTime, NewHitDuration,
		OldAttackRange, NewAttackRange);
}

FString UEnemyBlueprintAutomationLibrary::ConfigureCombatVFXAndFinisher()
{
	UParticleSystem* RegularImpactVFX = LoadObject<UParticleSystem>(nullptr,
		TEXT("/Game/StarterContent/Particles/P_Sparks.P_Sparks"));
	UParticleSystem* FinisherImpactVFX = LoadObject<UParticleSystem>(nullptr,
		TEXT("/Game/StarterContent/Particles/P_Explosion.P_Explosion"));
	UNiagaraSystem* LegacyActionVFX = LoadObject<UNiagaraSystem>(nullptr,
		TEXT("/Game/Vefects/Easy_Impact_Frames/VFX/Frames/Particles/Tests/NS_Impact_Frame_01_Always.NS_Impact_Frame_01_Always"));
	if (!RegularImpactVFX || !FinisherImpactVFX)
	{
		return TEXT("Refused: StarterContent impact particle systems could not be loaded.");
	}

	static const TCHAR* MontagePaths[] = {
		TEXT("/Game/Game/Combat/Montages/AM_Ground_A1.AM_Ground_A1"),
		TEXT("/Game/Game/Combat/Montages/AM_Ground_A2.AM_Ground_A2"),
		TEXT("/Game/Game/Combat/Montages/AM_Ground_A3.AM_Ground_A3"),
		TEXT("/Game/Game/Combat/Montages/AM_Ground_A4.AM_Ground_A4"),
		TEXT("/Game/Game/Combat/Montages/AM_Launcher_L2.AM_Launcher_L2"),
		TEXT("/Game/Game/Combat/Montages/AM_Skill_S4.AM_Skill_S4"),
		TEXT("/Game/Game/Combat/Montages/AM_Skill_S5.AM_Skill_S5"),
		TEXT("/Game/Game/Combat/Montages/AM_Skill_S6.AM_Skill_S6"),
		TEXT("/Game/Game/Combat/Montages/AM_Skill_S7.AM_Skill_S7"),
		TEXT("/Game/Game/Combat/Montages/AM_Dive_D3.AM_Dive_D3")
	};

	int32 ModifiedMontages = 0;
	int32 ModifiedHitboxes = 0;
	int32 AddedStartNotifies = 0;
	int32 RemovedActionNotifies = 0;
	FString Missing;
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;

	for (const TCHAR* Path : MontagePaths)
	{
		UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, Path);
		if (!Montage)
		{
			if (!Missing.IsEmpty())
			{
				Missing += TEXT(", ");
			}
			Missing += Path;
			continue;
		}

		const bool bIsFinisher = FString(Path).EndsWith(TEXT("AM_Skill_S7.AM_Skill_S7"));
		bool bChanged = false;
		for (FAnimNotifyEvent& Event : Montage->Notifies)
		{
			UANS_MeleeHitbox* Hitbox = Cast<UANS_MeleeHitbox>(Event.NotifyStateClass.Get());
			if (!Hitbox)
			{
				continue;
			}

			Hitbox->Modify();
			// The Easy Impact Frames test systems use a screen-space translucent
			// material that corrupts the environment when instances accumulate.
			// Use the engine sample particle as the visible, auto-destroying placeholder.
			Hitbox->ConfirmedHitVFXList.Reset();
			Hitbox->ConfirmedHitParticleVFXList.Reset();
			Hitbox->ConfirmedHitVFXEntries.Reset();
			Hitbox->ConfirmedHitVFX = nullptr;
			Hitbox->ConfirmedHitParticleVFX = nullptr;
			FConfirmedHitVFXEntry& ImpactEntry = Hitbox->ConfirmedHitVFXEntries.AddDefaulted_GetRef();
			ImpactEntry.Effect = bIsFinisher ? FinisherImpactVFX : RegularImpactVFX;
			ImpactEntry.Scale = FVector(bIsFinisher ? 0.45f : 0.65f);
			ImpactEntry.bAlignToHitNormal = true;
			Hitbox->ConfirmedHitVFXScale = 1.f;
			bChanged = true;
			++ModifiedHitboxes;
		}

		if (bIsFinisher)
		{
			const int32 Removed = Montage->Notifies.RemoveAll(
				[LegacyActionVFX](const FAnimNotifyEvent& Event)
				{
					if (Event.NotifyName == FName(TEXT("FinisherActionVFX")))
					{
						return true;
					}
					const UAnimNotify_PlayNiagaraEffect* Candidate = Cast<UAnimNotify_PlayNiagaraEffect>(Event.Notify);
					return Candidate && LegacyActionVFX && Candidate->Template == LegacyActionVFX;
				});
			if (Removed > 0)
			{
				RemovedActionNotifies += Removed;
				bChanged = true;
			}

			const int32 RemovedLegacyStart = Montage->Notifies.RemoveAll(
				[](const FAnimNotifyEvent& Event)
				{
					return Event.NotifyName == FName(TEXT("FinisherCinematicStart"));
				});
			if (RemovedLegacyStart > 0)
			{
				RemovedActionNotifies += RemovedLegacyStart;
				bChanged = true;
			}

			UANS_FinisherCinematic* CinematicState = nullptr;
			FAnimNotifyEvent* ExistingCinematicEvent = nullptr;
			for (FAnimNotifyEvent& Event : Montage->Notifies)
			{
				if (UANS_FinisherCinematic* Candidate = Cast<UANS_FinisherCinematic>(Event.NotifyStateClass.Get()))
				{
					CinematicState = Candidate;
					ExistingCinematicEvent = &Event;
					break;
				}
			}

			if (!CinematicState)
			{
				CinematicState = NewObject<UANS_FinisherCinematic>(
					Montage, UANS_FinisherCinematic::StaticClass(), NAME_None, RF_Transactional);
			}
			if (CinematicState)
			{
				constexpr float FinisherStartTime = 0.02f;
				constexpr float FinisherWindowDuration = 0.68f;
				bool bAddedCinematicEvent = false;
				if (!ExistingCinematicEvent)
				{
					FAnimNotifyEvent& Event = Montage->Notifies.AddDefaulted_GetRef();
					ExistingCinematicEvent = &Event;
					bAddedCinematicEvent = true;
					++AddedStartNotifies;
				}

				CinematicState->Modify();
				ExistingCinematicEvent->NotifyName = FName(TEXT("FinisherCinematicWindow"));
				ExistingCinematicEvent->TriggerTimeOffset = 0.f;
				ExistingCinematicEvent->EndTriggerTimeOffset = 0.f;
				ExistingCinematicEvent->TrackIndex = 2;
				ExistingCinematicEvent->Notify = nullptr;
				ExistingCinematicEvent->NotifyStateClass = CinematicState;
				if (bAddedCinematicEvent)
				{
					ExistingCinematicEvent->Link(Montage, FinisherStartTime, 0);
					ExistingCinematicEvent->SetDuration(
						FMath::Min(FinisherWindowDuration, FMath::Max(0.f, Montage->GetPlayLength() - FinisherStartTime)));
					ExistingCinematicEvent->EndLink.Link(Montage, ExistingCinematicEvent->EndLink.GetTime(), 0);
				}
				ExistingCinematicEvent->MontageTickType = EMontageNotifyTickType::Queued;
				ExistingCinematicEvent->NotifyTriggerChance = 1.f;
#if WITH_EDITORONLY_DATA
				if (bAddedCinematicEvent)
				{
					ExistingCinematicEvent->DisplayTime_DEPRECATED = FinisherStartTime;
				}
				if (!ExistingCinematicEvent->Guid.IsValid())
				{
					ExistingCinematicEvent->Guid = FGuid::NewGuid();
				}
#endif
				bChanged = true;
			}
		}

		if (!bChanged)
		{
			continue;
		}

		Montage->Modify();
		Montage->SortNotifies();
		Montage->InitializeNotifyTrack();
		Montage->RefreshCacheData();
		Montage->MarkPackageDirty();

		UPackage* Package = Montage->GetOutermost();
		FString PackageFileName;
		if (!Package || !FPackageName::TryConvertLongPackageNameToFilename(
			Package->GetName(), PackageFileName, FPackageName::GetAssetPackageExtension()) ||
			!UPackage::SavePackage(Package, Montage, *PackageFileName, SaveArgs))
		{
			return FString::Printf(TEXT("Applied in memory, but saving %s failed."), Path);
		}
		++ModifiedMontages;
	}

	FString Result = FString::Printf(
		TEXT("Applied: safe confirmed-hit particle lists on %d hitboxes across %d montages; S7 cinematic window added=%d, legacy action/start notifies removed=%d."),
		ModifiedHitboxes, ModifiedMontages, AddedStartNotifies, RemovedActionNotifies);
	if (!Missing.IsEmpty())
	{
		Result += FString::Printf(TEXT(" Missing: %s"), *Missing);
	}
	return Result;
}

FString UEnemyBlueprintAutomationLibrary::ConfigureEComboElectricTrailSample()
{
	if (GEditor && GEditor->PlayWorld)
	{
		return TEXT("Refused: stop PIE/SIE before configuring the E combo trail.");
	}

	UNiagaraSystem* SourceSystem = LoadObject<UNiagaraSystem>(nullptr,
		TEXT("/Game/SwordTrailVFX/VFX/NS_Trail_10.NS_Trail_10"));
	UNiagaraSystem* DonorSystem = LoadObject<UNiagaraSystem>(nullptr,
		TEXT("/Game/SwordTrailVFX/VFX/NS_Trail_15.NS_Trail_15"));
	UMaterialInterface* SourceMainMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/SwordTrailVFX/Materials/Trails_MI/MI_Trail_Color_08.MI_Trail_Color_08"));
	UMaterialInterface* ElectricMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/SwordTrailVFX/Materials/Trails_MI/MI_Trail_Color_17.MI_Trail_Color_17"));
	if (!SourceSystem || !DonorSystem || !SourceMainMaterial || !ElectricMaterial)
	{
		return TEXT("Refused: NS_Trail_10, NS_Trail_15, MI_Trail_Color_08, or MI_Trail_Color_17 is missing.");
	}

	auto CountRibbonMaterial = [](UNiagaraSystem* System, UMaterialInterface* Material)
	{
		int32 Count = 0;
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			const FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
			if (!EmitterData)
			{
				continue;
			}
			for (const UNiagaraRendererProperties* Renderer : EmitterData->GetRenderers())
			{
				const UNiagaraRibbonRendererProperties* Ribbon = Cast<UNiagaraRibbonRendererProperties>(Renderer);
				Count += Ribbon && Ribbon->Material == Material ? 1 : 0;
			}
		}
		return Count;
	};

	const int32 SourceRendererCount = CountRibbonMaterial(SourceSystem, SourceMainMaterial);
	const int32 DonorRendererCount = CountRibbonMaterial(DonorSystem, ElectricMaterial);
	if (SourceRendererCount <= 0 || DonorRendererCount <= 0)
	{
		return FString::Printf(
			TEXT("Refused: renderer contract changed (NS10 main=%d, NS15 electric=%d)."),
			SourceRendererCount, DonorRendererCount);
	}

	static const FString TargetAssetName(TEXT("NS_ECombo_ElectricTrail"));
	static const FString TargetPackagePath(TEXT("/Game/Game/Combat/VFX/Trails"));
	static const FString TargetObjectPath = TargetPackagePath / TargetAssetName + TEXT(".") + TargetAssetName;
	UNiagaraSystem* TargetSystem = LoadObject<UNiagaraSystem>(nullptr, *TargetObjectPath);
	bool bCreatedSystem = false;
	if (!TargetSystem)
	{
		TargetSystem = Cast<UNiagaraSystem>(
			FAssetToolsModule::GetModule().Get().DuplicateAsset(TargetAssetName, TargetPackagePath, SourceSystem));
		bCreatedSystem = TargetSystem != nullptr;
	}
	if (!TargetSystem)
	{
		return TEXT("Refused: could not duplicate NS_Trail_10 into the project Combat/VFX folder.");
	}

	TArray<UNiagaraRibbonRendererProperties*> RibbonsToReplace;
	int32 ExistingElectricRenderers = 0;
	for (FNiagaraEmitterHandle& Handle : TargetSystem->GetEmitterHandles())
	{
		FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
		if (!EmitterData)
		{
			continue;
		}
		for (UNiagaraRendererProperties* Renderer : EmitterData->GetRenderers())
		{
			UNiagaraRibbonRendererProperties* Ribbon = Cast<UNiagaraRibbonRendererProperties>(Renderer);
			if (!Ribbon)
			{
				continue;
			}
			if (Ribbon->Material == SourceMainMaterial)
			{
				if (Ribbon->GetOutermost() != TargetSystem->GetOutermost())
				{
					return TEXT("Refused: duplicated renderer is not owned by the target package; vendor asset was not modified.");
				}
				RibbonsToReplace.Add(Ribbon);
			}
			else if (Ribbon->Material == ElectricMaterial)
			{
				++ExistingElectricRenderers;
			}
		}
	}
	if (RibbonsToReplace.IsEmpty() && ExistingElectricRenderers <= 0)
	{
		return TEXT("Refused: target asset exists but does not match the expected NS_Trail_10 renderer layout.");
	}

	TargetSystem->Modify();
	FProperty* MaterialProperty = FindFProperty<FProperty>(
		UNiagaraRibbonRendererProperties::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UNiagaraRibbonRendererProperties, Material));
	for (UNiagaraRibbonRendererProperties* Ribbon : RibbonsToReplace)
	{
		Ribbon->Modify();
		Ribbon->Material = ElectricMaterial;
		FPropertyChangedEvent MaterialChangedEvent(MaterialProperty);
		Ribbon->PostEditChangeProperty(MaterialChangedEvent);
	}
	TargetSystem->bFixedBounds = true;
	TargetSystem->SetFixedBounds(FBox(FVector(-300.f), FVector(300.f)));
	TargetSystem->PostEditChange();
	if (TargetSystem->HasOutstandingCompilationRequests(true))
	{
		TargetSystem->WaitForCompilationComplete(true, true);
	}
	TargetSystem->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	UPackage* TargetPackage = TargetSystem->GetOutermost();
	FString TargetPackageFileName;
	if (!TargetPackage || !FPackageName::TryConvertLongPackageNameToFilename(
		TargetPackage->GetName(), TargetPackageFileName, FPackageName::GetAssetPackageExtension()) ||
		!UPackage::SavePackage(TargetPackage, TargetSystem, *TargetPackageFileName, SaveArgs))
	{
		return TEXT("Applied the Niagara hybrid in memory, but saving NS_ECombo_ElectricTrail failed.");
	}

	static const TCHAR* MontagePaths[] = {
		TEXT("/Game/Game/Combat/Montages/AM_Skill_S4.AM_Skill_S4"),
		TEXT("/Game/Game/Combat/Montages/AM_Skill_S5.AM_Skill_S5"),
		TEXT("/Game/Game/Combat/Montages/AM_Skill_S6.AM_Skill_S6"),
		TEXT("/Game/Game/Combat/Montages/AM_Skill_S7.AM_Skill_S7")
	};
	static const FName TrailNotifyName(TEXT("EComboElectricTrail"));
	static const FName TrailTrackName(TEXT("E Trail"));
	int32 ModifiedMontages = 0;
	for (const TCHAR* MontagePath : MontagePaths)
	{
		UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, MontagePath);
		if (!Montage)
		{
			return FString::Printf(TEXT("Applied Niagara hybrid, but montage is missing: %s"), MontagePath);
		}

		Montage->Modify();
		Montage->Notifies.RemoveAll([](const FAnimNotifyEvent& Event)
		{
			return Event.NotifyName == TrailNotifyName;
		});

		int32 TrailTrackIndex = Montage->AnimNotifyTracks.IndexOfByPredicate([](const FAnimNotifyTrack& Track)
		{
			return Track.TrackName == TrailTrackName;
		});
		if (TrailTrackIndex == INDEX_NONE)
		{
			TrailTrackIndex = Montage->AnimNotifyTracks.Add(
				FAnimNotifyTrack(TrailTrackName, FLinearColor(0.1f, 0.65f, 1.f, 1.f)));
		}

		UANS_TimedNiagaraEffect* TrailState = NewObject<UANS_TimedNiagaraEffect>(
			Montage, UANS_TimedNiagaraEffect::StaticClass(), NAME_None, RF_Transactional);
		if (!TrailState)
		{
			return FString::Printf(TEXT("Applied Niagara hybrid, but could not create trail state for %s."), MontagePath);
		}
		TrailState->Template = TargetSystem;
		TrailState->SocketName = TEXT("hand_r");
		TrailState->LocationOffset = FVector::ZeroVector;
		TrailState->RotationOffset = FRotator::ZeroRotator;
		TrailState->Scale = FVector::OneVector;
		TrailState->bApplyRateScaleAsTimeDilation = true;
		TrailState->bDestroyAtEnd = false;

		const float StartTime = FMath::Min(0.01f, Montage->GetPlayLength() * 0.1f);
		const float Duration = FMath::Max(1.f / 60.f, Montage->GetPlayLength() - StartTime - 0.01f);
		FAnimNotifyEvent& Event = Montage->Notifies.AddDefaulted_GetRef();
		Event.NotifyName = TrailNotifyName;
		Event.Link(Montage, StartTime, 0);
		Event.TriggerTimeOffset = GetTriggerTimeOffsetForType(Montage->CalculateOffsetForNotify(StartTime));
		Event.EndTriggerTimeOffset = 0.f;
		Event.TrackIndex = TrailTrackIndex;
		Event.Notify = nullptr;
		Event.NotifyStateClass = TrailState;
		Event.SetDuration(Duration);
		Event.EndLink.Link(Montage, Event.EndLink.GetTime(), 0);
		Event.MontageTickType = EMontageNotifyTickType::Queued;
		Event.NotifyTriggerChance = 1.f;
#if WITH_EDITORONLY_DATA
		Event.DisplayTime_DEPRECATED = StartTime;
		Event.Guid = FGuid::NewGuid();
#endif

		Montage->SortNotifies();
		Montage->InitializeNotifyTrack();
		Montage->RefreshCacheData();
		Montage->MarkPackageDirty();
		UPackage* MontagePackage = Montage->GetOutermost();
		FString MontagePackageFileName;
		if (!MontagePackage || !FPackageName::TryConvertLongPackageNameToFilename(
			MontagePackage->GetName(), MontagePackageFileName, FPackageName::GetAssetPackageExtension()) ||
			!UPackage::SavePackage(MontagePackage, Montage, *MontagePackageFileName, SaveArgs))
		{
			return FString::Printf(TEXT("Configured trail in memory, but saving failed: %s"), MontagePath);
		}
		++ModifiedMontages;
	}

	return FString::Printf(
		TEXT("Applied: %s NS_Trail_10 hybrid, replaced %d main ribbon renderer(s) with NS_Trail_15 electric material; hand_r Timed Niagara spans %d E montages."),
		bCreatedSystem ? TEXT("created") : TEXT("updated"),
		RibbonsToReplace.Num(), ModifiedMontages);
}
