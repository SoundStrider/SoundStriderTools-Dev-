// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "UObject/GCObject.h"
#include "Widgets/SCompoundWidget.h"
#include "SST_IOToolTypes.h"
#include "Widgets/Views/SListView.h"

class USST_IO;
class SBox;

// FGCObject: each item's parameter stash can hold object references outside
// any UPROPERTY, so the widget reports them to the garbage collector itself.
class SSST_IOToolWidget : public SCompoundWidget, public FSelfRegisteringEditorUndoClient, public FGCObject
{
public:
	//slate arguments 
	SLATE_BEGIN_ARGS(SSST_IOToolWidget) {}
	SLATE_END_ARGS()

	//slate constructor
	void Construct(const FArguments& InArgs);
	virtual ~SSST_IOToolWidget();
	
	//debug drawing
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	bool bDebugDrawOutputs = false;
	bool bDebugDrawInputs = false;

	// FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;

	
private:
	void HandleEditorSelectionChanged(UObject* NewSelection);

	// Bound to the header text, and called every frame by the pull model.
	FText GetSelectedActorText() const;

	// Drives whether the "nothing selected" message or the real UI is shown.
	EVisibility GetOutputsVisibility() const;
	EVisibility GetEmptyStateVisibility() const;
	
	//ref to current selected IO Component
	TWeakObjectPtr<USST_IO> SelectedIO;
	
	//Output row helpers
	TSharedRef<ITableRow> OnGenerateOutputRow(FSST_IOOutputItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
	void RebuildItems();
	FReply OnAddOutputClicked();
	void HandleRequestDelete(FSST_IOOutputItemPtr Item);
	void HandleRequestDuplicate(FSST_IOOutputItemPtr Item);

	//Output items for refs in the input rows
	TArray<FSST_IOOutputItemPtr> Items;

	// The actor Items were last built for. RebuildItems carries per-row view
	// state (parameter expansion) across a rebuild only for the same actor.
	// The actor rather than the component, because undo on a Blueprint actor
	// can recreate its components while the actor itself survives.
	TWeakObjectPtr<AActor> ItemsOwner;
	TSharedPtr<SListView<FSST_IOOutputItemPtr>> OutputListView;
	
	//More Input refs
	TArray<FSST_IO_Input> Inputs;
	TSharedPtr<SVerticalBox> InputContainer;
	bool bInputContainerExpanded = false;
	const FSlateBrush* GetInputExpanderBrush()const;
	
	//Input helper functions
	const FSST_IO_Output* GetIncomingOutputAt(int32 OutputIndex)const;
	FReply OnToggleInputClicked();
	FText GetInputCountText() const;
	void RebuildInputs();
	TSharedRef<SWidget> MakeInputRow(int32 InputIndex);
	EVisibility GetInputContainerVisibility() const;
	FReply OnInputObjectClicked(int32 InputIndex);
	FText InputActorText(int32 InputIndex);
	FText TargetFunctionTooltip(int32 InputIndex)const;
	FText TriggerFunctionTooltip(int32 InputIndex)const;
	
	// --- Target name and per-instance override ---
	// The override's tag picker is a single-property view onto
	// InstanceTargetName, so it's the engine's own picker (tag creation
	// included) without depending on the GameplayTagsEditor plugin.
	TSharedPtr<SBox> OverrideViewBox;
	FText GetEffectiveTargetNameText() const;
	ECheckBoxState IsOverrideChecked() const;
	void OnOverrideChanged(ECheckBoxState NewState);
	EVisibility GetOverrideViewVisibility() const;
	void RebuildOverrideView();
	
	// State the header was last built for. Tick compares against these so a
	// change from anywhere -- the checkbox, the picker, undo, the Details
	// panel -- refreshes the override view and the incoming links.
	bool bOverrideViewBuiltFor = false;
	FGameplayTag LastEffectiveTargetName;
	
	//debug draw helper function
	ECheckBoxState IsDuplicateInputsChecked()const;
	void OnToggleDuplicateInputsChecked(ECheckBoxState NewState);
	ECheckBoxState IsDrawInputsChecked()const;
	void OnToggleDrawInputsChecked(ECheckBoxState NewState);
	ECheckBoxState IsDrawOutputsChecked()const;
	void OnToggleDrawOutputsChecked(ECheckBoxState NewState);
	
	//undo redo
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	// Refreshes the selected actor for construction scripts and undo/redo
	void RefreshSelectedActor();
};
