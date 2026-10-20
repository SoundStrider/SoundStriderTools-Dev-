// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.
#include "SST_IOMessage.h"


#include "SST_Subsystem.h" 
#include "TimerManager.h"
#include "Engine/World.h"
#include "SST_LOG.h"

USST_IOMessage* USST_IOMessage::RegisterMessage(const FSST_IO_Output& Output, USST_IO* TargetIO,
                                                  USST_Subsystem* IOManager)
{
	USST_IOMessage* MessageObj = NewObject<USST_IOMessage>(IOManager); 
	MessageObj->Initialize(Output, TargetIO, IOManager);
	return MessageObj;
}

void USST_IOMessage::Initialize(const FSST_IO_Output& Output, USST_IO* TargetIO, USST_Subsystem* IOManager)
{
	_Output = Output;
	_IOManager = IOManager;
	_TargetIO = TargetIO;
	
	Delay = Output.Delay;
}

void USST_IOMessage::TriggerMessage()
{
	if (Delay <= 0.0f)
	{
		Deliver();
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		// Without a world there is no timer to wait on. Deregister rather than
		// return quietly, or the message sits in IO_Messages indefinitely.
		UE_LOG(LOG_SST, Warning, TEXT("IO message for '%s' has no world; dropped"), *_Output.TargetEvent.ToString());
		DeRegisterMessage();
		return;
	}

	// Captured weakly because the timer can outlive this object. IsValid(this)
	// on a raw pointer reads memory that may already have been freed; a weak
	// pointer reports that safely instead.
	TWeakObjectPtr<USST_IOMessage> WeakThis(this);

	World->GetTimerManager().SetTimer(
		DelayTimerHandle,
		[WeakThis]()
		{
			if (USST_IOMessage* Self = WeakThis.Get())
			{
				Self->Deliver();
			}
		},
		Delay,
		false);
}

void USST_IOMessage::Deliver()
{
	// _TargetIO is weak, and the delayed path is exactly where it goes stale:
	// the target can be destroyed while the timer is pending.
	if (USST_IO* Target = _TargetIO.Get())
	{
		Target->ReceiveInput(_Output);
	}
	else
	{
		UE_LOG(LOG_SST, Warning, TEXT("IO message target was destroyed before '%s' could be delivered"),
			*_Output.TargetEvent.ToString());
	}

	// Always deregisters, delivered or not. A message that skips this stays
	// alive in IO_Messages for the rest of the session.
	DeRegisterMessage();
}

void USST_IOMessage::DeRegisterMessage()
{
	// _IOManager is weak too. If the subsystem is already gone there is
	// nothing to deregister from, and the message will be collected anyway.
	if (USST_Subsystem* Manager = _IOManager.Get())
	{
		Manager->DestroyMessage(this);
	}
}