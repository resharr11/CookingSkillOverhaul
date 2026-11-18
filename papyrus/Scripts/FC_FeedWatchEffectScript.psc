Scriptname FC_FeedWatchEffectScript extends ActiveMagicEffect
{Cooking Skill Overhaul: feeding bonuses, 30 minutes from the last feed. A vampire biting a person gains Mighty and Brainy. A werewolf feeding gains Mighty.}

Actor player
MagicEffect wolfFeed
Spell wolfBonus
Spell vampireBonus

Event OnEffectStart(Actor akTarget, Actor akCaster)
	player = akTarget
	; WerewolfFeedRestoreHealth: vanilla casts it on the player each time a werewolf feeds (PlayerWerewolfChangeScript.Feed)
	wolfFeed = Game.GetFormFromFile(0x0EC356, "Skyrim.esm") as MagicEffect
	wolfBonus = Game.GetFormFromFile(0x976, "FreeCooking.esp") as Spell
	vampireBonus = Game.GetFormFromFile(0x977, "FreeCooking.esp") as Spell
EndEvent

; vanilla sends it when the player bites a person (Actor.StartVampireFeed); blood potions do not count
Event OnVampireFeed(Actor akTarget)
	if vampireBonus
		vampireBonus.Cast(player, player)
		Debug.Notification(FC_Menu.Tr("Fresh blood makes you Mighty and Brainy."))
	endif
EndEvent

Event OnMagicEffectApply(ObjectReference akCaster, MagicEffect akEffect)
	if akEffect == wolfFeed && wolfBonus
		wolfBonus.Cast(player, player)
		Debug.Notification(FC_Menu.Tr("The feast makes you Mighty."))
	endif
EndEvent
