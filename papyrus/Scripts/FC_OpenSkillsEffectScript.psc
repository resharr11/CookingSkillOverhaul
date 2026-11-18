Scriptname FC_OpenSkillsEffectScript extends ActiveMagicEffect
{Cooking Skill Overhaul: the "Cooking Skills" power opens the Cooking skill tree (Custom Skills Framework group "FreeCooking").}

Event OnEffectStart(Actor akTarget, Actor akCaster)
	if akTarget == Game.GetPlayer()
		CustomSkills.OpenCustomSkillMenu("FreeCooking")
	endif
EndEvent
