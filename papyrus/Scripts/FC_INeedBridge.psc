Scriptname FC_INeedBridge Hidden
{Cooking Skill Overhaul and iNeed (his call 2026-11-14: a Survival switch on applies Cooking Skill Overhaul's feature to the survival mod in
use). Only iNeed's own functions are used (_SNQuestScript ModHunger / ModThirst / ModFatigue, which also apply its
stage effects; its needs are 0..110, higher is better). Called only while iNeed runs (FC_Menu.NeedOwner).}

_SNQuestScript Function Main() global
	return Game.GetFormFromFile(0x000D62, "iNeed.esp") as _SNQuestScript  ; _SNQuest
EndFunction

; the hold (FC_Core.HoldNeeds; his OK 2026-11-22 21:41): iNeed's rates are not touched, because its sleep and carriage
; changes don't use them and its menu copies them into its own settings at every load (Codex review 2026-11-22d M1, M4).
; The needs' values are held instead: when the hold starts each value is noted on the quest in PapyrusUtil storage; on
; every request (on change and every 10 s) a value that got worse (lower: iNeed's needs are 0..110, higher is better) is
; given back through iNeed's own ModHunger / ModThirst, and a value that got better (the player ate or drank) becomes
; the new mark. This also takes in iNeed's own random +-1 an hour. "FC_INeedHeld" says something is held.
; Sleeping, waiting and travel (his design 2026-11-22): while the plugin flags a skip (skipHunger / skipThirst), nothing is
; given back and the mark stays; once iNeed has counted the skipped hours (its value has stopped changing between two
; requests, 2 s apart) or on the last call (skipFinal), the share of the loss the dish covered (FC_Menu.SkipShare) is
; given back, the mark moves to the value now, and the plugin is told (FC_Menu.SkipDone). The skip's number (skipId)
; goes with both, so a request from before a newer skip can't settle it (Codex review 2026-11-22e P3).
; A plain request never gives a skip's loss back (Codex review 2026-11-22e P2, Claude L1/L2): not while the plugin
; counts a skip of that need (FC_Menu.SkipPending: a plain request that was already running when the skip started), and
; not after a skip that ended without its give-back (the 60 s limit, or a load in the middle): then the mark moves to
; the value now, as SkipSettle would with nothing covered.
Function Hold(bool hunger, bool thirst, bool skipHunger = false, bool skipThirst = false, bool skipFinal = false, int skipId = 0) global
	_SNQuestScript q = Main()
	if !q
		StorageUtil.UnsetIntValue(none, "FC_INeedHeld")
		return
	endif
	; a rate zeroed by the first hold build (test builds of 2026-11-22 only) is given back once
	if StorageUtil.HasFloatValue(q, "FC_HeldHungerRate")
		if q.HungerRate == 0.0
			q.HungerRate = StorageUtil.GetFloatValue(q, "FC_HeldHungerRate")
		endif
		StorageUtil.UnsetFloatValue(q, "FC_HeldHungerRate")
	endif
	if StorageUtil.HasFloatValue(q, "FC_HeldThirstRate")
		if q.ThirstRate == 0.0
			q.ThirstRate = StorageUtil.GetFloatValue(q, "FC_HeldThirstRate")
		endif
		StorageUtil.UnsetFloatValue(q, "FC_HeldThirstRate")
	endif
	if hunger
		float mark = q.HungerState
		if StorageUtil.HasFloatValue(q, "FC_HeldHunger")
			mark = StorageUtil.GetFloatValue(q, "FC_HeldHunger")
			if skipHunger
				mark = SkipSettle(q, 0, mark, skipFinal, skipId)
			elseif FC_Menu.SkipPending(0)
				; a skip is being counted: keep the mark, its own request settles it
			elseif StorageUtil.HasFloatValue(q, "FC_SkipSeenHunger")
				ClearSeen(q, "FC_SkipSeenHunger")
				mark = q.HungerState
			elseif q.HungerState < mark
				q.ModHunger(mark - q.HungerState)
			endif
		endif
		if q.HungerState > mark
			mark = q.HungerState
		endif
		StorageUtil.SetFloatValue(q, "FC_HeldHunger", mark)
	else
		StorageUtil.UnsetFloatValue(q, "FC_HeldHunger")
		ClearSeen(q, "FC_SkipSeenHunger")
	endif
	if thirst
		float markT = q.ThirstState
		if StorageUtil.HasFloatValue(q, "FC_HeldThirst")
			markT = StorageUtil.GetFloatValue(q, "FC_HeldThirst")
			if skipThirst
				markT = SkipSettle(q, 1, markT, skipFinal, skipId)
			elseif FC_Menu.SkipPending(1)
				; a skip is being counted: keep the mark, its own request settles it
			elseif StorageUtil.HasFloatValue(q, "FC_SkipSeenThirst")
				ClearSeen(q, "FC_SkipSeenThirst")
				markT = q.ThirstState
			elseif q.ThirstState < markT
				q.ModThirst(markT - q.ThirstState)
			endif
		endif
		if q.ThirstState > markT
			markT = q.ThirstState
		endif
		StorageUtil.SetFloatValue(q, "FC_HeldThirst", markT)
	else
		StorageUtil.UnsetFloatValue(q, "FC_HeldThirst")
		ClearSeen(q, "FC_SkipSeenThirst")
	endif
	if hunger || thirst
		StorageUtil.SetIntValue(none, "FC_INeedHeld", 1)
	else
		StorageUtil.UnsetIntValue(none, "FC_INeedHeld")
	endif
EndFunction

; one need after a skip (Hold): the mark to keep. need 0 hunger, 1 thirst. It settles once iNeed has counted every skip
; the give-back spans: iNeed's own clock (_SNQuest.TimeUpdate, stamped at the start of each count, _snneeds.psc) has
; reached the newest skip's end (FC_Menu.SkipEndDay), and its value then stayed the same for one more request (2 s), so
; the count has landed ("unchanged" allows 0.01). His pick 2026-11-23 03:06: the earlier value-only rules settled too
; early on skips back to back (test 22h). The seen value is kept with its skip number ("...Skip"); a skip with no game
; time, which iNeed doesn't count, settles on the 30 s last call.
float Function SkipSettle(_SNQuestScript q, int need, float mark, bool isLast, int skipId) global
	string seenKey = "FC_SkipSeenHunger"
	if need == 1
		seenKey = "FC_SkipSeenThirst"
	endif
	float endDay = FC_Menu.SkipEndDay(need, skipId)
	if endDay < 0.0
		return mark  ; still running, or a newer skip started: the next request settles it
	endif
	; the clock is read before the value, and a value is only kept from a look made after the clock counted: iNeed
	; stamps the clock first and writes the drop some calls later (review 22h F1), so two looks after the stamp with
	; the same value are needed
	bool counted = q.TimeUpdate >= endDay - 0.001  ; (0.001 day: under 1.5 game minutes)
	float now = q.HungerState
	if need == 1
		now = q.ThirstState
	endif
	float loss = mark - now
	bool still = false
	if counted && StorageUtil.GetIntValue(q, seenKey + "After") == 1 && StorageUtil.GetIntValue(q, seenKey + "Skip") == skipId && StorageUtil.HasFloatValue(q, seenKey)
		float seen = StorageUtil.GetFloatValue(q, seenKey)
		still = seen - now < 0.01 && now - seen < 0.01
	endif
	if !isLast && !(counted && still)
		StorageUtil.SetIntValue(q, seenKey + "Skip", skipId)
		StorageUtil.SetFloatValue(q, seenKey, now)  ; iNeed may still be counting: look again next time
		StorageUtil.SetIntValue(q, seenKey + "After", counted as int)  ; only a value seen after the stamp counts
		return mark
	endif
	float share = FC_Menu.SkipShare(need, skipId)
	if share < 0.0
		return mark  ; a newer skip started (or this one ended): the next request settles it
	endif
	ClearSeen(q, seenKey)
	if loss > 0.0
		float give = loss * share
		if need == 0
			q.ModHunger(give)
			now = q.HungerState
		else
			q.ModThirst(give)
			now = q.ThirstState
		endif
	endif
	FC_Menu.SkipDone(need, skipId)
	return now
EndFunction

; the skip watch of one need, all three keys
Function ClearSeen(_SNQuestScript q, string seenKey) global
	StorageUtil.UnsetFloatValue(q, seenKey)
	StorageUtil.UnsetFloatValue(q, seenKey + "Base")
	StorageUtil.UnsetIntValue(q, seenKey + "Skip")
	StorageUtil.UnsetIntValue(q, seenKey + "After")
EndFunction

; iNeed's rates as its menu set them, also while a hold keeps them at 0
float Function HungerRateNow(_SNQuestScript q) global
	if !q
		return 0.0
	endif
	if q.HungerRate == 0.0 && StorageUtil.HasFloatValue(q, "FC_HeldHungerRate")
		return StorageUtil.GetFloatValue(q, "FC_HeldHungerRate")
	endif
	return q.HungerRate
EndFunction

float Function ThirstRateNow(_SNQuestScript q) global
	if !q
		return 0.0
	endif
	if q.ThirstRate == 0.0 && StorageUtil.HasFloatValue(q, "FC_HeldThirstRate")
		return StorageUtil.GetFloatValue(q, "FC_HeldThirstRate")
	endif
	return q.ThirstRate
EndFunction

; iNeed's needs fall its own rate a game hour; 1000 or more: all the way back
Function LowerHunger(float hours) global
	_SNQuestScript q = Main()
	float rate = HungerRateNow(q)
	if !q || rate <= 0.0
		return
	endif
	float points = 110.0
	if hours < 1000.0
		points = hours * rate
	endif
	q.ModHunger(points)
EndFunction

Function LowerThirst(float hours) global
	_SNQuestScript q = Main()
	float rate = ThirstRateNow(q)
	if !q || rate <= 0.0
		return
	endif
	float points = 110.0
	if hours < 1000.0
		points = hours * rate
	endif
	q.ModThirst(points)
EndFunction

; camp rest: a few seconds after a night in one of iNeed's bedrolls, the plugin asks (mod event FC_INeedWoke, the
; night's number) for iNeed's rest after the night and its stomach rot factor (_SNNeeds.StomachRotCheck), so the payout
; is what the bedroll really held back: iNeed caps rest at 110, so a night that filled it lost nothing (Codex review
; 2026-11-16c). Read here, never by the plugin (his rule: C++ doesn't read scripts)
Function ReportRest(int night) global
	_SNQuestScript q = Main()
	if !q
		return
	endif
	float stomach = 1.0
	int salmonella = q.HasSalmonella
	if salmonella == 1
		stomach = 0.9
	elseif salmonella == 2
		stomach = 0.8
	elseif salmonella == 3
		stomach = 0.6
	elseif salmonella == 4
		stomach = 0.2
	endif
	; a vampire under iNeed's vampire option, or a werewolf: iNeed rests them its own way, with no bedroll cut
	; (_SNNeeds), so nothing was held back: reported as no rest at all (stomach 0), the plugin then owes nothing
	if q._SNVampWereToggle && q._SNVampWereToggle.GetValue() != 0.0 && q.Vampire && Game.GetPlayer().HasKeyword(q.Vampire)
		stomach = 0.0
	elseif q.IsWerewolf
		stomach = 0.0
	endif
	FC_Menu.INeedRestNow(q.FatigueState, stomach, night)
EndFunction

; camp rest: points in iNeed's scale (the 15% its bedroll held back, worked out by the plugin)
Function LowerTiredness(float points) global
	_SNQuestScript q = Main()
	if q && q.FatigueRate > 0.0 && points > 0.0
		q.ModFatigue(points)
	endif
EndFunction

; a Dragon Scales drink: up to Rested (95), not into Well Rested (iNeed has no cold of its own)
Function DragonRefresh() global
	_SNQuestScript q = Main()
	if q && q.FatigueRate > 0.0 && q.FatigueState < 95.0
		q.ModFatigue(95.0 - q.FatigueState)
	endif
EndFunction
