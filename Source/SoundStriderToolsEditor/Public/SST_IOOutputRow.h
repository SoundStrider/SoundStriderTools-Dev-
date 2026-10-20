// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "ScopedTransaction.h"
#include "Widgets/Views/STableRow.h"

#include "SST_IOToolTypes.h"
#include "Styling/SlateTypes.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SComboBox.h"

class IPropertyRowGenerator;
class IDetailTreeNode;

DECLARE_DELEGATE_OneParam(FOnOutputRowChanged, FSST_IOOutputItemPtr);

class SSST_IOOutputRow : public STableRow<FSST_IOOutputItemPtr>
{
public:
    SLATE_BEGIN_ARGS(SSST_IOOutputRow) {}
        // The row's data. ARGUMENT because it's read once at construction —
        // the item never changes for a given row widget.
        SLATE_ARGUMENT(FSST_IOOutputItemPtr, Item)

        // Fired when this row deletes itself, so the owning tool can rebuild
        // the list. EVENT because it flows outward, not inward.
        SLATE_EVENT(FOnOutputRowChanged, OnRequestDelete)

        SLATE_EVENT(FOnOutputRowChanged, OnRequestDuplicate)
    SLATE_END_ARGS()

    // STableRow's Construct needs the owning table as well as the args —
    // that's how the row attaches itself to the list's selection handling.
    void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable);

    // Re-reads the cached target state and parameter rows from the data
    // without recreating the row. Called by the tool widget after an undo or
    // redo, or when the same actor is reselected.
    void RefreshFromData();

private:
    static UWorld* GetEditorWorld();
    
    // --- Combo box plumbing ---
    // SComboBox<T> needs its options as a TArray<TSharedPtr<T>> that stays
    // alive for the widget's lifetime, so these are members, not locals.
    TArray<TSharedPtr<FName>> TriggerEventOptions;
    TArray<TSharedPtr<FName>> TargetEventOptions;
    TSharedPtr<SComboBox<TSharedPtr<FName>>> TargetEventCombo;
    TSharedPtr<SComboBox<TSharedPtr<FGameplayTag>>> TargetNameCombo;

    void RebuildTriggerEventOptions();
    void RebuildTargetEventOptions();
    


    FText GetTriggerEventText() const;
    void OnTriggerEventSelected(TSharedPtr<FName> NewValue, ESelectInfo::Type SelectInfo);

    FText GetTargetEventText() const;
    FSlateColor TargetStatusColor() const;
    void OnTargetEventSelected(TSharedPtr<FName> NewValue, ESelectInfo::Type SelectInfo);

    // --- Numeric fields ---
    void OnDelayCommitted(float NewValue, ETextCommit::Type CommitType);
    float GetDelayValue() const;
    void OnDelayChanged(float NewValue);
    
    void OnFireCountCommitted(int32 NewValue, ETextCommit::Type CommitType);
    int32 GetFireCountValue() const;
    void OnFireCountChanged(int32 NewValue);

    void BeginSliderTransaction(const FText& TransactionText);
    void EndSliderTransaction();
    TUniquePtr<FScopedTransaction> SliderTransaction;
    

    FReply OnDeleteClicked();
    FReply OnDuplicateClicked();

    FSST_IOOutputItemPtr Item;
    FOnOutputRowChanged OnRequestDelete;
    FOnOutputRowChanged OnRequestDuplicate;
    TSharedPtr<SButton> DeleteButton;
    TSharedPtr<SButton> DuplicateButton;


    // Drives the SWidgetSwitcher: 0 = Message (actor picker), 1 = Broadcast (name combo)
    int32 GetTargetWidgetIndex() const;

    // SObjectPropertyEntryBox works in object *paths*, not pointers, so it can
    // display an asset that isn't loaded yet.
    FString GetTargetActorPath() const;
    void OnTargetActorChanged(const FAssetData& AssetData);

    // Every registered tag under SST_IO, drawn from the tag manager rather
    // than from placed actors, so targets that aren't placed or loaded yet
    // are still selectable. Tags are created elsewhere, never from this list.
    TArray<TSharedPtr<FGameplayTag>> TargetNameOptions;
    void RebuildTargetNameOptions(); 
    void OnTargetNameComboOpening();
    void OnTargetEventComboOpening();

    TSharedRef<SWidget> MakeNameOptionWidget(TSharedPtr<FGameplayTag> InOption);
    FText GetTargetNameText() const;
    void OnTargetNameSelected(TSharedPtr<FGameplayTag> NewValue, ESelectInfo::Type SelectInfo);

    // Opt-in hierarchical Broadcast. Changing it changes which actors are
    // targeted, so the target event options are rebuilt.
    ECheckBoxState IsIncludeChildrenChecked() const;
    void OnIncludeChildrenChanged(ECheckBoxState NewState);
    
    
    TArray<TSharedPtr<ESSTIOType>> IOTypeOptions;
    TSharedRef<SWidget> MakeIOTypeOptionWidget(TSharedPtr<ESSTIOType> InOption);
    FText GetIOTypeText() const;
    void OnIOTypeSelected(TSharedPtr<ESSTIOType> NewValue, ESelectInfo::Type SelectInfo);
    static FText IOTypeToText(ESSTIOType InType);
    
    // The event the selected target event runs, read from the first target
    // that declares it. Null when it can't be read: no target event, nothing
    // loaded, or the declared event doesn't exist or can't be used.
    const UFunction* GetActiveTargetEventFunction() const;
    
    // --- Parameters ---
    void SyncParametersToSpec();
    TSharedPtr<SVerticalBox> ParameterContainer;
    // Expanded state is Item->bParametersExpanded, not a member, so it
    // survives the row being destroyed and regenerated.
    bool IsParametersExpanded() const;

    // The parameter rows are the engine's own property rows for this output's
    // bag, the same widgets the Details panel draws, so every type the bag can
    // hold gets its proper editor, with undo, for free. The generator is only
    // created while the parameters are expanded.
    TSharedPtr<IPropertyRowGenerator> ParameterRowGenerator;

    // Recreates the generator, then fills the container from it.
    void RebuildParameterRows();
    // Fills the container from the existing generator. Also runs when the
    // generator refreshes itself, which invalidates the nodes it handed out.
    void PopulateParameterRows();
    // The Parameters node of this row's output in the generator's tree.
    TSharedPtr<IDetailTreeNode> FindParametersNode() const;

    // Adds Node's row, then, if it is expanded, its children one level deeper:
    // a struct's members, a container's elements. Recursive, depth-capped.
    void AddParameterRow(const TSharedRef<IDetailTreeNode>& Node, int32 Depth, const FString& ParentKey, const FString& ParametersPath);
    TSharedRef<SWidget> MakeParameterRow(const TSharedRef<IDetailTreeNode>& Node, int32 Depth, bool bHasChildren, const FString& Key);

    // Redraws the parameter rows on the next frame rather than immediately.
    // The requests come from inside widget handlers (an expander arrow, an
    // array's add button), and redrawing there would destroy the widget
    // whose handler is still running. Repeated requests collapse into one.
    void RequestPopulateParameterRows();
    EActiveTimerReturnType HandlePopulateTimer(double InCurrentTime, float InDeltaTime);
    bool bPopulatePending = false;

    FReply OnToggleParametersClicked();
    EVisibility GetParameterContainerVisibility() const;
    const FSlateBrush* GetExpanderBrush() const;
    FText GetParameterCountText() const;
    
    bool bParameterOutOfDate = false;
    FReply OnSyncParametersClicked();

    FText GetTriggerEventTooltip() const;
    FText TriggerDescriptionFor(FName EventName) const;
    TSharedRef<SWidget> MakeTriggerOptionWidget(TSharedPtr<FName> InOption);
    
    const FSST_IO_TargetEventSpec* FindTargetEventSpecByName(FName EventName) const;
    FText GetTargetEventTooltip() const;
    FText TargetDescriptionFor(FName EventName) const;
    TSharedRef<SWidget> MakeTargetOptionWidget(TSharedPtr<FName> InOption);
    
    //True for when a target's declaration can be actively read
    //False specifically means "a target is assigned but not loaded" 
    bool CanVerifyTarget() const;

    // How many of the loaded targets declare EventName, when an output reaches
    // more than one. Empty for a single target, where it would say nothing.
    FText CoverageTextFor(FName EventName) const;
    
    //what were the results last time this was built?
    ESST_IO_Results LastTargetResolution = ESST_IO_Results::Unassigned;
    bool bTargetEventMissing = false;
    void ApplyTargetChange();
};
