// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "SST_LogicActor.h"
#include "SST_Transmitter.generated.h"

/**
 * A routing hub with no presence in the world. Receives one IO event and
 * fans it out to any number of targets, so a single trigger on a gameplay
 * actor doesn't have to enumerate every consequence itself.
 */
UCLASS(ClassGroup=(SoundStriderTools), DisplayName="SST Transmitter")
class SOUNDSTRIDERTOOLS_API ASST_Transmitter : public ASST_LogicActor
{
	GENERATED_BODY()

public:
	ASST_Transmitter();

	/**
	 * The single incoming event. Anything targeting this transmitter calls
	 * IO_Transmit, which re-emits as the trigger event named below. Registered
	 * in the constructor, as every native logic actor's events are.
	 */
	UFUNCTION(BlueprintCallable, Category="SoundStriderTools|IO")
	void IO_Transmit();

protected:
	// What IO_Transmit fires as. Must appear in the component's IO_TriggerEvents.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SoundStriderTools|Logic")
	FName TransmitterTriggerEvent = FName(TEXT("Transmit"));
};
