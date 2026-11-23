Scriptname FC_SunHelmBridge Hidden
{Cooking Skill Overhaul and SunHelm Survival (his call 2026-11-14: a Survival switch on applies Cooking Skill Overhaul's feature to the
survival mod in use). Only SunHelm's own functions are used: its need systems' Decrease... and SetColdLevel, which also
apply its stage effects. Called only while SunHelm runs (FC_Menu.NeedOwner), so this script is never loaded without it.}

; SunHelm's main quest (_SHMainQuest, its script _SunHelmMain holds the four need systems)
_SunHelmMain Function Main() global
	return Game.GetFormFromFile(0x000D61, "SunHelmSurvival.esp") as _SunHelmMain
EndFunction

float Function Rate(int globalId, float fallback) global
	GlobalVariable rate = Game.GetFormFromFile(globalId, "SunHelmSurvival.esp") as GlobalVariable
	if FC_Core.RateNow(rate) > 0.0
		return FC_Core.RateNow(rate)  ; its own rate, also while a dish holds it at 0 (FC_Core.HoldNeeds)
	endif
	return fallback
EndFunction

; hours of hunger a dish takes away (1000 or more: all of it); SunHelm's hunger rises _SHHungerRate (10) a game hour
Function LowerHunger(float hours) global
	_SunHelmMain m = Main()
	if !m || !m.Hunger
		return
	endif
	float points = 100000.0
	if hours < 1000.0
		points = hours * Rate(0x00EAAF, 10.0)
	endif
	m.Hunger.DecreaseHungerLevel(points)
EndFunction

Function LowerThirst(float hours) global
	_SunHelmMain m = Main()
	if !m || !m.Thirst
		return
	endif
	float points = 100000.0
	if hours < 1000.0
		points = hours * Rate(0x05C473, 10.0)
	endif
	m.Thirst.DecreaseThirstLevel(points)
EndFunction

; after a skip: points in SunHelm's own scale (FC_Core.SkipGiveBack; need 0 hunger, 1 thirst). SunHelm's Decrease...Level
; adds 10% (rank 1) or 15% (rank 2) of its Connoisseur (hunger) or Hydrated (thirst) perk, meant for food and drink; a
; give-back is the rise the dish held, so that bonus is taken out first (as SlumberFactor does for rest)
Function GiveBack(int need, float points) global
	_SunHelmMain m = Main()
	if !m
		return
	endif
	if need == 0 && m.Hunger
		m.Hunger.DecreaseHungerLevel(points / PerkFactor(0x863D32))  ; _SH_PerkRank_Connoisseur
	elseif need == 1 && m.Thirst
		m.Thirst.DecreaseThirstLevel(points / PerkFactor(0x863D2F))  ; _SH_PerkRank_Hydrated
	endif
EndFunction

float Function PerkFactor(int rankId) global
	GlobalVariable rank = Game.GetFormFromFile(rankId, "SunHelmSurvival.esp") as GlobalVariable
	int r = 0
	if rank
		r = rank.GetValueInt()  ; a perk rank: a whole number (Codex review 2026-11-22e L2)
	endif
	if r == 1
		return 1.1
	elseif r == 2
		return 1.15
	endif
	return 1.0
EndFunction

; SunHelm's Slumber perk adds 10% (rank 1) or 15% (rank 2) to every rest DecreaseFatigueLevel gives
; (_SH_PerkRank_Slumber 0x863D30): amounts worked out in real points are divided by this first, so they land exactly
float Function SlumberFactor() global
	return PerkFactor(0x863D30)
EndFunction

; camp rest: real tiredness points (the 25% its bedroll held back, Slumber included, worked out by the plugin)
Function LowerTiredness(float points) global
	_SunHelmMain m = Main()
	if m && m.Fatigue && points > 0.0
		m.Fatigue.DecreaseFatigueLevel(points / SlumberFactor())
	endif
EndFunction

; a Dragon Scales drink: fully warm, and tiredness down to Rested (80), not into Well Rested, which stays a reward for sleeping
Function DragonWarmth(bool warm, bool refresh) global
	_SunHelmMain m = Main()
	if !m
		return
	endif
	if warm && m.Cold
		m.Cold.SetColdLevel(0.0)
	endif
	GlobalVariable tired = Game.GetFormFromFile(0x021E3F, "SunHelmSurvival.esp") as GlobalVariable  ; _SHCurrentFatigueLevel
	; (aimed half a point above 80: after the Slumber division SunHelm's float maths could land a hair under 80, its Well
	; Rested line; Codex review 2026-11-16f, L5)
	if refresh && m.Fatigue && tired && tired.GetValue() > 80.5
		m.Fatigue.DecreaseFatigueLevel((tired.GetValue() - 80.5) / SlumberFactor())
	endif
EndFunction

; Spicy: the place counts one stage warmer per Spicy level, so the cold never passes Spicy 1 Freezing, 2 Cold, 3 Chilly,
; 4 Comfortable, and less in milder places. SunHelm's cold stages start at 25/150/250/450/700 (Survival Mode Improved's
; numbers), so the ceiling sits 20 under the next stage, as there.
; Returns true while the player is Chilly or colder.
bool Function HoldCold(int level) global
	GlobalVariable coldNow = Game.GetFormFromFile(0x6A13C5, "SunHelmSurvival.esp") as GlobalVariable  ; _SHCurrentColdLevel
	_SunHelmMain m = Main()
	if !coldNow || !m || !m.Cold
		return false
	endif
	; each Spicy level makes the place count one stage warmer (his call 2026-11-15, as under Survival Mode). The place's
	; stage comes from SunHelm's own place cold (_SHAmbientTemperature; 600 or more = all the way) against its stages
	; 25/150/250/450/700 (Comfortable, Chilly, Cold, Freezing, Frigid). Mild places are left alone.
	int place = 5
	GlobalVariable amb = Game.GetFormFromFile(0x989757, "SunHelmSurvival.esp") as GlobalVariable
	if amb && amb.GetValue() < 600.0
		float a = amb.GetValue()
		place = 0
		if a >= 450.0
			place = 4
		elseif a >= 250.0
			place = 3
		elseif a >= 150.0
			place = 2
		elseif a >= 25.0
			place = 1
		endif
	endif
	int stage = place - level
	if stage < 1
		stage = 1
	endif
	if place <= stage
		return coldNow.GetValue() >= 150.0  ; this place can't push the cold past what the spice allows
	endif
	float nextStart = 700.0
	if stage == 1
		nextStart = 150.0
	elseif stage == 2
		nextStart = 250.0
	elseif stage == 3
		nextStart = 450.0
	endif
	float ceiling = nextStart - 20.0
	float now = coldNow.GetValue()
	if now > ceiling
		m.Cold.SetColdLevel(ceiling)
		FC_Core.SpicyFirstHold()
	endif
	return now >= 150.0
EndFunction
