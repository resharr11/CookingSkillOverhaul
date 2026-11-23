Scriptname _SNQuestScript extends Quest
; Compile-only stand-in for another mod's script (Free Cooking never ships or compiles it): only what Free Cooking calls. iNeed.
Float Property HungerRate = 10.5 Auto
Float Property FatigueRate = 5.0 Auto
Float Property ThirstRate = 13.0 Auto
Float Property FatigueState = 100.0 Auto
Float Property HungerState = 75.0 Auto
Float Property ThirstState = 75.0 Auto
Float Property TimeUpdate Auto
Int Property HasSalmonella = 0 Auto
Keyword Property Vampire Auto
GlobalVariable Property _SNVampWereToggle Auto
Bool Property IsWerewolf Auto
Function ModHunger(Float HungerDelta)
EndFunction
Function ModFatigue(Float FatigueDelta)
EndFunction
Function ModThirst(Float ThirstDelta)
EndFunction
