// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.


#include "SST_Subsystem.h"

#include "SST_IO.h"
#include "SST_LOG.h"
#include "GameFramework/Actor.h"


void USST_Subsystem::RegisterSST_IO(const FGameplayTag& TargetName, USST_IO* IOComponent)
{
	// An untagged component can't be reached by Broadcast, so there is
	// nothing to register it under.
	if (!IOComponent || !TargetName.IsValid())
	{
		return;
	}
	
	TArray<TWeakObjectPtr<USST_IO>>& Bucket = IOComponentRegistry.FindOrAdd(TargetName);
	Bucket.AddUnique(IOComponent);
	
	UE_LOG(LOG_SST, Log, TEXT("Registered SST_IO '%s' under TargetName '%s'"), *IOComponent->GetOwner()->GetName(), *TargetName.ToString());
}

void USST_Subsystem::UnregisterSST_IO(const FGameplayTag& TargetName, USST_IO* IOComponent)
{
	if (TArray<TWeakObjectPtr<USST_IO>>* Bucket = IOComponentRegistry.Find(TargetName))
	{
		Bucket->RemoveAll([IOComponent](const TWeakObjectPtr<USST_IO>& Entry)
		{
			return !Entry.IsValid() || Entry.Get() == IOComponent;
		});

		// Clean up empty buckets so the map doesn't accumulate stale keys forever
		if (Bucket->Num() == 0)
		{
			IOComponentRegistry.Remove(TargetName);
		}
	}
}

TArray<USST_IO*> USST_Subsystem::GetIOComponentsByTargetName(const FGameplayTag& TargetName, bool bIncludeChildren) const
{
	TArray<USST_IO*> Result;

	if (!TargetName.IsValid())
	{
		return Result;
	}

	auto CollectBucket = [&Result](const TArray<TWeakObjectPtr<USST_IO>>& Bucket)
	{
		for (const TWeakObjectPtr<USST_IO>& Entry : Bucket)
		{
			if (USST_IO* Valid = Entry.Get())
			{
				Result.Add(Valid);
			}
		}
	};

	if (!bIncludeChildren)
	{
		if (const TArray<TWeakObjectPtr<USST_IO>>* Bucket = IOComponentRegistry.Find(TargetName))
		{
			CollectBucket(*Bucket);
		}
		return Result;
	}

	// Each component is registered under exactly one tag, so a key that
	// matches contributes its bucket once and no component can appear twice.
	for (const TPair<FGameplayTag, TArray<TWeakObjectPtr<USST_IO>>>& Pair : IOComponentRegistry)
	{
		if (Pair.Key.MatchesTag(TargetName))
		{
			CollectBucket(Pair.Value);
		}
	}

	return Result;
}

void USST_Subsystem::IO_ReceiveOutput(const FSST_IO_Output& Output)
{
	FRecursiveCounter RecursiveCounter(RecursiveCount);
	if (RecursiveCount >= RecursiveMax)
	{
		FString Target;
		if (Output.IOType == ESSTIOType::Message)
		{
			Target = Output.TargetActor.IsNull() ? TEXT ("<none>") : Output.TargetActor.ToString();
		}
		else if (Output.IOType == ESSTIOType::Broadcast)
		{
			Target = Output.TargetName.ToString();
		}
			
		UE_LOG(LOG_SST, Warning, TEXT("Target '%s', trigger '%s', target event '%s' exceeded the maximum IO chain depth of %d"),
			*Target, *Output.TriggerEvent.ToString(), *Output.TargetEvent.ToString(), RecursiveMax);
		return;
	}
	
	
	
	TArray<USST_IO*> Targets;
	const ESST_IO_Results Result = USST_IO::ResolveOutputTargets(Output, GetWorld(), Targets);

	// Everything except Resolved is a reason not to fire, each with its own
	// message so a silent no-op can be told apart from the others.
	switch (Result)
	{
	case ESST_IO_Results::Resolved:
		break;

	case ESST_IO_Results::Unassigned:
		UE_LOG(LOG_SST, Warning, TEXT("Output for trigger '%s' has no target assigned"),
			*Output.TriggerEvent.ToString());
		return;

	case ESST_IO_Results::NotLoaded:
		UE_LOG(LOG_SST, Warning, TEXT("Target '%s' is not loaded, so trigger '%s' did not fire"),
			*Output.TargetActor.ToString(), *Output.TriggerEvent.ToString());
		return;

	case ESST_IO_Results::MissingIO:
		UE_LOG(LOG_SST, Warning, TEXT("Target '%s' has no SST_IO component"),
			*Output.TargetActor.ToString());
		return;

	case ESST_IO_Results::NoMatch:
		UE_LOG(LOG_SST, Warning, TEXT("No loaded actor answers to target name '%s'%s"),
			*Output.TargetName.ToString(), Output.bIncludeChildren ? TEXT(" or its children") : TEXT(""));
		return;
	}

	//Only can be reached if the Enum is resolved
	for (USST_IO* Entry : Targets)
	{
		//create the IO Message Object
		USST_IOMessage* MessageObject = USST_IOMessage::RegisterMessage(Output, Entry, this);
		
		//add unique entry to the message obj array
		IO_Messages.AddUnique(MessageObject);
		
		//Triggers the message object to call Receive Output on target component 
		MessageObject->TriggerMessage();
	}
}

void USST_Subsystem::DestroyMessage(USST_IOMessage* Message)
{
	IO_Messages.Remove(Message);
	Message->MarkAsGarbage();
}
