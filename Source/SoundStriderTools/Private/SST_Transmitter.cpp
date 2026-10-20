// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.


#include "SST_Transmitter.h"
#include "SST_IO.h"
#include "SST_GameplayTags.h"

ASST_Transmitter::ASST_Transmitter()
{
	// Root, sprite, and the SST_IO component all come from ASST_LogicActor,
	// whose constructor has already run by this point.
	if (IOComponent)
	{
		// Native logic actors declare their events through the component's
		// helpers. The checked name turns a rename of IO_Transmit into a
		// compile error instead of a spec that silently points at nothing.
		IOComponent->RegisterTargetEvent(GET_FUNCTION_NAME_CHECKED(ASST_Transmitter, IO_Transmit),
			TEXT("Fires every output on this Transmitter."));
		IOComponent->RegisterTriggerEvent(TransmitterTriggerEvent,
			TEXT("Fires when this Transmitter receives IO_Transmit."));
		
		// The type tag. A designer narrows it per instance, e.g. to
		// SST_IO.Logic.Transmitter.BossDeath, through the instance override.
		IOComponent->TargetName = TAG_SST_IO_Logic_Transmitter;
	}
}

void ASST_Transmitter::IO_Transmit()
{
	if (IOComponent)
	{
		IOComponent->TriggerOutputsFromEvent(TransmitterTriggerEvent);
	}
}
