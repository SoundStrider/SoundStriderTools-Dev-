// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#pragma once

#include "Modules/ModuleManager.h"
#include "Widgets/Docking/SDockTab.h"
// The editor module logs through the runtime module's LOG_SST category.
#include "SST_LOG.h"
#include "EdGraphUtilities.h"
#include "MessageLogModule.h"


class FSoundStriderToolsEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// Broadcast after the duplication handler has finished rewiring links and
	// clearing tags. The editor selects the copies before that handler runs,
	// so anything showing IO data (the IO tool) refreshes on this instead.
	static FSimpleMulticastDelegate OnIODuplicationProcessed;

	
private:

	TSharedRef<SDockTab> SpawnIOToolTab(const FSpawnTabArgs& Args);
	
	void HandleDuplicateActorsBegin();
	void HandleDuplicateActorsEnd();

	// Trigger event picker on TriggerOutputsFromEvent nodes. Kept so the exact
	// instance registered can be unregistered on shutdown.
	TSharedPtr<FGraphPanelPinFactory> TriggerEventPinFactory;

	FDelegateHandle DuplicateActorsBeginHandle;
	FDelegateHandle DuplicateActorsEndHandle;

	
	//TArray<TWeakObjectPtr<AActor>> PreDuplicateSelection;
	TMap<FGuid, TWeakObjectPtr<AActor>> PreDuplicateMap;
	TSet<TWeakObjectPtr<AActor>> PreDuplicateWorldActors;
	
	//Validation	
	void RegisterMenu();
};
