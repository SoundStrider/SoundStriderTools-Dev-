# About

---
SoundStriderTools is currently in Beta release. Versions of this plugin from GitHub fall under a PolyForm Noncommercial License 1.0.0 . 
Once SoundStriderTools - Dev - is out of Beta, a commercial license will be available on FAB. 

SoundStriderTools - Dev is a collection of Unreal Engine tools intended for designers to improve workflows and ease development struggels. 
Currently, the only tool included in this package is the Scripting_IO tool (SST_IO) and its related features and tools

## SST_IO

---
Introducing SoundStriderTools Input-Output scripting! This is a feature set that is modeled after
the scripting system present in the Source Engine by Valve&trade; Software. It works by defining on an Actor basis
what that actor is allowed to do. An actor will have defined `Trigger Events` (notify that I have performed an action) 
and `Target Events` (what am I able to do). A designer will then be able to use the Scripting Editor Tool to have an actor (A)
target another actor (B), and when Actor A's `Trigger Event` is hit, Actor B can perform the set `Target Event`. 

### How Does it Work?
SST_IO is fairly simple to get up in running. First a designer will take a Blueprint (BP) Actor and add an SST_IO Actor Component to the BP.
From there they would go into the SST_IO details and begin defining that Actor's `TargetName`, `TriggerEvent`, and `TargetEvent`. 

<details>
<summary> Target Name </summary>

An Actor's `TargetName` is a GameplayTag GUID that is created by the user to give a name to a class of Actor. A GameplayTag category `SST_IO` is automatically created in the `DefaultEngine.ini`,
but if you would like to change the save location of GameplayTags out of `DefaultEngine` make sure that wherever the new location is all tags have a `SST_IO` prefix or 
the tool will not be able to find it.

Target Names can also have overrides on a per-actor or per-class basis. Examples of this would be 
- You have a base BP Actor class called `BP_Door` this is functionally just a parent class of logic and never used in the level, and can be called `SST_IO.Door`. You would define all the logic, trigger, and target events here.
Then on the `BP_Door_Wood` child class that's actually implemented in the level, you can override that class at the BP level to use the TargetName `SST_IO.Door.Wood`. 
From there, a broadcast output (all actors of a class output) could target `SST_IO.Door` with an include all children call, and every type of door would receive that broadcast. Alternatively, you could choose
to target only `SST_IO.Door.Wood` and only the wooden doors in that level would receive that call
- SST_IO_Transmitters are an example of overriding on a per-actor basis. These Logic Actors already start with the override turned on (if the override doesn't actually get set but is left on, it defaults back to the class name).
This allows for more direct communication, as well as organization, when dealing with lots of the same types of actors. Call one Transmitter `SST_IO.Logic.Transmitter.BossDeath` for when that 
transmitter needs to be told X boss has died, but you don't accidentally want it to get caught in a Broadcast to all Transmitters. 

</details>

<details>
<summary> Trigger Event </summary>

Trigger Events are what allows an Input to be called from a player action with an Actor. A `TriggerEvent` is created by going to the SST_IO Component in the Actor BP and going to the `TriggerEvent` 
section. There you can add to the Array of `IO Trigger Events` with whatever you want them to be defined as. This array is also where you can define a tooltip for the event. 
From there, wherever you want to call in your BP scripting the trigger (my suggestion is at the very front or very end of the function/event) you'll call `Trigger Outputs From Event' 
pulling off the SST_IO component. That function will provide you a drop-down list of all the defined 'TriggerEvents'. 

</details>


<details>
<summary> Target Event </summary>

Target Events are defined in a very similar way to `TriggerEvents`. In the `IO Target Events` array, you'll add a new item to that array. The item in the array will have 2 fields to fill. The same
Tooltip field as the `TriggerEvent`, but instead of a user defined string, the IO Tool will automatically find all Custom Events that Actor holds, and will populate them into an available list to pick
from and add as a `TargetEvent`. Target Events will then grab all the inputs the Custom Event uses, and allow them to be set as parameters in the IO Editor Tool.

[!WARNING]
Target Events that contain return values or use inputs that are Pass-by-Reference will not be valid as a `TargetEvent`

</details>