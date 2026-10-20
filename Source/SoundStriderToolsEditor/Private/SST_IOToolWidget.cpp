// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.


#include "SST_IOToolWidget.h"
#include "SST_IO.h"
#include "SST_IOToolTypes.h"
#include "SST_IOOutputRow.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWidgetSwitcher.h"


#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/Selection.h"
#include "GameFramework/Actor.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "DrawDebugHelpers.h"
#include "Widgets/Layout/SBox.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ISinglePropertyView.h"
#include "EditorUndoClient.h"
#include "SST_LOG.h"
#include "SoundStriderToolsEditor.h"

#define LOCTEXT_NAMESPACE "SSST_IOToolWidget"

void SSST_IOToolWidget::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		// --- Header: actor name on the left, Add Output on the right ---
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 8.0f, 8.0f, 4.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SSST_IOToolWidget::GetSelectedActorText)
				.Font(FAppStyle::GetFontStyle("HeadingSmall"))
			]

			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 1, 0)
			[
				SNew(SButton)
				             .Text(LOCTEXT("AddOutput", "Add Output"))
				             .OnClicked(this, &SSST_IOToolWidget::OnAddOutputClicked)
				// Nothing to add an output to when there's no selection.
				             .Visibility(this, &SSST_IOToolWidget::GetOutputsVisibility)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 2.0f, 8.0f, 2.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "Checkbox")
				.Padding(2)
				.IsChecked(this, &SSST_IOToolWidget::IsDrawInputsChecked)
				.OnCheckStateChanged(this, &SSST_IOToolWidget::OnToggleDrawInputsChecked)
				.Visibility(this, &SSST_IOToolWidget::GetOutputsVisibility)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DrawInputs", "Draw Inputs"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "Checkbox")
				.Padding(2)
				.IsChecked(this, &SSST_IOToolWidget::IsDrawOutputsChecked)
				.OnCheckStateChanged(this, &SSST_IOToolWidget::OnToggleDrawOutputsChecked)
				.Visibility(this, &SSST_IOToolWidget::GetOutputsVisibility)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DrawOutputs", "Draw Outputs"))
				]
			]

			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNullWidget::NullWidget
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "Checkbox")
				.Padding(2)
				.IsChecked(this, &SSST_IOToolWidget::IsDuplicateInputsChecked)
				.OnCheckStateChanged(this, &SSST_IOToolWidget::OnToggleDuplicateInputsChecked)
				.Visibility(this, &SSST_IOToolWidget::GetOutputsVisibility)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ToggleDuplicate", "Duplicate Inputs?"))
				]
			]
		]

		// --- Target name: what this actor answers to, and the instance override ---
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 2.0f, 8.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			.Visibility(this, &SSST_IOToolWidget::GetOutputsVisibility)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0)
			[
				SNew(STextBlock)
				.Text(this, &SSST_IOToolWidget::GetEffectiveTargetNameText)
				.ToolTipText(LOCTEXT("EffectiveTargetNameTip", "The tag this actor answers to for Broadcast outputs."))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(12.0f, 0, 4.0f, 0)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "Checkbox")
				.Padding(2)
				.IsChecked(this, &SSST_IOToolWidget::IsOverrideChecked)
				.OnCheckStateChanged(this, &SSST_IOToolWidget::OnOverrideChanged)
				.ToolTipText(LOCTEXT("OverrideTargetNameTip",
					"Give this placed actor its own tag instead of its class's. Leave the tag empty to keep using the class tag."))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OverrideTargetName", "Override for this instance"))
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0)
			[
				SAssignNew(OverrideViewBox, SBox)
				.Visibility(this, &SSST_IOToolWidget::GetOverrideViewVisibility)
			]
		]

		// --- Empty state ---
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoSelection", "Select an actor with an SST_IO component."))
			.Visibility(this, &SSST_IOToolWidget::GetEmptyStateVisibility)
		]

		// --- Outputs / Inputs list ---
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Visibility(this, &SSST_IOToolWidget::GetOutputsVisibility)
			[
				SNew(SScrollBox)
				.Orientation(Orient_Horizontal)

				+ SScrollBox::Slot()
				.FillSize(1.0f)
				[
					SAssignNew(OutputListView, SListView<FSST_IOOutputItemPtr>)
					.ListItemsSource(&Items)
					.OnGenerateRow(this, &SSST_IOToolWidget::OnGenerateOutputRow)
					.SelectionMode(ESelectionMode::Single)
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(1.0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.OnClicked(this, &SSST_IOToolWidget::OnToggleInputClicked)
			.ToolTipText(LOCTEXT("ToggleInput", "Show or hide this Actors incoming IO Events"))
			.Visibility(this, &SSST_IOToolWidget::GetOutputsVisibility)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(this, &SSST_IOToolWidget::GetInputExpanderBrush)
					.ColorAndOpacity(FSlateColor::UseForeground())
				]

				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 0, 0)
				[
					SNew(STextBlock)
					.Text(this, &SSST_IOToolWidget::GetInputCountText)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]
		]


		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(1.0)
		[
			SAssignNew(InputContainer, SVerticalBox)
			.Visibility(this, &SSST_IOToolWidget::GetInputContainerVisibility)
		]
	];


	USelection::SelectionChangedEvent.AddSP(this, &SSST_IOToolWidget::HandleEditorSelectionChanged);

	// The copies are selected before the duplication handler rewires them, so
	// the selection refresh shows them unwired. This refreshes once it's done.
	FSoundStriderToolsEditorModule::OnIODuplicationProcessed.AddSP(this, &SSST_IOToolWidget::RefreshSelectedActor);

	//Fix up views so the selection does not break and then rebuild the items instead of trying to patch the array 
	HandleEditorSelectionChanged(nullptr);
	RebuildItems();
}

SSST_IOToolWidget::~SSST_IOToolWidget()
{
	USelection::SelectionChangedEvent.RemoveAll(this);
	FSoundStriderToolsEditorModule::OnIODuplicationProcessed.RemoveAll(this);
}

void SSST_IOToolWidget::AddReferencedObjects(FReferenceCollector& Collector)
{
	// Only the stashes: everything else the items reach is a weak pointer.
	// Clearing Items (selecting another actor, or nothing) releases them.
	for (const FSST_IOOutputItemPtr& Item : Items)
	{
		if (Item.IsValid())
		{
			Item->StashedParameters.AddStructReferencedObjects(Collector);
		}
	}
}

FString SSST_IOToolWidget::GetReferencerName() const
{
	return TEXT("SSST_IOToolWidget");
}

void SSST_IOToolWidget::HandleEditorSelectionChanged(UObject* NewSelection)
{
	USST_IO* NewIO = nullptr;

	if (GEditor)
	{
		if (USelection* Selection = GEditor->GetSelectedActors())
		{
			if (AActor* SelectedActor = Cast<AActor>(Selection->GetSelectedObject(0)))
			{
				NewIO = SelectedActor->FindComponentByClass<USST_IO>();
			}
		}
	}

	// The editor re-broadcasts selection after an undo, and clicking the
	// selected actor again does too. Neither changes what's shown, so the
	// existing rows are refreshed in place instead of regenerated.
	const bool bSameComponent = NewIO && NewIO == SelectedIO.Get();
	SelectedIO = NewIO;

	if (bSameComponent)
	{
		RefreshSelectedActor();
	}
	else
	{
		RebuildItems();
		RebuildOverrideView();
	}

	// RebuildItems just computed Inputs against this tag, so Tick shouldn't
	// immediately recompute them.
	const USST_IO* IO = SelectedIO.Get();
	LastEffectiveTargetName = IO ? IO->GetEffectiveTargetName() : FGameplayTag();
}

FText SSST_IOToolWidget::GetSelectedActorText() const
{
	if (USST_IO* IO = SelectedIO.Get())
	{
		if (AActor* Owner = IO->GetOwner())
		{
			return FText::FromString(Owner->GetActorNameOrLabel());
		}
	}
	return LOCTEXT("NoActor", "No actor selected");
}

EVisibility SSST_IOToolWidget::GetOutputsVisibility() const
{
	return SelectedIO.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SSST_IOToolWidget::GetEmptyStateVisibility() const
{
	return SelectedIO.IsValid() ? EVisibility::Collapsed : EVisibility::Visible;
}

TSharedRef<ITableRow> SSST_IOToolWidget::OnGenerateOutputRow(
	FSST_IOOutputItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SSST_IOOutputRow, OwnerTable)
		.Item(Item)
		.OnRequestDelete(this, &SSST_IOToolWidget::HandleRequestDelete)
		.OnRequestDuplicate(this, &SSST_IOToolWidget::HandleRequestDuplicate);
}

void SSST_IOToolWidget::RebuildItems()
{
	USST_IO* IO = SelectedIO.Get();
	AActor* NewOwner = IO ? IO->GetOwner() : nullptr;

	// Every row is about to be destroyed, so per-row view state is read off
	// the old items first and reapplied by position. Only for the same actor:
	// a different actor's rows start collapsed.
	// The old items themselves are kept (they hold the expansion flag and the
	// parameter stash) so both survive the rebuild.
	TArray<FSST_IOOutputItemPtr> PreviousItems;
	if (NewOwner && NewOwner == ItemsOwner.Get())
	{
		PreviousItems = Items;
	}

	Items.Reset();
	Inputs.Reset();
	ItemsOwner = NewOwner;

	if (IO)
	{
		for (int32 i = 0; i < IO->IO_Outputs.Num(); ++i)
		{
			FSST_IOOutputItemPtr NewItem = MakeShared<FSST_IOOutputItem>(IO, i);
			if (PreviousItems.IsValidIndex(i) && PreviousItems[i].IsValid())
			{
				NewItem->bParametersExpanded = PreviousItems[i]->bParametersExpanded;
				NewItem->StashedParameters = PreviousItems[i]->StashedParameters;
				NewItem->WarnedDifferingInputs = PreviousItems[i]->WarnedDifferingInputs;
				NewItem->ExpandedParameterRows = PreviousItems[i]->ExpandedParameterRows;
			}
			Items.Add(NewItem);
		}

		Inputs = IO->FindIncomingCommunication();
	}

	// Without this the list keeps showing its previously generated rows,
	// because it has no way to know Items changed underneath it.
	if (OutputListView.IsValid())
	{
		OutputListView->RequestListRefresh();
	}

	RebuildInputs();
}

FReply SSST_IOToolWidget::OnAddOutputClicked()
{
	if (USST_IO* IO = SelectedIO.Get())
	{
		const FScopedTransaction Transaction(LOCTEXT("AddOutput", "Add IO Output"));
		IO->Modify();
		IO->IO_Outputs.AddDefaulted();

		RebuildItems();
	}
	return FReply::Handled();
}

void SSST_IOToolWidget::HandleRequestDelete(FSST_IOOutputItemPtr Item)
{
	if (!Item.IsValid() || !Item->OwningIO.IsValid()) return;

	USST_IO* IO = Item->OwningIO.Get();
	if (!IO->IO_Outputs.IsValidIndex(Item->Index)) return;

	const FScopedTransaction Transaction(LOCTEXT("RemoveOutput", "Remove IO Output"));
	IO->Modify();
	IO->IO_Outputs.RemoveAt(Item->Index);

	// Mirror the removal in Items so the expansion state RebuildItems carries
	// over by position stays with the outputs that remain.
	if (Items.IsValidIndex(Item->Index))
	{
		Items.RemoveAt(Item->Index);
	}

	// Every item after the removed one now holds a stale index, so the whole
	// array is rebuilt rather than patched.
	RebuildItems();
}

void SSST_IOToolWidget::HandleRequestDuplicate(FSST_IOOutputItemPtr Item)
{
	if (!Item.IsValid() || !Item->OwningIO.IsValid()) return;

	USST_IO* IO = Item->OwningIO.Get();
	if (!IO->IO_Outputs.IsValidIndex(Item->Index)) return;

	const FScopedTransaction Transaction(LOCTEXT("DuplicateOutput", "Duplicate IO Output"));
	IO->Modify();

	//copy  the current item and insert it in the array next to the one you're copying 
	const FSST_IO_Output Copy = IO->IO_Outputs[Item->Index];
	IO->IO_Outputs.Insert(Copy, Item->Index + 1);

	// Mirror the insert in Items so expansion carries over by position; the
	// copy starts with the same expanded state as its source.
	if (Items.IsValidIndex(Item->Index))
	{
		Items.Insert(MakeShared<FSST_IOOutputItem>(*Item), Item->Index + 1);
	}

	RebuildItems();
}

const FSST_IO_Output* SSST_IOToolWidget::GetIncomingOutputAt(int32 OutputIndex) const
{
	if (!Inputs.IsValidIndex(OutputIndex))
	{
		return nullptr;
	}

	const FSST_IO_Input& Link = Inputs[OutputIndex];

	const USST_IO* SendingIO = Link.IOSender.Get();
	if (!SendingIO)
	{
		return nullptr;
	}

	if (!SendingIO->IO_Outputs.IsValidIndex(Link.OutputArrayLocation))
	{
		return nullptr;
	}

	return &SendingIO->IO_Outputs[Link.OutputArrayLocation];
}


FReply SSST_IOToolWidget::OnToggleInputClicked()
{
	bInputContainerExpanded = !bInputContainerExpanded;
	return FReply::Handled();
}

const FSlateBrush* SSST_IOToolWidget::GetInputExpanderBrush() const
{
	return FAppStyle::GetBrush(bInputContainerExpanded
		                           ? TEXT("TreeArrow_Expanded")
		                           : TEXT("TreeArrow_Collapsed"));
}

FText SSST_IOToolWidget::GetInputCountText() const
{
	return FText::AsNumber(Inputs.Num());
}

EVisibility SSST_IOToolWidget::GetInputContainerVisibility() const
{
	return (bInputContainerExpanded && SelectedIO.IsValid()) ? EVisibility::Visible : EVisibility::Collapsed;
}

void SSST_IOToolWidget::RebuildInputs()
{
	if (!InputContainer.IsValid()) return;
	InputContainer->ClearChildren();


	//sort inputs so messages appear above broadcasts 
	Algo::Sort(Inputs, [](const FSST_IO_Input& A, const FSST_IO_Input& B)
	{
		auto GetType = [](const FSST_IO_Input& Link) -> int32
		{
			const USST_IO* IO = Link.IOSender.Get();
			return (IO && IO->IO_Outputs.IsValidIndex(Link.OutputArrayLocation))
				       ? static_cast<int32>(IO->IO_Outputs[Link.OutputArrayLocation].IOType)
				       : MAX_int32;
		};

		return GetType(A) < GetType(B);
	});


	//build the rows 
	for (int32 i = 0; i < Inputs.Num(); ++i)
	{
		InputContainer->AddSlot()
		              .AutoHeight()
		              .Padding(0)
		[
			MakeInputRow(i)
		];
	}
}

TSharedRef<SWidget> SSST_IOToolWidget::MakeInputRow(int32 InputIndex)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f)
		[
			SNew(SBox).WidthOverride(130)
			[
				SNew(SButton)
				.OnClicked_Lambda([this, InputIndex]()
				{
					return OnInputObjectClicked(InputIndex);
				})
				.Text_Lambda([this, InputIndex]()
				{
					return InputActorText(InputIndex);
				})
				.ToolTipText_Lambda([this, InputIndex]()
				{
					return InputActorText(InputIndex);
				})
			]

		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TriggerFunctionLabel", "Trigger Function: "))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f)
		[
			SNew(SBox).WidthOverride(80)
			[
				SNew(STextBlock)
				.Text_Lambda([this, InputIndex]()
				{
					const FSST_IO_Output* Output = GetIncomingOutputAt(InputIndex);
					return Output ? FText::FromName(Output->TriggerEvent) : FText::GetEmpty();
				})
				.Font([]
				{
					FSlateFontInfo Font = FAppStyle::GetFontStyle("NormalText");
					Font.TypefaceFontName = TEXT("Bold");
					return Font;
				}())
				.ToolTipText_Lambda([this, InputIndex]()
				{
					return TriggerFunctionTooltip(InputIndex);
				})
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TargetFunctionLabel", "Target Function: "))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f)
		[
			SNew(STextBlock)
			.Text_Lambda([this, InputIndex]()
			{
				const FSST_IO_Output* Output = GetIncomingOutputAt(InputIndex);
				return Output ? FText::FromName(Output->TargetEvent) : FText::GetEmpty();
			})
			.Font([]
			{
				FSlateFontInfo Font = FAppStyle::GetFontStyle("NormalText");
				Font.TypefaceFontName = TEXT("Bold");
				return Font;
			}())
			.ToolTipText_Lambda([this, InputIndex]()
			{
				return TargetFunctionTooltip(InputIndex);
			})
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("OutputType", "Output Type: "))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f)
		[
			SNew(STextBlock)
			.Text_Lambda([this, InputIndex]()
			{
				const FSST_IO_Output* Output = GetIncomingOutputAt(InputIndex);
				if (!Output) return FText::GetEmpty();

				return Output->IOType == ESSTIOType::Message
					       ? LOCTEXT("MessageType", "Message")
					       : LOCTEXT("BroadcastType", "Broadcast");
			})
		];
}

FText SSST_IOToolWidget::InputActorText(int32 InputIndex)
{
	if (!Inputs.IsValidIndex(InputIndex)) return FText::GetEmpty();

	const USST_IO* SendingIO = Inputs[InputIndex].IOSender.Get();
	if (!SendingIO) return FText::GetEmpty();

	const AActor* SendingActor = SendingIO->GetOwner();
	return SendingActor
		       ? FText::FromString(SendingActor->GetActorNameOrLabel())
		       : FText::GetEmpty();
}

FReply SSST_IOToolWidget::OnInputObjectClicked(int32 InputIndex)
{
	if (Inputs.IsValidIndex(InputIndex))
	{
		USST_IO* SendingIO = Inputs[InputIndex].IOSender.Get();
		if (!SendingIO) return FReply::Handled();

		AActor* Actor = SendingIO->GetOwner();
		if (!Actor) return FReply::Handled();

		if (GEditor)
		{
			GEditor->SelectNone(true, false, false);
			GEditor->SelectActor(Actor, true, true);
		}
	}

	return FReply::Handled();
}


FText SSST_IOToolWidget::TriggerFunctionTooltip(int32 InputIndex) const
{
	const FSST_IO_Output* Output = GetIncomingOutputAt(InputIndex);
	if (!Output) return FText::GetEmpty();

	if (!Inputs.IsValidIndex(InputIndex)) return FText::GetEmpty();

	const USST_IO* SendingIO = Inputs[InputIndex].IOSender.Get();
	if (!SendingIO) return FText::GetEmpty();

	const FSST_IO_TriggerEventSpec* Spec = SendingIO->FindTriggerEventSpec(Output->TriggerEvent);
	if (!Spec) return FText::GetEmpty();

	return FText::FromString(Spec->TriggerDescription);
}


FText SSST_IOToolWidget::TargetFunctionTooltip(int32 InputIndex) const
{
	const FSST_IO_Output* Output = GetIncomingOutputAt(InputIndex);
	if (!Output) return FText::GetEmpty();

	const USST_IO* ReceivingIO = SelectedIO.Get();
	if (!ReceivingIO) return FText::GetEmpty();

	const FSST_IO_TargetEventSpec* Spec = ReceivingIO->FindTargetEventSpec(Output->TargetEvent);
	if (!Spec) return FText::GetEmpty();

	return FText::FromString(Spec->TargetDescription);
}

//debug drawing 
void SSST_IOToolWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	
	if (!SelectedIO.IsValid() && ItemsOwner.IsValid())
	{
		RefreshSelectedActor();
	}

	USST_IO* IO = SelectedIO.Get();

	if (IO)
	{
		// Catches edits made outside this widget's own handlers, such as undo
		// or the Details panel. Two cheap comparisons; the expensive work only
		// runs when something actually changed.
		if (IO->bOverrideTargetName != bOverrideViewBuiltFor)
		{
			RebuildOverrideView();
		}

		// A new effective tag changes which Broadcast outputs reach this
		// actor, so the incoming links have to be recomputed.
		const FGameplayTag EffectiveTargetName = IO->GetEffectiveTargetName();
		if (EffectiveTargetName != LastEffectiveTargetName)
		{
			LastEffectiveTargetName = EffectiveTargetName;
			Inputs = IO->FindIncomingCommunication();
			RebuildInputs();
		}
	}

	if (!GEditor) return;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return;

	if (!IO) return;

	AActor* Owner = IO->GetOwner();
	if (!Owner) return;

	FVector MyLocation = Owner->GetActorLocation();

	// Incoming: the drop-up already holds every link targeting this actor,
	// so drawing reads it instead of sweeping the world every frame.
	if (bDebugDrawInputs)
	{
		for (const FSST_IO_Input& Link : Inputs)
		{
			const USST_IO* Sender = Link.IOSender.Get();
			if (!Sender || !Sender->IO_Outputs.IsValidIndex(Link.OutputArrayLocation)) continue;

			const AActor* SenderActor = Sender->GetOwner();
			if (!SenderActor) continue;

			const bool bIsMessage = Sender->IO_Outputs[Link.OutputArrayLocation].IOType == ESSTIOType::Message;
			DrawDebugDirectionalArrow(World, SenderActor->GetActorLocation(), MyLocation, 2.0f,
				bIsMessage ? FColor::Green : FColor::Cyan);
		}
	}

	// Outgoing: resolved the same way runtime dispatch resolves them, so the
	// arrows show exactly what would fire, Include Children included.
	if (bDebugDrawOutputs)
	{
		TArray<USST_IO*> Targets;
		for (const FSST_IO_Output& Output : IO->IO_Outputs)
		{
			USST_IO::ResolveOutputTargets(Output, World, Targets);

			const FColor Color = (Output.IOType == ESSTIOType::Message) ? FColor::Red : FColor::Magenta;
			for (const USST_IO* Target : Targets)
			{
				if (const AActor* TargetActor = Target->GetOwner())
				{
					DrawDebugDirectionalArrow(World, MyLocation, TargetActor->GetActorLocation(), 2.0f, Color);
				}
			}
		}
	}
}

FText SSST_IOToolWidget::GetEffectiveTargetNameText() const
{
	const USST_IO* IO = SelectedIO.Get();
	if (!IO) return FText::GetEmpty();

	const FGameplayTag EffectiveTag = IO->GetEffectiveTargetName();
	return EffectiveTag.IsValid()
		? FText::Format(LOCTEXT("EffectiveTargetName", "Target Name: {0}"), FText::FromName(EffectiveTag.GetTagName()))
		: LOCTEXT("NoTargetName", "Target Name: (none)");
}

ECheckBoxState SSST_IOToolWidget::IsOverrideChecked() const
{
	const USST_IO* IO = SelectedIO.Get();
	return (IO && IO->bOverrideTargetName) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SSST_IOToolWidget::OnOverrideChanged(ECheckBoxState NewState)
{
	USST_IO* IO = SelectedIO.Get();
	if (!IO) return;

	const FScopedTransaction Transaction(LOCTEXT("SetOverrideTargetName", "Set IO Target Name Override"));
	IO->Modify();
	IO->bOverrideTargetName = (NewState == ECheckBoxState::Checked);

	// Turning the override off keeps InstanceTargetName, so turning it back
	// on restores the designer's value rather than starting empty.
	RebuildOverrideView();
}

EVisibility SSST_IOToolWidget::GetOverrideViewVisibility() const
{
	const USST_IO* IO = SelectedIO.Get();
	return (IO && IO->bOverrideTargetName) ? EVisibility::Visible : EVisibility::Collapsed;
}

void SSST_IOToolWidget::RebuildOverrideView()
{
	if (!OverrideViewBox.IsValid()) return;

	OverrideViewBox->SetContent(SNullWidget::NullWidget);

	USST_IO* IO = SelectedIO.Get();
	bOverrideViewBuiltFor = IO ? IO->bOverrideTargetName : false;

	if (!IO || !IO->bOverrideTargetName) return;

	// A single-property view draws the property exactly as the Details panel
	// would, using the FGameplayTag customization the editor already loads --
	// picker, Categories filter, tag creation, and its own transactions.
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	TSharedPtr<ISinglePropertyView> View = PropertyEditor.CreateSingleProperty(
		IO, GET_MEMBER_NAME_CHECKED(USST_IO, InstanceTargetName), FSinglePropertyParams());

	// Null means the property editor couldn't build a view for it. The
	// override still works through the Details panel in that case.
	if (View.IsValid())
	{
		OverrideViewBox->SetContent(View.ToSharedRef());
	}
}


ECheckBoxState SSST_IOToolWidget::IsDuplicateInputsChecked() const
{
	const USST_IO* IO = SelectedIO.Get();

	return (IO && IO->bCopyInputsOnDuplication)
		       ? ECheckBoxState::Checked
		       : ECheckBoxState::Unchecked;
}

void SSST_IOToolWidget::OnToggleDuplicateInputsChecked(ECheckBoxState NewState)
{
	USST_IO* IO = SelectedIO.Get();
	if (!IO) return;

	const FScopedTransaction Transaction(
		LOCTEXT("SetCopyInputs", "Set Copy Inputs On Duplication"));
	IO->Modify();

	IO->bCopyInputsOnDuplication = (NewState == ECheckBoxState::Checked);

}


ECheckBoxState SSST_IOToolWidget::IsDrawInputsChecked() const
{
	return bDebugDrawInputs ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SSST_IOToolWidget::OnToggleDrawInputsChecked(ECheckBoxState NewState)
{
	bDebugDrawInputs = (NewState == ECheckBoxState::Checked);
}

ECheckBoxState SSST_IOToolWidget::IsDrawOutputsChecked() const
{
	return bDebugDrawOutputs ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SSST_IOToolWidget::OnToggleDrawOutputsChecked(ECheckBoxState NewState)
{
	bDebugDrawOutputs = (NewState == ECheckBoxState::Checked);
}

void SSST_IOToolWidget::PostUndo(bool bSuccess)
{
	FSelfRegisteringEditorUndoClient::PostUndo(bSuccess);
	RefreshSelectedActor();
}

void SSST_IOToolWidget::PostRedo(bool bSuccess)
{
	FSelfRegisteringEditorUndoClient::PostRedo(bSuccess);
	RefreshSelectedActor();
}

void SSST_IOToolWidget::RefreshSelectedActor()
{
	USST_IO* IO = SelectedIO.Get();

	// Undo on a Blueprint actor can rerun its construction script, which
	// replaces its components with new objects. The old pointer is then
	// dead but the actor is the same, so the replacement is found through it.
	// Without this the rebuild below would see no component and drop the
	// rows' view state before the selection refresh arrives.
	bool bComponentReplaced = false;
	if (!IO)
	{
		if (AActor* Owner = ItemsOwner.Get())
		{
			IO = Owner->FindComponentByClass<USST_IO>();
			SelectedIO = IO;
			bComponentReplaced = (IO != nullptr);
		}
	}

	// A full rebuild is required when the items no longer line up with the
	// array: the selected component is gone, outputs were added or removed,
	// or the items belong to a different component than the one selected.
	const bool bShapeChanged =
		!IO ||
		IO->IO_Outputs.Num() != Items.Num() ||
		(Items.Num() > 0 && Items[0]->OwningIO.Get() != IO);

	if (bShapeChanged)
	{
		RebuildItems();

		// The override view is a property view bound to the old component.
		if (bComponentReplaced)
		{
			RebuildOverrideView();
		}
		return;
	}

	// Same component, same count, so every item still points at a valid
	// index and its row widget can be kept. The rows' bound attributes
	// already re-read the data each paint; only their cached state needs
	// refreshing.
	if (OutputListView.IsValid())
	{
		for (const FSST_IOOutputItemPtr& Item : Items)
		{
			// Null for rows that were never generated (scrolled out of view).
			// Those are built fresh from the data when they scroll in.
			const TSharedPtr<ITableRow> RowWidget = OutputListView->WidgetFromItem(Item);
			if (!RowWidget.IsValid()) continue;

			// Every row this list generates is an SSST_IOOutputRow (see
			// OnGenerateOutputRow), so the static cast is safe.
			StaticCastSharedPtr<SSST_IOOutputRow>(RowWidget)->RefreshFromData();
		}
	}

	// An undo can change which outputs target this actor, so the incoming
	// links are recomputed either way.
	Inputs = IO->FindIncomingCommunication();
	RebuildInputs();
}

#undef LOCTEXT_NAMESPACE
