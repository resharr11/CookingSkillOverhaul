Scriptname FC_HungerEffectScript extends ActiveMagicEffect
{Cooking Skill Overhaul: a cooked dish eases Survival Mode hunger by Amount (hunger points), through Survival's own hunger.}

; Set on each hunger effect in FreeCooking.esp (FC_Hunger40 ... FC_HungerFull). The effect has no duration, so it has
; ended by the time this runs and GetMagnitude has nothing to read: the amount has to come from the property.
float property Amount = 0.0 auto

Event OnEffectStart(Actor akTarget, Actor akCaster)
	if akTarget != Game.GetPlayer()
		return
	endif
	if FC_Menu.TakeForbiddenRefund(0)
		return  ; a Namira dish eaten without the ring went back in the bag: it feeds nothing (review 2026-11-15)
	endif
	float points = Amount
	if points <= 0.0
		points = 240.0  ; the old single hunger effect (dishes cooked before the steps): a middling meal
	endif
	; who runs hunger (FC_Menu.NeedOwner 0): 1 Survival Mode, 2 Survival Mode Improved, 3-6 SunHelm, Last Seed, iNeed,
	; Realistic Needs (his call 2026-11-14: the switch on feeds whichever mod is in use)
	int owner = FC_Menu.NeedOwner(0)
	if owner >= 3
		FC_Core.OtherHunger(owner, points)
		return
	elseif owner == 0
		return
	endif
	if owner == 2
		; Survival Mode Improved removes Survival's hunger script and feeds through its own API (2026-11-14: before this, our
		; dishes eased no hunger at all under it)
		; our points are vanilla Survival's (30 a game hour: buff + 5 h); Survival Mode Improved's hunger rises
		; Survival_HungerNeedRate a game hour (10 in its own esp), so the same hours are fewer points there
		GlobalVariable rate = Game.GetFormFromFile(0x829, "ccQDRSSE001-SurvivalMode.esl") as GlobalVariable
		float perHour = FC_Core.RateNow(rate)  ; its own rate, also while a dish holds it at 0 (FC_Core.HoldNeeds)
		if perHour > 0.0 && points < 1000.0
			points = points / 30.0 * perHour
		endif
		SurvivalModeImprovedApi.RestoreHungerLevel(points)
		return
	endif
	FC_SurvivalBridge.FeedHunger(points)  ; owner 1: Survival Mode runs hunger (its own script types live in the bridge)
EndEvent
