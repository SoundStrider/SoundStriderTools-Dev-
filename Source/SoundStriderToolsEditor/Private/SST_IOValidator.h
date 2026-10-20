// Copyright (c) 2026 SoundStrider. Licensed under the PolyForm Noncommercial License 1.0.0; see LICENSE.

#pragma once

#include "CoreMinimal.h"

struct FSST_IO_Output;
class UClass;
class UWorld;
class USST_IO;
enum class ESST_IO_Results : uint8;

enum class ESST_IO_ValidatorSeverity : uint8
{
	Error,
	Warning,
	Unverified,
	Info
};

struct FSST_IO_Issue
{
	ESST_IO_ValidatorSeverity Severity = ESST_IO_ValidatorSeverity::Unverified;
	TWeakObjectPtr<USST_IO> Component;
	int32 OutputIndex = INDEX_NONE;
	FText Message;
};

struct FSST_IO_Result
{
	TArray<FSST_IO_Issue> IssuesArray;
	bool bWorldFullyLoaded = false;
	int32 ComponentsChecked = 0;
	int32 OutputsChecked = 0;
};

class FSST_IOValidator
{
public:
	
	static FName GetLogListingName();
	static void RunOnCurrentLevel();
	static FSST_IO_Result ValidateWorld(UWorld* World);
	static void ReportIssues(const FSST_IO_Result& Result);
	
	
private:
	
	static bool IsWorldFullyLoaded(UWorld* World);
	static ESST_IO_ValidatorSeverity ClassifyResolution(ESST_IO_Results Resolution, bool bWorldFullyLoaded);
	static FText DescribeResolution(ESST_IO_Results Resolution, bool bWorldFullyLoaded, const FSST_IO_Output& Output);
	// Checks that are the same for every instance of a class (the spec and the
	// Blueprint's trigger pins). Run once per class; Component is the first
	// instance found, used so the issue links to an actor in the level.
	static void ValidateClassSpec(UClass* OwnerClass, USST_IO* Component, TArray<FSST_IO_Issue>& OutIssues);
	static void ValidateTriggerPins(UClass* OwnerClass, USST_IO* Component, TArray<FSST_IO_Issue>& OutIssues);
	
	// Checks that can differ per placed instance.
	static void ValidateComponent(USST_IO* Component, TArray<FSST_IO_Issue>& OutIssues);
	static void ValidateOutput(USST_IO* Component, int32 OutputIndex, UWorld* World, bool bWorldFullyLoaded, TArray<FSST_IO_Issue>& OutIssues);
	static void ValidateResolvedTarget(USST_IO* Component, int32 OutputIndex, USST_IO* Target, TArray<FSST_IO_Issue>& OutIssues);
	static void ValidateParameters(USST_IO* Component, int32 OutputIndex, bool bWorldFullyLoaded, TArray<FSST_IO_Issue>& OutIssues);
	
	static void AddIssue(TArray<FSST_IO_Issue>& OutIssues, ESST_IO_ValidatorSeverity Severity, USST_IO* Component, int32 OutputIndex, const FText& Message);
};
