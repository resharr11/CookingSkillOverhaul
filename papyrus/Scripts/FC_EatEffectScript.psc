Scriptname FC_EatEffectScript extends ActiveMagicEffect
{Cooking Skill Overhaul prototype: runs when a cooked dish is eaten. Applies the buff from a static spell, then removes itself,
so no active effect ever points at the dynamically created dish (that crashed save loading).}

Event OnEffectStart(Actor akTarget, Actor akCaster)
	MagicEffect eaten = GetBaseObject()
	int mag = GetMagnitude() as int
	int dur = GetDuration() as int
	FC_Core.ApplyFood(akTarget, eaten, mag, dur)
	; Shared Table: followers near you share it (only when the player eats)
	if akTarget == Game.GetPlayer()
		FC_Core.ShareMeal(eaten, mag, dur)
	endif
	Dispel()
EndEvent
