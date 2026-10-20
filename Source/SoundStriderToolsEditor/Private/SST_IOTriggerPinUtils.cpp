// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#include "SST_IOTriggerPinUtils.h"

#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Knot.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"

namespace
{
	// The parameter name of USST_IO::TriggerOutputsFromEvent. There is no
	// checked macro for parameter names, so this has to be kept in step with
	// the declaration by hand; a mismatch means the pin keeps the engine's box.
	const FName TriggerEventPinName(TEXT("TriggerEvent"));

	// Reroute nodes can chain; a cap keeps a malformed chain from looping.
	constexpr int32 MaxRerouteHops = 64;

	// True when the wire feeding the Target pin starts at a Get of one of this
	// Blueprint's own SST_IO component variables, through any reroute nodes.
	bool IsOwnComponentSource(const UEdGraphPin* SourcePin)
	{
		for (int32 Hop = 0; SourcePin && Hop < MaxRerouteHops; ++Hop)
		{
			const UK2Node_Knot* Knot = Cast<UK2Node_Knot>(SourcePin->GetOwningNode());
			if (!Knot) break;

			const UEdGraphPin* KnotInput = Knot->GetInputPin();
			SourcePin = (KnotInput && KnotInput->LinkedTo.Num() > 0) ? KnotInput->LinkedTo[0] : nullptr;
		}

		if (!SourcePin) return false;

		// Self context: a variable of this Blueprint (or a parent), not one
		// read off another object.
		const UK2Node_VariableGet* GetNode = Cast<UK2Node_VariableGet>(SourcePin->GetOwningNode());
		if (!GetNode || !GetNode->VariableReference.IsSelfContext()) return false;

		// The pin's type rather than the variable's property: the pin is
		// always there, while the property can be missing from a Blueprint
		// that hasn't been compiled since the variable was added.
		const UClass* PinClass = Cast<UClass>(SourcePin->PinType.PinSubCategoryObject.Get());
		return PinClass && PinClass->IsChildOf(USST_IO::StaticClass());
	}
}

bool SST_IOTriggerPinUtils::IsTriggerEventPin(const UEdGraphPin* Pin)
{
	if (!Pin || Pin->Direction != EGPD_Input) return false;
	if (Pin->PinName != TriggerEventPinName) return false;
	if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Name) return false;

	// UK2Node_CallFunction also covers its subclasses, such as the message
	// node used for calls through an interface.
	const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Pin->GetOwningNode());
	const UFunction* Function = CallNode ? CallNode->GetTargetFunction() : nullptr;
	const UClass* FunctionOwner = Function ? Function->GetOwnerClass() : nullptr;

	return FunctionOwner
		&& FunctionOwner->IsChildOf(USST_IO::StaticClass())
		&& Function->GetFName() == GET_FUNCTION_NAME_CHECKED(USST_IO, TriggerOutputsFromEvent);
}

UClass* SST_IOTriggerPinUtils::GetBlueprintClass(const UEdGraphPin* Pin)
{
	const UEdGraphNode* Node = Pin ? Pin->GetOwningNode() : nullptr;
	const UBlueprint* Blueprint = Node ? FBlueprintEditorUtils::FindBlueprintForNode(Node) : nullptr;
	return Blueprint ? Blueprint->GeneratedClass.Get() : nullptr;
}

bool SST_IOTriggerPinUtils::UsesOwnTriggers(const UEdGraphPin* TriggerPin)
{
	const UEdGraphNode* Node = TriggerPin ? TriggerPin->GetOwningNode() : nullptr;
	const UEdGraphPin* TargetPin = Node ? Node->FindPin(UEdGraphSchema_K2::PN_Self) : nullptr;
	if (!TargetPin) return false;

	if (TargetPin->LinkedTo.Num() == 0)
	{
		// Unwired means "self". That is only an SST_IO when the Blueprint
		// is itself an SST_IO subclass; in an actor Blueprint the node
		// won't compile until Target is wired.
		const UClass* BlueprintClass = GetBlueprintClass(TriggerPin);
		return BlueprintClass && BlueprintClass->IsChildOf(USST_IO::StaticClass());
	}

	// Every wire into Target has to qualify; a Target pin can take several,
	// and one foreign wire means some calls go elsewhere.
	for (const UEdGraphPin* Linked : TargetPin->LinkedTo)
	{
		if (!IsOwnComponentSource(Linked)) return false;
	}
	return true;
}

bool SST_IOTriggerPinUtils::GatherClassTriggers(UClass* Class, TArray<FSST_IO_TriggerEventSpec>& OutTriggers)
{
	OutTriggers.Reset();
	if (!Class) return false;

	TArray<const USST_IO*> Components;

	if (Class->IsChildOf(AActor::StaticClass()))
	{
		// The class's default components: native ones and those added in
		// the Blueprint's Components panel, which the CDO alone doesn't
		// hold. These are the templates the Blueprint editor edits, so a
		// trigger added in the Details panel shows up without a compile.
		TArray<const UActorComponent*> Defaults;
		AActor::GetActorClassDefaultComponents(Class, USST_IO::StaticClass(), Defaults);

		for (const UActorComponent* Component : Defaults)
		{
			if (const USST_IO* IO = Cast<USST_IO>(Component))
			{
				Components.Add(IO);
			}
		}
	}
	else if (Class->IsChildOf(USST_IO::StaticClass()))
	{
		Components.Add(Cast<USST_IO>(Class->GetDefaultObject()));
	}

	Components.Remove(nullptr);
	if (Components.Num() == 0) return false;

	for (const USST_IO* IO : Components)
	{
		for (const FSST_IO_TriggerEventSpec& Spec : IO->IO_TriggerEvents)
		{
			if (Spec.TriggerEventName.IsNone()) continue;

			const bool bAlreadyListed = OutTriggers.ContainsByPredicate(
				[&Spec](const FSST_IO_TriggerEventSpec& Listed) { return Listed.TriggerEventName == Spec.TriggerEventName; });

			if (!bAlreadyListed)
			{
				OutTriggers.Add(Spec);
			}
		}
	}
	return true;
}

bool SST_IOTriggerPinUtils::GatherOwnTriggers(const UEdGraphPin* Pin, TArray<FSST_IO_TriggerEventSpec>& OutTriggers)
{
	return GatherClassTriggers(GetBlueprintClass(Pin), OutTriggers);
}
