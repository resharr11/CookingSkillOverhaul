Scriptname Survival_NeedBase extends Quest
{COMPILE-ONLY STAND-IN for Survival Mode's script, so Free Cooking's scripts compile. Never compile this file or ship a
Survival_NeedBase.pex: the game must use Survival Mode's real script. Only what Free Cooking calls is declared here.}

GlobalVariable property NeedValue auto
GlobalVariable property NeedMaxValue auto
float property needStage1Value auto hidden
float property needStage2Value auto hidden
float property needStage3Value auto hidden
float property needStage4Value auto hidden
float property needStage5Value auto hidden
float property lastValue auto hidden
Message[] property needMessagesWhenDecreasing auto

float Function DecrementNeed(float currentNeedValue, float amountToDecrementBy, float customMinValue = -1.0, float customMaxValue = -1.0)
	return 0.0
EndFunction

Function WaitForUnlock()
EndFunction
