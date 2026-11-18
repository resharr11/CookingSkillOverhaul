Scriptname Survival_NeedCold extends Survival_NeedBase
{COMPILE-ONLY STAND-IN for Survival Mode's script, so Free Cooking's scripts compile. Never compile this file or ship a
Survival_NeedCold.pex: the game must use Survival Mode's real script.}

float property cachedColdLevelMult auto hidden
int property currentColdLevel auto hidden   ; the area's cold level (region + night + weather), set on each update

Function DecreaseCold(float amount, float customMinValue = -1.0, bool suppressWarmUpMessage = false)
EndFunction

Function IncreaseCold(float amount, float customMaxValue = -1.0)
EndFunction
