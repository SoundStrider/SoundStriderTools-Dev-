// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/PropertyBag.h"

class UFunction;

// Parameter bags are identified slot by slot: a property's name together with
// its exact type. A value moves between bags only when both match, so an input
// retyped from Float to Int starts at its default rather than being converted.
class FSST_IOParameterUtils
{
public:

	// The parameters an output should hold for Function: one property per input,
	// in the event's order. Each value comes from Existing when the slot matches,
	// failing that from Stash, failing that the type's default. Null Function ->
	// empty bag.
	static FInstancedPropertyBag BuildParametersForEvent(const UFunction* Function, const FInstancedPropertyBag& Existing, const FInstancedPropertyBag* Stash = nullptr);

	// Records in Stash every property in Old that New no longer carries, so its
	// value can be restored later. An entry with the same name is replaced, so
	// the stash always holds the most recent value. Entries New carries again
	// are removed: they live in the output now.
	static void StashDroppedParameters(const FInstancedPropertyBag& Old, const FInstancedPropertyBag& New, FInstancedPropertyBag& Stash);

	// True when the two bags differ in which slots they hold or in what order.
	// Values are not compared.
	static bool ParameterLayoutDiffers(const FInstancedPropertyBag& A, const FInstancedPropertyBag& B);

	// True when two events take different inputs, by name, type or order. Used
	// when one Broadcast reaches several classes declaring the same event name.
	static bool EventInputsDiffer(const UFunction* A, const UFunction* B);
};
