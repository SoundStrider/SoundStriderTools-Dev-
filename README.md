# About

---
SoundStriderTools is currently in Beta release. Versions of this plugin from GitHub fall under a PolyForm Noncommercial License 1.0.0 . 
Once SoundStriderTools - Dev - is out of Beta, a commercial license will be available on FAB. 

SoundStriderTools - Dev is a collection of Unreal Engine tools intended for designers to improve workflows and ease development struggles. 
Currently, the only tool included in this package is the Scripting_IO tool (SST_IO) and its related features and editor tools

>[!WARNING]
>Currently the tools have only been tested in the UE 5.8 dev environment on the latest build. It should be functional from UE 5.5 onward (StructUtils/PropertyBag.h was a plugin before 5.5), but regression testing has not occured yet.

# SST_IO

---
Introducing SoundStriderTools Input-Output scripting! This is a feature set that is modeled after
the scripting system present in the Source Engine by Valve&trade; Software, as well as other Unreal Scripting tools like ActorIO and JoeIO. It works by defining on an Actor basis
what that actor is allowed to do. An actor will have defined `Trigger Events` (notify that I have performed an action) 
and `Target Events` (what am I able to do). A designer will then be able to use the Scripting Editor Tool to have an actor (A)
target another actor (B), and when Actor A's `Trigger Event` is hit, Actor B can perform the set `Target Event`. 

## How Does it Work?
SST_IO is fairly simple to get up and running. First a designer will take a Blueprint (BP) Actor and add an SST_IO Actor Component to the BP.
From there they would go into the SST_IO details and begin defining that Actor's `TargetName`, `TriggerEvent`, and `TargetEvent`. 

<details>
<summary> Target Name </summary>

An Actor's `TargetName` is a GameplayTag that is created by the user to give a name to a class of Actor. The prefix `SST_IO` is built into the plugin, so it’s always there and can’t be renamed or deleted. 
Tags you create (e.g. SST_IO.Door) are saved to `Config/DefaultGameplayTags.ini` by default. You can save them elsewhere, but every target name must start with `SST_IO.` or the tool will be unable to find them.

Target Names can also have overrides on a per-actor or per-class basis. Examples of this would be 
- You have a base BP Actor class called `BP_Door` this is functionally just a parent class of logic and never used in the level, and can be called `SST_IO.Door`. You would define all the logic, trigger, and target events here.
Then on the `BP_Door_Wood` child class that's actually implemented in the level, you can override that class at the BP level to use the TargetName `SST_IO.Door.Wood`. 
From there, a broadcast output could target `SST_IO.Door` with an include all children call, and every type of door would receive that broadcast. Alternatively, you could choose
to target only `SST_IO.Door.Wood` and only the wooden doors in that level would receive that call
- SST_IO_Transmitters are an example of overriding on a per-actor basis. These Logic Actors already start with the override turned on (if the override doesn't actually get set but is left on, it defaults back to the class tag).
This allows for more direct communication, as well as organization, when dealing with lots of the same types of actors. Call one Transmitter `SST_IO.Logic.Transmitter.BossDeath` for when that 
transmitter needs to be told X boss has died, but you don't accidentally want it to get caught in a Broadcast to all Transmitters. Just make sure you have the `Include Children` boolean unchecked on the broadcast!

</details>

<details>
<summary> Trigger Event </summary>

Trigger Events are what allows an Input to be called from wherever the function is placed in scripting. A `TriggerEvent` is created by going to the SST_IO Component in the Actor BP and going to the `TriggerEvent` 
section. There you can add to the Array of `IO Trigger Events` with whatever you want them to be defined as. This array is also where you can define a tooltip for the event. 
From there, wherever you want to call in your BP scripting the trigger (my suggestion is at the very front or very end of the function/event) you'll call `Trigger Outputs From Event` 
pulling off the SST_IO component. That function will provide you a drop-down list of all the defined `TriggerEvents`. 

>[!WARNING]
>If `TriggerOutputsFromEvent` is not pulling off the owning actor's SST_IO component, the drop-down will not work, and it will default back to a string text box

</details>


<details>
<summary> Target Event </summary>

Target Events are defined in a very similar way to `TriggerEvents`. In the `IO Target Events` array, you'll add a new item to that array. The item in the array will have 2 fields to fill. The same
Tooltip field as the `TriggerEvent`, but instead of a user defined string, the IO Details panel will have a dropdown list of all Custom Events that Actor holds. You will be able to pick from them instead of needing a user string value.
Target Events will then grab all the inputs the Custom Event uses, and allow them to be set as parameters in the IO Editor Tool.

>[!WARNING]
>Target Events that use inputs that are Pass-by-Reference will not be valid as a `TargetEvent`, or if they use inputs that the tool does not support such as delegates or interfaces. 
>
>Target Events do work with Arrays, Sets, and Maps as inputs.
>
>Blueprint functions, overrides like BeginPlay, event dispatchers and timeline functions are excluded from being valid `TargetEvents`, but events inherited from a parent are valid `TargetEvents`

</details>


## SST IO Editor Tools

Once all of an Actors functionality is created, now comes hooking everything up in a level! There are two editor tools that will come into play, the actual `SST IO Editor` and the
`SST IO Data Validator`. 

### SST IO Editor
The SST IO Editor is a simple slate widget that is opened up through the Level Editor's primary `Tools` menu. This is the main tool that an end user will be interfacing with, and where all 
the Outputs are defined. A user will select a compatible SST_IO Actor, and then will `Add Output` to it. This will create a row with a few fields of data to fill in. The first being `Message/Broadcast`, 
then the `TriggerEvent` for the selected actor to trigger the output, then the `TargetActor` (Message) or Tag (Broadcast), what `TargetEvent` to call on the Target, if there should be a `Delay` before the input 
is received on the Target, and the `Max` number of times the Output can fire (0 = Infinite). 

<details>
<summary> Message vs Broadcast </summary>

- Message = Direct Actor -> Actor. Most likely the main form of communication and scripting. You hit Button (A), it will open corresponding Door(A)
- Broadcast = Actor -> Tag. More niche feature, but extremely powerful. You hit Button(A), it will open all doors of a specific type that are loaded (SST_IO.Door.Wood)

</details>

### SST IO Validator 

The data validator is how a user can check the status of all loaded outputs in the current world. The tool will return if an output is unloaded and unable to be checked. 
The validator will check for missing Targets, missing `TargetEvents`, parameters that are invalid, and `TriggerEvents` that no longer work. All of this is collected into a named message log
`SST IO Validation Tool Log` in the message log. From the validation log, you can directly navigate to actors and fix those invalid outputs. 

Alongside checking all outputs, the Data Validation tool will also check to make sure that everything is functioning correctly on the BP side. This includes checking
- Empty or duplicate trigger and target event entries
- Target events that were renamed or deleted
- Trigger pins that are unset, wired, or not declared
- Broadcasts where some or none of the receivers declare the event
- Parameters that are out of date, and soft or hard references that are broken
- Unloaded levels, which it reports as “isn’t loaded, so … weren’t checked”