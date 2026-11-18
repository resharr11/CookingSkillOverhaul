Scriptname FC_INeedBridge Hidden
{Cooking Skill Overhaul and iNeed (his call 2026-11-14: a Survival switch on applies Cooking Skill Overhaul's feature to the survival mod in
use). Only iNeed's own functions are used (_SNQuestScript ModHunger / ModThirst / ModFatigue, which also apply its
stage effects; its needs are 0..110, higher is better). Called only while iNeed runs (FC_Menu.NeedOwner).}

_SNQuestScript Function Main() global
	return Game.GetFormFromFile(0x000D62, "iNeed.esp") as _SNQuestScript  ; _SNQuest
EndFunction

; iNeed's needs fall its own rate a game hour; 1000 or more: all the way back
Function LowerHunger(float hours) global
	_SNQuestScript q = Main()
	if !q || q.HungerRate <= 0.0
		return
	endif
	float points = 110.0
	if hours < 1000.0
		points = hours * q.HungerRate
	endif
	q.ModHunger(points)
EndFunction

Function LowerThirst(float hours) global
	_SNQuestScript q = Main()
	if !q || q.ThirstRate <= 0.0
		return
	endif
	float points = 110.0
	if hours < 1000.0
		points = hours * q.ThirstRate
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
