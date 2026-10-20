// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#include "SST_IOTriggerEventPin.h"

#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Knot.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "ScopedTransaction.h"
#include "HAL/PlatformTime.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "SST_IOTriggerPinUtils.h"

#define LOCTEXT_NAMESPACE "SSST_IOTriggerEventPin"

namespace SST_IOTriggerEventPinPrivate
{
	// How long a pin's cached declarations are trusted before re-reading them.
	static constexpr double RefreshIntervalSeconds = 0.5;
}


TSharedPtr<SGraphPin> FSST_IOTriggerEventPinFactory::CreatePin(UEdGraphPin* Pin) const
{
	if (SST_IOTriggerPinUtils::IsTriggerEventPin(Pin))
	{
		return SNew(SSST_IOTriggerEventPin, Pin);
	}
	return nullptr;
}


void SSST_IOTriggerEventPin::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
}

TSharedRef<SWidget> SSST_IOTriggerEventPin::GetDefaultValueWidget()
{
	Refresh();
	RebuildOptions();

	return SNew(SWidgetSwitcher)
		// Hidden whenever a wire feeds the pin, like every default value widget.
		.Visibility(this, &SSST_IOTriggerEventPin::GetDefaultValueVisibility)
		.WidgetIndex(this, &SSST_IOTriggerEventPin::GetModeIndex)

		+ SWidgetSwitcher::Slot() // 0: dropdown of this Blueprint's trigger events
		[
			SAssignNew(Combo, SComboBox<TSharedPtr<FName>>)
			.OptionsSource(&Options)
			.OnComboBoxOpening(this, &SSST_IOTriggerEventPin::OnComboOpening)
			.OnGenerateWidget(this, &SSST_IOTriggerEventPin::MakeOptionWidget)
			.OnSelectionChanged(this, &SSST_IOTriggerEventPin::OnOptionSelected)
			[
				SNew(STextBlock)
				.Text(this, &SSST_IOTriggerEventPin::GetComboText)
				.ColorAndOpacity(this, &SSST_IOTriggerEventPin::GetComboColor)
				.ToolTipText(this, &SSST_IOTriggerEventPin::GetComboTooltip)
			]
		]

		+ SWidgetSwitcher::Slot() // 1: plain Name box, target unknown
		[
			SNew(SEditableTextBox)
			.Text(this, &SSST_IOTriggerEventPin::GetPlainText)
			.OnTextCommitted(this, &SSST_IOTriggerEventPin::OnPlainTextCommitted)
			.MinDesiredWidth(80.0f)
			.ToolTipText(LOCTEXT("PlainTip",
				"Target isn't this Blueprint's own SST_IO, so its trigger events can't be listed. The name must match an IO_TriggerEvents entry on the target."))
		];
}

void SSST_IOTriggerEventPin::RefreshIfStale() const
{
	if (FPlatformTime::Seconds() - LastRefreshTime >= SST_IOTriggerEventPinPrivate::RefreshIntervalSeconds)
	{
		Refresh();
	}
}

void SSST_IOTriggerEventPin::Refresh() const
{
	using namespace SST_IOTriggerPinUtils;

	LastRefreshTime = FPlatformTime::Seconds();

	const UEdGraphPin* Pin = GetPinObj();

	// The dropdown only applies when the call targets this Blueprint's own IO
	// and there is an IO to read. Either missing -> plain box.
	bUsesOwnTriggers = Pin && UsesOwnTriggers(Pin) && GatherOwnTriggers(Pin, DeclaredTriggers);
	if (!bUsesOwnTriggers)
	{
		DeclaredTriggers.Reset();
	}
}

const FSST_IO_TriggerEventSpec* SSST_IOTriggerEventPin::FindDeclared(FName EventName) const
{
	if (EventName.IsNone()) return nullptr;
	return DeclaredTriggers.FindByPredicate(
		[EventName](const FSST_IO_TriggerEventSpec& Spec) { return Spec.TriggerEventName == EventName; });
}

FName SSST_IOTriggerEventPin::GetPinValue() const
{
	const UEdGraphPin* Pin = GetPinObj();
	return Pin ? FName(*Pin->GetDefaultAsString()) : NAME_None;
}

void SSST_IOTriggerEventPin::SetPinValue(FName NewValue)
{
	UEdGraphPin* Pin = GetPinObj();
	const UEdGraphSchema* Schema = Pin ? Pin->GetSchema() : nullptr;
	if (!Schema) return;

	const FString NewString = NewValue.ToString();

	// Case-sensitive, so retyping only the case is still a real edit, and
	// re-picking the current value adds nothing to the undo history.
	if (Pin->GetDefaultAsString().Equals(NewString, ESearchCase::CaseSensitive)) return;

	// The same steps the engine's own pin widgets take: open the undo entry,
	// snapshot the pin's node, then set through the schema, which also marks
	// the Blueprint modified.
	const FScopedTransaction Transaction(LOCTEXT("SetTriggerEvent", "Set IO Trigger Event"));
	Pin->Modify();
	Schema->TrySetDefaultValue(*Pin, NewString);
}

int32 SSST_IOTriggerEventPin::GetModeIndex() const
{
	RefreshIfStale();
	return bUsesOwnTriggers ? 0 : 1;
}

void SSST_IOTriggerEventPin::RebuildOptions()
{
	TArray<TSharedPtr<FName>> OldOptions = MoveTemp(Options);

	for (const FSST_IO_TriggerEventSpec& Spec : DeclaredTriggers)
	{
		const FName Name = Spec.TriggerEventName;
		const TSharedPtr<FName>* Found = OldOptions.FindByPredicate(
			[Name](const TSharedPtr<FName>& Old) { return Old.IsValid() && *Old == Name; });

		Options.Add(Found ? *Found : MakeShared<FName>(Name));
	}
}

void SSST_IOTriggerEventPin::OnComboOpening()
{
	// Fresh on open, not on the interval: the list a designer picks from
	// should always be the current one.
	Refresh();
	RebuildOptions();

	if (!Combo.IsValid()) return;
	Combo->RefreshOptions();

	// A combo box only reports a selection when it differs from its own
	// remembered one, which goes stale when the pin changes some other way
	// (undo, the plain box). Syncing it to the pin here means picking any
	// value other than the current one always registers. This programmatic
	// selection arrives as Direct and is ignored in OnOptionSelected.
	const FName Current = GetPinValue();
	const TSharedPtr<FName>* Match = Options.FindByPredicate(
		[Current](const TSharedPtr<FName>& Option) { return Option.IsValid() && *Option == Current; });

	if (Match)
	{
		Combo->SetSelectedItem(*Match);
	}
	else
	{
		Combo->ClearSelection();
	}
}

TSharedRef<SWidget> SSST_IOTriggerEventPin::MakeOptionWidget(TSharedPtr<FName> InOption)
{
	const FName Name = InOption.IsValid() ? *InOption : NAME_None;
	const FSST_IO_TriggerEventSpec* Spec = FindDeclared(Name);

	return SNew(STextBlock)
		.Text(FText::FromName(Name))
		.ToolTipText(Spec ? FText::FromString(Spec->TriggerDescription) : FText::GetEmpty());
}

void SSST_IOTriggerEventPin::OnOptionSelected(TSharedPtr<FName> NewValue, ESelectInfo::Type SelectInfo)
{
	// Direct is the sync in OnComboOpening, not a designer's pick.
	if (SelectInfo == ESelectInfo::Direct || !NewValue.IsValid()) return;
	SetPinValue(*NewValue);
}

FText SSST_IOTriggerEventPin::GetComboText() const
{
	return FText::FromName(GetPinValue());
}

FSlateColor SSST_IOTriggerEventPin::GetComboColor() const
{
	RefreshIfStale();

	// None is never declared, so an unset pin is red too: it fires nothing.
	return FindDeclared(GetPinValue()) ? FSlateColor::UseForeground() : FSlateColor(FStyleColors::Error);
}

FText SSST_IOTriggerEventPin::GetComboTooltip() const
{
	RefreshIfStale();

	const FName Value = GetPinValue();

	if (const FSST_IO_TriggerEventSpec* Spec = FindDeclared(Value))
	{
		return Spec->TriggerDescription.IsEmpty()
			? FText::Format(LOCTEXT("NoDescription", "'{0}' has no description in IO_TriggerEvents."), FText::FromName(Value))
			: FText::FromString(Spec->TriggerDescription);
	}

	if (DeclaredTriggers.Num() == 0)
	{
		return LOCTEXT("NoneDeclared",
			"This Blueprint's SST_IO declares no trigger events. Add them to IO_TriggerEvents in its defaults.");
	}

	if (Value.IsNone())
	{
		return LOCTEXT("Unset", "No trigger event selected, so nothing fires. Pick one of this Blueprint's IO_TriggerEvents.");
	}

	return FText::Format(
		LOCTEXT("Undeclared", "'{0}' is not in this Blueprint's IO_TriggerEvents, so nothing fires. Pick a declared one."),
		FText::FromName(Value));
}

FText SSST_IOTriggerEventPin::GetPlainText() const
{
	const FName Value = GetPinValue();
	return Value.IsNone() ? FText::GetEmpty() : FText::FromName(Value);
}

void SSST_IOTriggerEventPin::OnPlainTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	// Trimmed, since a stray space would make a name that never matches. An
	// empty box is None.
	const FString Trimmed = NewText.ToString().TrimStartAndEnd();
	SetPinValue(Trimmed.IsEmpty() ? NAME_None : FName(*Trimmed));
}

#undef LOCTEXT_NAMESPACE
