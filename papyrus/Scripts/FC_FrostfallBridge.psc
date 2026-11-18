Scriptname FC_FrostfallBridge Hidden
{Cooking Skill Overhaul and Frostfall (his call 2026-11-14: Spicy keeps real cold protection with every survival mod). Only
Frostfall's public API is used (FrostUtil: GetPlayerExposure, ModPlayerExposure). Exposure is 0..120 and rises as you
get colder, in levels of 20: Warm, Comfortable, Chilly, Very Cold, Freezing, Freezing to Death. Called only while
Frostfall runs (FC_Menu.NeedOwner).}

; Spicy: the place counts one level warmer per Spicy level, so exposure never passes Spicy 1 Freezing, 2 Very Cold,
; 3 Chilly, 4 Comfortable, and less in milder places; held 2 under the next level. Returns true while the player is Chilly or colder.
bool Function HoldCold(int level) global
	; each Spicy level makes the place count one stage (20 exposure) warmer (his call 2026-11-15, as under Survival
	; Mode). The place's stage comes from Frostfall's own exposure target (_Frost_ExposureTarget), which already counts
	; fire, shelter, weather and clothes. With "Spicy protects from freezing water" on, its cold water doesn't run
	; at all while Spicy lasts (the potion keyword, Cooking/OtherMods.inl). Mild places are left alone.
	int place = 5
	GlobalVariable target = Game.GetFormFromFile(0x0805E4, "Frostfall.esp") as GlobalVariable
	if target
		place = (target.GetValue() / 20.0) as int
		if place > 5
			place = 5
		elseif place < 0
			place = 0
		endif
	endif
	int stage = place - level
	if stage < 1
		stage = 1
	endif
	if place <= stage
		return FrostUtil.GetPlayerExposure() >= 40.0  ; this place can't push the cold past what the spice allows
	endif
	float ceiling = 20.0 * (stage + 1) - 2.0
	float now = FrostUtil.GetPlayerExposure()
	if now > ceiling
		FrostUtil.ModPlayerExposure(ceiling - now, ceiling)
		FC_Core.SpicyFirstHold()
	endif
	return now >= 40.0
EndFunction

; a Dragon Scales drink: fully warm
Function WarmFully() global
	FrostUtil.ModPlayerExposure(-120.0, 0.0)
EndFunction
