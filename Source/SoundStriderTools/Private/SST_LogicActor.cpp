// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#include "SST_LogicActor.h"
#include "SST_IO.h"
#include "Components/SceneComponent.h"
#include "Components/BillboardComponent.h"

ASST_LogicActor::ASST_LogicActor()
{
	// No tick, no render, no collision: a logic actor exists only to hold an
	// SST_IO component and be findable by the routing system.
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);

	// A root has to exist before anything can attach to it. The transmitter
	// previously had none, so its sprite was attaching to null.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	IOComponent = CreateDefaultSubobject<USST_IO>(TEXT("SST_IO"));

	// Unlike a regular actor, where overriding the class tag is a rare,
	// deliberate act, a logic actor almost always needs its own name -- two
	// transmitters in one level are two different things. So the override
	// starts on. With no instance tag set yet, GetEffectiveTargetName still
	// falls back to the subclass's type tag.
	IOComponent->bOverrideTargetName = true;

#if WITH_EDITORONLY_DATA
	SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (SpriteComponent)
	{
		SpriteComponent->SetupAttachment(RootComponent);
		SpriteComponent->bIsScreenSizeScaled = true;
		SpriteComponent->SetHiddenInGame(true);
	}
#endif
}
