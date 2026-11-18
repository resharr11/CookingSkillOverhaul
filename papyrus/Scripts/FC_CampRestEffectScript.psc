Scriptname FC_CampRestEffectScript extends ActiveMagicEffect
{Cooking Skill Overhaul: on every brewed drink while a survival mod is installed (the hidden FC_CampRest). After a night outdoors
it gives back the rest the hard ground took (the plugin keeps the amount; it is given out once, so a second drink adds
nothing). Another mod's thirst is no longer handed on here: the plugin does it when any drink is drunk (2026-11-16).}

Event OnEffectStart(Actor akTarget, Actor akCaster)
	if akTarget != Game.GetPlayer()
		return
	endif
	if FC_Menu.TakeForbiddenRefund(2)
		return  ; a Namira drink drunk without the ring went back in the bag: the rest stays owed (his call 2026-11-16)
	endif
	FC_Core.GiveBackRest(FC_Menu.TakeCampRest())  ; 0 with the camp rest switch off
	; (another mod's thirst: the plugin now hands each drink's thirst on itself when it is drunk, so drinks cooked
	; without this effect quench too; his call 2026-11-16)
EndEvent
