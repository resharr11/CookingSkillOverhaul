Scriptname FC_SpicyWarmthEffectScript extends ActiveMagicEffect
{Cooking Skill Overhaul, Survival Mode: a Spicy effect is the cold-weather safety net. While it lasts the cold can't get past a
stage (Option C): each level counts as 3 cold levels warmer, floors 1 Freezing, 2 Cold, 3 Chilly, 4 immune. The work is in FC_Core.SpicyHoldCold.}

float property Level = 1.0 auto  ; set on each Spicy effect in FreeCooking.esp

bool ended = false
bool warned = false
bool frostfall = false  ; Frostfall is installed
bool frostWarm = false  ; Spicy's Frostfall food warmth was started (the cold switch was on)
bool hadSoup = false    ; Frostfall's own hot soup was on at the last update

Event OnEffectStart(Actor akTarget, Actor akCaster)
	if akTarget != Game.GetPlayer()
		return
	endif
	if FC_Menu.SurvivalInstalled()  ; Survival Mode or another survival mod's cold (FC_Core.SpicyHoldCold finds which)
		RegisterForSingleUpdate(1.0)
	endif
	frostfall = Game.IsPluginInstalled("Frostfall.esp")
	frostWarm = frostfall && FC_Menu.FeatureOn(1)
	hadSoup = frostfall && FC_Core.FrostfallSoupOn()
	FC_Core.SpicyFrostfallStart()
EndEvent

Event OnUpdate()
	if ended
		return
	endif
	bool inCold = FC_Core.SpicyHoldCold(Level as int)
	if ended
		; the effect finished while this update worked: undo the hold it may have just set again (Codex review 2026-11-16)
		FC_Core.SpicyColdRelease()
		return
	endif
	if frostfall
		; Frostfall keeps one food warmth for its hot soup and for Spicy: it lasts until the later of the two ends (his call
		; 2026-11-16). Its soup ending clears it, so Spicy starts it again; the cold switch turned off ends Spicy's share
		bool soup = FC_Core.FrostfallSoupOn()
		bool wanted = FC_Menu.FeatureOn(1)
		if wanted && (!frostWarm || (hadSoup && !soup))
			FC_Core.SpicyFrostfallStart()
		elseif !wanted && frostWarm && !soup
			FC_Core.SendFrostfallEvent("Frost_SoupEffectStop")
		endif
		frostWarm = wanted
		hadSoup = soup
	endif
	float left = GetDuration() - GetTimeElapsed()
	if inCold && !warned && left <= 30.0
		warned = true
		Debug.Notification(FC_Menu.Tr("The spice's warmth is fading."))
	endif
	if !ended
		RegisterForSingleUpdate(2.0)
	endif
EndEvent

Event OnEffectFinish(Actor akTarget, Actor akCaster)
	ended = true
	if akTarget == Game.GetPlayer()
		FC_Core.SpicyColdRelease()
		FC_Core.SpicyFrostfallStop()
	endif
EndEvent
