// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "SST_IO.h"
#include "StructUtils/PropertyBag.h"

struct FSST_IOOutputItem
{
	TWeakObjectPtr<USST_IO> OwningIO;
	int32 Index = INDEX_NONE;

	// View state for the row's parameter list. Lives on the item rather than
	// the row widget so the tool can carry it across a rebuild, which
	// destroys and recreates every row.
	bool bParametersExpanded = false;

	// Parameter values dropped by a sync, kept while this actor stays selected
	// so switching an output's event away and back restores what was typed.
	// View-side only: never saved, never part of an undo transaction.
	// It can hold object references, and this struct isn't a UPROPERTY, so
	// SSST_IOToolWidget reports it to the garbage collector itself.
	FInstancedPropertyBag StashedParameters;

	// Mismatches already logged for this output, keyed "Event|Tag", so a
	// Broadcast whose targets disagree about an event's inputs warns once
	// rather than every time the row refreshes or its dropdown opens.
	TSet<FName> WarnedDifferingInputs;

	// Which struct and array parameter rows are expanded in the IO tool, by a
	// path relative to the output's Parameters, so it doesn't change when the
	// tool rebuilds or an earlier output is removed. View state, kept like
	// bParametersExpanded.
	TSet<FString> ExpandedParameterRows;

	FSST_IOOutputItem(USST_IO* InIO, int32 InIndex)
		: OwningIO(InIO), Index(InIndex) {}

	bool IsValid() const
	{
		return OwningIO.IsValid() && OwningIO->IO_Outputs.IsValidIndex(Index);
	}

	// Returns a pointer into the live array. Null if the actor was deleted
	// or the array shrank underneath us — always check before dereferencing.
	FSST_IO_Output* Get() const
	{
		return IsValid() ? &OwningIO->IO_Outputs[Index] : nullptr;
	}
};

using FSST_IOOutputItemPtr = TSharedPtr<FSST_IOOutputItem>;
