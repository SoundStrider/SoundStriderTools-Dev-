// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/ObjectKey.h"

#include "SST_IO.generated.h"

class USST_Subsystem;
class USST_IO;

UENUM(BlueprintType)
enum class ESSTIOType : uint8
{
	Message = 0,
	Broadcast = 1
};

USTRUCT(BlueprintType)
struct FSST_IO_TargetEventSpec
{
	GENERATED_BODY()

	// The custom event on the owning actor this describes. Picked from a list of
	// the owner's Blueprint custom events, so it can't be mistyped. The event's
	// inputs are the parameters an output fills in; there is nothing else to
	// declare here.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SoundStriderTools|IO", meta=(GetOptions="GetTargetEventOptions"))
	FName TargetEventName;

	//The tooltip description for this target event. Please fill it out here
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SoundStriderTools|IO", meta=(MultiLine = true))
	FString TargetDescription;
};

USTRUCT(BlueprintType)
struct FSST_IO_TriggerEventSpec
{
	GENERATED_BODY()

	//The name this trigger event is fired under with TriggerOutputsFromEvent
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SoundStriderTools|IO")
	FName TriggerEventName;

	//The tooltip description you want to use
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SoundStriderTools|IO", meta=(MultiLine = true))
	FString TriggerDescription;
};


USTRUCT(BlueprintType)
struct FSST_IO_Output
{
	GENERATED_BODY()

	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SoundStriderTools|IO")
	ESSTIOType IOType = ESSTIOType::Message;
	
	// Picked from the owning component's IO_TriggerEvents.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SoundStriderTools|IO", meta=(GetOptions="GetTriggerEventOptions"))
	FName TriggerEvent; //Thing it goes - OnOpen, OnClose, OnTriggered, OnDestroyed etc
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SoundStriderTools|IO", 
		meta=(EditCondition = "IOType == ESSTIOType::Message", EditConditionHides))
	TSoftObjectPtr<AActor> TargetActor; //Direct actor reference you are talking to - must be utilizing SST_IO
	
	// The tag Broadcast delivers to, e.g. SST_IO.Door. Categories restricts the
	// picker to the SST_IO tree and must match the root tag's name exactly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SoundStriderTools|IO", 
		meta=(EditCondition = "IOType == ESSTIOType::Broadcast", EditConditionHides, Categories = "SST_IO"))
	FGameplayTag TargetName;
	
	// Off: only actors tagged exactly TargetName receive this. On: descendants
	// of TargetName receive it too, so SST_IO.Door also reaches SST_IO.Door.Wood.
	// Opt-in per output, because a parent tag gaining a new child would
	// otherwise silently widen every existing Broadcast to that parent.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SoundStriderTools|IO", 
		meta=(EditCondition = "IOType == ESSTIOType::Broadcast", EditConditionHides))
	bool bIncludeChildren = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SoundStriderTools|IO")
	FName TargetEvent; //The event the target runs - Open, Close, Explode, TurnOn, TurnOff

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SoundStriderTools|IO")
	float Delay = 0.0; //how long before the Output Event fires
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SoundStriderTools|IO")
	int32 FireOnlyNTimes = 0; //how many times can this OutEvent be triggered? 0 is default for INFINITE 
	
	// One value per input of TargetEvent, laid out from the event itself by the
	// IO tool. FixedLayout: values are editable, but properties can't be added,
	// removed or retyped by hand -- the event's signature is the only source.
	// Not Blueprint-visible: the receiving event gets these as its inputs.
	UPROPERTY(EditAnywhere, Category="SoundStriderTools|IO", meta=(FixedLayout))
	FInstancedPropertyBag Parameters;

};

#if WITH_EDITOR
//struct to see incoming events on a selected actor in the IO tool
//does not need to be compiled at runtime and is also used for actor duplication receiving the same targeted input functionality
struct FSST_IO_Input
{
	TWeakObjectPtr<USST_IO> IOSender;
	int32 OutputArrayLocation;
};
#endif

enum class ESST_IO_Results : uint8
{
	Unassigned,
	NotLoaded,
	NoMatch,
	MissingIO,
	Resolved
};

UCLASS( ClassGroup=(SoundStriderTools), meta=(BlueprintSpawnableComponent), DisplayName="SST IO Component" )
class SOUNDSTRIDERTOOLS_API USST_IO : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	USST_IO();
	
	//IO Scripting Properties
	// The tag this class answers to for Broadcast, set once in the Blueprint's
	// class defaults. Read it through GetEffectiveTargetName(), never directly,
	// so a placed instance's override is respected everywhere.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SoundStriderTools|IO", meta=(Categories = "SST_IO"))
	FGameplayTag TargetName;
	
	// Placed-instance opt-in to answer to InstanceTargetName instead of the
	// class tag. Instance-only, so it never clutters the Blueprint defaults.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="SoundStriderTools|IO")
	bool bOverrideTargetName = false;
	
	// This instance's own tag, used when the override is on.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="SoundStriderTools|IO",
		meta=(EditCondition = "bOverrideTargetName", EditConditionHides, Categories = "SST_IO"))
	FGameplayTag InstanceTargetName;
	
	//Any potential Outgoing Events you want to be able to handle - what DID I do. Defined once in Class Defaults - InputEvent in struct - OnTriggerAction
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SoundStriderTools|IO")
	TArray<FSST_IO_TriggerEventSpec> IO_TriggerEvents;
	
	//Any potential Incoming Events you want to be able to handle - what DO I do. Defined once in Class Defaults - OutputEvent in struct.
	//Each entry is a custom event on the owner; its inputs become the output's parameters.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SoundStriderTools|IO")
	TArray<FSST_IO_TargetEventSpec> IO_TargetEvents;
	
	//Any outgoing functions you want this specific actor to be able to perform. 
	//This is defined on a per-actor basis and utilizes the events defined at the class level to create the struct
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SoundStriderTools|Outputs")
	TArray<FSST_IO_Output> IO_Outputs;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SoundStriderTools|IO")
	bool bCopyInputsOnDuplication = true;
	
	//IO Scripting Functions
	
	// TriggerEvent must match a TriggerEventName in IO_TriggerEvents.
	UFUNCTION(BlueprintCallable, Category="SoundStriderTools|IO")
	void TriggerOutputsFromEvent(FName TriggerEvent);

	// The tag this component actually answers to. The instance tag wins only
	// when the override is on AND it has been set; an override left empty
	// falls back to the class tag, so a freshly placed logic actor still
	// answers to its type tag until a designer names it.
	UFUNCTION(BlueprintPure, Category="SoundStriderTools|IO")
	FGameplayTag GetEffectiveTargetName() const;

	// Native owners declare their events with these, from the constructor,
	// after creating this component. Pass the function name with
	// GET_FUNCTION_NAME_CHECKED so a rename is a compile error, not a silent
	// break. Registering a name twice keeps the first entry.
	void RegisterTargetEvent(FName EventName, const FString& Description);
	void RegisterTriggerEvent(FName EventName, const FString& Description);

	//Take in an Output and trigger events from there
	void ReceiveInput(const FSST_IO_Output& Input);

	//Spec lookups by event name. Null when the event isn't declared.
	const FSST_IO_TargetEventSpec* FindTargetEventSpec(FName EventName) const;
	const FSST_IO_TriggerEventSpec* FindTriggerEventSpec(FName EventName) const;

	// The function a target event name runs on this component's owner. Works
	// on a Blueprint's component template as well as a placed instance. Null
	// if there is no function by that name.
	UFunction* FindTargetEventFunction(FName EventName) const;

	// What the IO_TargetEvents picker offers: a custom event declared in a
	// Blueprint graph. Excludes Blueprint functions, overrides of parent or
	// native events, event dispatchers, and engine-generated functions.
	static bool IsBlueprintCustomEvent(const UFunction* Function);

	// True when every parameter is an input (see IsEventInput) whose type an
	// output's parameter bag can hold. Outputs, a return value and explicit
	// Pass-by-Reference inputs fail it: there is nowhere for an IO message
	// to send a result back to.
	static bool HasSupportedSignature(const UFunction* Function);
	
	// Whether Param is an input IO fills in when it fires an event. True for
	// plain inputs (native const& inputs included) and for arrays, sets and
	// maps, which Blueprint always passes by reference and so flags like
	// outputs. False for the return value, real outputs, and non-container
	// inputs marked Pass-by-Reference. An array explicitly marked
	// Pass-by-Reference can't be told apart from the automatic case, so it
	// counts as an input; IO passes a copy either way.
	static bool IsEventInput(const FProperty* Param);

	// GetOptions source for FSST_IO_TargetEventSpec::TargetEventName.
	UFUNCTION()
	TArray<FName> GetTargetEventOptions() const;
	
	// GetOptions source for FSST_IO_Output::TriggerEvent: this component's
	// declared trigger events, in declaration order.
	UFUNCTION()
	TArray<FName> GetTriggerEventOptions() const;

	
	//Resolving Inputs and Outputs
	static ESST_IO_Results ResolveOutputTargets(const FSST_IO_Output& Output, UWorld* World, TArray<USST_IO*>& OutComponents);
	static bool DoesOutputTarget(const FSST_IO_Output& Output, const USST_IO* Candidate);
	
	// The single definition of whether a component's tag satisfies a Broadcast
	// output: exact equality, or MatchesTag when the output includes children.
	// MatchesTag is true when CandidateTag equals TargetName or descends from it.
	static bool TagMatchesBroadcast(const FGameplayTag& CandidateTag, const FSST_IO_Output& Output);

	
#if WITH_EDITOR
	//helper function to find Outputs targeting this component
	TArray<FSST_IO_Input> FindIncomingCommunication()const;
#endif
	
#if WITH_EDITORONLY_DATA
	// Pairs a copy with its original during an editor duplication; empty the
	// rest of the time. Visible rather than a bare UPROPERTY() on purpose: a
	// component added in a Blueprint's Components panel is rebuilt when the
	// pasted copy's construction script runs, and that rebuild only carries
	// over values of editable or visible properties. A bare UPROPERTY() was
	// dropped there, so Blueprint actors never paired with their originals.
	UPROPERTY(VisibleInstanceOnly, AdvancedDisplay, Category="SoundStriderTools|IO", meta=(DisplayName="Duplication GUID"))
	FGuid IO_Guid;
#endif
	
	
	//Override functions
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
private:
	//reference to SST_Subsystem which is also the IO Scripting manager
	UPROPERTY()
	TObjectPtr<USST_Subsystem> IOManager;
	
	//increment the times fired on an output if we're gating the total possible amount of times an event output can be triggered
	void IncrementTimesOutputHasFired(const int32 OutputIndex);
	
	//map of output to how many times it has fired if it has a FireOnlyNTimes value 
	UPROPERTY()
	TMap<int32, int32> OutputTimesFiredMap;

	// The tag this component registered under in BeginPlay. EndPlay
	// unregisters with this rather than recomputing, so the two always match.
	FGameplayTag RegisteredTargetName;

	// Problems already warned about, so a broken event logs once per
	// component instead of on every trigger.
	TSet<FName> WarnedTargetEvents;
	
	// HasSupportedSignature's answer for each event this component has run,
	// so the signature is checked once per event instead of on every fire.
	// TObjectKey, not a raw pointer: a function recreated by a Blueprint
	// recompile is a different key, so a stale entry can never match it.
	TMap<TObjectKey<UFunction>, bool> SupportedSignatureCache;

	// Logs Message once per Key on this component. Key is the event name, or
	// "Event.Input" for a problem with one input.
	void WarnOnce(FName Key, const FString& Message);

	// The class whose functions target events name: the owner's class for a
	// placed component, the Blueprint class for a component template.
	const UClass* GetOwnerClass() const;
};
