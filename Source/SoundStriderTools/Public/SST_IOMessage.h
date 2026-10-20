// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SST_IO.h"

#include "SST_IOMessage.generated.h"

class USST_Subsystem;
/**
 * 
 */
UCLASS()
class SOUNDSTRIDERTOOLS_API USST_IOMessage : public UObject
{
	GENERATED_BODY()
	
public:
	
	static USST_IOMessage* RegisterMessage(const FSST_IO_Output& Output, USST_IO* TargetIO, USST_Subsystem* IOManager);
	
	//Function that calls the ReceiveInput on the target SST_IO Component 
	void TriggerMessage(); 
	
private:
	void Initialize(const FSST_IO_Output& Output, USST_IO* TargetIO, USST_Subsystem* IOManager);
	
	//references to be set during Initialize
	UPROPERTY()
	TWeakObjectPtr<USST_Subsystem> _IOManager;
	UPROPERTY()
	TWeakObjectPtr<USST_IO> _TargetIO;
	UPROPERTY()
	FSST_IO_Output _Output;
	
	FTimerHandle DelayTimerHandle;
	float Delay = 0.0f;
	
	void Deliver();
	void DeRegisterMessage();
};
