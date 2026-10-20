// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#include "SST_IOParameterUtils.h"

#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "SST_IO.h"

namespace
{
	// The bag's properties, or an empty view for a bag with no layout yet.
	TConstArrayView<FPropertyBagPropertyDesc> DescsOf(const FInstancedPropertyBag& Bag)
	{
		const UPropertyBag* Struct = Bag.GetPropertyBagStruct();
		return Struct ? Struct->GetPropertyDescs() : TConstArrayView<FPropertyBagPropertyDesc>();
	}

	// Same name and the same type both ways round. CompatibleType alone lets
	// an object property accept a subclass, which is right for copying a value
	// but not for deciding two slots are the same slot.
	bool SameSlot(const FPropertyBagPropertyDesc& A, const FPropertyBagPropertyDesc& B)
	{
		return A.Name == B.Name && A.CompatibleType(B) && B.CompatibleType(A);
	}

	bool BagHasSlot(const FInstancedPropertyBag& Bag, const FPropertyBagPropertyDesc& Slot)
	{
		const FPropertyBagPropertyDesc* Found = Bag.FindPropertyDescByName(Slot.Name);
		return Found && SameSlot(*Found, Slot);
	}

	// Every input parameter, in declaration order, by the same rule the runtime
	// fills them with (USST_IO::IsEventInput), so the bag and the call agree.
	TArray<const FProperty*> InputsOf(const UFunction* Function)
	{
		TArray<const FProperty*> Inputs;
		if (!Function) return Inputs;

		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (!USST_IO::IsEventInput(*It)) continue;
			Inputs.Add(*It);
		}
		return Inputs;
	}
}

FInstancedPropertyBag FSST_IOParameterUtils::BuildParametersForEvent(const UFunction* Function,
	const FInstancedPropertyBag& Existing, const FInstancedPropertyBag* Stash)
{
	FInstancedPropertyBag Rebuilt;
	if (!Function) return Rebuilt;

	TArray<FPropertyBagPropertyDesc> Descs;
	for (const FProperty* Input : InputsOf(Function))
	{
		FPropertyBagPropertyDesc Desc(Input->GetFName(), Input);

		// A type the bag can't hold has no slot. The event is rejected at
		// runtime anyway; this just keeps the layout from carrying a None.
		if (Desc.ValueType == EPropertyBagPropertyType::None) continue;

		Descs.Add(MoveTemp(Desc));
	}

	if (Descs.Num() == 0) return Rebuilt;

	Rebuilt.AddProperties(Descs, /*bOverwrite*/ true);

	// Stash first, then the output's own values on top, so a value still on
	// the output always wins over an older stashed one. Only names whose types
	// are compatible are copied; anything else keeps its default.
	if (Stash)
	{
		Rebuilt.CopyMatchingValuesByName(*Stash, TOptional<TConstArrayView<FName>>());
	}
	Rebuilt.CopyMatchingValuesByName(Existing, TOptional<TConstArrayView<FName>>());

	return Rebuilt;
}

void FSST_IOParameterUtils::StashDroppedParameters(const FInstancedPropertyBag& Old,
	const FInstancedPropertyBag& New, FInstancedPropertyBag& Stash)
{
	// Slots Old holds that New doesn't. Built fresh from each property rather
	// than copying the descriptor, so the stash gets its own property IDs.
	TArray<FPropertyBagPropertyDesc> Dropped;
	for (const FPropertyBagPropertyDesc& OldDesc : DescsOf(Old))
	{
		if (BagHasSlot(New, OldDesc) || !OldDesc.CachedProperty) continue;
		Dropped.Emplace(OldDesc.Name, OldDesc.CachedProperty);
	}

	if (Dropped.Num() > 0)
	{
		// Overwrite: an older stash entry with the same name, possibly a
		// different type, is replaced. Newest value wins.
		Stash.AddProperties(Dropped, /*bOverwrite*/ true);

		// Copies every name the two share. Besides the dropped values, that
		// can refresh an older stash entry from a property Old still keeps,
		// which is harmless: it is the newer value, and the cleanup below
		// removes it if New carries that slot.
		Stash.CopyMatchingValuesByName(Old, TOptional<TConstArrayView<FName>>());
	}

	// Anything New carries is on the output again, so it no longer needs
	// holding here. Keeps the stash from serving a stale value later.
	TArray<FName> Restored;
	for (const FPropertyBagPropertyDesc& StashDesc : DescsOf(Stash))
	{
		if (BagHasSlot(New, StashDesc))
		{
			Restored.Add(StashDesc.Name);
		}
	}
	if (Restored.Num() > 0)
	{
		Stash.RemovePropertiesByName(Restored);
	}
}

bool FSST_IOParameterUtils::ParameterLayoutDiffers(const FInstancedPropertyBag& A, const FInstancedPropertyBag& B)
{
	const TConstArrayView<FPropertyBagPropertyDesc> DescsA = DescsOf(A);
	const TConstArrayView<FPropertyBagPropertyDesc> DescsB = DescsOf(B);

	if (DescsA.Num() != DescsB.Num()) return true;

	// By index: the order is the event's input order, which is the order the
	// rows are shown in, so a reorder is worth a sync too.
	for (int32 i = 0; i < DescsA.Num(); ++i)
	{
		if (!SameSlot(DescsA[i], DescsB[i])) return true;
	}
	return false;
}

bool FSST_IOParameterUtils::EventInputsDiffer(const UFunction* A, const UFunction* B)
{
	if (A == B) return false;

	const TArray<const FProperty*> InputsA = InputsOf(A);
	const TArray<const FProperty*> InputsB = InputsOf(B);

	if (InputsA.Num() != InputsB.Num()) return true;

	for (int32 i = 0; i < InputsA.Num(); ++i)
	{
		if (InputsA[i]->GetFName() != InputsB[i]->GetFName() || !InputsA[i]->SameType(InputsB[i])) return true;
	}
	return false;
}
