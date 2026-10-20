// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#include "SST_IOValidator.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "SST_IO.h"
#include "Logging/MessageLog.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "Algo/StableSort.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/UObjectToken.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "GameFramework/Actor.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "SST_IOParameterUtils.h"
#include "SST_IOTriggerPinUtils.h"


#define LOCTEXT_NAMESPACE "SST_IOValidator"


namespace
{
	// The message log has its own severity levels and no "Unverified", so
	// Unverified is shown as Info; ReportIssues prefixes its text instead.
	EMessageSeverity::Type ToMessageSeverity(ESST_IO_ValidatorSeverity Severity)
	{
		switch (Severity)
		{
		case ESST_IO_ValidatorSeverity::Error:
			return EMessageSeverity::Error;

		case ESST_IO_ValidatorSeverity::Warning:
			return EMessageSeverity::Warning;

		case ESST_IO_ValidatorSeverity::Unverified:
		case ESST_IO_ValidatorSeverity::Info:
		default:
			return EMessageSeverity::Info;
		}
	}

	// A class's name as a designer knows it: BP_Door, not BP_Door_C.
	FText ClassDisplayName(const UClass* Class)
	{
		FString Name = Class ? Class->GetName() : FString();
		Name.RemoveFromEnd(TEXT("_C"));
		return FText::FromString(Name);
	}
}


FName FSST_IOValidator::GetLogListingName()
{
	return FName("SST_IO_Validation");
}

void FSST_IOValidator::RunOnCurrentLevel()
{
	if (!GEditor)return;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return;
	const FSST_IO_Result& Result = ValidateWorld(World);
	ReportIssues(Result);
}

FSST_IO_Result FSST_IOValidator::ValidateWorld(UWorld* World)
{
	FSST_IO_Result Result;
	if (!World) return Result;

	Result.bWorldFullyLoaded = IsWorldFullyLoaded(World);

	// Actors in unloaded sub-levels aren't visited at all, so each one is
	// named; otherwise a clean result could hide a level nobody checked.
	for (const ULevelStreaming* Streaming : World->GetStreamingLevels())
	{
		if (Streaming && !Streaming->IsLevelLoaded())
		{
			AddIssue(Result.IssuesArray, ESST_IO_ValidatorSeverity::Info, nullptr, INDEX_NONE,
				FText::Format(LOCTEXT("LevelNotLoaded", "Level '{0}' isn't loaded, so its SST_IO components weren't checked."),
					FText::FromString(FPackageName::GetShortName(Streaming->GetWorldAssetPackageFName()))));
		}
	}

	// The spec and the Blueprint graph are the same for every instance of a
	// class, so they're checked once per class; ten doors, one report.
	TSet<const UClass*> CheckedClasses;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		USST_IO* IO = It->FindComponentByClass<USST_IO>();
		if (!IO) continue;

		++Result.ComponentsChecked;

		UClass* OwnerClass = It->GetClass();
		bool bClassAlreadyChecked = false;
		CheckedClasses.Add(OwnerClass, &bClassAlreadyChecked);
		if (!bClassAlreadyChecked)
		{
			ValidateClassSpec(OwnerClass, IO, Result.IssuesArray);
		}

		ValidateComponent(IO, Result.IssuesArray);

		for (int32 OutputIndex = 0; OutputIndex < IO->IO_Outputs.Num(); ++OutputIndex)
		{
			++Result.OutputsChecked;
			ValidateOutput(IO, OutputIndex, World, Result.bWorldFullyLoaded, Result.IssuesArray);
		}
	}

	return Result;
}


bool FSST_IOValidator::IsWorldFullyLoaded(UWorld* World)
{
	if (!World) return false;
	if (World->IsPartitionedWorld()) return false; // World Partition: can't cheaply tell what's unloaded

	for (const ULevelStreaming* Streaming : World->GetStreamingLevels())
	{
		if (Streaming && !Streaming->IsLevelLoaded()) return false;
	}
	return true;
}

ESST_IO_ValidatorSeverity FSST_IOValidator::ClassifyResolution(ESST_IO_Results Resolution, bool bWorldFullyLoaded)
{
	switch (Resolution)
	{
	// Nothing assigned: the output can never fire, whatever is loaded.
	case ESST_IO_Results::Unassigned:
		return ESST_IO_ValidatorSeverity::Error;

	// The target actor exists but has no SST_IO, so it can't receive IO.
	case ESST_IO_Results::MissingIO:
		return ESST_IO_ValidatorSeverity::Error;

	// Message target not found. With every level loaded it can't be hiding
	// in an unloaded one, so it was deleted. Otherwise it may just be unloaded.
	case ESST_IO_Results::NotLoaded:
		return bWorldFullyLoaded ? ESST_IO_ValidatorSeverity::Error : ESST_IO_ValidatorSeverity::Unverified;

	// Broadcast tag with no actor answering. Possibly intentional (actors
	// spawned at runtime), so a Warning rather than an Error.
	case ESST_IO_Results::NoMatch:
		return bWorldFullyLoaded ? ESST_IO_ValidatorSeverity::Warning : ESST_IO_ValidatorSeverity::Unverified;

	// Resolved isn't a problem. Callers check for it before classifying;
	// Info is returned only so the switch is complete.
	case ESST_IO_Results::Resolved:
	default:
		return ESST_IO_ValidatorSeverity::Info;
	}
}

FText FSST_IOValidator::DescribeResolution(ESST_IO_Results Resolution, bool bWorldFullyLoaded,
                                           const FSST_IO_Output& Output)
{
	switch (Resolution)
	{
	case ESST_IO_Results::Unassigned:
		return Output.IOType == ESSTIOType::Message
			       ? LOCTEXT("NoTargetActor", "Message has no target actor.")
			       : LOCTEXT("NoTargetName", "Broadcast has no target name.");

	case ESST_IO_Results::MissingIO:
		return FText::Format(LOCTEXT("TargetMissingIO",
		                             "Target actor '{0}' has no SST_IO component, so it can't receive IO."),
		                     FText::FromString(Output.TargetActor.GetAssetName()));

	case ESST_IO_Results::NotLoaded:
		return bWorldFullyLoaded
			       ? FText::Format(LOCTEXT("TargetDeleted",
			                               "Target actor '{0}' no longer exists. It was probably deleted; pick a new target."),
			                       FText::FromString(Output.TargetActor.GetAssetName()))
			       : FText::Format(LOCTEXT("TargetUnloaded",
			                               "Target actor '{0}' is in a level that isn't loaded, so it couldn't be checked."),
			                       FText::FromString(Output.TargetActor.GetAssetName()));

	case ESST_IO_Results::NoMatch:
		return bWorldFullyLoaded
			       ? FText::Format(LOCTEXT("NoTagMatch",
			                               "No actor in this level answers to '{0}'. Unless they're spawned at runtime, this Broadcast reaches nothing."),
			                       FText::FromName(Output.TargetName.GetTagName()))
			       : FText::Format(LOCTEXT("NoLoadedTagMatch",
			                               "No loaded actor answers to '{0}'. Matching actors may be in a level that isn't loaded."),
			                       FText::FromName(Output.TargetName.GetTagName()));

	case ESST_IO_Results::Resolved:
	default:
		return FText::GetEmpty();
	}
}

void FSST_IOValidator::ValidateClassSpec(UClass* OwnerClass, USST_IO* Component, TArray<FSST_IO_Issue>& OutIssues)
{
	if (!OwnerClass || !Component) return;

	const FText ClassName = ClassDisplayName(OwnerClass);

	// --- IO_TargetEvents: what other actors can tell this one to do ---
	TSet<FName> SeenTargetEvents;
	for (int32 SpecIndex = 0; SpecIndex < Component->IO_TargetEvents.Num(); ++SpecIndex)
	{
		const FName EventName = Component->IO_TargetEvents[SpecIndex].TargetEventName;

		if (EventName.IsNone())
		{
			AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Error, Component, INDEX_NONE,
				FText::Format(LOCTEXT("TargetSpecEmpty", "{0}: IO_TargetEvents entry {1} has no event selected."),
					ClassName, SpecIndex));
			continue;
		}

		bool bDuplicate = false;
		SeenTargetEvents.Add(EventName, &bDuplicate);
		if (bDuplicate)
		{
			AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Warning, Component, INDEX_NONE,
				FText::Format(LOCTEXT("TargetSpecDuplicate", "{0}: '{1}' is declared more than once in IO_TargetEvents."),
					ClassName, FText::FromName(EventName)));
			continue;
		}

		const UFunction* Function = Component->FindTargetEventFunction(EventName);
		if (!Function)
		{
			AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Error, Component, INDEX_NONE,
				FText::Format(LOCTEXT("TargetSpecMissing", "{0}: IO_TargetEvents declares '{1}', but there is no event by that name. Was it renamed or deleted?"),
					ClassName, FText::FromName(EventName)));
		}
		else if (!USST_IO::HasSupportedSignature(Function))
		{
			AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Error, Component, INDEX_NONE,
				FText::Format(LOCTEXT("TargetSpecUnsupported", "{0}: '{1}' can't be used as an IO target event. It has an output, a Pass-by-Reference input, or an input type IO parameters can't hold."),
					ClassName, FText::FromName(EventName)));
		}
	}

	// --- IO_TriggerEvents: what this actor can fire ---
	TSet<FName> SeenTriggerEvents;
	for (int32 SpecIndex = 0; SpecIndex < Component->IO_TriggerEvents.Num(); ++SpecIndex)
	{
		const FName EventName = Component->IO_TriggerEvents[SpecIndex].TriggerEventName;

		if (EventName.IsNone())
		{
			AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Error, Component, INDEX_NONE,
				FText::Format(LOCTEXT("TriggerSpecEmpty", "{0}: IO_TriggerEvents entry {1} has no name."),
					ClassName, SpecIndex));
			continue;
		}

		bool bDuplicate = false;
		SeenTriggerEvents.Add(EventName, &bDuplicate);
		if (bDuplicate)
		{
			AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Warning, Component, INDEX_NONE,
				FText::Format(LOCTEXT("TriggerSpecDuplicate", "{0}: '{1}' is declared more than once in IO_TriggerEvents."),
					ClassName, FText::FromName(EventName)));
		}
	}

	// --- Hidden events: custom events the IO_TargetEvents list leaves out ---
	// Answers "why isn't my event in the dropdown?" Events already declared
	// were reported above, so they're skipped here.
	for (TFieldIterator<UFunction> It(OwnerClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		const UFunction* Function = *It;
		if (!USST_IO::IsBlueprintCustomEvent(Function) || USST_IO::HasSupportedSignature(Function)) continue;
		if (Component->FindTargetEventSpec(Function->GetFName())) continue;

		AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Info, Component, INDEX_NONE,
			FText::Format(LOCTEXT("HiddenEvent", "{0}: custom event '{1}' isn't offered as an IO target event because it has a Pass-by-Reference input or an input type IO parameters can't hold."),
				ClassName, FText::FromName(Function->GetFName())));
	}

	ValidateTriggerPins(OwnerClass, Component, OutIssues);
}

void FSST_IOValidator::ValidateTriggerPins(UClass* OwnerClass, USST_IO* Component, TArray<FSST_IO_Issue>& OutIssues)
{
	// A native class has no graphs to check.
	UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(OwnerClass);
	if (!Blueprint) return;

	using namespace SST_IOTriggerPinUtils;

	// The declarations are the same for every node in this Blueprint, so
	// they're gathered once.
	TArray<FSST_IO_TriggerEventSpec> Declared;
	const bool bHasOwnIO = GatherClassTriggers(OwnerClass, Declared);

	const FText ClassName = ClassDisplayName(OwnerClass);

	TArray<UK2Node_CallFunction*> CallNodes;
	FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_CallFunction>(Blueprint, CallNodes);

	for (const UK2Node_CallFunction* Node : CallNodes)
	{
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!IsTriggerEventPin(Pin)) continue;

			const UEdGraph* Graph = Node->GetGraph();
			const FText GraphName = Graph ? FText::FromName(Graph->GetFName()) : FText::GetEmpty();

			// Wired: the value is only known at runtime.
			if (Pin->LinkedTo.Num() > 0)
			{
				AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Info, Component, INDEX_NONE,
					FText::Format(LOCTEXT("TriggerPinWired", "{0}: a TriggerOutputsFromEvent call in graph '{1}' gets its trigger event from a wire, so it can't be checked."),
						ClassName, GraphName));
				continue;
			}

			// Calls into another actor's IO, or a Blueprint with no IO of its
			// own: its declarations can't be read here, same as the pin.
			if (!bHasOwnIO || !UsesOwnTriggers(Pin)) continue;

			const FName Value(*Pin->GetDefaultAsString());
			const bool bDeclared = !Value.IsNone() && Declared.ContainsByPredicate(
				[Value](const FSST_IO_TriggerEventSpec& Spec) { return Spec.TriggerEventName == Value; });

			if (bDeclared) continue;

			AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Error, Component, INDEX_NONE,
				Value.IsNone()
					? FText::Format(LOCTEXT("TriggerPinUnset", "{0}: a TriggerOutputsFromEvent call in graph '{1}' has no trigger event selected, so it fires nothing."),
						ClassName, GraphName)
					: FText::Format(LOCTEXT("TriggerPinUndeclared", "{0}: a TriggerOutputsFromEvent call in graph '{1}' fires '{2}', which isn't in IO_TriggerEvents, so it fires nothing."),
						ClassName, GraphName, FText::FromName(Value)));
		}
	}
}

void FSST_IOValidator::ValidateComponent(USST_IO* Component, TArray<FSST_IO_Issue>& OutIssues)
{
	if (!Component) return;

	// With no tag, Broadcasts can't reach this actor. Messages still can, so
	// for a plain actor this is only worth knowing, not a problem.
	if (!Component->GetEffectiveTargetName().IsValid())
	{
		if (Component->bOverrideTargetName)
		{
			AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Warning, Component, INDEX_NONE,
				LOCTEXT("OverrideNoTag", "Target name override is on, but no instance tag is set and the class has none, so no Broadcast can reach this actor."));
		}
		else
		{
			AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Info, Component, INDEX_NONE,
				LOCTEXT("NoBroadcastName", "Has no target name, so Broadcasts can't reach it. Messages still can."));
		}
	}
}

void FSST_IOValidator::ValidateOutput(USST_IO* Component, int32 OutputIndex, UWorld* World, bool bWorldFullyLoaded,
                                      TArray<FSST_IO_Issue>& OutIssues)
{
	const FSST_IO_Output& Output = Component->IO_Outputs[OutputIndex];

	// --- Trigger: when this output fires ---
	if (Output.TriggerEvent.IsNone())
	{
		AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Error, Component, OutputIndex,
			LOCTEXT("NoTrigger", "Has no trigger event, so it never fires."));
	}
	else if (!Component->FindTriggerEventSpec(Output.TriggerEvent))
	{
		AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Error, Component, OutputIndex,
			FText::Format(LOCTEXT("UndeclaredTrigger", "Fires on '{0}', which isn't in this actor's IO_TriggerEvents, so it never fires."),
				FText::FromName(Output.TriggerEvent)));
	}

	// Parameters don't depend on the target resolving, so a missing target
	// doesn't hide an emptied reference.
	ValidateParameters(Component, OutputIndex, bWorldFullyLoaded, OutIssues);

	const bool bHasTargetEvent = !Output.TargetEvent.IsNone();
	if (!bHasTargetEvent)
	{
		AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Error, Component, OutputIndex,
			LOCTEXT("NoTargetEvent", "Has no target event selected."));
	}

	// --- Target: who receives it ---
	TArray<USST_IO*> Targets;
	const ESST_IO_Results Resolution = USST_IO::ResolveOutputTargets(Output, World, Targets);

	if (Resolution != ESST_IO_Results::Resolved)
	{
		AddIssue(OutIssues, ClassifyResolution(Resolution, bWorldFullyLoaded), Component, OutputIndex,
		         DescribeResolution(Resolution, bWorldFullyLoaded, Output));
		return;
	}

	// Everything past here is about the target event on the targets.
	if (!bHasTargetEvent) return;

	// --- Broadcast coverage: how many of the targets declare the event ---
	if (Output.IOType == ESSTIOType::Broadcast)
	{
		int32 Declaring = 0;
		for (const USST_IO* Target : Targets)
		{
			if (Target->FindTargetEventSpec(Output.TargetEvent)) ++Declaring;
		}

		if (Declaring == 0)
		{
			AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Error, Component, OutputIndex,
				FText::Format(LOCTEXT("BroadcastNoneDeclare", "None of the {0} actors answering to '{1}' declare '{2}', so this Broadcast does nothing."),
					Targets.Num(), FText::FromName(Output.TargetName.GetTagName()), FText::FromName(Output.TargetEvent)));
			return;
		}

		if (Declaring < Targets.Num())
		{
			AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Info, Component, OutputIndex,
				FText::Format(LOCTEXT("BroadcastSomeDeclare", "{0} of {1} actors answering to '{2}' declare '{3}'; the rest ignore it."),
					Declaring, Targets.Num(), FText::FromName(Output.TargetName.GetTagName()), FText::FromName(Output.TargetEvent)));
		}
	}

	// --- Per target ---
	// Parameters are compared against the first usable event, the same one
	// the IO tool syncs to, so a Broadcast reports them once, not per target.
	const UFunction* FirstFunction = nullptr;
	bool bWarnedDifferingInputs = false;

	for (USST_IO* Target : Targets)
	{
		ValidateResolvedTarget(Component, OutputIndex, Target, OutIssues);

		if (!Target->FindTargetEventSpec(Output.TargetEvent)) continue;

		// A missing or unusable event is reported on the target's own class
		// by ValidateClassSpec, so it's skipped here rather than repeated.
		const UFunction* Function = Target->FindTargetEventFunction(Output.TargetEvent);
		if (!Function || !USST_IO::HasSupportedSignature(Function)) continue;

		if (!FirstFunction)
		{
			FirstFunction = Function;
			continue;
		}

		if (!bWarnedDifferingInputs && FSST_IOParameterUtils::EventInputsDiffer(FirstFunction, Function))
		{
			AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Warning, Component, OutputIndex,
				FText::Format(LOCTEXT("DifferingInputs", "Actors answering to '{0}' declare '{1}' with different inputs. Parameters follow {2}; the others get defaults for inputs that don't match."),
					FText::FromName(Output.TargetName.GetTagName()), FText::FromName(Output.TargetEvent),
					ClassDisplayName(FirstFunction->GetOwnerClass())));
			bWarnedDifferingInputs = true;
		}
	}

	if (FirstFunction)
	{
		const FInstancedPropertyBag Expected = FSST_IOParameterUtils::BuildParametersForEvent(FirstFunction, Output.Parameters);
		if (FSST_IOParameterUtils::ParameterLayoutDiffers(Expected, Output.Parameters))
		{
			AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Warning, Component, OutputIndex,
				FText::Format(LOCTEXT("ParamsOutOfDate", "Parameters no longer match the inputs of '{0}'. Sync them in the IO tool; until then, inputs that don't match fire with their defaults."),
					FText::FromName(Output.TargetEvent)));
		}
	}
}

void FSST_IOValidator::ValidateResolvedTarget(USST_IO* Component, int32 OutputIndex, USST_IO* Target,
                                              TArray<FSST_IO_Issue>& OutIssues)
{
	if (!Component || !Target) return;

	const FSST_IO_Output& Output = Component->IO_Outputs[OutputIndex];

	// A Broadcast reaching actors that don't declare the event is normal
	// ("every door, open" reaches doors that can't open) and is covered by
	// the coverage count. A Message names this actor, so it's a real error.
	if (Output.IOType == ESSTIOType::Message && !Target->FindTargetEventSpec(Output.TargetEvent))
	{
		const AActor* TargetActor = Target->GetOwner();
		AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Error, Component, OutputIndex,
			FText::Format(LOCTEXT("TargetDoesNotDeclare", "Target '{0}' doesn't declare '{1}' in its IO_TargetEvents, so it ignores this Message."),
				FText::FromString(TargetActor ? TargetActor->GetActorNameOrLabel() : FString()),
				FText::FromName(Output.TargetEvent)));
	}
}

void FSST_IOValidator::ValidateParameters(USST_IO* Component, int32 OutputIndex, bool bWorldFullyLoaded,
                                          TArray<FSST_IO_Issue>& OutIssues)
{
	const FInstancedPropertyBag& Bag = Component->IO_Outputs[OutputIndex].Parameters;

	const UPropertyBag* BagStruct = Bag.GetPropertyBagStruct();
	const uint8* Memory = Bag.GetValue().GetMemory();
	if (!BagStruct || !Memory) return;

	// Top-level parameters only. References inside arrays and structs aren't
	// inspected yet.
	for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
	{
		const FProperty* Property = Desc.CachedProperty;
		if (!Property) continue;

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Memory);
		const FText ParamName = FText::FromName(Desc.Name);

		// Soft first: FSoftObjectProperty derives from FObjectPropertyBase,
		// so the hard-reference branch below would also match it.
		if (const FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(Property))
		{
			const FSoftObjectPath Path = SoftProperty->GetPropertyValue(ValuePtr).ToSoftObjectPath();

			// An empty soft reference may be intentional, and it resolves fine.
			if (Path.IsNull() || Path.ResolveObject()) continue;

			const FString PackageName = Path.GetLongPackageName();
			const bool bPackageLoaded = FindPackage(nullptr, *PackageName) != nullptr;

			// An actor's path has a sub-path inside its level's package
			// (PersistentLevel.Cube); an asset's doesn't.
			const bool bIsLevelObject = !Path.GetSubPathString().IsEmpty();

			if (!bPackageLoaded && !FPackageName::DoesPackageExist(PackageName))
			{
				AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Error, Component, OutputIndex,
					FText::Format(LOCTEXT("SoftParamMissingPackage", "Parameter '{0}' points at '{1}', which doesn't exist."),
						ParamName, FText::FromString(Path.ToString())));
			}
			else if (bIsLevelObject && bPackageLoaded)
			{
				// The level is loaded but the actor isn't in it. With every
				// level loaded that means deleted; otherwise (World Partition)
				// it may just be streamed out.
				AddIssue(OutIssues,
					bWorldFullyLoaded ? ESST_IO_ValidatorSeverity::Error : ESST_IO_ValidatorSeverity::Unverified,
					Component, OutputIndex,
					FText::Format(bWorldFullyLoaded
						? LOCTEXT("SoftParamDeleted", "Parameter '{0}' points at '{1}', which no longer exists. It was probably deleted.")
						: LOCTEXT("SoftParamStreamedOut", "Parameter '{0}' points at '{1}', which isn't loaded, so it couldn't be checked."),
						ParamName, FText::FromString(Path.GetSubPathString())));
			}
			else if (bIsLevelObject)
			{
				AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Unverified, Component, OutputIndex,
					FText::Format(LOCTEXT("SoftParamUnloadedLevel", "Parameter '{0}' points into level '{1}', which isn't loaded, so it couldn't be checked."),
						ParamName, FText::FromString(FPackageName::GetShortName(PackageName))));
			}
			else if (bPackageLoaded)
			{
				AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Error, Component, OutputIndex,
					FText::Format(LOCTEXT("SoftParamMissingAsset", "Parameter '{0}' points at '{1}', which doesn't exist in its package."),
						ParamName, FText::FromString(Path.ToString())));
			}
			// An asset whose package exists but isn't loaded is the normal
			// state of a soft reference; nothing to report.
			continue;
		}

		if (const FObjectPropertyBase* HardProperty = CastField<FObjectPropertyBase>(Property))
		{
			const UObject* Object = HardProperty->GetObjectPropertyValue(ValuePtr);

			if (!Object)
			{
				AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Warning, Component, OutputIndex,
					FText::Format(LOCTEXT("HardParamEmpty", "Parameter '{0}' is empty, so the event receives None. If it pointed at an actor, the actor was probably moved to another level or deleted."),
						ParamName));
			}
			else if (Object->IsA<AActor>())
			{
				AddIssue(OutIssues, ESST_IO_ValidatorSeverity::Info, Component, OutputIndex,
					FText::Format(LOCTEXT("HardParamActor", "Parameter '{0}' is a hard reference to an actor. Make the event input a Soft Object Reference so moving the actor to another level doesn't clear it."),
						ParamName));
			}
		}
	}
}


void FSST_IOValidator::AddIssue(TArray<FSST_IO_Issue>& OutIssues, ESST_IO_ValidatorSeverity Severity,
                                USST_IO* Component, int32 OutputIndex, const FText& Message)
{
	FSST_IO_Issue& Issue = OutIssues.AddDefaulted_GetRef();
	Issue.Severity = Severity;
	Issue.Component = Component;
	Issue.OutputIndex = OutputIndex;
	Issue.Message = Message;
}


void FSST_IOValidator::ReportIssues(const FSST_IO_Result& Result)
{
	TArray<FSST_IO_Issue> Issues = Result.IssuesArray;
	Algo::StableSortBy(Issues, [](const FSST_IO_Issue& Issue) { return static_cast<uint8>(Issue.Severity); });


	FMessageLog Log(GetLogListingName());
	Log.NewPage(LOCTEXT("ValidationPage", "SST IO Validation"));
	for (const FSST_IO_Issue& Issue : Issues)
	{
		const TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(ToMessageSeverity(Issue.Severity));

		if (Issue.Severity == ESST_IO_ValidatorSeverity::Unverified)
		{
			Message->AddToken(FTextToken::Create(LOCTEXT("UnverifiedPrefix", "Unverified:")));
		}

		// Clickable actor name. Skipped for issues with no component, such as the unloaded-level notes.
		if (const USST_IO* IO = Issue.Component.Get())
		{
			if (AActor* Owner = IO->GetOwner())
			{
				Message->AddToken(FUObjectToken::Create(Owner));
			}
		}

		if (Issue.OutputIndex != INDEX_NONE)
		{
			Message->AddToken(
				FTextToken::Create(FText::Format(LOCTEXT("OutputIndex", "Output {0}:"), Issue.OutputIndex)));
		}

		Message->AddToken(FTextToken::Create(Issue.Message));
		Log.AddMessage(Message);
	}

	int32 Counts[4] = {}; // indexed by ESST_IO_ValidatorSeverity
	for (const FSST_IO_Issue& Issue : Issues)
	{
		++Counts[static_cast<uint8>(Issue.Severity)];
	}

	Log.Info(FText::Format(
		LOCTEXT("Summary",
		        "Checked {0} SST_IO components and {1} outputs: {2} errors, {3} warnings, {4} unverified, {5} info."),
		Result.ComponentsChecked, Result.OutputsChecked,
		Counts[0], Counts[1], Counts[2], Counts[3]));

	if (!Result.bWorldFullyLoaded)
	{
		Log.Info(LOCTEXT("NotFullyLoaded",
		                 "Some levels aren't loaded (or this is a World Partition map), so components in them weren't checked and links into them are Unverified."));
	}

	Log.Open(EMessageSeverity::Info, true);
}


#undef LOCTEXT_NAMESPACE
