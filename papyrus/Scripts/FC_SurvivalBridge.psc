Scriptname FC_SurvivalBridge Hidden
{Cooking Skill Overhaul: everything that uses Survival Mode's own script types (Survival_NeedHunger, Survival_NeedCold,
Survival_NeedExhaustion). Kept out of FC_Core and the effect scripts so they still load for a player who never
installed the free Survival Mode Creation (its scripts are then missing; review 2026-11-15). Only called when
Survival Mode runs the need (FC_Menu.NeedOwner = 1).}

; a cooked dish feeds Survival Mode's hunger (FC_HungerEffectScript)
Function FeedHunger(float points) global
	Survival_NeedHunger hunger = Game.GetFormFromFile(0x894, "ccQDRSSE001-SurvivalMode.esl") as Survival_NeedHunger
	if hunger && hunger.IsRunning()
		hunger.DecreaseHungerBuffered(points)
	endif
EndFunction

; Spicy holds off Survival Mode's cold (FC_Core.SpicyHoldCold, owner 1). true while the player is in the cold
bool Function HoldCold(int level) global
	Survival_NeedCold cold = Game.GetFormFromFile(0x896, "ccQDRSSE001-SurvivalMode.esl") as Survival_NeedCold
	if !cold || !cold.IsRunning()
		return false
	endif
	float ceiling = FC_Core.SpicyColdCeiling(level, cold.currentColdLevel)
	if FC_Core.SpicyColdCeiling(0, cold.currentColdLevel) <= ceiling
		; this place can't push the cold past what the spice allows: nothing to hold (no message in mild places)
		FC_Core.SpicyColdRelease()
		return cold.NeedValue.GetValue() >= cold.needStage2Value
	endif
	float now = cold.NeedValue.GetValue()
	if now >= ceiling - 70.0
		if cold.cachedColdLevelMult != 0.0
			cold.cachedColdLevelMult = 0.0
			StorageUtil.SetIntValue(none, "FC_SpiceHold", 1)
			if StorageUtil.GetIntValue(none, "FC_SpiceHoldTold") == 0
				StorageUtil.SetIntValue(none, "FC_SpiceHoldTold", 1)
				Debug.Notification(FC_Menu.Tr("The spice keeps the worst of the cold at bay."))
			endif
			; the first time ever (per character): a one-time help box, which replaced the Spicy loading screen on
			; 2026-11-12 (his rule: no loading-screen messages, teach at the moment it matters)
			if StorageUtil.GetIntValue(none, "FC_SpicyHelpShown") == 0
				StorageUtil.SetIntValue(none, "FC_SpicyHelpShown", 1)
				if FC_Menu.FeatureOn(2)
					Debug.MessageBox(FC_Menu.Tr("A Spicy meal or drink keeps the worst of the cold at bay and protects you from freezing water. The higher the dish's level, the less cold you feel."))
				else
					Debug.MessageBox(FC_Menu.Tr("A Spicy meal or drink keeps the worst of the cold at bay. The higher the dish's level, the less cold you feel."))
				endif
			endif
		endif
		if now > ceiling
			cold.WaitForUnlock()
			cold.DecreaseCold(now - ceiling, ceiling, true)
		endif
	else
		FC_Core.SpicyColdRelease()
	endif
	return now >= cold.needStage2Value
EndFunction

; the cold rises again as Survival Mode sets it (FC_Core.SpicyColdRelease)
Function ReleaseCold() global
	Survival_NeedCold cold = Game.GetFormFromFile(0x896, "ccQDRSSE001-SurvivalMode.esl") as Survival_NeedCold
	GlobalVariable rate = Game.GetFormFromFile(0x81C, "ccQDRSSE001-SurvivalMode.esl") as GlobalVariable  ; Survival_ColdLevelMult
	if cold && rate
		cold.cachedColdLevelMult = rate.GetValue()
	endif
EndFunction

; the dragon drink under Survival Mode: fully warm, tiredness down to Refreshed (FC_Core.DragonWarmth)
Function DragonWarmth() global
	Survival_NeedCold cold = Game.GetFormFromFile(0x896, "ccQDRSSE001-SurvivalMode.esl") as Survival_NeedCold
	if cold && cold.IsRunning()
		cold.WaitForUnlock()
		cold.DecreaseCold(cold.NeedMaxValue.GetValue(), -1.0, true)
	endif
	Survival_NeedExhaustion tired = Game.GetFormFromFile(0x899, "ccQDRSSE001-SurvivalMode.esl") as Survival_NeedExhaustion
	if tired && tired.IsRunning()
		tired.WaitForUnlock()
		float now = tired.NeedValue.GetValue()
		float refreshed = tired.needStage1Value
		if now > refreshed
			float after = tired.DecrementNeed(now, now - refreshed, refreshed)
			tired.ApplyExhaustionStage(after, tired.lastValue, false)
			tired.lastValue = after
		endif
	endif
EndFunction

; camp rest under Survival Mode (FC_Core.GiveBackRest)
Function GiveBackRest(int amount) global
	Survival_NeedExhaustion tired = Game.GetFormFromFile(0x899, "ccQDRSSE001-SurvivalMode.esl") as Survival_NeedExhaustion
	if tired && tired.IsRunning()
		tired.WaitForUnlock()
		float now = tired.NeedValue.GetValue()
		float after = tired.DecrementNeed(now, amount as float)
		if after < now
			; Survival would say "You awaken feeling ..." for the lower stage, as if the player had slept: its stage
			; messages are held back while the stage is applied (an empty entry is skipped by its
			; ApplyNeedStagePlayerEffects), then put back
			Message[] said = tired.needMessagesWhenDecreasing
			Message[] kept = new Message[8]
			int i = 0
			while said != none && i < said.Length && i < 8
				kept[i] = said[i]
				said[i] = none
				i += 1
			endwhile
			; the Rested bonus only with its option on (Survival Mode settings page, his call 2026-11-12) and when Survival
			; itself would give it (not with beast blood or the Doom Lover stone)
			bool rested = FC_Menu.FeatureOn(7) && tired.CanGetRestedBonus(false)
			; the Rested bonus has its own "You awaken feeling Rested." messages (his find 2026-11-12: a drink is no awakening):
			; they point at a blank message (FC_SilentMessage) while the stage is applied, then are put back
			Message silent = Game.GetFormFromFile(0xAA3, "FreeCooking.esp") as Message
			Message m1 = tired.RestedMessage
			Message m2 = tired.WellRestedMessage
			Message m3 = tired.MarriageRestedMessage
			Message m4 = tired.BYOHAdoptionRestedMessageMale
			Message m5 = tired.BYOHAdoptionRestedMessageFemale
			if silent
				tired.RestedMessage = silent
				tired.WellRestedMessage = silent
				tired.MarriageRestedMessage = silent
				tired.BYOHAdoptionRestedMessageMale = silent
				tired.BYOHAdoptionRestedMessageFemale = silent
			endif
			tired.ApplyExhaustionStage(after, tired.lastValue, rested)
			tired.lastValue = after
			if silent
				tired.RestedMessage = m1
				tired.WellRestedMessage = m2
				tired.MarriageRestedMessage = m3
				tired.BYOHAdoptionRestedMessageMale = m4
				tired.BYOHAdoptionRestedMessageFemale = m5
			endif
			i = 0
			while said != none && i < said.Length && i < 8
				said[i] = kept[i]
				i += 1
			endwhile
			Debug.Notification(FC_Menu.Tr("The brewed drink shakes off the stiffness of sleeping on the ground."))
		endif
	endif
EndFunction
