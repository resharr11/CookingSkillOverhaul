Scriptname FC_PotContainerScript extends ObjectReference
{Cooking Skill Overhaul prototype: attached to the hidden pot container.
Adding several items fires several events at once; recalculations are serialised so the last one always sees the final pot.}

bool busy = false
bool dirty = false

Function RequestRecalc()
	dirty = true
	if busy
		return
	endif
	busy = true
	while dirty
		dirty = false
		FC_Core.Recalc(self)
	endwhile
	busy = false
EndFunction

Event OnItemAdded(Form akBaseItem, int aiItemCount, ObjectReference akItemReference, ObjectReference akSourceContainer)
	if StorageUtil.GetIntValue(none, "FC_Busy") == 0
		RequestRecalc()
	endif
EndEvent

Event OnItemRemoved(Form akBaseItem, int aiItemCount, ObjectReference akItemReference, ObjectReference akDestContainer)
	if StorageUtil.GetIntValue(none, "FC_Busy") != 0
		return
	endif
	if akBaseItem == FC_Core.PreviewForm() && akDestContainer == Game.GetPlayer()
		while busy
			Utility.WaitMenuMode(0.05)
		endwhile
		FC_Core.Cook(self)
	else
		RequestRecalc()
	endif
EndEvent

; the cooking screen (SKSE plugin) sends these when you press E Create or R Season in pot
Function ListenToMenu()
	RegisterForModEvent("FC_MenuCook", "OnMenuCook")
	RegisterForModEvent("FC_MenuSeason", "OnMenuSeason")
	RegisterForModEvent("FC_MenuTaste", "OnMenuTaste")
	RegisterForModEvent("FC_MenuLearnDish", "OnMenuLearnDish")
	RegisterForModEvent("FC_IdTest", "OnIdTest")
	RegisterForModEvent("FC_OtherThirst", "OnOtherThirst")
	RegisterForModEvent("FC_INeedWoke", "OnINeedWoke")
	RegisterForModEvent("FC_CampRestPay", "OnCampRestPay")
	CollectCampRestPay()  ; rest that waited while nothing listened (a drink right after a load)
EndFunction

; rest a drink took that the plugin holds for the scripts (a night worked out after the drink, an old bottle without the
; effect): collected from the plugin, once, and given as the drink would have. The event only says there is some; a
; script that wasn't listening yet collects it when it listens (Codex reviews 2026-11-16d M1, 16f M3)
Event OnCampRestPay(string eventName, string strArg, float numArg, Form sender)
	CollectCampRestPay()
EndEvent

Function CollectCampRestPay()
	int[] pay = FC_Menu.TakeCampRestPay()  ; [points, needs mod]: given to the mod it was worked out for
	if pay.Length >= 2 && pay[0] > 0
		FC_Core.GiveBackRestTo(pay[0], pay[1])
	endif
EndFunction

; the player woke from a night in one of iNeed's bedrolls: report iNeed's rest now (numArg: the night's number)
Event OnINeedWoke(string eventName, string strArg, float numArg, Form sender)
	FC_INeedBridge.ReportRest(numArg as int)
EndEvent

; a drink was drunk while another mod runs thirst: the plugin sends the game hours it quenches (numArg; 1000: fully)
; as a mod event, so it never calls the scripts directly (his rule: C++ doesn't call into Papyrus; 2026-11-16)
Event OnOtherThirst(string eventName, string strArg, float numArg, Form sender)
	FC_Core.OtherThirst(numArg)
EndEvent

Event OnMenuCook(string eventName, string strArg, float numArg, Form sender)
	FC_Core.CookFromMenu(FC_Core.IdsToItems(strArg))
EndEvent

; ID self-test (FC_Core.SelfTest): the plugin sends every item it found, as the cooking screen sends them
Function ListenForIdTest()
	RegisterForModEvent("FC_IdTest", "OnIdTest")
EndFunction

Event OnIdTest(string eventName, string strArg, float numArg, Form sender)
	FC_Core.IdTestScripts(strArg)
EndEvent

; Cookbook Memory: the plugin saw the player eat a dish they had never cooked (strArg: the dish's index)
Event OnMenuLearnDish(string eventName, string strArg, float numArg, Form sender)
	StorageUtil.SetIntValue(none, "FC_Known|" + FC_Menu.DishSaveKey(strArg as int), 1)
EndEvent

Event OnMenuTaste(string eventName, string strArg, float numArg, Form sender)
	FC_Core.TasteFromMenu(strArg)
EndEvent

Event OnMenuSeason(string eventName, string strArg, float numArg, Form sender)
	FC_Core.SeasonFromMenu(FC_Core.IdsToItems(strArg))
EndEvent
