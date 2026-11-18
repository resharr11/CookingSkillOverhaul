Scriptname FC_LastSeedBridge Hidden
{Cooking Skill Overhaul and Last Seed (his call 2026-11-14: a Survival switch on applies Cooking Skill Overhaul's feature to the survival
mod in use). Only Last Seed's own API is used (LastSeedAPI's need systems: DecreaseAttribute, which also applies its
stage effects). Called only while Last Seed runs (FC_Menu.NeedOwner), so this script is never loaded without it.}

LastSeedAPI Function Api() global
	return Game.GetFormFromFile(0x00B16A, "LastSeed.esp") as LastSeedAPI  ; LastSeedSystem
EndFunction

; Last Seed's needs rise its rate (1.25) every half game hour; 1000 or more: all of it
float Function Points(float hours, int rateId) global
	if hours >= 1000.0
		return 1000.0
	endif
	float rate = 1.25
	GlobalVariable g = Game.GetFormFromFile(rateId, "LastSeed.esp") as GlobalVariable
	if g && g.GetValue() > 0.0
		rate = g.GetValue()
	endif
	return hours * 2.0 * rate
EndFunction

Function LowerHunger(float hours) global
	LastSeedAPI api = Api()
	if api && api.Hunger
		api.Hunger.DecreaseAttribute(Points(hours, 0x0029CB))  ; _Seed_HungerRate
	endif
EndFunction

Function LowerThirst(float hours) global
	LastSeedAPI api = Api()
	if api && api.Thirst
		api.Thirst.DecreaseAttribute(Points(hours, 0x0029D3))  ; _Seed_ThirstRate
	endif
EndFunction

; camp rest: points in Last Seed's scale (its outdoor floor, worked out by the plugin)
Function LowerTiredness(float points) global
	LastSeedAPI api = Api()
	if api && api.Fatigue && points > 0.0
		api.Fatigue.DecreaseAttribute(points)
	endif
EndFunction

; a Dragon Scales drink: tiredness down to the end of Rested (20), not into it (Last Seed has no cold of its own)
Function DragonRefresh() global
	LastSeedAPI api = Api()
	if api && api.Fatigue
		api.Fatigue.DecreaseAttribute(120.0, 20.0)
	endif
EndFunction
