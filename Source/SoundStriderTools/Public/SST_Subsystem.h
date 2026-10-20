// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#pragma once

#include "CoreMinimal.h"

#include "SST_IO.h"
#include "SST_IOMessage.h"
#include "Subsystems/WorldSubsystem.h"

#include "SST_Subsystem.generated.h"

/**
 * Per-world registry and dispatcher for the IO system. A world subsystem so
 * pending delayed messages die with their level instead of outliving it.
 */
UCLASS()
class SOUNDSTRIDERTOOLS_API USST_Subsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	
	//registration functionality
	void RegisterSST_IO(const FGameplayTag& TargetName, USST_IO* IOComponent);
	void UnregisterSST_IO(const FGameplayTag& TargetName, USST_IO* IOComponent);
	
	// Exact: components registered under exactly TargetName (a map lookup).
	// bIncludeChildren: also components whose tag descends from TargetName,
	// found by scanning the registry's distinct tags rather than its actors.
	UFUNCTION(BlueprintCallable, Category = "SoundStriderTools|IO")
	TArray<USST_IO*> GetIOComponentsByTargetName(const FGameplayTag& TargetName, bool bIncludeChildren = false) const;

	//SST_IO has broadcasted that an input event has been triggered
	void IO_ReceiveOutput(const FSST_IO_Output& Output);
	
	//Function to destroy the IOMessage once it has triggered the output function 
	void DestroyMessage(USST_IOMessage* Message);
	
private:
	// Every registered component, keyed by the tag it answers to. Not a
	// UPROPERTY: weak pointers invalidate themselves without GC tracking.
	TMap<FGameplayTag, TArray<TWeakObjectPtr<USST_IO>>> IOComponentRegistry;
	
	//Array of all unique message objects 
	UPROPERTY()
	TArray<TObjectPtr<USST_IOMessage>> IO_Messages;
	
	
	//struct to count the length of a recursive chain on a single Output
	int32 RecursiveCount = 0;
	int32 RecursiveMax = 32;
	
	struct FRecursiveCounter
	{
		int32& CounterRef;
		FRecursiveCounter(int32& InCounter) : CounterRef(InCounter)
		{
			CounterRef++;
		}
		
		~FRecursiveCounter()
		{
			CounterRef--;
		}
	};
};
