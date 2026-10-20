// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SST_LogicActor.generated.h"

class USST_IO;
class UBillboardComponent;

/**
 * Shared base for actors that exist only to drive IO logic: transmitters,
 * timers, spawners, branches. Owns the scaffolding every one of them needs --
 * a root, an editor-only sprite so it can be selected in the viewport, and the
 * SST_IO component -- so each subclass only declares its own events.
 *
 * Abstract: a logic actor with no behavior means nothing, so only subclasses
 * can be placed.
 */
UCLASS(Abstract, ClassGroup=(SoundStriderTools))
class SOUNDSTRIDERTOOLS_API ASST_LogicActor : public AActor
{
	GENERATED_BODY()

public:
	ASST_LogicActor();
	
	

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SoundStriderTools|Logic", DisplayName="SST_IO")
	TObjectPtr<USST_IO> IOComponent;
	
#if WITH_EDITORONLY_DATA
	// Stripped from packaged builds entirely, so this costs nothing at runtime.
	UPROPERTY()
	TObjectPtr<UBillboardComponent> SpriteComponent;
#endif
};
