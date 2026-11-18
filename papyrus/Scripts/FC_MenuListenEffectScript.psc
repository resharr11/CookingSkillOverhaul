Scriptname FC_MenuListenEffectScript extends ActiveMagicEffect
{Cooking Skill Overhaul: the plugin casts this hidden spell on the player at every game load and every time the cooking screen
opens, so the pot's script always hears the screen's Cook, Season in pot and Taste requests (new games included).}

Event OnEffectStart(Actor akTarget, Actor akCaster)
	if akTarget == Game.GetPlayer()
		FC_Core.GrantPrototypeBits()
		(FC_Core.GetPot() as FC_PotContainerScript).ListenToMenu()
	endif
EndEvent
