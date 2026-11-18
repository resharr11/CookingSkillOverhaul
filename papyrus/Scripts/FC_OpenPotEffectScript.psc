Scriptname FC_OpenPotEffectScript extends ActiveMagicEffect
{Cooking Skill Overhaul: cooking pots, spits and the test power open the cooking screen.}

Event OnEffectStart(Actor akTarget, Actor akCaster)
	if akTarget == Game.GetPlayer()
		FC_Core.OpenMenu()
	endif
EndEvent
