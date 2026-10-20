// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "SST_IO.h"

class UEdGraphPin;

// The rules for TriggerOutputsFromEvent's TriggerEvent pin, shared by the pin
// widget (SSST_IOTriggerEventPin) and the validator, so the pin's red/valid
// state and the validator's errors can never disagree.
namespace SST_IOTriggerPinUtils
{
	// True for the TriggerEvent input pin of a TriggerOutputsFromEvent call.
	bool IsTriggerEventPin(const UEdGraphPin* Pin);

	// The class of the Blueprint whose graph the pin is in. Null if the pin
	// isn't in a Blueprint, or the Blueprint has no generated class yet.
	UClass* GetBlueprintClass(const UEdGraphPin* Pin);

	// Whether the call targets this Blueprint's own SST_IO, so its declared
	// trigger events apply. False when Target is wired from anything else.
	bool UsesOwnTriggers(const UEdGraphPin* TriggerPin);

	// Every trigger event Class's own SST_IO components declare, in
	// declaration order, first declaration winning. False when the class has
	// no SST_IO to read.
	bool GatherClassTriggers(UClass* Class, TArray<FSST_IO_TriggerEventSpec>& OutTriggers);

	// GatherClassTriggers for the Blueprint the pin is in.
	bool GatherOwnTriggers(const UEdGraphPin* Pin, TArray<FSST_IO_TriggerEventSpec>& OutTriggers);
}
