Scriptname FC_Menu Hidden
{Cooking Skill Overhaul: the cooking screen (SKSE plugin). Test from the console: cgf "FC_Core.OpenMenu"}

; opens the cooking screen; known = indices of the discovered dishes
Function Open(int[] known) global native

; refreshes the open cooking screen after cooking
Function Refresh(int[] known) global native

; remembers the discovered dishes before the player sits at a pot (the screen opens with the pot's crafting menu)
Function SetKnown(int[] known) global native
Function SetTasted(int[] formIds) global native
Function Cooked(string message, bool made) global native
; the cook was refused (one still runs): the screen says so and keeps its pot
Function CookBusy(string message) global native

; ID self-test: checks every item and dish the rules name, then sends the items to FC_Core.IdTestScripts (mod event FC_IdTest)
Function IdTest() global native

; tells the recipe book a dish was cooked (its Most used and Recent orders)
Function DishCooked(int dish) global native

; a brew was set to ferment: the young one (given to you) and the finished one it becomes at 6 AM (form IDs); then it
; ages a star a morning (the plugin makes the aged steps). cat: its flavour ("" for no seasoning: it doesn't age); name:
; its seasoned name; stars: the finished brew's stars; crit: a critical cook (one level above its stars)
; returns the young dish to give: young, or a young dish of its own when an earlier batch made the same young dish but
; matures into another finished one
int Function YoungBrew(int young, int finished, string cat, string name, int stars, bool crit, bool house, bool water) global native

; fills in one of FreeCooking.esp's blank dishes (or finds the one already made the same way) and returns its form ID;
; 0 when every blank dish in this game is used. effects: form IDs (0: none), with their magnitudes and durations;
; recipe: the recipe's number (its needs setting goes with the dish)
int Function MakeDish(int base, string name, int value, int[] effects, float[] magnitudes, int[] durations, int recipe) global native

; Shared Table: the followers who share the meal the player just ate (taken by the plugin when it was eaten):
; each follower's form ID, then the effect best for them (0 Mighty, 1 Brainy, 2 Hearty). effect: the eat effect that is
; sharing (its own meal's list, even when more food was eaten since). One entry (-1): that meal had nobody at the
; table. Empty: not taken yet (ask again); last true on the final try gives the last list taken instead
int[] Function SharedTableFollowers(int effect, bool last) global native

; Shared Table: the follower (form ID) eats (drink 0) or drinks (drink 1) with you for a few seconds, when free for it
Function ShareIdle(int follower, int drink) global native

; which dish a pot makes (FC_Core.Recalc): keys = the rules' key of each pot entry, one per copy (the first count are
; used). Returns the dish index (-1: none), 1 if an insect egg in its egg slot needs a drink (else 0), then the entries
; its recipe uses
int[] Function ChooseDish(string[] keys, int count) global native

; a dish's duration (seconds) with the player's Fortify Cooking; dragon 1 for a dragon dish's half day
int Function FortifyDuration(int dur, int dragon) global native

; camp rest: the rest the last night outdoors didn't give, in tiredness points; given out once (the next call returns 0)
int Function TakeCampRest() global native

; rest a drink already took that is waiting to be given (whole points), once: 0 when none (Codex review 2026-11-16f, M3)
int[] Function TakeCampRestPay() global native
; iNeed's rest after a night in one of its bedrolls, and its stomach rot factor (FC_INeedBridge.ReportRest)
Function INeedRestNow(float fatigue, float stomach, int night) global native

; true once for each Namira dish just eaten without the ring (it went back in the bag) that carried this effect (kind:
; 0 hunger, 1 dragon warmth, 2 camp rest): that effect does nothing
bool Function TakeForbiddenRefund(int kind) global native

; water used in a brew, given back the way its mod does (SunHelm's Waterskin 3/3 -> 2/3 -> 1/3 -> empty): the form ID
; to give back, 0 for nothing
int Function EmptierWater(int formId) global native

; Survival Mode is on (Survival_ModeEnabled; false without Survival Mode)
bool Function SurvivalOn() global native

; which mod runs a need right now (need: 0 hunger, 1 thirst, 2 tiredness, 3 cold): 0 none, 1 Survival Mode,
; 2 Survival Mode Improved (and Starfrost), 3 SunHelm, 4 Last Seed, 5 iNeed, 6 Realistic Needs and Diseases, 7 Frostfall
int Function NeedOwner(int need) global native

; Survival Mode or another survival mod is installed (cooking then puts the hunger, camp rest and dragon effects on dishes)
bool Function SurvivalInstalled() global native

; the game hours the last brewed drink quenches in another mod's thirst (given out once; 1000: fully)
float Function TakeOtherThirst() global native
; the hold (FC_Core.HoldRate / ReleaseRate): sets a needs mod's rate global, also a constant one like Survival Mode's
; Survival_HungerNeedRate, which GlobalVariable.SetValue refuses
Function SetRate(GlobalVariable rate, float value) global native
; the hold and skips (sleeping, waiting, travel): FC_Core.HoldRate / ReleaseRate tell the plugin which rates are held and
; their own values, so it can give them back the moment a skip starts
Function NoteHeld(GlobalVariable rate, float own) global native
Function NoteReleased(GlobalVariable rate) global native
; iNeed's bridge after a skip: the share of the loss the dish covered (need 0 hunger, 1 thirst; skip: the number its
; request carried; -1: that skip isn't being settled any more, give nothing back now), and done
float Function SkipShare(int need, int skip) global native
Function SkipDone(int need, int skip) global native
; iNeed's bridge: a skip of this need is being counted (a plain hold must not give its loss back)
bool Function SkipPending(int need) global native
; iNeed's bridge: game days when the newest skip ended (-1: still running, or an old skip number); it settles once
; iNeed's own clock (_SNQuest.TimeUpdate) has reached it
float Function SkipEndDay(int need, int skip) global native
; the number of the game running now (changes at every load): the pot script's hold runs from before a load are stale
int Function HoldSession() global native

; a Cooking Skill Overhaul Survival feature's switch on the Survival Mode settings page (only the switch; ask SurvivalOn too):
; 0 cooked food eases hunger, 1 Spicy holds off the cold, 2 Spicy protects from freezing water, 3 brewed drinks make up
; for sleeping outdoors, 4 penalties in steps, 5 Hearty heals over time, 6 dragon drinks warm and refresh, 7 the camp
; rest drink can also leave you Rested, 8 thirst
bool Function FeatureOn(int feature) global native

; the pot rules (data.json, plus what the modded-foods patcher added at load). Keys are not case-sensitive.
int Function RuleInt(string ruleKey) global native
string Function RuleText(string ruleKey) global native
; player-facing text in the player's language (Interface/Translations/FreeCooking_<LANGUAGE>.txt): the key is the whole
; English sentence; TrF fills {0} {1} {2}, in whatever order the translation puts them
string Function Tr(string english) global native
string Function TrF(string english, string a0, string a1 = "", string a2 = "") global native
bool Function HasRuleText(string ruleKey) global native

; the key a discovered dish is saved under: its index for Cooking Skill Overhaul's own dishes (as before), "m:<plugin>|<local ID>"
; for a dish the patcher added, so adding or removing a food mod never mixes up which dishes were discovered
string Function DishSaveKey(int dish) global native

; the pot rules' key for an item: "<plugin without extension>_<local ID in decimal>" (the extension stays when an
; earlier plugin has the same name)
string Function ItemKey(int formId) global native

; a potion's flavour as a pot seasoning ("" = not one), by the plugin's rule (the Potions page included)
string Function PotionFlavourOf(int formId) global native

; a dish's name as cooked: a general dish (Meat Stew, Fish Pie) is named after the main ingredient the pot's last
; choice used ("Goat Stew"); cat: the flavour ("Mighty Goat Stew"), "" for the plain name
string Function DishName(int dish, string cat) global native
