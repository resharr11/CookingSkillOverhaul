Scriptname Survival_NeedExhaustion extends Survival_NeedBase
{COMPILE-ONLY STAND-IN for Survival Mode's script, so Free Cooking's scripts compile. Never compile this file or ship a
Survival_NeedExhaustion.pex: the game must use Survival Mode's real script.}

; the Rested bonus messages (Free Cooking points them at a blank message while a brewed drink applies the stage)
Message property RestedMessage auto
Message property WellRestedMessage auto
Message property MarriageRestedMessage auto
Message property BYOHAdoptionRestedMessageMale auto
Message property BYOHAdoptionRestedMessageFemale auto

Function ApplyExhaustionStage(float newExhaustionValue, float oldExhaustionValue, bool canGetRestedBonus)
EndFunction

bool Function CanGetRestedBonus(bool showMessages)
	return false
EndFunction
