// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#include "SoundStriderToolsEditor.h"

#include "SST_IOToolWidget.h"
#include "SST_IOTriggerEventPin.h"
#include "EdGraphUtilities.h"

#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Framework/Application/SlateApplication.h"

//duplication
#include "SST_IO.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"
#include "SST_LOG.h"
#include "Misc/ScopeExit.h"
#include "GameplayTagContainer.h"
#include "SST_IOValidator.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"


#define LOCTEXT_NAMESPACE "FSoundStriderToolsEditorModule"

// Unique identifier for this tab across the whole editor. The layout system
// uses it to remember where you docked the tab between sessions.
static const FName SST_IOToolTabName("SST_IOTool");

FSimpleMulticastDelegate FSoundStriderToolsEditorModule::OnIODuplicationProcessed;

void FSoundStriderToolsEditorModule::StartupModule()
{
	// A "nomad" tab is one that isn't owned by a specific asset editor —
	// it can be docked anywhere in the main editor window. The Content
	// Browser and Output Log are nomad tabs.
	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(
			SST_IOToolTabName,
			FOnSpawnTab::CreateRaw(this, &FSoundStriderToolsEditorModule::SpawnIOToolTab))
		.SetDisplayName(LOCTEXT("IOToolTabTitle", "SST IO Editor"))
		.SetTooltipText(LOCTEXT("IOToolTabTooltip", "Create IO events between SST_IO Components"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"));
	
	DuplicateActorsBeginHandle = FEditorDelegates::OnDuplicateActorsBegin.AddRaw(
	this, &FSoundStriderToolsEditorModule::HandleDuplicateActorsBegin);

	DuplicateActorsEndHandle = FEditorDelegates::OnDuplicateActorsEnd.AddRaw(
		this, &FSoundStriderToolsEditorModule::HandleDuplicateActorsEnd);

	// Every Blueprint graph asks the registered factories for each pin's
	// widget; this one only answers for TriggerOutputsFromEvent's TriggerEvent.
	TriggerEventPinFactory = MakeShared<FSST_IOTriggerEventPinFactory>();
	FEdGraphUtilities::RegisterVisualPinFactory(TriggerEventPinFactory);
	
	//Validation tool message log and menu button registration
	FText LogLabel = LOCTEXT("SSTIOValidationLog", "SST IO Validation Tool Log");
	FMessageLogInitializationOptions LogOptions;
		LogOptions.bShowFilters = true;
	
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");	
	MessageLogModule.RegisterLogListing(FSST_IOValidator::GetLogListingName(), LogLabel, LogOptions);
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FSoundStriderToolsEditorModule::RegisterMenu));	
}



void FSoundStriderToolsEditorModule::ShutdownModule()
{
	if (FSlateApplication::IsInitialized())
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SST_IOToolTabName);
	}
	FEditorDelegates::OnDuplicateActorsBegin.Remove(DuplicateActorsBeginHandle);
	FEditorDelegates::OnDuplicateActorsEnd.Remove(DuplicateActorsEndHandle);

	if (TriggerEventPinFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualPinFactory(TriggerEventPinFactory);
		TriggerEventPinFactory.Reset();
	}

	if (FModuleManager::Get().IsModuleLoaded("MessageLog"))
	{
		FModuleManager::GetModuleChecked<FMessageLogModule>("MessageLog").UnregisterLogListing(FSST_IOValidator::GetLogListingName());
	}
	
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

TSharedRef<SDockTab> FSoundStriderToolsEditorModule::SpawnIOToolTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(NomadTab)
		[
			SNew(SSST_IOToolWidget)
		];

	Tab->SetTabIcon(FAppStyle::GetBrush("Icons.Settings"));

	return Tab;
}

void FSoundStriderToolsEditorModule::HandleDuplicateActorsBegin()
{
    PreDuplicateWorldActors.Reset();
	PreDuplicateMap.Reset();

    if (!GEditor) return;

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) return;

    USelection* Selection = GEditor->GetSelectedActors();
    if (!Selection) return;

    TArray<AActor*> Selected;
    Selection->GetSelectedObjects<AActor>(Selected);
    for (AActor* Actor : Selected)
    {
    	if (USST_IO* IOComponent = Actor->FindComponentByClass<USST_IO>())
    	{
    		IOComponent->IO_Guid = FGuid::NewGuid();
    		FGuid MapGuid = IOComponent->IO_Guid;
    		PreDuplicateMap.Add(MapGuid, Actor);
    	}
    }

    // Snapshot every SST_IO actor already in the world. Anything present at
    // End that isn't in here is a new copy — the selection can't be trusted
    // to have switched over to the duplicates by the time End fires.
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->FindComponentByClass<USST_IO>())
        {
            PreDuplicateWorldActors.Add(*It);
        }
    }

    UE_LOG(LOG_SST, Log, TEXT("[Dup] Begin — %d original(s), %d existing SST_IO actor(s)"),
        PreDuplicateMap.Num(), PreDuplicateWorldActors.Num());
}

namespace SST_DuplicatePrivate
{
	// One duplicated SST_IO actor and the actor it was copied from.
	struct FDuplicatePair
	{
		AActor* Source = nullptr;
		AActor* Copy = nullptr;
		USST_IO* SourceIO = nullptr;
		USST_IO* CopyIO = nullptr;
	};

	// The notification lists at most this many lines; the log has them all.
	static constexpr int32 MaxNotificationLines = 6;
}

void FSoundStriderToolsEditorModule::HandleDuplicateActorsEnd()
{
	using namespace SST_DuplicatePrivate;

	// Begin's snapshot is for this one duplication only, so it is dropped on
	// every way out of this function, early returns included.
	ON_SCOPE_EXIT
	{
		PreDuplicateMap.Reset();
		PreDuplicateWorldActors.Reset();
	};

    if (!GEditor) return;

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) return;

    // Anything with an SST_IO that wasn't in the Begin snapshot is new.
    TArray<AActor*> NewActors;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (!It->FindComponentByClass<USST_IO>()) continue;
        if (PreDuplicateWorldActors.Contains(*It)) continue;

        NewActors.Add(*It);
    }

    UE_LOG(LOG_SST, Log, TEXT("[Dup] End — %d new actor(s), %d original(s) recorded"),
        NewActors.Num(), PreDuplicateMap.Num());

	// --- 1. Pair every copy with its original ---
	// The whole batch has to be known before anything is rewired: whether a
	// link stays inside the duplicated group depends on both of its ends.
	// A copy carries its original's GUID, stamped in Begin.
	TArray<FDuplicatePair> Pairs;
	TMap<AActor*, AActor*> SourceToCopy;
	TMap<AActor*, AActor*> CopyToSource;

	for (AActor* CopyActor : NewActors)
	{
		// Every new actor was found by its SST_IO, so this only fails if the
		// component vanished in between.
		USST_IO* CopyIO = CopyActor->FindComponentByClass<USST_IO>();
		if (!CopyIO) continue;

		// Each skip below leaves that copy's links and tag untouched, so it is
		// logged with its reason rather than passing silently.
		if (!CopyIO->IO_Guid.IsValid())
		{
			UE_LOG(LOG_SST, Warning, TEXT("[Dup] %s: no duplication GUID, so it can't be matched to its original; its IO links and target name were not updated"),
				*CopyActor->GetActorNameOrLabel());
			continue;
		}

		const TWeakObjectPtr<AActor>* SourcePointer = PreDuplicateMap.Find(CopyIO->IO_Guid);
		AActor* SourceActor = SourcePointer ? SourcePointer->Get() : nullptr;
		if (!SourceActor || SourceActor == CopyActor)
		{
			UE_LOG(LOG_SST, Warning, TEXT("[Dup] %s: its duplication GUID matches no original in this duplication; its IO links and target name were not updated"),
				*CopyActor->GetActorNameOrLabel());
			continue;
		}

		USST_IO* SourceIO = SourceActor->FindComponentByClass<USST_IO>();
		if (!SourceIO)
		{
			UE_LOG(LOG_SST, Warning, TEXT("[Dup] %s: its original %s no longer has an SST_IO; its IO links and target name were not updated"),
				*CopyActor->GetActorNameOrLabel(), *SourceActor->GetActorNameOrLabel());
			continue;
		}

		Pairs.Add({ SourceActor, CopyActor, SourceIO, CopyIO });
		SourceToCopy.Add(SourceActor, CopyActor);
		CopyToSource.Add(CopyActor, SourceActor);
	}

	// The GUIDs only exist to make that pairing. Cleared on both sides for
	// the whole batch now, so none can match a later duplication by accident.
	for (const TPair<FGuid, TWeakObjectPtr<AActor>>& Entry : PreDuplicateMap)
	{
		if (AActor* SourceActor = Entry.Value.Get())
		{
			if (USST_IO* SourceIO = SourceActor->FindComponentByClass<USST_IO>())
			{
				SourceIO->IO_Guid.Invalidate();
			}
		}
	}
	for (AActor* CopyActor : NewActors)
	{
		if (USST_IO* CopyIO = CopyActor->FindComponentByClass<USST_IO>())
		{
			CopyIO->IO_Guid.Invalidate();
		}
	}

	if (Pairs.Num() == 0) return;

	const FScopedTransaction Transaction(
		LOCTEXT("UpdateDuplicatedIO", "Update IO links on duplicated actors"));

	// --- 2. Remap links inside the duplicated group ---
	// A copy's Message output that points at an original in this batch is
	// pointed at that original's copy instead, so the new group is wired to
	// itself: button' -> door', not button' -> door. Links to actors outside
	// the batch are left alone, so a lone duplicated button still opens the
	// same door. Runs regardless of bCopyInputsOnDuplication, which governs
	// outside senders only.
	int32 TotalRemapped = 0;
	for (const FDuplicatePair& Pair : Pairs)
	{
		bool bModified = false;
		for (FSST_IO_Output& Output : Pair.CopyIO->IO_Outputs)
		{
			if (Output.IOType != ESSTIOType::Message) continue;

			AActor* Target = Output.TargetActor.Get();
			AActor* const* TargetCopy = Target ? SourceToCopy.Find(Target) : nullptr;
			if (!TargetCopy) continue;

			// Snapshot before the first change, once per component.
			if (!bModified)
			{
				Pair.CopyIO->Modify();
				bModified = true;
			}

			Output.TargetActor = *TargetCopy;
			++TotalRemapped;
		}
	}

	// --- 3. Give outside senders a link to the copy ---
	// Only senders outside the batch. A copy in the batch was handled by the
	// remap above; an original whose copy is in the batch must not gain a
	// link to the new group, or the two groups are cross-wired.
	int32 TotalPropagated = 0;
	for (const FDuplicatePair& Pair : Pairs)
	{
		if (!Pair.CopyIO->bCopyInputsOnDuplication) continue;

		const TArray<FSST_IO_Input> Inputs = Pair.SourceIO->FindIncomingCommunication();

		for (const FSST_IO_Input& Input : Inputs)
		{
			USST_IO* SendingIO = Input.IOSender.Get();
			if (!SendingIO) continue;
			if (!SendingIO->IO_Outputs.IsValidIndex(Input.OutputArrayLocation)) continue;

			AActor* SendingActor = SendingIO->GetOwner();
			if (SendingActor && (SourceToCopy.Contains(SendingActor) || CopyToSource.Contains(SendingActor))) continue;

			// Copied by value up front — the Add below can reallocate.
			FSST_IO_Output NewOutput = SendingIO->IO_Outputs[Input.OutputArrayLocation];
			if (NewOutput.IOType != ESSTIOType::Message) continue;

			SendingIO->Modify();
			NewOutput.TargetActor = Pair.Copy;
			SendingIO->IO_Outputs.Add(NewOutput);
			++TotalPropagated;
		}
	}

	// --- 4. Clear the copies' instance target names ---
	// A copy keeping its original's name would answer every Broadcast meant
	// for the original. Cleared, it answers to its class tag until renamed.
	// bOverrideTargetName is kept, so the override is ready for a new name.
	// Only a name that was in use (override on) is worth warning about; a
	// dormant one is cleared silently.
	TArray<FString> WarningLines;
	int32 TotalTagsCleared = 0;

	for (const FDuplicatePair& Pair : Pairs)
	{
		const FGameplayTag ClearedTag = Pair.CopyIO->InstanceTargetName;
		if (!ClearedTag.IsValid()) continue;

		const bool bWasInUse = Pair.CopyIO->bOverrideTargetName;

		Pair.CopyIO->Modify();
		Pair.CopyIO->InstanceTargetName = FGameplayTag();
		++TotalTagsCleared;

		if (!bWasInUse) continue;

		WarningLines.Add(FString::Printf(TEXT("%s no longer answers to %s"),
			*Pair.Copy->GetActorNameOrLabel(), *ClearedTag.ToString()));

		// Broadcasts can't be remapped like Messages: they name a tag, not an
		// actor. Any copy in this batch broadcasting to the cleared name now
		// reaches only the original, which is rarely what a duplicated group
		// means, so each one is called out.
		for (const FDuplicatePair& Sender : Pairs)
		{
			const TArray<FSST_IO_Output>& Outputs = Sender.CopyIO->IO_Outputs;
			for (int32 OutputIndex = 0; OutputIndex < Outputs.Num(); ++OutputIndex)
			{
				const FSST_IO_Output& Output = Outputs[OutputIndex];
				if (Output.IOType != ESSTIOType::Broadcast) continue;

				// Exactly the runtime's rule for whether this output reaches
				// an actor tagged ClearedTag, Include Children included.
				if (!USST_IO::TagMatchesBroadcast(ClearedTag, Output)) continue;

				WarningLines.Add(FString::Printf(
					TEXT("  %s output %d broadcasts to %s, which now reaches only %s; re-point it after renaming"),
					*Sender.Copy->GetActorNameOrLabel(), OutputIndex, *Output.TargetName.ToString(),
					*Pair.Source->GetActorNameOrLabel()));
			}
		}
	}

	UE_LOG(LOG_SST, Log, TEXT("[Dup] Remapped %d link(s) inside the group, propagated %d Input(s), cleared %d instance target name(s)"),
		TotalRemapped, TotalPropagated, TotalTagsCleared);

	// Everything is rewired by now, so this is the point views can re-read.
	OnIODuplicationProcessed.Broadcast();

	if (WarningLines.Num() == 0) return;

	// The log gets every line. Designers rarely watch it while placing
	// actors, so the same message is also raised as an editor notification,
	// trimmed to stay readable.
	for (const FString& Line : WarningLines)
	{
		UE_LOG(LOG_SST, Warning, TEXT("[Dup] %s"), *Line);
	}

	TArray<FString> ShownLines;
	for (int32 i = 0; i < WarningLines.Num() && i < MaxNotificationLines; ++i)
	{
		ShownLines.Add(WarningLines[i]);
	}
	if (WarningLines.Num() > MaxNotificationLines)
	{
		ShownLines.Add(FString::Printf(TEXT("...and %d more in the Output Log"), WarningLines.Num() - MaxNotificationLines));
	}

	FNotificationInfo Info(FText::Format(
		LOCTEXT("DuplicateTagsCleared",
			"Duplicated IO actors had their instance target names cleared. They answer to their class tag until renamed.\n{0}"),
		FText::FromString(FString::Join(ShownLines, TEXT("\n")))));
	Info.ExpireDuration = 10.0f;
	Info.bFireAndForget = true;
	FSlateNotificationManager::Get().AddNotification(Info);
}

void FSoundStriderToolsEditorModule::RegisterMenu()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu(
		"LevelEditor.MainMenu.Tools");
	FToolMenuSection& ValidationTool = ToolbarMenu->FindOrAddSection("DataValidation");
	
	ValidationTool.AddMenuEntry(
	   "SST_ValidateIO",
	   LOCTEXT("ValidateIO", "Validate SST IO"),
	   LOCTEXT("ValidateIOTip", "Check every SST_IO component in the current level and list problems in the SST IO Validation log."),
	   FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"),
	   FUIAction(FExecuteAction::CreateStatic(&FSST_IOValidator::RunOnCurrentLevel)));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSoundStriderToolsEditorModule, SoundStriderToolsEditor)