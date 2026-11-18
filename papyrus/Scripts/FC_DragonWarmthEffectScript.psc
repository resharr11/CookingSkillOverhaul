Scriptname FC_DragonWarmthEffectScript extends ActiveMagicEffect
{Cooking Skill Overhaul: a Dragon Scales drink (Dragonfeast) fully warms you and refreshes you, through Survival Mode's own needs.}

Event OnEffectStart(Actor akTarget, Actor akCaster)
	if akTarget != Game.GetPlayer() || !FC_Menu.SurvivalInstalled()
		return
	endif
	if FC_Menu.TakeForbiddenRefund(1)
		return  ; a Namira drink drunk without the ring went back in the bag: it does nothing (his call 2026-11-16)
	endif
	FC_Core.DragonWarmth()
EndEvent
