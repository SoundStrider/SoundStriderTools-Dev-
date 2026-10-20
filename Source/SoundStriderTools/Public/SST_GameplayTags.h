// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#pragma once

#include "NativeGameplayTags.h"

/*
 * Every gameplay tag the plugin itself owns. These are native: they compile
 * into this module, so they exist in any project with the plugin enabled and
 * need no ini setup. The Gameplay Tag Manager also refuses to rename or delete
 * non-ini tags, so the root and the logic-actor parents can't be broken by a
 * designer reorganizing tags.
 *
 * Tags a project authors for its own actors (SST_IO.Door, SST_IO.Door.Wood)
 * live in that project's config, as children of the root declared here.
 *
 * The root's name must match the Categories meta on every target-name field,
 * which is written as the literal "SST_IO" because metadata can't reference
 * a C++ symbol.
 */

// Root of the target namespace. Every IO target tag descends from this.
SOUNDSTRIDERTOOLS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_SST_IO);

// Parent of every logic actor type, so an Include Children Broadcast to it
// reaches all logic actors at once.
SOUNDSTRIDERTOOLS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_SST_IO_Logic);

// Class default tag for ASST_Transmitter.
SOUNDSTRIDERTOOLS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_SST_IO_Logic_Transmitter);
