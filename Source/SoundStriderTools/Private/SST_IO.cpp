// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.


#include "SST_IO.h"

#include "SST_LOG.h"
#include "SST_Subsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "UObject/UnrealType.h"

// Sets default values for this component's properties
USST_IO::USST_IO()
{
	// Event-driven only. Nothing here needs a per-frame update.
	PrimaryComponentTick.bCanEverTick = false;
}

FGameplayTag USST_IO::GetEffectiveTargetName() const
{
	return (bOverrideTargetName && InstanceTargetName.IsValid()) ? InstanceTargetName : TargetName;
}


// Called when the game starts
void USST_IO::BeginPlay()
{
	Super::BeginPlay();


	if (UWorld* World = GetWorld())
	{
		IOManager = World->GetSubsystem<USST_Subsystem>();
	}

	if (!IOManager)
	{
		// Without this, a failed lookup means every TriggerOutputsFromEvent
		// silently returns early, which is indistinguishable from no outputs
		// being authored.
		UE_LOG(LOG_SST, Error, TEXT("SST_IO on '%s' could not find USST_Subsystem; IO will not fire"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("<unknown>"));
		return;
	}

	// Stored so EndPlay unregisters under exactly the tag used here.
	RegisteredTargetName = GetEffectiveTargetName();
	IOManager->RegisterSST_IO(RegisteredTargetName, this);
}

void USST_IO::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IOManager)
	{
		IOManager->UnregisterSST_IO(RegisteredTargetName, this);
	}
	
	Super::EndPlay(EndPlayReason);
}


//Function to call in the event to broadcast to the IO system that this Event has been triggered
void USST_IO::TriggerOutputsFromEvent(FName TriggerEvent)
{
	UE_LOG(LOG_SST, Log, TEXT("TriggerOutputsFromInputEvent: %s"), *TriggerEvent.ToString());
	if (!IOManager) return;

	if (!FindTriggerEventSpec(TriggerEvent))
	{
		UE_LOG(LOG_SST, Warning, TEXT("'%s' is not a declared trigger event on %s"),
			*TriggerEvent.ToString(), GetOwner() ? *GetOwner()->GetName() : TEXT("<unknown>"));
		return;
	}
	
	
	for (int32 Index = 0; Index < IO_Outputs.Num(); Index++)
	{
		const FSST_IO_Output& Output = IO_Outputs[Index];
		if (Output.TriggerEvent != TriggerEvent) continue;
		
		if (Output.FireOnlyNTimes > 0)
		{
			
			const int32* FoundTimesFired = OutputTimesFiredMap.Find(Index);
			const int32 TimesFired = FoundTimesFired ? *FoundTimesFired : 0;

			if (TimesFired < Output.FireOnlyNTimes)
			{
				IncrementTimesOutputHasFired(Index);
				IOManager->IO_ReceiveOutput(Output);
			}
		}
		else
		{
			IOManager->IO_ReceiveOutput(Output);
		}
	}
}


void USST_IO::IncrementTimesOutputHasFired(const int32 OutputIndex)
{
	OutputTimesFiredMap.FindOrAdd(OutputIndex)++;
}

void USST_IO::ReceiveInput(const FSST_IO_Output& Input)
{
	AActor* Owner = GetOwner();
	if (!Owner) return;
	
	const FName EventName = Input.TargetEvent;

	// Undeclared means this component doesn't participate in the event. For a
	// Broadcast that's normal -- "every door, open" reaches doors that can't
	// open -- so it stays silent. A Message names this actor explicitly, so an
	// undeclared event there means the output itself is broken.
	if (!FindTargetEventSpec(EventName))
	{
		if (Input.IOType == ESSTIOType::Message)
		{
			WarnOnce(EventName, FString::Printf(
				TEXT("'%s' received Message '%s', which it does not declare in IO_TargetEvents"),
				*Owner->GetName(), *EventName.ToString()));
		}
		return;
	}

	// From here the event is declared, so every failure is a real bug: the
	// spec promises an event the actor can't run.
	UFunction* Function = Owner->FindFunction(EventName);
	if (!Function)
	{
		WarnOnce(EventName, FString::Printf(
			TEXT("'%s' declares target event '%s' but has no event by that name"),
			*Owner->GetName(), *EventName.ToString()));
		return;
	}

	const TObjectKey<UFunction> FunctionKey(Function);
	const bool* CachedSupported = SupportedSignatureCache.Find(FunctionKey);
	const bool bSupported = CachedSupported
		? *CachedSupported
		: SupportedSignatureCache.Add(FunctionKey, HasSupportedSignature(Function));

	if (!bSupported)
	{
		WarnOnce(EventName, FString::Printf(
			TEXT("'%s' target event '%s' has an output, a Pass-by-Reference input, or an input type IO parameters can't hold"),
			*Owner->GetName(), *EventName.ToString()));
		return;
	}

	// The event's parameter buffer. Every input is constructed to its default
	// first, so anything the output doesn't supply still arrives as a valid
	// value rather than zeroed memory.
	uint8* Parms = Function->ParmsSize > 0
		? static_cast<uint8*>(FMemory_Alloca_Aligned(Function->ParmsSize, Function->GetMinAlignment()))
		: nullptr;

	if (Parms)
	{
		FMemory::Memzero(Parms, Function->ParmsSize);
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Parms);
		}
	}

	// Inputs are matched to the bag by name, and only copied when the stored
	// type is exactly the input's type. A mismatch means the event changed
	// after the output was last synced in the editor.
	const uint8* BagMemory = Input.Parameters.GetValue().GetMemory();

	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		const FProperty* Param = *It;
		if (!IsEventInput(Param)) continue;

		// The warning key is only built when there is something to warn
		// about. This loop runs on every fire, and formatting a string and
		// registering an FName per input each time is wasted work.
		auto MakeInputKey = [&EventName, Param]()
		{
			return FName(*FString::Printf(TEXT("%s.%s"), *EventName.ToString(), *Param->GetName()));
		};

		const FPropertyBagPropertyDesc* Desc = Input.Parameters.FindPropertyDescByName(Param->GetFName());

		if (!Desc || !Desc->CachedProperty || !BagMemory)
		{
			WarnOnce(MakeInputKey(), FString::Printf(
				TEXT("'%s' target event '%s' got no value for input '%s'; it fires with the default. Sync the output in the IO tool"),
				*Owner->GetName(), *EventName.ToString(), *Param->GetName()));
			continue;
		}

		if (!Desc->CachedProperty->SameType(Param))
		{
			WarnOnce(MakeInputKey(), FString::Printf(
				TEXT("'%s' target event '%s' input '%s' is stored as a different type; it fires with the default. Sync the output in the IO tool"),
				*Owner->GetName(), *EventName.ToString(), *Param->GetName()));
			continue;
		}

		Param->CopyCompleteValue(
			Param->ContainerPtrToValuePtr<void>(Parms),
			Desc->CachedProperty->ContainerPtrToValuePtr<void>(BagMemory));
	}

	Owner->ProcessEvent(Function, Parms);

	// Alloca memory is freed with the stack frame, but the values in it
	// (strings, arrays) own heap memory that has to be released explicitly.
	if (Parms)
	{
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Parms);
		}
	}
}

void USST_IO::WarnOnce(FName Key, const FString& Message)
{
	bool bAlreadyWarned = false;
	WarnedTargetEvents.Add(Key, &bAlreadyWarned);

	if (!bAlreadyWarned)
	{
		UE_LOG(LOG_SST, Warning, TEXT("%s"), *Message);
	}
}

const FSST_IO_TargetEventSpec* USST_IO::FindTargetEventSpec(FName EventName) const
{
	if (EventName.IsNone()) return nullptr;
	return IO_TargetEvents.FindByPredicate(
		[EventName](const FSST_IO_TargetEventSpec& S) { return S.TargetEventName == EventName; });
}

const FSST_IO_TriggerEventSpec* USST_IO::FindTriggerEventSpec(FName EventName) const
{
	if (EventName.IsNone()) return nullptr;
	return IO_TriggerEvents.FindByPredicate(
		[EventName](const FSST_IO_TriggerEventSpec& S) { return S.TriggerEventName == EventName; });
}

void USST_IO::RegisterTargetEvent(FName EventName, const FString& Description)
{
	if (EventName.IsNone() || FindTargetEventSpec(EventName)) return;

	FSST_IO_TargetEventSpec Spec;
	Spec.TargetEventName = EventName;
	Spec.TargetDescription = Description;
	IO_TargetEvents.Add(Spec);
}

void USST_IO::RegisterTriggerEvent(FName EventName, const FString& Description)
{
	if (EventName.IsNone() || FindTriggerEventSpec(EventName)) return;

	FSST_IO_TriggerEventSpec Spec;
	Spec.TriggerEventName = EventName;
	Spec.TriggerDescription = Description;
	IO_TriggerEvents.Add(Spec);
}

const UClass* USST_IO::GetOwnerClass() const
{
	// A placed component, or a native default subobject on a Blueprint's
	// defaults, has an owning actor. A component added in a Blueprint's
	// Components panel is a template whose outer is the Blueprint class.
	if (const AActor* Owner = GetOwner())
	{
		return Owner->GetClass();
	}
	return Cast<UClass>(GetOuter());
}

UFunction* USST_IO::FindTargetEventFunction(FName EventName) const
{
	const UClass* OwnerClass = GetOwnerClass();
	return (OwnerClass && !EventName.IsNone()) ? OwnerClass->FindFunctionByName(EventName) : nullptr;
}

bool USST_IO::IsBlueprintCustomEvent(const UFunction* Function)
{
	if (!Function) return false;

	// Declared in a Blueprint graph, not native code.
	if (!Cast<UBlueprintGeneratedClass>(Function->GetOwnerClass())) return false;

	// An override of a parent's event or function (BeginPlay, a native
	// BlueprintImplementableEvent) has a super function. Those are engine
	// hooks, not events a designer declared for IO.
	if (Function->GetSuperFunction()) return false;

	// Event dispatcher signatures live on the class as functions too.
	if (Function->HasAnyFunctionFlags(FUNC_Delegate)) return false;

	// Generated by the compiler, not authored.
	const FString Name = Function->GetName();
	if (Name.StartsWith(TEXT("ExecuteUbergraph")) || Name == TEXT("UserConstructionScript")) return false;

	// Measured on BP_Door: a custom event is Callable|BlueprintEvent with no
	// access specifier. A Blueprint function adds Public/Private/Protected,
	// and timeline update/finished functions aren't Callable.
	if (!Function->HasAllFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintEvent)) return false;
	if (Function->HasAnyFunctionFlags(FUNC_Public | FUNC_Private | FUNC_Protected)) return false;

	return true;
}

bool USST_IO::HasSupportedSignature(const UFunction* Function)
{
	if (!Function) return false;

	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		if (!IsEventInput(*It)) return false;

		// The bag's own type mapping decides what it can hold. A type it
		// doesn't recognise (delegates, interfaces) comes back as None.
		const FPropertyBagPropertyDesc Desc(It->GetFName(), *It);
		if (Desc.ValueType == EPropertyBagPropertyType::None) return false;
	}
	return true;
}

bool USST_IO::IsEventInput(const FProperty* Param)
{
	if (!Param || !Param->HasAnyPropertyFlags(CPF_Parm)) return false;
	if (Param->HasAnyPropertyFlags(CPF_ReturnParm)) return false;

	// Not flagged as an output: a plain input.
	if (!Param->HasAnyPropertyFlags(CPF_OutParm)) return true;

	// Flagged as an output. Blueprint passes containers by reference, which
	// sets this flag along with ReferenceParm, so those are still inputs. A
	// real output has no ReferenceParm; any other reference is a designer's
	// explicit Pass-by-Reference, which is hidden by design.
	const bool bIsContainer = CastField<FArrayProperty>(Param)
		|| CastField<FSetProperty>(Param)
		|| CastField<FMapProperty>(Param);

	return bIsContainer && Param->HasAnyPropertyFlags(CPF_ReferenceParm);
}

TArray<FName> USST_IO::GetTargetEventOptions() const
{
	TArray<FName> Options;

	const UClass* OwnerClass = GetOwnerClass();
	if (!OwnerClass) return Options;

	// IncludeSuper, so a child Blueprint can pick events its parent declared.
	for (TFieldIterator<UFunction> It(OwnerClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		const UFunction* Function = *It;
		if (IsBlueprintCustomEvent(Function) && HasSupportedSignature(Function))
		{
			Options.AddUnique(Function->GetFName());
		}
	}

	// Events a native owner registered aren't Blueprint custom events, but
	// they are valid targets, so they stay selectable rather than reading as
	// an unlisted value.
	for (const FSST_IO_TargetEventSpec& Spec : IO_TargetEvents)
	{
		const UFunction* Function = OwnerClass->FindFunctionByName(Spec.TargetEventName);
		if (Function && HasSupportedSignature(Function))
		{
			Options.AddUnique(Spec.TargetEventName);
		}
	}

	Options.Sort(FNameLexicalLess());
	return Options;
}

TArray<FName> USST_IO::GetTriggerEventOptions() const
{
	TArray<FName> Options;
	for (const FSST_IO_TriggerEventSpec& Spec : IO_TriggerEvents)
	{
		// An entry whose name hasn't been typed yet isn't something to fire.
		if (!Spec.TriggerEventName.IsNone())
		{
			Options.AddUnique(Spec.TriggerEventName);
		}
	}
	return Options;
}

#if WITH_EDITOR
//function for finding what's targeting this IO Component 
TArray<FSST_IO_Input> USST_IO::FindIncomingCommunication()const
{
	TArray<FSST_IO_Input> Inputs;
	
	UWorld* World = GetWorld();
	if (!World)
	{
		return Inputs;
	}
	
	for (TActorIterator<AActor> ActorItr(World); ActorItr; ++ActorItr)
	{
		if (USST_IO* IO = ActorItr->FindComponentByClass<USST_IO>())
		{
			if (IO == this) continue;
			
			for (int32 i = 0; i < IO->IO_Outputs.Num(); ++i)
			{
				const FSST_IO_Output& Output = IO->IO_Outputs[i];
				
				if (DoesOutputTarget(Output, this))
				{
					Inputs.Add({IO, i});
				}
			}
		}
	}
	return Inputs;
}

#endif



ESST_IO_Results USST_IO::ResolveOutputTargets(const FSST_IO_Output& Output, UWorld* World, TArray<USST_IO*>& OutComponents)
{
	OutComponents.Reset();
	
	if (Output.IOType == ESSTIOType::Message)
	{
		if (Output.TargetActor.IsNull())
		{
			return ESST_IO_Results::Unassigned;
		}
		
		AActor* TargetActor = Output.TargetActor.Get();
		if (!TargetActor)
		{
			return ESST_IO_Results::NotLoaded;
		}
		
		USST_IO* TargetIO = TargetActor->FindComponentByClass<USST_IO>();
		if (!TargetIO)
		{
			return ESST_IO_Results::MissingIO;
		}
		OutComponents.Add(TargetIO);
		return  ESST_IO_Results::Resolved;
	}

	if (Output.IOType == ESSTIOType::Broadcast)
	{
		if (!Output.TargetName.IsValid()) return ESST_IO_Results::Unassigned;
		
		if (!World) return ESST_IO_Results::NoMatch;
		
		// The only place runtime and editor diverge. The registry is filled in
		// BeginPlay, so it is empty in the editor and has to be swept instead.
		if (World->IsGameWorld())
		{
			if (USST_Subsystem* Subsystem = World->GetSubsystem<USST_Subsystem>())
			{
				OutComponents = Subsystem->GetIOComponentsByTargetName(Output.TargetName, Output.bIncludeChildren);
			}
		}
		else
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				USST_IO* IO = It->FindComponentByClass<USST_IO>();
				if (IO && TagMatchesBroadcast(IO->GetEffectiveTargetName(), Output))
				{
					OutComponents.Add(IO);
				}
			}
		}
		return OutComponents.Num() > 0 ? ESST_IO_Results::Resolved : ESST_IO_Results::NoMatch;
	}

	
	return ESST_IO_Results::Unassigned;
}

bool USST_IO::DoesOutputTarget(const FSST_IO_Output& Output, const USST_IO* Candidate)
{
	if (!Candidate) return false;
		
	if (Output.IOType == ESSTIOType::Message)
	{
		if (Output.TargetActor.IsNull()) return false;
		return FSoftObjectPath(Candidate->GetOwner()) == Output.TargetActor.ToSoftObjectPath();
	}
	
	if (Output.IOType == ESSTIOType::Broadcast)
	{
		return TagMatchesBroadcast(Candidate->GetEffectiveTargetName(), Output);
	}
	
	return false;
}

bool USST_IO::TagMatchesBroadcast(const FGameplayTag& CandidateTag, const FSST_IO_Output& Output)
{
	// An unassigned output targets nothing, and an untagged component answers
	// to nothing -- neither side may match by both being empty.
	if (!Output.TargetName.IsValid() || !CandidateTag.IsValid()) return false;

	return Output.bIncludeChildren
		? CandidateTag.MatchesTag(Output.TargetName)
		: CandidateTag == Output.TargetName;
}
