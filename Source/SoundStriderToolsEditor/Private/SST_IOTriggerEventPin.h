// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "EdGraphUtilities.h"
#include "SGraphPin.h"
#include "Widgets/Input/SComboBox.h"
#include "SST_IO.h"

// Gives the TriggerEvent pin of every TriggerOutputsFromEvent node a picker.
// Every other pin is left to the engine (CreatePin returns null for it).
// Registered in FSoundStriderToolsEditorModule::StartupModule.
class FSST_IOTriggerEventPinFactory : public FGraphPanelPinFactory
{
public:
	virtual TSharedPtr<SGraphPin> CreatePin(UEdGraphPin* Pin) const override;
};

// The TriggerEvent pin's value widget. Two modes, chosen from what the node's
// Target pin is wired to:
//   - this Blueprint's own SST_IO: a dropdown of its IO_TriggerEvents, with
//     the value shown red when it isn't one of them (None included);
//   - anything else (another actor's component, Get Component By Class, the
//     Level Blueprint, nothing wired): a plain Name box, unvalidated, because
//     the target's declarations can't be known here.
// The mode is re-checked while the graph is open, so rewiring Target switches
// it without reopening the graph.
class SSST_IOTriggerEventPin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SSST_IOTriggerEventPin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

protected:
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override;

private:
	// --- Cached view of the Blueprint's declarations ---
	// The bound getters below run every paint, and gathering the Blueprint's
	// components isn't free, so the result is cached and refreshed on a short
	// interval, and immediately when the dropdown opens.
	void RefreshIfStale() const;
	void Refresh() const;

	mutable bool bUsesOwnTriggers = false;
	mutable TArray<FSST_IO_TriggerEventSpec> DeclaredTriggers;
	mutable double LastRefreshTime = -1.0;

	const FSST_IO_TriggerEventSpec* FindDeclared(FName EventName) const;

	// --- Pin value ---
	FName GetPinValue() const;
	void SetPinValue(FName NewValue);

	// 0 = dropdown, 1 = plain Name box. Drives the widget switcher.
	int32 GetModeIndex() const;

	// --- Dropdown ---
	// Options are reused by name across rebuilds: a combo box tracks its
	// selection by pointer, so fresh pointers every time would break it.
	TArray<TSharedPtr<FName>> Options;
	TSharedPtr<SComboBox<TSharedPtr<FName>>> Combo;
	void RebuildOptions();
	void OnComboOpening();
	TSharedRef<SWidget> MakeOptionWidget(TSharedPtr<FName> InOption);
	void OnOptionSelected(TSharedPtr<FName> NewValue, ESelectInfo::Type SelectInfo);

	FText GetComboText() const;
	FSlateColor GetComboColor() const;
	FText GetComboTooltip() const;

	// --- Plain Name box ---
	FText GetPlainText() const;
	void OnPlainTextCommitted(const FText& NewText, ETextCommit::Type CommitType);
};
