// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.


#include "SST_IOOutputRow.h"

#include "ScopedTransaction.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"
#include "GameFramework/Actor.h"
#include "PropertyCustomizationHelpers.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "AssetRegistry/AssetData.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Algo/Sort.h"
#include "GameplayTagsManager.h"
#include "SST_GameplayTags.h"
#include "SST_LOG.h"
#include "SST_IOParameterUtils.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IPropertyRowGenerator.h"
#include "IDetailTreeNode.h"
#include "PropertyHandle.h"
#include "Widgets/Layout/SBox.h"
#include "StructUtils/PropertyBag.h"

#define LOCTEXT_NAMESPACE "SSST_IOOutputRow"

void SSST_IOOutputRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
{
	Item = InArgs._Item;
	OnRequestDelete = InArgs._OnRequestDelete;
	OnRequestDuplicate = InArgs._OnRequestDuplicate;

	// A small helper so the connective words share one style. Lambdas returning
	// TSharedRef<SWidget> are the normal way to factor Slate layout — there is
	// no template or prefab system, you just write functions.
	auto MakeLabel = [](const FText& InText)
	{
		return SNew(STextBlock)
			.Text(InText)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground());
	};

	IOTypeOptions.Reset();
	IOTypeOptions.Add(MakeShared<ESSTIOType>(ESSTIOType::Message));
	IOTypeOptions.Add(MakeShared<ESSTIOType>(ESSTIOType::Broadcast));

	RebuildTriggerEventOptions();
	RebuildTargetNameOptions();

	// STableRow::Construct takes its own FArguments, which is where the row's
	// content goes. This is the one place the two-stage Construct pattern shows.
	STableRow<FSST_IOOutputItemPtr>::Construct(
		STableRow<FSST_IOOutputItemPtr>::FArguments()
		.Padding(2.0f)
		.Content()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)

				// --- Message / Broadcast ---
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
				[
					SNew(SBox).WidthOverride(100.0f)
					[
						SNew(SComboBox<TSharedPtr<ESSTIOType>>)
						.OptionsSource(&IOTypeOptions)
						.OnGenerateWidget(this, &SSST_IOOutputRow::MakeIOTypeOptionWidget)
						.OnSelectionChanged(this, &SSST_IOOutputRow::OnIOTypeSelected)
						[
							SNew(STextBlock).Text(this, &SSST_IOOutputRow::GetIOTypeText)
						]
					]
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
				[
					MakeLabel(LOCTEXT("On", "On"))
				]

				// --- TriggerEvent: what this actor emits ---
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(120.0f)
					[
						SNew(SComboBox<TSharedPtr<FName>>)
						.OptionsSource(&TriggerEventOptions)
						.OnGenerateWidget(this, &SSST_IOOutputRow::MakeTriggerOptionWidget)
						.OnSelectionChanged(this, &SSST_IOOutputRow::OnTriggerEventSelected)
						[
							// The combo's *closed* appearance is its content slot,
							// separate from the dropdown rows above.
							SNew(STextBlock)
							.Text(this, &SSST_IOOutputRow::GetTriggerEventText)
							.ToolTipText(this, &SSST_IOOutputRow::GetTriggerEventTooltip)
						]
					]
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
				[
					MakeLabel(LOCTEXT("Target", "target"))
				]

				// --- Target: actor picker or name combo, depending on IOType ---
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SWidgetSwitcher)
					// Bound, so it follows IOType automatically — no rebuild needed.
					.WidgetIndex(this, &SSST_IOOutputRow::GetTargetWidgetIndex)

					+ SWidgetSwitcher::Slot() // index 0 — Message - this is the actor selector
					[
						SNew(SBox).WidthOverride(220.0f)
						[
							SNew(SObjectPropertyEntryBox)
							.AllowedClass(AActor::StaticClass())
							.OnShouldFilterActor(FOnShouldFilterActor::CreateLambda([](const AActor* const Actor)
							{
								return Actor && Actor->FindComponentByClass<USST_IO>() != nullptr;
							}))
							.ObjectPath(this, &SSST_IOOutputRow::GetTargetActorPath)
							.OnObjectChanged(this, &SSST_IOOutputRow::OnTargetActorChanged)
							.AllowClear(true)
							.DisplayBrowse(false)
							.DisplayUseSelected(true)
							.DisplayThumbnail(false)
						]
					]

					+ SWidgetSwitcher::Slot() // index 1 — Broadcast: tag selector plus the children opt-in
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox).WidthOverride(220.0f)
							[
								SAssignNew(TargetNameCombo, SComboBox<TSharedPtr<FGameplayTag>>)
								.OptionsSource(&TargetNameOptions)
								.OnGenerateWidget(this, &SSST_IOOutputRow::MakeNameOptionWidget)
								.OnSelectionChanged(this, &SSST_IOOutputRow::OnTargetNameSelected)
								// Tags can be created while the tool is open, e.g. from the
								// header's instance override, so the list refreshes on open.
								.OnComboBoxOpening(this, &SSST_IOOutputRow::OnTargetNameComboOpening)
								[
									SNew(STextBlock).Text(this, &SSST_IOOutputRow::GetTargetNameText)
								]
							]
						]

						// Lives inside the Broadcast slot, so it's only visible in
						// the one mode where it means anything.
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
						[
							SNew(SCheckBox)
							.IsChecked(this, &SSST_IOOutputRow::IsIncludeChildrenChecked)
							.OnCheckStateChanged(this, &SSST_IOOutputRow::OnIncludeChildrenChanged)
							.ToolTipText(LOCTEXT("IncludeChildrenTip",
								"Also target actors whose tag descends from this one, so SST_IO.Door also reaches SST_IO.Door.Wood."))
							[
								SNew(STextBlock).Text(LOCTEXT("IncludeChildren", "include children"))
							]
						]
					]
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
				[
					MakeLabel(LOCTEXT("Using", "using function"))
				]

				// --- TargetEvent: a function on the target. Stored in a member so
				//     RefreshOptions() can be called when the target changes. ---
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(120.0f)
					[
						SAssignNew(TargetEventCombo, SComboBox<TSharedPtr<FName>>)
						.OptionsSource(&TargetEventOptions)
						.OnGenerateWidget(this, &SSST_IOOutputRow::MakeTargetOptionWidget)
						.OnComboBoxOpening(this, &SSST_IOOutputRow::OnTargetEventComboOpening)
						.OnSelectionChanged(this, &SSST_IOOutputRow::OnTargetEventSelected)
						[
							SNew(STextBlock)
							.Text(this, &SSST_IOOutputRow::GetTargetEventText)
							.ColorAndOpacity(this, &SSST_IOOutputRow::TargetStatusColor)
							.ToolTipText(this, &SSST_IOOutputRow::GetTargetEventTooltip)
						]
					]
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
				[
					MakeLabel(LOCTEXT("AfterDelay", "after delay"))
				]

				//where delay is set
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(50.0f)
					[
						SNew(SSpinBox<float>)
						.OnBeginSliderMovement_Lambda([this]()
						{
							BeginSliderTransaction(LOCTEXT("DragDelayTime", "Set IO Delay Time"));
						})
						.Value(this, &SSST_IOOutputRow::GetDelayValue)
						.OnValueChanged(this, &SSST_IOOutputRow::OnDelayChanged)
						.OnValueCommitted(this, &SSST_IOOutputRow::OnDelayCommitted)
						.OnEndSliderMovement_Lambda([this](float)
						{
							EndSliderTransaction();
						})
						.MinValue(0.0f)
						.MinSliderValue(0.0f)
						.MaxValue(99.9f)
						.Delta(0.1f)
					]
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
				[
					MakeLabel(LOCTEXT("Max", "Max"))
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(40.0f)
					[
						SNew(SSpinBox<int32>)
						.OnBeginSliderMovement_Lambda([this]()
						{
							BeginSliderTransaction(LOCTEXT("DragFireCount", "Set IO Fire Count"));
						})
						.Value(this, &SSST_IOOutputRow::GetFireCountValue)
						.OnValueChanged(this, &SSST_IOOutputRow::OnFireCountChanged)
						.OnValueCommitted(this, &SSST_IOOutputRow::OnFireCountCommitted)
						.OnEndSliderMovement_Lambda([this](int32)
						{
							EndSliderTransaction();
						})
						.MinValue(0)
						.MinSliderValue(0)
						.MaxSliderValue(99)
						.Delta(1)
					]
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.OnClicked(this, &SSST_IOOutputRow::OnToggleParametersClicked)
					.ToolTipText(LOCTEXT("ToggleParams", "Show or hide this event's parameters"))
					.Visibility_Lambda([this]()
					{
						const FSST_IO_Output* O = Item.IsValid() ? Item->Get() : nullptr;
						return ((O && O->Parameters.GetNumPropertiesInBag() > 0) || bParameterOutOfDate) ? EVisibility::Visible : EVisibility::Collapsed;
					})
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SImage)
							.Image(this, &SSST_IOOutputRow::GetExpanderBrush)
							.ColorAndOpacity(FSlateColor::UseForeground())
						]

						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 0, 0)
						[
							SNew(STextBlock)
							.Text(this, &SSST_IOOutputRow::GetParameterCountText)
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
					]
				]
				//Sync button for parameters that are out of spec
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.OnClicked(this, &SSST_IOOutputRow::OnSyncParametersClicked)
					.ToolTipText(LOCTEXT("SyncParams", "This event's parameters have changed on the target. Click to update; values whose key and type still match are kept"))
					.Visibility_Lambda([this]()
					{
						return bParameterOutOfDate ? EVisibility::Visible : EVisibility::Collapsed;
					})
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.Refresh"))
						.ColorAndOpacity(FStyleColors::Warning)
					]
				]
				

				// FillWidth on an empty slot pushes everything after it to the
				// right — the standard way to right-align trailing controls.
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNullWidget::NullWidget
				]

				//duplicate
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(30, 0, 0, 0)
				[
					SAssignNew(DuplicateButton, SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.OnClicked(this, &SSST_IOOutputRow::OnDuplicateClicked)
					.ToolTipText(LOCTEXT("DuplicateOutput", "Duplicate this output as a new entry"))
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.Plus"))
						.ColorAndOpacity_Lambda([this]()
						{
							return (DuplicateButton.IsValid() && DuplicateButton->IsHovered())
								       ? FSlateColor(FLinearColor::Yellow)
								       : FSlateColor::UseForeground();
						})
					]
				]

				//delete
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SAssignNew(DeleteButton, SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.OnClicked(this, &SSST_IOOutputRow::OnDeleteClicked)
					.ToolTipText(LOCTEXT("DeleteOutput", "Remove this output"))
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.Delete"))
						.ColorAndOpacity_Lambda([this]()
						{
							return (DeleteButton.IsValid() && DeleteButton->IsHovered())
								       ? FSlateColor(FLinearColor::Red)
								       : FSlateColor::UseForeground();
						})
					]
				]
			] // closes the SVerticalBox's first AutoHeight slot

			// --- Parameter form. Indented so the child relationship reads. ---
			+ SVerticalBox::Slot().AutoHeight().Padding(24, 2, 0, 0)
			[
				SAssignNew(ParameterContainer, SVerticalBox)
				.Visibility(this, &SSST_IOOutputRow::GetParameterContainerVisibility)
			]
		], // closes .Content()[
		OwnerTable);

	RebuildParameterRows();
	RebuildTargetEventOptions();
}

void SSST_IOOutputRow::RefreshFromData()
{
	// View-only: re-resolves the target, recomputes the missing-event flag,
	// refreshes the target event options and rebuilds the parameter rows.
	// Never writes to the output, so it is safe to run after any undo.
	RebuildTargetEventOptions();
	RebuildParameterRows();
}


/*
 *
 *	Trigger Section
 *
 */

void SSST_IOOutputRow::RebuildTriggerEventOptions()
{
	TriggerEventOptions.Reset();

	if (!Item.IsValid() || !Item->OwningIO.IsValid()) return;

	// Depends only on the owning actor, so this never needs re-running.
	for (const FSST_IO_TriggerEventSpec& Spec : Item->OwningIO->IO_TriggerEvents)
	{
		TriggerEventOptions.Add(MakeShared<FName>(Spec.TriggerEventName));
	}
}

FText SSST_IOOutputRow::GetTriggerEventText() const
{
	const FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr;
	return (Output && !Output->TriggerEvent.IsNone()) ? FText::FromName(Output->TriggerEvent) : FText::GetEmpty();
}

FText SSST_IOOutputRow::GetTriggerEventTooltip() const
{
	const FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr;
	return Output ? TriggerDescriptionFor(Output->TriggerEvent) : FText::GetEmpty();
}

// Trigger events are declared by the owning actor — this component emits them,
// so the spec always lives here rather than on whoever receives the output.
FText SSST_IOOutputRow::TriggerDescriptionFor(FName EventName) const
{
	if (!Item.IsValid() || !Item->OwningIO.IsValid() || EventName.IsNone())
	{
		return FText::GetEmpty();
	}

	const FSST_IO_TriggerEventSpec* Spec = Item->OwningIO->FindTriggerEventSpec(EventName);
	return Spec ? FText::FromString(Spec->TriggerDescription) : FText::GetEmpty();
}

TSharedRef<SWidget> SSST_IOOutputRow::MakeTriggerOptionWidget(TSharedPtr<FName> InOption)
{
	const FName Name = InOption.IsValid() ? *InOption : NAME_None;

	// Resolved once at generation rather than bound — dropdown entries are
	// rebuilt each time the list opens, so there's nothing to keep in sync.
	return SNew(STextBlock)
		.Text(FText::FromName(Name))
		.ToolTipText(TriggerDescriptionFor(Name));
}

void SSST_IOOutputRow::OnTriggerEventSelected(TSharedPtr<FName> NewValue, ESelectInfo::Type SelectInfo)
{
	if (!NewValue.IsValid() || !Item.IsValid() || !Item->OwningIO.IsValid()) return;

	// FScopedTransaction is what gives you Ctrl+Z. It opens an undo entry;
	// Modify() tells the object to snapshot itself into that entry before
	// you change it. Skip either and the edit is silently un-undoable.
	const FScopedTransaction Transaction(LOCTEXT("SetTriggerEvent", "Set IO Trigger Event"));
	Item->OwningIO->Modify();

	if (FSST_IO_Output* Output = Item->Get())
	{
		Output->TriggerEvent = *NewValue;
	}
}


/*
 *
 *	Delay and Times Fired
 *
 */

float SSST_IOOutputRow::GetDelayValue() const
{
	const FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr;
	return Output ? Output->Delay : 0.f;
}

void SSST_IOOutputRow::OnDelayChanged(float NewValue)
{
	if (!SliderTransaction.IsValid()) return;

	if (FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr)
	{
		Output->Delay = NewValue;
	}
}

void SSST_IOOutputRow::OnDelayCommitted(float NewValue, ETextCommit::Type CommitType)
{
	if (!Item.IsValid() || !Item->OwningIO.IsValid()) return;


	if (FSST_IO_Output* Output = Item->Get())
	{
		if (Output->Delay == NewValue) return;
		
		const FScopedTransaction Transaction(LOCTEXT("SetDelay", "Set IO Delay"));
		Item->OwningIO->Modify();
		Output->Delay = NewValue;
	}
}

int32 SSST_IOOutputRow::GetFireCountValue() const
{
	const FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr;
	return Output ? Output->FireOnlyNTimes : 0;
}

void SSST_IOOutputRow::OnFireCountChanged(int32 NewValue)
{
	if (!SliderTransaction.IsValid()) return;

	if (FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr)
	{
		Output->FireOnlyNTimes = NewValue;
	}
}

void SSST_IOOutputRow::BeginSliderTransaction(const FText& TransactionText)
{
	if (!Item.IsValid() || !Item->OwningIO.IsValid()) return;
	
	SliderTransaction = MakeUnique<FScopedTransaction>(TransactionText);
	Item->OwningIO->Modify();
}

void SSST_IOOutputRow::EndSliderTransaction()
{
	SliderTransaction.Reset();
}

void SSST_IOOutputRow::OnFireCountCommitted(int32 NewValue, ETextCommit::Type CommitType)
{
	if (!Item.IsValid() || !Item->OwningIO.IsValid()) return;

	

	if (FSST_IO_Output* Output = Item->Get())
	{
		if (Output->FireOnlyNTimes == NewValue) return;
		
		const FScopedTransaction Transaction(LOCTEXT("SetFireCount", "Set IO Fire Count"));
		Item->OwningIO->Modify();
		Output->FireOnlyNTimes = NewValue;
	}
}


/*
 *
 *	Duplicate - Delete buttons
 *
 */

FReply SSST_IOOutputRow::OnDeleteClicked()
{
	// FReply tells Slate whether the event was consumed. Handled() stops it
	// propagating further up the widget tree; Unhandled() lets it continue.
	OnRequestDelete.ExecuteIfBound(Item);
	return FReply::Handled();
}

FReply SSST_IOOutputRow::OnDuplicateClicked()
{
	OnRequestDuplicate.ExecuteIfBound(Item);
	return FReply::Handled();
}


/*
 *
 *	Target Section
 *
 */

void SSST_IOOutputRow::RebuildTargetEventOptions()
{
	TArray<TSharedPtr<FName>> OldOptions = MoveTemp(TargetEventOptions);
	
	bTargetEventMissing = false;
	bParameterOutOfDate = false;
	LastTargetResolution = ESST_IO_Results::Unassigned;
	
	FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr;

	if (Output)
	{
		//Resolve stores the resolution to reference back to when building the target list options
		TArray<USST_IO*> Targets;
		LastTargetResolution = USST_IO::ResolveOutputTargets(*Output, GetEditorWorld(), Targets);

		// Message and Broadcast no longer need separate paths: the resolver
		// returns an array of components for both, so the union works either
		// way. For Message it's a set of one.
		TSet<FName> Unique;
		for (USST_IO* IO : Targets)
		{
			for (const FSST_IO_TargetEventSpec& Spec : IO->IO_TargetEvents)
			{
				Unique.Add(Spec.TargetEventName);
			}
		}

		for (const FName& Name : Unique)
		{
			if (Name.IsNone()) continue;

			const TSharedPtr<FName>* Found = OldOptions.FindByPredicate(
		[&Name](const TSharedPtr<FName>& Old) { return Old.IsValid() && *Old == Name; });

			TargetEventOptions.Add(Found ? *Found : MakeShared<FName>(Name));
		}

		// TSet iteration order isn't meaningful, so the list is sorted to stay
		// put between rebuilds.
		Algo::Sort(TargetEventOptions, [](const TSharedPtr<FName>& A, const TSharedPtr<FName>& B)
		{
			return FNameLexicalLess()(*A, *B);
		});
		

		// Records when a selection is missing
		const bool bVerifiable = LastTargetResolution != ESST_IO_Results::NotLoaded && LastTargetResolution != ESST_IO_Results::NoMatch;

		if (bVerifiable && !Output->TargetEvent.IsNone())
		{
			const bool bStillValid = TargetEventOptions.ContainsByPredicate(
				[Output](const TSharedPtr<FName>& Opt)
				{
					return Opt.IsValid() && *Opt == Output->TargetEvent;
				});

			if (!bStillValid)
			{
				bTargetEventMissing = true;
			}
			
		}
		// Out of date: the parameters this output holds aren't laid out the way
		// the event's inputs are now, e.g. an input was added in the graph.
		if (bVerifiable && !bTargetEventMissing && !Output->TargetEvent.IsNone())
		{
			const FInstancedPropertyBag Expected = FSST_IOParameterUtils::BuildParametersForEvent(
				GetActiveTargetEventFunction(), Output->Parameters);
			bParameterOutOfDate = FSST_IOParameterUtils::ParameterLayoutDiffers(Expected, Output->Parameters);
		}
	}

	if (TargetEventCombo.IsValid())
	{
		TargetEventCombo->RefreshOptions();
	}
}

void SSST_IOOutputRow::ApplyTargetChange()
{
	RebuildTargetEventOptions();
	RebuildParameterRows();
	
	if (bTargetEventMissing)
	{
		if (FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr)
		{
			Output->TargetEvent = NAME_None;
			bTargetEventMissing = false;
			SyncParametersToSpec();
		}
	}
}

// Resolves the spec for an arbitrary target event name through whichever
// targeting mode this output uses.
const FSST_IO_TargetEventSpec* SSST_IOOutputRow::FindTargetEventSpecByName(FName EventName) const
{
	const FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr;
	if (!Output || EventName.IsNone()) return nullptr;

	// The result value isn't needed: only Resolved fills Targets, so any
	// other outcome leaves the loop empty and falls through to nullptr.
	TArray<USST_IO*> Targets;
	USST_IO::ResolveOutputTargets(*Output, GetEditorWorld(), Targets);

	for (USST_IO* IO : Targets)
	{
		if (const FSST_IO_TargetEventSpec* Spec = IO->FindTargetEventSpec(EventName))
		{
			return Spec;
		}
	}

	return nullptr;
}

const UFunction* SSST_IOOutputRow::GetActiveTargetEventFunction() const
{
	const FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr;
	if (!Output || Output->TargetEvent.IsNone()) return nullptr;

	TArray<USST_IO*> Targets;
	USST_IO::ResolveOutputTargets(*Output, GetEditorWorld(), Targets);

	// First usable event wins, and the rest are compared against it in the
	// same pass. A Message target resolves to one component, so the
	// comparison only ever runs for a Broadcast reaching several classes.
	const UFunction* Function = nullptr;

	for (USST_IO* IO : Targets)
	{
		if (!IO->FindTargetEventSpec(Output->TargetEvent)) continue;

		const UFunction* Candidate = IO->FindTargetEventFunction(Output->TargetEvent);
		if (!Candidate || !USST_IO::HasSupportedSignature(Candidate)) continue;

		if (!Function)
		{
			Function = Candidate;
			continue;
		}

		if (FSST_IOParameterUtils::EventInputsDiffer(Function, Candidate))
		{
			// Keyed by event and tag: retargeting the output to another
			// mismatch is new information and warns again.
			const FName WarningKey(*FString::Printf(TEXT("%s|%s"),
				*Output->TargetEvent.ToString(), *Output->TargetName.ToString()));

			bool bAlreadyWarned = false;
			Item->WarnedDifferingInputs.Add(WarningKey, &bAlreadyWarned);

			if (!bAlreadyWarned)
			{
				UE_LOG(LOG_SST, Warning,
					TEXT("'%s' takes different inputs across actors named '%s'; parameters follow '%s'"),
					*Output->TargetEvent.ToString(), *Output->TargetName.ToString(),
					*GetNameSafe(Function->GetOwnerClass()));
			}
			break;
		}
	}

	return Function;
}

FText SSST_IOOutputRow::GetTargetEventTooltip() const
{
	const FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr;
	if (!Output) return FText::GetEmpty();

	// Without this the tooltip is blank for an unloaded target, which reads
	// as "no description" rather than "cannot be read right now."
	if (LastTargetResolution == ESST_IO_Results::NotLoaded)
	{
		return LOCTEXT("TargetNotLoaded",
			"This target is not loaded, so its event list and descriptions are unavailable.");
	}
	
	if (LastTargetResolution == ESST_IO_Results::NoMatch)
	{
		return LOCTEXT("TargetNoMatch",
			"No loaded actors match this tag, so the events cannot be listed.");
	}

	if (bTargetEventMissing)
	{
		return LOCTEXT("TargetMissing",
			"This event is no longer available.");
	}
	const FText Description = TargetDescriptionFor(Output->TargetEvent);
	const FText Coverage = CoverageTextFor(Output->TargetEvent);

	if (Coverage.IsEmpty()) return Description;
	if (Description.IsEmpty()) return Coverage;
	return FText::Format(LOCTEXT("DescriptionWithCoverage", "{0}\n\n{1}"), Description, Coverage);
}

// Target events are declared by the receiver, so the spec is resolved through
// TargetActor or TargetName rather than from the owning component.
FText SSST_IOOutputRow::TargetDescriptionFor(FName EventName) const
{
	const FSST_IO_TargetEventSpec* Spec = FindTargetEventSpecByName(EventName);
	return Spec ? FText::FromString(Spec->TargetDescription) : FText::GetEmpty();
}

TSharedRef<SWidget> SSST_IOOutputRow::MakeTargetOptionWidget(TSharedPtr<FName> InOption)
{
	const FName Name = InOption.IsValid() ? *InOption : NAME_None;

	FText Tip;
	
		const FText Description = TargetDescriptionFor(Name);
		const FText Coverage = CoverageTextFor(Name);

		Tip = Coverage.IsEmpty() ? Description
			: Description.IsEmpty() ? Coverage
			: FText::Format(LOCTEXT("OptionDescriptionWithCoverage", "{0}\n\n{1}"), Description, Coverage);
	

	return SNew(STextBlock)
		.Text(FText::FromName(Name))
		.ToolTipText(Tip);
}

void SSST_IOOutputRow::RebuildTargetNameOptions()
{
	TArray<TSharedPtr<FGameplayTag>> OldNames = MoveTemp(TargetNameOptions);

	// From the tag manager rather than a level sweep: every registered tag
	// under the root, whether or not anything placed or loaded holds it, and
	// including parents like SST_IO.Door that only exist because a child was
	// defined. RequestGameplayTagChildren excludes the root itself, which is
	// wanted -- a Broadcast to SST_IO with children would reach every IO actor.
	const FGameplayTagContainer Children = UGameplayTagsManager::Get().RequestGameplayTagChildren(TAG_SST_IO);

	for (const FGameplayTag& ChildTag : Children.GetGameplayTagArray())
	{
		const TSharedPtr<FGameplayTag>* Found = OldNames.FindByPredicate(
			[&ChildTag](const TSharedPtr<FGameplayTag>& Old) { return Old.IsValid() && *Old == ChildTag; });
		TargetNameOptions.Add(Found ? *Found : MakeShared<FGameplayTag>(ChildTag));
	}

	// Alphabetical, which also places each parent directly above its children.
	Algo::Sort(TargetNameOptions, [](const TSharedPtr<FGameplayTag>& A, const TSharedPtr<FGameplayTag>& B)
	{
		return A->ToString() < B->ToString();
	});
}

void SSST_IOOutputRow::OnTargetNameComboOpening()
{
	RebuildTargetNameOptions();

	if (TargetNameCombo.IsValid())
	{
		TargetNameCombo->RefreshOptions();
	}
}

void SSST_IOOutputRow::OnTargetEventComboOpening()
{
	RebuildTargetEventOptions();
}

int32 SSST_IOOutputRow::GetTargetWidgetIndex() const
{
	const FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr;
	return (Output && Output->IOType == ESSTIOType::Message) ? 0 : 1;
}

FString SSST_IOOutputRow::GetTargetActorPath() const
{
	const FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr;
	return Output ? Output->TargetActor.ToString() : FString();
}

void SSST_IOOutputRow::OnTargetActorChanged(const FAssetData& AssetData)
{
	if (!Item.IsValid() || !Item->OwningIO.IsValid()) return;

	const FScopedTransaction Transaction(LOCTEXT("SetTargetActor", "Set IO Target Actor"));
	Item->OwningIO->Modify();

	if (FSST_IO_Output* Output = Item->Get())
	{
		// Stored as a path, so selecting an actor never loads it. Clearing the
		// field passes empty AssetData, whose path is empty -- the cleared state.
		Output->TargetActor = AssetData.ToSoftObjectPath();
		ApplyTargetChange();
	}
}

TSharedRef<SWidget> SSST_IOOutputRow::MakeNameOptionWidget(TSharedPtr<FGameplayTag> InOption)
{
	return SNew(STextBlock).Text(InOption.IsValid() ? FText::FromName(InOption->GetTagName()) : FText::GetEmpty());
}

FText SSST_IOOutputRow::GetTargetNameText() const
{
	const FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr;

	// An unset tag's name is NAME_None, which would display as "None".
	return (Output && Output->TargetName.IsValid()) ? FText::FromName(Output->TargetName.GetTagName()) : FText::GetEmpty();
}

ECheckBoxState SSST_IOOutputRow::IsIncludeChildrenChecked() const
{
	const FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr;
	return (Output && Output->bIncludeChildren) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SSST_IOOutputRow::OnIncludeChildrenChanged(ECheckBoxState NewState)
{
	if (!Item.IsValid() || !Item->OwningIO.IsValid()) return;

	const FScopedTransaction Transaction(LOCTEXT("SetIncludeChildren", "Set IO Include Children"));
	Item->OwningIO->Modify();

	if (FSST_IO_Output* Output = Item->Get())
	{
		Output->bIncludeChildren = (NewState == ECheckBoxState::Checked);

		// A different set of actors is targeted now, so the events they
		// declare -- and therefore the valid options -- may have changed.
		ApplyTargetChange();
	}
}

void SSST_IOOutputRow::OnTargetNameSelected(TSharedPtr<FGameplayTag> NewValue, ESelectInfo::Type SelectInfo)
{
	if (!NewValue.IsValid() || !Item.IsValid() || !Item->OwningIO.IsValid()) return;

	const FScopedTransaction Transaction(LOCTEXT("SetTargetName", "Set IO Target Name"));
	Item->OwningIO->Modify();

	if (FSST_IO_Output* Output = Item->Get())
	{
		Output->TargetName = *NewValue;
		ApplyTargetChange();
	}
}

FText SSST_IOOutputRow::GetTargetEventText() const
{
	const FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr;
	if (!Output) return FText::GetEmpty();

	// An authored value is shown as-is. The dropdown may be empty because the
	// target is streamed out, but the stored event is still valid and should
	// not read as unset.
	if (!Output->TargetEvent.IsNone())
	{
		if (bTargetEventMissing)
		{
			return FText::Format(LOCTEXT("MissingEvent", "!{0}"), FText::FromName(Output->TargetEvent));
		}
		return FText::FromName(Output->TargetEvent);
	}

	// Message only, deliberately. This is a bound attribute evaluated every
	// paint, and resolving Message is just a soft-pointer lookup. Resolving
	// Broadcast here would sweep the world on every frame for every row.
	if (Output->IOType == ESSTIOType::Message)
	{
		TArray<USST_IO*> Targets;
		switch (USST_IO::ResolveOutputTargets(*Output, GetEditorWorld(), Targets))
		{
		case ESST_IO_Results::Unassigned: return LOCTEXT("NoTargetActor", "<no target>");
		case ESST_IO_Results::NotLoaded:  return LOCTEXT("TargetNotLoaded", "<target not loaded>");
		default: break;
		}
	}

	return FText::GetEmpty();
}


FSlateColor SSST_IOOutputRow::TargetStatusColor() const
{
	if (bTargetEventMissing)
	{
		return FStyleColors::Error;
	}
	if (LastTargetResolution == ESST_IO_Results::NotLoaded || LastTargetResolution == ESST_IO_Results::NoMatch)	
	{
		return FSlateColor::UseSubduedForeground();
	}
	
	return FSlateColor::UseForeground();
}


void SSST_IOOutputRow::OnTargetEventSelected(TSharedPtr<FName> NewValue, ESelectInfo::Type SelectInfo)
{
	if (!NewValue.IsValid() || !Item.IsValid() || !Item->OwningIO.IsValid()) return;

	const FScopedTransaction Transaction(LOCTEXT("SetTargetEvent", "Set IO Target Event"));
	Item->OwningIO->Modify();

	if (FSST_IO_Output* Output = Item->Get())
	{
		Output->TargetEvent = *NewValue;
		bTargetEventMissing = false;
	}

	SyncParametersToSpec();
}


/*
 *
 *  IO Type Section - Message vs Broadcast
 *
 */

FText SSST_IOOutputRow::IOTypeToText(ESSTIOType InType)
{
	return InType == ESSTIOType::Message
		       ? LOCTEXT("IOType_Message", "Message")
		       : LOCTEXT("IOType_Broadcast", "Broadcast");
}

TSharedRef<SWidget> SSST_IOOutputRow::MakeIOTypeOptionWidget(TSharedPtr<ESSTIOType> InOption)
{
	return SNew(STextBlock)
		.Text(InOption.IsValid() ? IOTypeToText(*InOption) : FText::GetEmpty());
}

FText SSST_IOOutputRow::GetIOTypeText() const
{
	const FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr;
	return Output ? IOTypeToText(Output->IOType) : FText::GetEmpty();
}

void SSST_IOOutputRow::OnIOTypeSelected(TSharedPtr<ESSTIOType> NewValue, ESelectInfo::Type SelectInfo)
{
	if (!NewValue.IsValid() || !Item.IsValid() || !Item->OwningIO.IsValid()) return;

	const FScopedTransaction Transaction(LOCTEXT("SetIOType", "Set IO Type"));
	Item->OwningIO->Modify();

	if (FSST_IO_Output* Output = Item->Get())
	{
		Output->IOType = *NewValue;
	}

	ApplyTargetChange();
}


/*
 *
 *	Parameter Section
 *
 */

void SSST_IOOutputRow::SyncParametersToSpec()
{
	if (!Item.IsValid() || !Item->OwningIO.IsValid()) return;

	FSST_IO_Output* Output = Item->Get();
	if (!Output) return;
	
	if (!CanVerifyTarget())
	{
		RebuildParameterRows();
		return;
	}

	// Null when there is no target event, or it can't be used; the rebuilt
	// bag is then empty and whatever the output held goes to the stash.
	const UFunction* Function = GetActiveTargetEventFunction();
	
	// The stash supplies values the output no longer holds, so switching the
	// event away and back restores what was typed rather than defaults.
	FInstancedPropertyBag Rebuilt = FSST_IOParameterUtils::BuildParametersForEvent(
		Function, Output->Parameters, &Item->StashedParameters);

	if (FSST_IOParameterUtils::ParameterLayoutDiffers(Rebuilt, Output->Parameters))
	{
		// Remember what this sync is about to drop before it's overwritten.
		FSST_IOParameterUtils::StashDroppedParameters(Output->Parameters, Rebuilt, Item->StashedParameters);

		const FScopedTransaction Transaction(LOCTEXT("SyncParameters", "Update IO Parameters"));
		Item->OwningIO->Modify();
		Output->Parameters = MoveTemp(Rebuilt);
	}

	RebuildParameterRows();
}

bool SSST_IOOutputRow::CanVerifyTarget() const
{
	const FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr;
	if (!Output) return false;

	TArray<USST_IO*> Targets;
	const ESST_IO_Results Result = USST_IO::ResolveOutputTargets(*Output, GetEditorWorld(), Targets);

	// Unassigned and MissingIO are both known states: nothing to read, or a
	// target that genuinely declares nothing. Only NotLoaded and NoMatch mean
	// "couldn't look," which is when authored data must not be touched.
	return Result != ESST_IO_Results::NotLoaded && Result != ESST_IO_Results::NoMatch;
}

FText SSST_IOOutputRow::CoverageTextFor(FName EventName) const
{
	const FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr;
	if (!Output || EventName.IsNone()) return FText::GetEmpty();

	TArray<USST_IO*> Targets;
	USST_IO::ResolveOutputTargets(*Output, GetEditorWorld(), Targets);

	// A single target either declares the event or it wouldn't be an option,
	// so coverage only means something when there are several.
	if (Targets.Num() <= 1) return FText::GetEmpty();

	int32 Declared = 0;
	for (USST_IO* IO : Targets)
	{
		if (IO->FindTargetEventSpec(EventName))
		{
			++Declared;
		}
	}

	// "Loaded" is deliberate: the editor sweep only sees loaded actors, so in
	// a partitioned level this counts what's loaded, not what exists.
	if (Declared == Targets.Num())
	{
		return FText::Format(LOCTEXT("EventCoverageAll", "Declared by all {0} loaded targets."), Targets.Num());
	}

	return FText::Format(
		LOCTEXT("EventCoveragePartial", "Declared by {0} of {1} loaded targets. The rest will ignore it."),
		Declared, Targets.Num());
}

// The editor world the tool operates on. Every resolution in this widget goes
// through USST_IO::ResolveOutputTargets with this world, so this is the only
// place the row decides which world it's looking at.
UWorld* SSST_IOOutputRow::GetEditorWorld()
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

FReply SSST_IOOutputRow::OnSyncParametersClicked()
{
	if (Item.IsValid())
	{
		// Expanded first: the sync rebuilds the parameter rows, and only
		// builds them when they are showing.
		Item->bParametersExpanded = true;
		SyncParametersToSpec();
		RebuildTargetEventOptions();
	}
	return FReply::Handled();
}

EVisibility SSST_IOOutputRow::GetParameterContainerVisibility() const
{
	const FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr;
	const bool bHasAny = Output && Output->Parameters.GetNumPropertiesInBag() > 0;
	return (bHasAny && IsParametersExpanded()) ? EVisibility::Visible : EVisibility::Collapsed;
}

bool SSST_IOOutputRow::IsParametersExpanded() const
{
	return Item.IsValid() && Item->bParametersExpanded;
}

const FSlateBrush* SSST_IOOutputRow::GetExpanderBrush() const
{
	return FAppStyle::GetBrush(IsParametersExpanded()
		                           ? TEXT("TreeArrow_Expanded")
		                           : TEXT("TreeArrow_Collapsed"));
}

FText SSST_IOOutputRow::GetParameterCountText() const
{
	const FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr;
	return Output ? FText::AsNumber(Output->Parameters.GetNumPropertiesInBag()) : FText::GetEmpty();
}

FReply SSST_IOOutputRow::OnToggleParametersClicked()
{
	// View state, not data — no transaction, and the bound visibility picks
	// it up on the next paint. Stored on the item so a rebuild keeps it.
	if (Item.IsValid())
	{
		Item->bParametersExpanded = !Item->bParametersExpanded;

		// The rows are only built while expanded, so this builds or
		// releases them.
		RebuildParameterRows();
	}
	return FReply::Handled();
}

namespace SST_IOOutputRowPrivate
{
	// Parameter row layout. The name column narrows by the indent, so the
	// value column stays aligned at every depth, as in the Details panel.
	static constexpr int32 MaxParameterDepth = 8;
	static constexpr float IndentPerLevel = 16.0f;
	static constexpr float ExpanderWidth = 16.0f;
	static constexpr float NameColumnWidth = 140.0f;
	static constexpr float MinNameColumnWidth = 60.0f;

	// A key for a parameter row that survives a generator rebuild and doesn't
	// depend on the output's index: the property path with the Parameters
	// property's own path (which contains IO_Outputs[i]) removed. A node with
	// no property, such as a customization's own row, falls back to its
	// parent's key plus its node name.
	FString MakeRowKey(const TSharedRef<IDetailTreeNode>& Node, const FString& ParentKey, const FString& ParametersPath)
	{
		const TSharedPtr<IPropertyHandle> Handle = Node->CreatePropertyHandle();
		if (Handle.IsValid() && Handle->IsValidHandle())
		{
			FString Path = Handle->GeneratePathToProperty();
			if (!ParametersPath.IsEmpty() && Path.StartsWith(ParametersPath))
			{
				Path = Path.RightChop(ParametersPath.Len());
			}
			if (!Path.IsEmpty())
			{
				return Path;
			}
		}
		return ParentKey + TEXT("/") + Node->GetNodeName().ToString();
	}

	// Depth-first search of a detail tree for the first node whose property
	// handle satisfies Pred. Depth-limited: the nodes searched here are a few
	// levels down at most, and a struct property can nest without end.
	TSharedPtr<IDetailTreeNode> FindNode(const TArray<TSharedRef<IDetailTreeNode>>& Nodes,
		TFunctionRef<bool(IPropertyHandle&)> Pred, int32 MaxDepth)
	{
		if (MaxDepth < 0) return nullptr;

		for (const TSharedRef<IDetailTreeNode>& Node : Nodes)
		{
			const TSharedPtr<IPropertyHandle> Handle = Node->CreatePropertyHandle();
			if (Handle.IsValid() && Handle->IsValidHandle() && Pred(*Handle))
			{
				return Node;
			}

			TArray<TSharedRef<IDetailTreeNode>> Children;
			Node->GetChildren(Children);
			if (TSharedPtr<IDetailTreeNode> Found = FindNode(Children, Pred, MaxDepth - 1))
			{
				return Found;
			}
		}
		return nullptr;
	}

	// True for a property that lives directly in a parameter bag, as opposed
	// to the bag itself or a member of a struct inside it.
	bool IsBagProperty(IPropertyHandle& Handle)
	{
		const FProperty* Property = Handle.GetProperty();
		const UStruct* Owner = Property ? Property->GetOwnerStruct() : nullptr;
		return Owner && Owner->IsA<UPropertyBag>();
	}

	// The bag's value rows under the Parameters node. The bag's customization
	// adds them through a builder node of its own, so this descends through
	// anything that isn't a bag property until it reaches them. A bag
	// property's own children (a struct's members, an array's elements) are
	// left to its value widget.
	void CollectBagRows(const TSharedRef<IDetailTreeNode>& Node, TArray<TSharedRef<IDetailTreeNode>>& OutRows, int32 MaxDepth)
	{
		if (MaxDepth < 0) return;

		TArray<TSharedRef<IDetailTreeNode>> Children;
		Node->GetChildren(Children);

		for (const TSharedRef<IDetailTreeNode>& Child : Children)
		{
			const TSharedPtr<IPropertyHandle> Handle = Child->CreatePropertyHandle();
			if (Handle.IsValid() && Handle->IsValidHandle() && IsBagProperty(*Handle))
			{
				OutRows.Add(Child);
				continue;
			}
			CollectBagRows(Child, OutRows, MaxDepth - 1);
		}
	}
}

void SSST_IOOutputRow::RebuildParameterRows()
{
	if (!ParameterContainer.IsValid()) return;

	ParameterContainer->ClearChildren();
	ParameterRowGenerator.Reset();

	// Collapsed rows hold no generator: each one reads the whole component,
	// so only the rows a designer is actually looking at pay for it.
	if (!IsParametersExpanded()) return;

	USST_IO* IO = Item.IsValid() ? Item->OwningIO.Get() : nullptr;
	const FSST_IO_Output* Output = Item.IsValid() ? Item->Get() : nullptr;
	if (!IO || !Output || Output->Parameters.GetNumPropertiesInBag() == 0) return;

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	ParameterRowGenerator = PropertyEditor.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());

	// The generator edits the component itself, so every value change goes
	// through the engine's property system: a transaction, Modify(), and the
	// usual change notifications, exactly as in the Details panel.
	TArray<UObject*> Objects;
	Objects.Add(IO);
	ParameterRowGenerator->SetObjects(Objects);

	// Bound after SetObjects so the initial build doesn't populate twice.
	// Deferred like every other redraw request: the generator can refresh
	// while one of its own widgets is mid-edit.
	ParameterRowGenerator->OnRowsRefreshed().AddSP(this, &SSST_IOOutputRow::RequestPopulateParameterRows);

	PopulateParameterRows();
}

void SSST_IOOutputRow::PopulateParameterRows()
{
	if (!ParameterContainer.IsValid()) return;

	ParameterContainer->ClearChildren();

	// No generator means the list is collapsed or empty. A redraw requested
	// just before the list collapsed lands here and does nothing.
	if (!ParameterRowGenerator.IsValid()) return;

	const TSharedPtr<IDetailTreeNode> ParametersNode = FindParametersNode();

	TArray<TSharedRef<IDetailTreeNode>> Rows;
	if (ParametersNode.IsValid())
	{
		SST_IOOutputRowPrivate::CollectBagRows(ParametersNode.ToSharedRef(), Rows, 4);
	}

	// Never silently empty: if the tree couldn't be read the values still
	// exist and are still editable, just not from here.
	if (Rows.Num() == 0)
	{
		ParameterContainer->AddSlot()
		.AutoHeight()
		.Padding(0, 1)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ParamsUnavailable", "Parameters can't be shown here. Edit them on the IO component in the Details panel."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
		return;
	}

	// Row keys are taken relative to this path, so they don't change if the
	// output moves to a different index.
	FString ParametersPath;
	const TSharedPtr<IPropertyHandle> ParametersHandle = ParametersNode->CreatePropertyHandle();
	if (ParametersHandle.IsValid() && ParametersHandle->IsValidHandle())
	{
		ParametersPath = ParametersHandle->GeneratePathToProperty();
	}

	for (const TSharedRef<IDetailTreeNode>& Row : Rows)
	{
		AddParameterRow(Row, 0, FString(), ParametersPath);
	}
}

void SSST_IOOutputRow::AddParameterRow(const TSharedRef<IDetailTreeNode>& Node, int32 Depth,
	const FString& ParentKey, const FString& ParametersPath)
{
	using namespace SST_IOOutputRowPrivate;

	const FString Key = MakeRowKey(Node, ParentKey, ParametersPath);

	// Past the cap a row is still shown, just not expandable, so a deeply
	// nested value is never silently missing.
	TArray<TSharedRef<IDetailTreeNode>> Children;
	if (Depth < MaxParameterDepth)
	{
		Node->GetChildren(Children);
	}

	const bool bHasChildren = Children.Num() > 0;
	const bool bExpanded = bHasChildren && Item.IsValid() && Item->ExpandedParameterRows.Contains(Key);

	// Adding or removing a container element changes the tree, so the rows
	// are redrawn when a container's value changes. The generator usually
	// refreshes on its own too; redrawing twice is harmless.
	const TSharedPtr<IPropertyHandle> Handle = Node->CreatePropertyHandle();
	if (Handle.IsValid() && Handle->IsValidHandle()
		&& (Handle->AsArray().IsValid() || Handle->AsSet().IsValid() || Handle->AsMap().IsValid()))
	{
		Handle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &SSST_IOOutputRow::RequestPopulateParameterRows));
	}

	ParameterContainer->AddSlot()
	.AutoHeight()
	.Padding(0, 1)
	[
		MakeParameterRow(Node, Depth, bHasChildren, Key)
	];

	if (!bExpanded) return;

	for (const TSharedRef<IDetailTreeNode>& Child : Children)
	{
		AddParameterRow(Child, Depth + 1, Key, ParametersPath);
	}
}

void SSST_IOOutputRow::RequestPopulateParameterRows()
{
	if (bPopulatePending) return;

	bPopulatePending = true;
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SSST_IOOutputRow::HandlePopulateTimer));
}

EActiveTimerReturnType SSST_IOOutputRow::HandlePopulateTimer(double InCurrentTime, float InDeltaTime)
{
	bPopulatePending = false;
	PopulateParameterRows();
	return EActiveTimerReturnType::Stop;
}

TSharedPtr<IDetailTreeNode> SSST_IOOutputRow::FindParametersNode() const
{
	if (!ParameterRowGenerator.IsValid() || !Item.IsValid()) return nullptr;

	using namespace SST_IOOutputRowPrivate;

	// IO_Outputs itself. Its elements report the same property name (an
	// array's inner property shares the array's name), so the array is told
	// apart by having no index of its own.
	const TSharedPtr<IDetailTreeNode> OutputsNode = FindNode(ParameterRowGenerator->GetRootTreeNodes(),
		[](IPropertyHandle& Handle)
		{
			const FProperty* Property = Handle.GetProperty();
			return Property
				&& Property->GetFName() == GET_MEMBER_NAME_CHECKED(USST_IO, IO_Outputs)
				&& Handle.GetIndexInArray() == INDEX_NONE
				&& Handle.AsArray().IsValid();
		}, 4);

	if (!OutputsNode.IsValid()) return nullptr;

	// This row's element: a direct child, matched by index.
	TArray<TSharedRef<IDetailTreeNode>> Elements;
	OutputsNode->GetChildren(Elements);

	const int32 OutputIndex = Item->Index;
	const TSharedPtr<IDetailTreeNode> ElementNode = FindNode(Elements,
		[OutputIndex](IPropertyHandle& Handle) { return Handle.GetIndexInArray() == OutputIndex; }, 0);

	if (!ElementNode.IsValid()) return nullptr;

	// The element's Parameters member.
	TArray<TSharedRef<IDetailTreeNode>> Members;
	ElementNode->GetChildren(Members);

	return FindNode(Members,
		[](IPropertyHandle& Handle)
		{
			const FProperty* Property = Handle.GetProperty();
			return Property
				&& Property->GetFName() == GET_MEMBER_NAME_CHECKED(FSST_IO_Output, Parameters)
				&& Property->GetOwnerStruct() == FSST_IO_Output::StaticStruct();
		}, 2);
}

TSharedRef<SWidget> SSST_IOOutputRow::MakeParameterRow(const TSharedRef<IDetailTreeNode>& Node, int32 Depth,
	bool bHasChildren, const FString& Key)
{
	using namespace SST_IOOutputRowPrivate;

	const FNodeWidgets Widgets = Node->CreateNodeWidgets();

	// Every row reserves the expander column, so rows with and without an
	// arrow line up.
	TSharedRef<SWidget> Expander = SNullWidget::NullWidget;
	if (bHasChildren)
	{
		Expander = SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(0)
			.OnClicked_Lambda([this, Key]()
			{
				// View state only: no transaction. Stored on the item, so a
				// rebuild of this row widget keeps it.
				if (Item.IsValid())
				{
					if (Item->ExpandedParameterRows.Contains(Key))
					{
						Item->ExpandedParameterRows.Remove(Key);
					}
					else
					{
						Item->ExpandedParameterRows.Add(Key);
					}
					RequestPopulateParameterRows();
				}
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image_Lambda([this, Key]()
				{
					const bool bExpanded = Item.IsValid() && Item->ExpandedParameterRows.Contains(Key);
					return FAppStyle::GetBrush(bExpanded ? TEXT("TreeArrow_Expanded") : TEXT("TreeArrow_Collapsed"));
				})
				.ColorAndOpacity(FSlateColor::UseForeground())
			];
	}

	const float Indent = Depth * IndentPerLevel;

	TSharedRef<SHorizontalBox> RowBox = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(Indent, 0, 0, 0)
		[
			SNew(SBox)
			.WidthOverride(ExpanderWidth)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				Expander
			]
		];

	// A customization can claim the whole row; use it as-is.
	if (Widgets.WholeRowWidget.IsValid())
	{
		RowBox->AddSlot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			Widgets.WholeRowWidget.ToSharedRef()
		];
		return RowBox;
	}

	const float NameWidth = FMath::Max(NameColumnWidth - Indent, MinNameColumnWidth);

	RowBox->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(0, 0, 6, 0)
	[
		SNew(SBox).WidthOverride(NameWidth)
		[
			Widgets.NameWidget.IsValid() ? Widgets.NameWidget.ToSharedRef() : SNullWidget::NullWidget
		]
	];

	RowBox->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		SNew(SBox).MinDesiredWidth(220.0f)
		[
			Widgets.ValueWidget.IsValid() ? Widgets.ValueWidget.ToSharedRef() : SNullWidget::NullWidget
		]
	];

	RowBox->AddSlot()
	.FillWidth(1.0f)
	[
		SNullWidget::NullWidget
	];

	return RowBox;
}


#undef LOCTEXT_NAMESPACE
