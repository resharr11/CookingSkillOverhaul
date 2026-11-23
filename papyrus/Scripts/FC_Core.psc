Scriptname FC_Core Hidden
{Cooking Skill Overhaul prototype: pot logic (step 2: full dish matching from the sheet rules).}

string Function DataFile() global
	return "../FreeCooking/data"
EndFunction

Form Function PreviewForm() global
	return Game.GetFormFromFile(0x803, "FreeCooking.esp")
EndFunction

ObjectReference Function GetPot() global
	ObjectReference pot = StorageUtil.GetFormValue(none, "FC_Pot") as ObjectReference
	if !pot
		Form potBase = Game.GetFormFromFile(0x802, "FreeCooking.esp")
		pot = Game.GetPlayer().PlaceAtMe(potBase, 1, true, true)
		StorageUtil.SetFormValue(none, "FC_Pot", pot)
	endif
	return pot
EndFunction

; Cooking screen (SKSE plugin FC_Menu): pots and spits open it; it calls back into the functions below.
Function GrantPrototypeBits() global
	Actor player = Game.GetPlayer()
	Perk cookPerk = Game.GetFormFromFile(0x81A, "FreeCooking.esp") as Perk
	if cookPerk && !player.HasPerk(cookPerk)
		player.AddPerk(cookPerk)
	endif
	; the "Cooking Skills" power is handed out by the plugin (SkillsMenu.cpp), only while Cooking isn't in the Skills menu
EndFunction

; indices of the dishes the player has discovered
int[] Function KnownDishes() global
	string dataPath = DataFile()
	StorageUtil.IntListClear(none, "FC_KnownTmp")
	int i = 0
	while FC_Menu.HasRuleText("dish." + i + ".n")
		if StorageUtil.GetIntValue(none, "FC_Known|" + FC_Menu.DishSaveKey(i)) == 1
			StorageUtil.IntListAdd(none, "FC_KnownTmp", i)
		endif
		i += 1
	endwhile
	int[] known = StorageUtil.IntListToArray(none, "FC_KnownTmp")
	StorageUtil.IntListClear(none, "FC_KnownTmp")
	return known
EndFunction

; form IDs of the ingredients whose flavour the player knows (tasted raw or cooked with)
int[] Function TastedIds() global
	StorageUtil.IntListClear(none, "FC_TastedTmp")
	int i = StorageUtil.FormListCount(none, "FC_Tasted")
	while i > 0
		i -= 1
		Form f = StorageUtil.FormListGet(none, "FC_Tasted", i)
		if f
			StorageUtil.IntListAdd(none, "FC_TastedTmp", f.GetFormID())
		endif
	endwhile
	int[] ids = StorageUtil.IntListToArray(none, "FC_TastedTmp")
	StorageUtil.IntListClear(none, "FC_TastedTmp")
	return ids
EndFunction

; the cooking screen tasted these (Curious Taster): "123,456"
; one form from the cooking screen's text: "<local id>@<plugin>", "high:low" (a form made in game: the form ID's two
; halves, as its ID is too big to read as a number), or a plain number
Form Function FormFromText(string t) global
	; "<local id>@<plugin>": a plugin's own form (Game.GetForm can't find a light plugin's form by its full ID)
	int at = StringUtil.Find(t, "@")
	if at > 0
		return Game.GetFormFromFile(StringUtil.Substring(t, 0, at) as int, StringUtil.Substring(t, at + 1))
	endif
	int colon = StringUtil.Find(t, ":")
	if colon < 0
		return Game.GetFormEx(t as int)
	endif
	int high = StringUtil.Substring(t, 0, colon) as int
	int low = StringUtil.Substring(t, colon + 1) as int
	return Game.GetFormEx(Math.LogicalOr(Math.LeftShift(high, 16), low))
EndFunction

; ID self-test (console: fcidtest, a command the plugin adds; this function is the same test for cgf). The plugin checks every item and dish the rules name, then sends the items here
; the way the cooking screen does; this checks the scripts get the same item and key back and can find every dish.
Function SelfTest() global
	(GetPot() as FC_PotContainerScript).ListenForIdTest()
	Debug.Notification(FC_Menu.Tr("Cooking Skill Overhaul: testing the item IDs."))
	FC_Menu.IdTest()
EndFunction

Function IdTestScripts(string text) global
	int semi = StringUtil.Find(text, ";")
	if semi < 0
		Debug.MessageBox(FC_Menu.Tr("Cooking Skill Overhaul ID test: the plugin sent no results."))
		return
	endif
	string[] p = StringUtil.Split(StringUtil.Substring(text, 0, semi), "/")
	if p.Length < 7
		Debug.MessageBox(FC_Menu.Tr("Cooking Skill Overhaul ID test: the plugin's results could not be read."))
		return
	endif
	string rest = StringUtil.Substring(text, semi + 1)
	; the items, as the cooking screen sends them
	int items = 0
	int itemsOk = 0
	if rest != ""
		string[] cases = StringUtil.Split(rest, ",")
		int i = 0
		while i < cases.Length
			int eq = StringUtil.Find(cases[i], "=")
			if eq > 0
				items += 1
				string want = StringUtil.Substring(cases[i], eq + 1)
				Form f = FormFromText(StringUtil.Substring(cases[i], 0, eq))
				if !f
					Debug.Trace("[FC] ID test: the scripts could not find " + want + " (sent as " + StringUtil.Substring(cases[i], 0, eq) + ")")
				elseif PlainKeyOf(f) != want
					Debug.Trace("[FC] ID test: " + f.GetName() + " has the key " + PlainKeyOf(f) + " in the scripts; the rules expect " + want)
				else
					itemsOk += 1
				endif
			endif
			i += 1
		endwhile
	endif
	; the dishes, as Cook() finds them
	string dataPath = DataFile()
	int dishes = 0
	int dishesOk = 0
	int d = 0
	while FC_Menu.HasRuleText("dish." + d + ".n")
		string plugin = FC_Menu.RuleText("dish." + d + ".p")
		if FC_Menu.RuleInt("dish." + d + ".off") != 1 && Game.IsPluginInstalled(plugin)
			dishes += 1
			if Game.GetFormFromFile(FC_Menu.RuleInt("dish." + d + ".id"), plugin) as Potion
				dishesOk += 1
			else
				Debug.Trace("[FC] ID test: the scripts could not find the dish " + FC_Menu.RuleText("dish." + d + ".n"))
			endif
		endif
		d += 1
	endwhile
	bool allOk = p[0] == p[1] && p[2] == p[3] && p[4] == p[5] && itemsOk == items && dishesOk == dishes
	; whole sentences for translators (his rule: all player text translatable; reviews 2026-11-16)
	string msg = FC_Menu.Tr("Cooking Skill Overhaul ID test.") + "\n\n"
	msg += FC_Menu.TrF("Plugin, items: {0} of {1}.", p[0], p[1]) + "\n"
	msg += FC_Menu.TrF("Plugin, dishes: {0} of {1}.", p[2], p[3]) + "\n"
	msg += FC_Menu.TrF("Plugin, effects, spells and other forms: {0} of {1}.", p[4], p[5]) + "\n"
	msg += FC_Menu.TrF("Scripts, items: {0} of {1}.", itemsOk, items) + "\n"
	msg += FC_Menu.TrF("Scripts, dishes: {0} of {1}.", dishesOk, dishes) + "\n"
	msg += FC_Menu.TrF("Skipped: {0} (content not installed).", p[6]) + "\n\n"
	if allOk
		msg += FC_Menu.Tr("All IDs were read correctly.")
	else
		msg += FC_Menu.Tr("Some IDs failed. The plugin's failures are in FreeCooking.log and the scripts' in Papyrus.0.log (lines with \"ID test\").")
	endif
	Debug.Trace("[FC] " + msg)
	Debug.MessageBox(msg)
EndFunction

Function TasteFromMenu(string ids) global
	string[] parts = StringUtil.Split(ids, ",")
	int i = 0
	while i < parts.Length
		Form f = FormFromText(parts[i])
		if f
			StorageUtil.FormListAdd(none, "FC_Tasted", f, false)
		endif
		i += 1
	endwhile
EndFunction

Function OpenMenu() global
	GrantPrototypeBits()
	; the pot's script listens for the screen's Create and Season in pot (mod events: renew each time)
	(GetPot() as FC_PotContainerScript).ListenToMenu()
	int[] known = KnownDishes()
	FC_Menu.SetTasted(TastedIds())
	; Sit at the pot like vanilla (animation and all): its crafting menu opens and the plugin shows the cooking
	; screen over it. A guard stops a loop in case this activation runs our perk again, and if the vanilla
	; menu never opens (no pot under the crosshair, or the test power) the screen opens directly.
	Actor player = Game.GetPlayer()
	ObjectReference station = Game.GetCurrentCrosshairRef()
	Keyword cookpot = Game.GetFormFromFile(0x0A5CB3, "Skyrim.esm") as Keyword
	float now = Utility.GetCurrentRealTime()
	; real time starts again from zero every time the game is launched, so a time saved from an earlier session can
	; be ahead of now: only a sit from the last 3 seconds of this session counts
	float since = now - StorageUtil.GetFloatValue(none, "FC_SitTime")
	bool recent = since >= 0.0 && since < 3.0
	; Hearthfire ovens (BYOHCraftingOven) bake: the plugin shows only baked dishes there
	Keyword oven = Game.GetFormFromFile(0x0117F7, "HearthFires.esm") as Keyword
	; Camping's campfire (Camping_CampfireCookingShared): grilling and roasting
	Keyword campfire = Game.GetFormFromFile(0x00314B, "Update.esm") as Keyword
	bool atStation = false
	if station
		Form stationBase = station.GetBaseObject()
		atStation = (cookpot && stationBase.HasKeyword(cookpot)) || (oven && stationBase.HasKeyword(oven)) || (campfire && stationBase.HasKeyword(campfire))
	endif
	if atStation && !recent
		StorageUtil.SetFloatValue(none, "FC_SitTime", now)
		FC_Menu.SetKnown(known)
		station.Activate(player, true)
		; wait up to 4 seconds for the pot's menu (the walk and sit animation); stop as soon as it opens
		float waited = 0.0
		while waited < 4.0
			if UI.IsMenuOpen("FreeCookingMenu") || UI.IsMenuOpen("Crafting Menu")
				return
			endif
			Utility.WaitMenuMode(0.25)
			waited += 0.25
		endwhile
		FC_Menu.Open(known)
	else
		FC_Menu.Open(known)
	endif
EndFunction

; puts everything left in the pot (except the preview item) back in the player's pack
Function ReturnPot(ObjectReference pot) global
	Actor player = Game.GetPlayer()
	Form preview = PreviewForm()
	int i = pot.GetNumItems()
	while i > 0
		i -= 1
		Form f = pot.GetNthForm(i)
		if f && f != preview
			pot.RemoveItem(f, pot.GetItemCount(f), true, player)
		endif
	endwhile
EndFunction

; puts one of each given item from the pack into the pot (repeats allowed: list an item twice)
Function FillPot(ObjectReference pot, Form[] items) global
	Actor player = Game.GetPlayer()
	int i = 0
	while i < items.Length
		if items[i] && player.GetItemCount(items[i]) > 0
			player.RemoveItem(items[i], 1, true, pot)
		endif
		i += 1
	endwhile
EndFunction

; the cooking screen sends its ingredients as form IDs in text: "123,456" (see FC_PotContainerScript)
Form[] Function IdsToItems(string ids) global
	Form[] items = new Form[5]
	string[] parts = StringUtil.Split(ids, ",")
	int i = 0
	while i < parts.Length && i < 5
		items[i] = FormFromText(parts[i])
		if !items[i]
			Debug.Trace("[FC] the cooking screen sent an item that was not found: " + parts[i])
		endif
		i += 1
	endwhile
	return items
EndFunction

; Create on the cooking screen: cook these ingredients right away, then refresh the screen
; a message from Cook: on the cooking screen it joins the screen's own line (notifications do not show while a menu
; is open), elsewhere it is a notification
Function CookNote(string text) global
	if UI.IsMenuOpen("FreeCookingMenu")
		StorageUtil.SetStringValue(none, "FC_CookNotes", StorageUtil.GetStringValue(none, "FC_CookNotes") + " " + text)
	else
		Debug.Notification(text)
	endif
EndFunction

Function CookFromMenu(Form[] items) global
	; one cook at a time: leaving the screen and sitting down again quickly could start a second cook while the first
	; still ran, and the two handed each other's ingredients back (reviews 2026-11-15). A cook takes a second or two; one
	; that never finished (a load in between) stops blocking after 15 seconds
	float now = Utility.GetCurrentRealTime()
	float started = StorageUtil.GetFloatValue(none, "FC_CookStarted", -100.0)
	if started >= 0.0 && now >= started && now - started < 15.0
		FC_Menu.CookBusy(FC_Menu.Tr("The pot is still cooking."))
		return
	endif
	StorageUtil.SetFloatValue(none, "FC_CookStarted", now)
	ObjectReference pot = GetPot()
	Form preview = PreviewForm()
	StorageUtil.SetIntValue(none, "FC_Busy", 1)
	if pot.GetItemCount(preview) == 0
		pot.AddItem(preview, 1, true)
	endif
	ReturnPot(pot)
	FillPot(pot, items)
	; keep the screen's order, so the plugin picks the same entries for the recipe as the screen's preview did
	StorageUtil.FormListClear(none, "FC_Order")
	int k = 0
	while k < items.Length
		if items[k]
			StorageUtil.FormListAdd(none, "FC_Order", items[k], true)
		endif
		k += 1
	endwhile
	StorageUtil.SetStringValue(none, "FC_LastCooked", "")
	StorageUtil.SetStringValue(none, "FC_CookNotes", "")
	Recalc(pot)
	Cook(pot)
	StorageUtil.FormListClear(none, "FC_Order")
	StorageUtil.SetIntValue(none, "FC_Busy", 1)
	ReturnPot(pot)
	StorageUtil.SetIntValue(none, "FC_Busy", 0)
	; tell the screen what came out (notifications do not show while a menu is open)
	string made = StorageUtil.GetStringValue(none, "FC_LastCooked")
	string msg = FC_Menu.Tr("Nothing to cook yet.")
	if made != ""
		; whole sentences for translators; drinks are brewed (his "we brew", 2026-11-12)
		; what Cook made, kept before it looked at the leftovers again (the leftovers' own values described the wrong dish:
		; review 2026-11-15)
		bool brewed = StorageUtil.GetIntValue(none, "FC_MadeDrink") == 1
		if StorageUtil.GetIntValue(none, "FC_MadeLevel") > 0
			int madeStars = StorageUtil.GetIntValue(none, "FC_MadeTotal")
			if brewed && madeStars == 1
				msg = FC_Menu.TrF("You brewed {0}, 1 star.", made)
			elseif brewed
				msg = FC_Menu.TrF("You brewed {0}, {1} stars.", made, madeStars)
			elseif madeStars == 1
				msg = FC_Menu.TrF("You cooked {0}, 1 star.", made)
			else
				msg = FC_Menu.TrF("You cooked {0}, {1} stars.", made, madeStars)
			endif
		elseif brewed
			msg = FC_Menu.TrF("You brewed {0}: a plain drink with no effect.", made)
		else
			msg = FC_Menu.TrF("You cooked {0}: a plain dish with no effect.", made)
		endif
		; what Cook would have said in a notification (a new dish, a critical cook, a house recipe): notifications do not
		; show while a menu is open, so the screen says it (found in game 2026-11-13: "New drink discovered" never showed)
		msg += StorageUtil.GetStringValue(none, "FC_CookNotes")
	endif
	StorageUtil.SetStringValue(none, "FC_CookNotes", "")
	StorageUtil.SetFloatValue(none, "FC_CookStarted", -100.0)
	FC_Menu.SetTasted(TastedIds())
	FC_Menu.Cooked(msg, made != "")
	FC_Menu.Refresh(KnownDishes())
EndFunction

; Season in pot on the cooking screen: open the classic pot with these ingredients already in it
Function SeasonFromMenu(Form[] items) global
	ObjectReference pot = GetPot()
	StorageUtil.SetIntValue(none, "FC_Busy", 1)
	ReturnPot(pot)
	FillPot(pot, items)
	StorageUtil.SetIntValue(none, "FC_Busy", 0)
	Utility.Wait(0.1)
	OpenPot()
EndFunction

Function OpenPot() global
	; prototype: make sure the player has the perk that turns cooking pots and spits into the Cooking Skill Overhaul pot
	Actor player = Game.GetPlayer()
	Perk cookPerk = Game.GetFormFromFile(0x81A, "FreeCooking.esp") as Perk
	if cookPerk && !player.HasPerk(cookPerk)
		player.AddPerk(cookPerk)
	endif
	ObjectReference pot = GetPot()
	Form preview = PreviewForm()
	if pot.GetItemCount(preview) == 0
		pot.AddItem(preview, 1, true)
	endif
	Recalc(pot)
	pot.Activate(Game.GetPlayer())
EndFunction

; Cooking skill tree perks (Custom Skills Framework skill "Cooking")
bool Function HasTreePerk(int localId) global
	Perk p = Game.GetFormFromFile(localId, "FreeCooking.esp") as Perk
	return p && Game.GetPlayer().HasPerk(p)
EndFunction

; JSON key for a form: "<plugin without extension>_<local form id in decimal>"
string Function KeyOf(Form akForm) global
	; pure potions share one entry per flavour ("potion_spicy")
	string pf = PotionFlavour(akForm)
	if pf != ""
		return "potion_" + pf
	endif
	return PlainKeyOf(akForm)
EndFunction

; A potion's flavour as a pot seasoning, or "": the plugin judges it by the same rule as the cooking screen (harmful
; effects, the player's choice on the Potions page, a name word, the first flavoured effect; FC_Menu.PotionFlavourOf).
; This script kept its own older copy of the rule until 2026-11-22 (review M1): one rule, one place.
string Function PotionFlavour(Form akForm) global
	if !akForm
		return ""
	endif
	return FC_Menu.PotionFlavourOf(akForm.GetFormID())
EndFunction

string Function PlainKeyOf(Form akForm) global
	; the plugin makes it, so the scripts and the plugin always agree (a plugin with the same name as an earlier one,
	; Foo.esp after Foo.esm, keeps its extension)
	if !akForm
		return ""
	endif
	return FC_Menu.ItemKey(akForm.GetFormID())
EndFunction

string Function IngStr(string dataPath, string k, string field) global
	return FC_Menu.RuleText("i" + field + "." + k)
EndFunction

int Function IngInt(string dataPath, string k, string field) global
	return FC_Menu.RuleInt("i" + field + "." + k)
EndFunction

string Function TimeText(int seconds) global
	int m = seconds / 60
	int s = seconds - m * 60
	if s < 10
		return m + ":0" + s
	endif
	return m + ":" + s
EndFunction

; Rebuilds the preview item's name from the pot contents and stores the result for Cook().
Function Recalc(ObjectReference pot) global
	string dataPath = DataFile()
	Form preview = PreviewForm()
	Form[] items = new Form[16]
	string[] keys = new string[16]
	bool[] used = new bool[16]
	int cnt = 0
	int n = pot.GetNumItems()
	int i = 0
	; the cooking screen's order (FC_Order, one entry per copy); the order no longer decides the dish (ChooseDish)
	int orderCount = StorageUtil.FormListCount(none, "FC_Order")
	while i < orderCount && cnt < 16
		Form ordered = StorageUtil.FormListGet(none, "FC_Order", i)
		; only as many copies as really reached the pot (the screen's count can be a moment old: before, a missing copy
		; still added its stars; reviews 2026-11-15)
		int taken = 0
		int t = 0
		while t < cnt
			if items[t] == ordered
				taken += 1
			endif
			t += 1
		endwhile
		if ordered && pot.GetItemCount(ordered) > taken
			items[cnt] = ordered
			keys[cnt] = KeyOf(ordered)
			cnt += 1
		endif
		i += 1
	endwhile
	if orderCount > 0
		n = 0  ; the pot's own order is not needed
	endif
	i = 0
	while i < n && cnt < 16
		Form item = pot.GetNthForm(i)
		if item && item != preview
			; repeats: each copy counts on its own (its stars, its slot), so add one entry per copy
			string itemKey = KeyOf(item)
			int copies = pot.GetItemCount(item)
			while copies > 0 && cnt < 16
				items[cnt] = item
				keys[cnt] = itemKey
				cnt += 1
				copies -= 1
			endwhile
		endif
		i += 1
	endwhile

	; 1. Which dish? The plugin decides (FC_Menu.ChooseDish, the same rule as the cooking screen's preview): order does
	; not matter; of the dishes whose recipe the pot fills, food comes before brews, then the one using the most
	; ingredients wins, then more named ones, then more of its vanilla recipe. It returns the dish, whether an insect
	; egg in its egg slot needs the drink, then the pot entries its recipe uses.
	int[] choice = FC_Menu.ChooseDish(keys, cnt)
	int best = -1
	bool recipeNeedsAlc = false
	if choice && choice.Length >= 2
		best = choice[0]
		recipeNeedsAlc = choice[1] == 1
	endif
	StorageUtil.FormListClear(none, "FC_Used")
	if best < 0
		preview.SetName("» Nothing to cook yet")
		StorageUtil.SetIntValue(none, "FC_Dish", -1)
		return
	endif
	int bestSlots = choice.Length - 2
	bool[] recipe = new bool[16]
	i = 2
	while i < choice.Length
		used[choice[i]] = true
		recipe[choice[i]] = true
		i += 1
	endwhile
	int spiceIdx = -1  ; no spiced dishes any more (Spiced Beef, Wine and Tea are not cookable)
	string dishName = FC_Menu.DishName(best, "")
	int tier = FC_Menu.RuleInt("dish." + best + ".t")
	bool isDrink = FC_Menu.RuleInt("dish." + best + ".dr") == 1
	bool ferment = FC_Menu.RuleInt("dish." + best + ".fe") == 1

	; Dragonfeast (perk): a Dragon Bone makes a meal and Dragon Scales a drink that always reach level 4 and last half a
	; day. The part stands in for any drink or garlic a seasoning needs (the ring is still needed). Either needs a
	; seasoning and a full pot (five ingredients, all of them used): a Bone meal with a vegetable or staple, a Scales
	; drink with at least three drink ingredients in the brew, its base included (a two-slot Mead or Ale takes one more,
	; a three-slot Wine is already there; until 2026-11-14 it asked one beyond any base, which a Wine could never fit). In a Scales drink the extra drink ingredients never
	; clash: one of the drink's flavour seasons it, any other is the brew's body. A pot that falls short cooks as if the
	; part were not there (the part stays in the pot). Same as the plugin's Rules.inl Project.
	int dragonIdx = -1
	int wrongPart = 0  ; a part that does not fit this dish: 1 a Bone in a drink, 2 Scales in food (a baked dish takes a
	; Bone like any meal: his call 2026-11-15, "there is no reason baked should be excluded")
	if HasTreePerk(0x85E)
		i = 0
		while i < cnt
			int part = IngInt(dataPath, keys[i], "d")
			if part > 0 && !used[i]
				if dragonIdx < 0 && ((part == 1 && !isDrink) || (part == 2 && isDrink))
					dragonIdx = i
				elseif wrongPart == 0 && !((part == 1 && !isDrink) || (part == 2 && isDrink))
					wrongPart = part
				endif
			endif
			i += 1
		endwhile
	endif
	bool[] dishUsed = new bool[16]
	i = 0
	while i < cnt
		dishUsed[i] = used[i]
		i += 1
	endwhile
	Form ring = Game.GetFormFromFile(FC_Menu.RuleInt("ring.id"), FC_Menu.RuleText("ring.p"))
	bool ringWorn = Game.GetPlayer().IsEquipped(ring)
	bool longSimmer = HasTreePerk(0x85D)
	bool dragon = false
	string dragonWhy = ""  ; why the part could not be used: "seasoning", "clash" or "full"
	string cat = ""
	bool cancelled = false
	bool blocked = false
	int stars = 0
	int enablerStar = 0
	bool gotVeg = false
	bool gotStaple = false
	int total = 0
	int level = 0
	int pass = 0  ; 0 with the dragon part, 1 without
	if dragonIdx < 0
		pass = 1
	endif
	int recipeHelper = -1  ; the drink an insect egg in the recipe slot needs (on the last pass): spent even by a plain dish
	while pass < 2
		bool withDragon = pass == 0
		i = 0
		while i < cnt
			used[i] = dishUsed[i]
			i += 1
		endwhile
		cat = ""
		cancelled = false
		blocked = false
		stars = 0
		enablerStar = 0
		gotVeg = false
		gotStaple = false
		total = 0
		level = 0
		int slotsLeft = 5 - bestSlots
		int brewExtras = 0  ; drink ingredients beyond a dragon brew's base
		if withDragon
			used[dragonIdx] = true
			slotsLeft -= 1
		endif

		; 2. Enablers in the pot (none with a dragon part: it does their job)
		int alcIdx = -1
		int garlicIdx = -1
		i = 0
		while i < cnt && !withDragon
			if !used[i]
				if alcIdx < 0 && IngInt(dataPath, keys[i], "a") == 1
					alcIdx = i
				elseif garlicIdx < 0 && IngInt(dataPath, keys[i], "g") == 1
					garlicIdx = i
				endif
			endif
			i += 1
		endwhile
		bool needAlc = false
		bool needGarlic = false
		i = 0
		while i < cnt
			if !used[i]
				string q0 = IngStr(dataPath, keys[i], "q")
				if q0 == "alc" && alcIdx >= 0 && !ferment
					needAlc = true
				elseif q0 == "garlic" && garlicIdx >= 0
					needGarlic = true
				endif
			endif
			i += 1
		endwhile
		recipeHelper = -1
		if recipeNeedsAlc && alcIdx >= 0 && !withDragon
			needAlc = true  ; an insect egg in its recipe slot stays gross: it needs the drink too
			recipeHelper = alcIdx
		endif
		; each helper adds its own star: a drink and Garlic both at work give 2 (his call 2026-11-15)
		if needAlc
			used[alcIdx] = true
			slotsLeft -= 1
			enablerStar += 1
		endif
		if needGarlic
			used[garlicIdx] = true
			slotsLeft -= 1
			enablerStar += 1
		endif

		; 3. Seasonings set the effect and add stars (every potion counts like any seasoning: his 2026-11-22 "drop the only one
		; potion counts rule"; same as the plugin's Rules.inl). In a dragon
		; brew the drink ingredients wait (round 1 below): those of the drink's flavour season it, the rest are its body
		bool brewBody = withDragon && isDrink
		i = 0
		while i < cnt
			if !used[i] && !(brewBody && IngInt(dataPath, keys[i], "b") > 0)
				string c = IngStr(dataPath, keys[i], "c")
				if c != ""
					string q = IngStr(dataPath, keys[i], "q")
					bool usable = q == "" || (q == "alc" && (ferment || alcIdx >= 0 || longSimmer || withDragon)) || (q == "garlic" && (garlicIdx >= 0 || longSimmer || withDragon)) || (q == "ring" && ringWorn)
					if !usable
						blocked = true
					elseif slotsLeft > 0
						int st = IngInt(dataPath, keys[i], "s")
						stars += st
						if cat == ""
							cat = c
						elseif cat != c
							cancelled = true
						endif
						used[i] = true
						slotsLeft -= 1
					endif
				endif
			endif
			i += 1
		endwhile

		if brewBody
			i = 0
			while i < cnt
				if !used[i] && slotsLeft > 0 && IngInt(dataPath, keys[i], "b") > 0
					brewExtras += 1
					used[i] = true
					slotsLeft -= 1
					string bc = IngStr(dataPath, keys[i], "c")
					string bq = IngStr(dataPath, keys[i], "q")
					bool bUsable = bq == "" || bq == "alc" || bq == "garlic" || (bq == "ring" && ringWorn)
					if bUsable && bc != "" && (cat == "" || cat == bc)
						stars += IngInt(dataPath, keys[i], "s")
						cat = bc
					endif
				endif
				i += 1
			endwhile
		endif

		; 4. A vegetable and a staple make a food dish last longer (one of each counts). In a drink only water does, one
		; water (his call 2026-11-15: water is a staple and a drink ingredient, brew code 5); in a dragon brew it already
		; counted as one of the drink ingredients above
		i = 0
		while i < cnt
			if !used[i] && slotsLeft > 0 && (!isDrink || IngInt(dataPath, keys[i], "b") == 5)
				string f = IngStr(dataPath, keys[i], "f")
				if f == "veg" && !gotVeg
					gotVeg = true
					used[i] = true
					slotsLeft -= 1
				elseif f == "staple" && !gotStaple
					gotStaple = true
					used[i] = true
					slotsLeft -= 1
				endif
			endif
			i += 1
		endwhile

		; 5. Level
		if cancelled
			cat = ""
		elseif cat != ""
			total = tier + stars + enablerStar
			if total >= 7
				level = 3
			elseif total >= 5
				level = 2
			else
				level = 1
			endif
		endif
		if withDragon
			int usedCount = 0
			bool hasSide = false  ; a vegetable or staple anywhere in the meal (the main ingredient counts)
			i = 0
			while i < cnt
				if used[i]
					usedCount += 1
					if IngStr(dataPath, keys[i], "f") != ""
						hasSide = true
					endif
				endif
				i += 1
			endwhile
			if cancelled
				dragonWhy = "clash"
			elseif cat == ""
				dragonWhy = "seasoning"
			elseif usedCount < 5 || (isDrink && bestSlots + brewExtras < 3) || (!isDrink && !hasSide)
				dragonWhy = "full"
			else
				dragon = true
				level = 4
			endif
		endif
		if dragon
			pass = 2
		else
			pass += 1
		endif
	endwhile
	if level == 0
		i = 0
		while i < cnt
			used[i] = recipe[i]
			i += 1
		endwhile
		; the drink the recipe itself needs is still used up (before, Braided Bread with Chaurus Eggs gave its Ale back, so
		; one Ale served forever; Codex review 2026-11-16)
		if recipeHelper >= 0
			used[recipeHelper] = true
		endif
	endif
	; durations: 20:00 is the ceiling, reached only by a perfect meal at Cooking 100 with the Cook perk; a dragon dish
	; lasts half a day of game time instead. sheet_v122 (built 2026-11-12): raw 4:00 / 6:00 / 8:20 by level; a meal adds
	; 1:00 for every pot ingredient it uses (recipe, seasonings, enabler drink, the one vegetable and one staple extra),
	; so a perfect full pot of 5 lasts 20:00 for a master cook at any tier; drinks last the level duration, plus 1:00
	; with water in them (2026-11-15)
	int dur = 0
	if dragon
		dur = FC_Menu.FortifyDuration(DragonDuration(), 1)  ; Fortify Cooking (the plugin, Cooking/ChefHat.inl)
	elseif level == 1
		dur = 240
	elseif level == 2
		dur = 360
	elseif level >= 3
		dur = 500
	endif
	if !dragon
		if level > 0 && !isDrink
			int u = 0
			while u < cnt
				if used[u]
					dur += 60
				endif
				u += 1
			endwhile
		elseif level > 0 && gotStaple
			dur += 60  ; water in a drink
		endif
		dur = ScaleDuration(dur)
	endif
	; a brew (mead, ale, wine) is a young drink until 6 AM: as a juice or tea would be (tier 0) with the same
	; seasoning, so it never comes out empty and never beats the finished brew. A dragon drink is ready at once.
	int youngLevel = 0
	int youngDur = 0
	if level > 0 && !dragon && FC_Menu.RuleInt("dish." + best + ".fe") == 1
		int youngTotal = stars + enablerStar
		youngLevel = 1
		youngDur = 240
		if youngTotal >= 7
			youngLevel = 3
			youngDur = 500
		elseif youngTotal >= 5
			youngLevel = 2
			youngDur = 360
		endif
		if gotStaple
			youngDur += 60  ; water in the young brew, as in the finished one
		endif
		youngDur = ScaleDuration(youngDur)
	endif

	; dishes are unknown until cooked once
	if StorageUtil.GetIntValue(none, "FC_Known|" + FC_Menu.DishSaveKey(best)) == 0
		dishName = "unknown dish"
		if isDrink
			dishName = "unknown drink"
		endif
	endif
	string label
	if dragon
		label = "» " + DragonPrefix(isDrink) + cat + " " + dishName + " (" + total + "*, Dragonfeast, " + TimeText(dur) + ")"
	elseif cancelled
		label = "» " + dishName + " (flavours clash)"
	elseif level == 0
		label = "» " + dishName + " (no seasoning)"
	else
		label = "» " + cat + " " + dishName + " (" + total + "*, Lv" + level + ", " + TimeText(dur) + ")"
	endif
	if blocked
		label += " - needs an enabler"
	endif
	if dragonIdx >= 0 && !dragon
		if dragonWhy == "full" && isDrink
			label += " - a dragon brew needs a full pot with at least three drink ingredients (water counts)"
		elseif dragonWhy == "full"
			label += " - a dragon meal needs a full pot with a vegetable or staple"
		elseif dragonWhy == "clash"
			label += " - the clash wastes the dragon part"
		else
			label += " - a dragon dish needs a seasoning"
		endif
	elseif wrongPart == 1
		label += " - Dragon Bone only goes into meals"
	elseif wrongPart == 2
		label += " - Dragon Scales only go into drinks"
	endif
	Debug.Trace("[FC] dish=" + dishName + " tier=" + tier + " seasoning=" + stars + " enabler=" + enablerStar + " -> " + label)
	preview.SetName(label)

	i = 0
	while i < cnt
		if used[i]
			StorageUtil.FormListAdd(none, "FC_Used", items[i], true)
		endif
		i += 1
	endwhile
	StorageUtil.SetIntValue(none, "FC_Dish", best)
	StorageUtil.SetStringValue(none, "FC_Cat", cat)
	StorageUtil.SetIntValue(none, "FC_Level", level)
	StorageUtil.SetIntValue(none, "FC_Dur", dur)
	StorageUtil.SetIntValue(none, "FC_YoungLevel", youngLevel)
	StorageUtil.SetIntValue(none, "FC_BrewWater", gotStaple as int)  ; water in the brew: its aged steps keep the minute
	StorageUtil.SetIntValue(none, "FC_YoungDur", youngDur)
	StorageUtil.SetIntValue(none, "FC_Total", total)
	StorageUtil.SetIntValue(none, "FC_Drink", isDrink as int)
	StorageUtil.SetIntValue(none, "FC_RecipeSlots", bestSlots)
	StorageUtil.SetIntValue(none, "FC_Clash", cancelled as int)
	StorageUtil.SetIntValue(none, "FC_Dragon", dragon as int)
EndFunction


; how long a meal or drink lasts: Cook (the tree's first perk) makes it half again as long; durations are a master
; cook's, 80% at Cooking 15 and 100% at Cooking 100, in 30-second steps, 20:00 at most before Fortify Cooking
int Function ScaleDuration(int dur) global
	if dur <= 0
		return dur
	endif
	if HasTreePerk(0x862)
		dur = dur * 3 / 2
	endif
	GlobalVariable cookLvl = Game.GetFormFromFile(0x850, "FreeCooking.esp") as GlobalVariable
	float skill = 15.0
	if cookLvl
		skill = cookLvl.GetValue()
	endif
	if skill > 100.0
		skill = 100.0
	elseif skill < 15.0
		skill = 15.0
	endif
	float scale = 0.8 + 0.2 * (skill - 15.0) / 85.0
	dur = ((dur * scale / 30.0) as int) * 30
	if dur > 1200
		dur = 1200
	endif
	; Fortify Cooking (head and chest enchantments): full up to 20:00, half past it, 25:00 at most (Cooking/ChefHat.inl)
	return FC_Menu.FortifyDuration(dur, 0)
EndFunction

; the dish a pot makes; young: a brew just set to ferment ("Young Nord Mead"). The plugin fills in one of
; FreeCooking.esp's blank dishes (FC_Menu.MakeDish), or hands back the one already made exactly the same way: it knows
; dishes by what they are (base dish, name, value, effects), so similar pots share one saved dish
; brewStars (a brew that ages, 2026-11-15): its stars, kept on a hidden line so brews that age differently never share a
; dish; fine: a critical brew ("Fine brew" line); ageLeft: mornings of aging still to come (-1: no aging line)
Potion Function MakeDish(int best, string cat, int level, int dur, int value, bool house, bool forbidden, int hungerPoints, bool young, bool dragon = false, int brewStars = 0, bool fine = false, int ageLeft = -1) global
	string dataPath = DataFile()
	string dishName = FC_Menu.DishName(best, "")
	Potion dish = none
	Form dishBase = Game.GetFormFromFile(FC_Menu.RuleInt("dish." + best + ".id"), FC_Menu.RuleText("dish." + best + ".p"))
	; the dish's effects, in the order its card lists them: 0 the meal itself, 1 how long it lasts in minutes, 2 Survival
	; Mode hunger (a dragon drink: its warmth), 3 the Fermenting or Dragonfeast note, 4 Camp rest (0: none). Thirst needs
	; no line: every drink quenches it (his call 2026-11-12), and the plugin does it when the drink is drunk
	int[] fx = new int[8]
	float[] mags = new float[8]
	int[] durs = new int[8]
	string fullName = dishName
	if level > 0
		; the dish only carries a trigger effect from our plugin: magnitude = level, duration = buff duration
		MagicEffect eatFx = Game.GetFormFromFile(FC_Menu.RuleInt("fxeatl." + cat + level), "FreeCooking.esp") as MagicEffect
		; magnitude carries the level; drinks add 10 so the eat script knows which slot (food or drink) they use
		float mag = level as float
		if StorageUtil.GetIntValue(none, "FC_Drink") == 1
			mag += 10.0
		elseif FC_Menu.RuleInt("dish." + best + ".ov") == 1
			; +5 marks a baked dish (an oven's): it uses the Baked slot, beside a meal and a drink
			mag += 5.0
		endif
		; +20 marks a dish with garlic (vampires get it at half strength): vanilla Garlic and any modded garlic the patcher
		; found ("ig." rule), not a Resist Poison potion, which also counts as garlic in the pot (2026-11-14, his "yes")
		int g = StorageUtil.FormListCount(none, "FC_Used")
		bool hasGarlic = false
		while g > 0 && !hasGarlic
			g -= 1
			Form usedForm = StorageUtil.FormListGet(none, "FC_Used", g)
			hasGarlic = usedForm && PotionFlavour(usedForm) == "" && FC_Menu.RuleInt("ig." + PlainKeyOf(usedForm)) == 1
		endwhile
		if hasGarlic
			mag += 20.0
		endif
		; +40 marks a forbidden dish: no effect unless the Ring of Namira is worn (the plugin also gives it back)
		if forbidden
			mag += 40.0
		endif
		; +100 marks a dragon dish (Dragonfeast): full strength for a vampire or werewolf player
		if dragon
			mag += 100.0
		endif
		if eatFx
			fx[0] = eatFx.GetFormID()
			mags[0] = mag
			durs[0] = dur
			; the card says "Lasts 36 minutes." (FC_DishDuration): the game itself would only count seconds
			MagicEffect lasts = Game.GetFormFromFile(0xA67, "FreeCooking.esp") as MagicEffect
			if lasts
				fx[1] = lasts.GetFormID()
				mags[1] = ((dur + 30) / 60) as float
			endif
		endif
		; seasoned name, prebuilt whole ("Chilly Beef", "Spicy Horker & Ash Yam Stew"): no "Cooked", "and" as "&"
		fullName = FC_Menu.DishName(best, cat)
		; a finished brew that ages counts down its mornings of aging ("Mighty Mead III"); done: "Vintage Mighty Mead"
		; (2026-11-15; the plugin names the aged steps the same way, Cooking/Hooks.inl BrewName)
		if ageLeft == 0
			fullName = FC_Menu.TrF("Vintage {0}", fullName)
		elseif ageLeft == 1
			fullName = FC_Menu.TrF("{0} I", fullName)
		elseif ageLeft == 2
			fullName = FC_Menu.TrF("{0} II", fullName)
		elseif ageLeft == 3
			fullName = FC_Menu.TrF("{0} III", fullName)
		elseif ageLeft == 4
			fullName = FC_Menu.TrF("{0} IV", fullName)
		elseif ageLeft == 5
			fullName = FC_Menu.TrF("{0} V", fullName)
		elseif ageLeft >= 6
			fullName = FC_Menu.TrF("{0} VI", fullName)
		endif
		; no quality word ("(Fine)", "(Superior)"): dropped 2026-11-12 (his call: the names are long enough; the card
		; shows the level). A house recipe still shows "(House)": it is always a perfect dish
		if dragon
			; "Akatosh's Chilly Venison Stew": the name says it is the best
			fullName = DragonName(StorageUtil.GetIntValue(none, "FC_Drink") == 1, fullName)
		elseif house
			fullName = FC_Menu.TrF("{0} (House)", fullName)
		endif
	else
		; never leave a dish without effects: the item card crashes on an empty effect list (the plugin also makes sure)
		MagicEffect plainFx = Game.GetFormFromFile(FC_Menu.RuleInt("plain"), "FreeCooking.esp") as MagicEffect
		if plainFx
			fx[0] = plainFx.GetFormID()
			if forbidden
				mags[0] = 40.0  ; the plugin's mark for a forbidden dish
			endif
		endif
	endif
	; Survival Mode feeds you through effects on the food itself: our hunger effect (its amount is in the effect itself:
	; magnitude 0, so the plugin never mistakes it for a dish marker). With the "Cooked food eases hunger" switch off
	; (Survival Mode settings page), the dish keeps the hunger Survival Mode gives the vanilla food it is made from
	if FC_Menu.FeatureOn(0)
		fx[2] = SurvivalHungerEffect(hungerPoints)
	elseif forbidden
		; a Namira dish: our own hunger effect, about as much as the vanilla food gives. Survival's own effect can't be
		; stopped when the dish is eaten without the ring, and a refused dish does nothing at all (his call 2026-11-16)
		fx[2] = SurvivalHungerEffect(VanillaHungerPoints(dishBase))
	else
		fx[2] = VanillaHungerEffect(dishBase)
	endif
	if dragon
		MagicEffect note = Game.GetFormFromFile(0xA65, "FreeCooking.esp") as MagicEffect  ; FC_Dragonfeast
		if note
			fx[3] = note.GetFormID()
		endif
		if StorageUtil.GetIntValue(none, "FC_Drink") == 1 && FC_Menu.SurvivalInstalled() && FC_Menu.FeatureOn(6)
			MagicEffect warmth = Game.GetFormFromFile(0xA66, "FreeCooking.esp") as MagicEffect  ; FC_DragonWarmth
			if warmth
				fx[2] = warmth.GetFormID()
			endif
		endif
	endif
	; Camp rest (Survival Mode on, 2026-11-12; replaced Refreshing): every brewed drink, alcohol and a plain one included,
	; carries the hidden FC_CampRest (FreeCooking.esp 0xA6A). Its script gives back the rest a night outdoors didn't give,
	; once (Cooking/Survival.inl); a moment is all it needs. A dragon drink already does more
	; it also hands a drink's thirst to another mod's thirst (2026-11-14: a plain drink has no eat effect to do it), so
	; dragon drinks carry it too and the thirst switch alone is enough
	; (whatever the switches when it is brewed: a bottle brewed with them off still gives the rest once Camp Rest is
	; switched on, and does nothing while it is off; Codex review 2026-11-16d, M6)
	if StorageUtil.GetIntValue(none, "FC_Drink") == 1 && FC_Menu.SurvivalInstalled()
		MagicEffect campRest = Game.GetFormFromFile(0xA6A, "FreeCooking.esp") as MagicEffect
		if campRest
			fx[4] = campRest.GetFormID()
			durs[4] = 1
		endif
	endif
	if young
		fullName = FC_Menu.TrF("Young {0}", fullName)
		; its card says it is still fermenting (FreeCooking.esp FC_Fermenting)
		MagicEffect fermenting = Game.GetFormFromFile(0xA3A, "FreeCooking.esp") as MagicEffect
		if fermenting
			fx[3] = fermenting.GetFormID()
		endif
	endif
	; aging (2026-11-15): 5 how many mornings it still ages (or "Fully aged"), 6 "Fine brew" for a critical brew, 7 its
	; stars (hidden). FreeCooking.esp FC_BrewAging 0x813, FC_BrewAged 0x814, FC_BrewFine 0x81B, FC_BrewStars 0x81C
	if ageLeft >= 0
		if ageLeft > 0
			fx[5] = Game.GetFormFromFile(0x813, "FreeCooking.esp").GetFormID()
			mags[5] = ageLeft as float
		else
			fx[5] = Game.GetFormFromFile(0x814, "FreeCooking.esp").GetFormID()
		endif
	endif
	if brewStars > 0
		if fine
			; FC_BrewFine "fully aged, it reaches level 4" when its stars get to 7 (4 or more now), else FC_BrewFineLow 0x81D
			if brewStars >= 4
				fx[6] = Game.GetFormFromFile(0x81B, "FreeCooking.esp").GetFormID()
			else
				fx[6] = Game.GetFormFromFile(0x81D, "FreeCooking.esp").GetFormID()
			endif
		endif
		fx[7] = Game.GetFormFromFile(0x81C, "FreeCooking.esp").GetFormID()
		mags[7] = brewStars as float
	endif
	int dishId = FC_Menu.MakeDish(dishBase.GetFormID(), fullName, value, fx, mags, durs, best)
	if dishId != 0
		dish = Game.GetFormEx(dishId) as Potion
	endif
	if !dish
		; every blank dish in this game is used (1280 different dishes): the plain dish it is based on
		Debug.Notification(FC_Menu.Tr("Cooking Skill Overhaul has no room for new dishes in this game."))
		dish = dishBase as Potion
	endif
	return dish
EndFunction

; Called when the player takes the preview item out of the pot.
Function Cook(ObjectReference pot) global
	Actor player = Game.GetPlayer()
	Form preview = PreviewForm()
	StorageUtil.SetIntValue(none, "FC_Busy", 1)
	if player.GetItemCount(preview) > 0
		player.RemoveItem(preview, player.GetItemCount(preview), true, pot)
	endif
	int best = StorageUtil.GetIntValue(none, "FC_Dish", -1)
	if best < 0
		Debug.Notification(FC_Menu.Tr("Nothing to cook yet."))
		StorageUtil.SetIntValue(none, "FC_Busy", 0)
		return
	endif
	string dataPath = DataFile()
	string cat = StorageUtil.GetStringValue(none, "FC_Cat")
	int level = StorageUtil.GetIntValue(none, "FC_Level")
	int dur = StorageUtil.GetIntValue(none, "FC_Dur")
	bool dragon = StorageUtil.GetIntValue(none, "FC_Dragon") == 1
	string dishName = FC_Menu.DishName(best, "")
	; Cook's Instinct: critical cook
	bool crit = false
	if level > 0 && level < 4 && HasTreePerk(0x859) && Utility.RandomInt(1, 100) <= 10
		level += 1
		crit = true
	endif
	; dish value: its ingredients' value plus 10% for the cooking
	int value = 0
	int u = StorageUtil.FormListCount(none, "FC_Used")
	while u > 0
		u -= 1
		Form valued = StorageUtil.FormListGet(none, "FC_Used", u)
		; a potion adds what a one-star seasoning would (no buying potions to sell as stew)
		if PotionFlavour(valued) != ""
			value += 5
		else
			value += valued.GetGoldValue()
		endif
	endwhile
	value = (value * 11 + 9) / 10
	; House Recipe: a perfect dish (7 stars or more) becomes a house recipe, worth half again as much
	bool house = level > 0 && !dragon && StorageUtil.GetIntValue(none, "FC_Total") >= 7 && HasTreePerk(0x85A)
	if house
		value = (value * 3 + 1) / 2
	endif
	; round the value to a coarse step so similar pots share one saved dish (keeps saves small)
	int step = 1
	if value >= 200
		step = 25
	elseif value >= 50
		step = 10
	elseif value >= 20
		step = 5
	endif
	value = ((value + step / 2) / step) * step
	; forbidden: Namira's own dishes and any dish with a forbidden ingredient (Human Flesh as a seasoning); eaten only with the ring
	bool forbidden = FC_Menu.RuleInt("dish." + best + ".nr") == 1
	int f = StorageUtil.FormListCount(none, "FC_Used")
	while f > 0 && !forbidden
		f -= 1
		forbidden = FC_Menu.RuleText("iq." + KeyOf(StorageUtil.FormListGet(none, "FC_Used", f))) == "ring"
	endwhile
	; Survival Mode hunger: you stay fed while the meal's effect lasts, plus about 5 game hours (the design from Simon
	; Magus's Gourmet + Starfrost). Survival's hunger rises 7.5 points every quarter of a game hour, 30 per hour (its
	; Survival_HungerNeedRate and Survival_UpdateGameTimeIntervalHunger); the effect's game hours come from the real
	; TimeScale. (Until 2026-11-12 this counted 7.5 per hour: dishes fed a quarter of the design.) Drinks don't feed you: 0
	float timeScale = 20.0
	GlobalVariable scaleGlobal = Game.GetFormFromFile(0x3A, "Skyrim.esm") as GlobalVariable
	if scaleGlobal && scaleGlobal.GetValue() > 0.0
		timeScale = scaleGlobal.GetValue()
	endif
	int hungerPoints = ((dur * timeScale / 3600.0 + 5.0) * 30.0) as int
	if StorageUtil.GetIntValue(none, "FC_Drink") == 1
		hungerPoints = 0
	elseif dragon
		hungerPoints = 1000  ; a Dragon Bone meal: fully sated
	endif
	; a brew (mead, ale, wine) comes out young, as good as a juice or tea; the plugin turns every young brew you carry
	; into the finished one at 6 AM
	bool brewing = FC_Menu.RuleInt("dish." + best + ".fe") == 1 && !dragon
	; a seasoned brew ages after it matures: a star a morning until it has 7 ("cap at seven stars", every brew gets there)
	int brewStars = 0
	int ageLeft = -1
	if brewing && level > 0
		brewStars = StorageUtil.GetIntValue(none, "FC_Total")
		ageLeft = 7 - brewStars
		if ageLeft < 0
			ageLeft = 0
		endif
	endif
	Potion dish = MakeDish(best, cat, level, dur, value, house, forbidden, hungerPoints, false, dragon, brewStars, crit, ageLeft)
	if brewing
		Potion finished = dish
		; young: as a juice or tea with the same seasoning (worked out with the preview); a critical one stays apart from
		; a normal one (its "Fine brew" line): they mature into different brews (review 2026-11-15)
		int youngLevel = StorageUtil.GetIntValue(none, "FC_YoungLevel")
		int youngDur = StorageUtil.GetIntValue(none, "FC_YoungDur")
		dish = MakeDish(best, cat, youngLevel, youngDur, value, false, forbidden, hungerPoints, true, false, brewStars, crit)
		string brewCat = ""
		if brewStars > 0
			brewCat = cat
		endif
		int youngId = FC_Menu.YoungBrew(dish.GetFormID(), finished.GetFormID(), brewCat, FC_Menu.DishName(best, cat), brewStars, crit, house, StorageUtil.GetIntValue(none, "FC_BrewWater") == 1)
		Potion ownYoung = Game.GetFormEx(youngId) as Potion
		if ownYoung
			dish = ownYoung  ; (a young dish of its own: an earlier batch's young dish matures into another brew)
		endif
	endif
	int i = StorageUtil.FormListCount(none, "FC_Used")
	while i > 0
		i -= 1
		Form usedItem = StorageUtil.FormListGet(none, "FC_Used", i)
		; cooking with an ingredient teaches its flavour (not a potion the player brewed: those never need tasting, and
		; each one is a new form, so the list would only grow; Codex review 2026-11-15, M12)
		if Math.LogicalAnd(usedItem.GetFormID(), -16777216) != -16777216  ; -16777216 = 0xFF000000: the game's made-at-runtime forms
			StorageUtil.FormListAdd(none, "FC_Tasted", usedItem, false)
		endif
		pot.RemoveItem(usedItem, 1, true)
		; a waterskin's water: the skin comes back one sip emptier, as its mod does it (Codex review 2026-11-15: the whole
		; skin was used up)
		int emptier = FC_Menu.EmptierWater(usedItem.GetFormID())
		if emptier != 0
			player.AddItem(Game.GetFormEx(emptier), 1, true)
		endif
	endwhile
	if house && StorageUtil.GetIntValue(none, "FC_House|" + FC_Menu.DishSaveKey(best)) == 0
		StorageUtil.SetIntValue(none, "FC_House|" + FC_Menu.DishSaveKey(best), 1)
		CookNote(FC_Menu.TrF("New house recipe: {0}.", dishName))
	endif
	int portions = 1
	player.AddItem(dish, portions)
	StorageUtil.SetStringValue(none, "FC_LastCooked", dish.GetName())
	FC_Menu.DishCooked(best)
	StorageUtil.SetIntValue(none, "FC_LastNew", (StorageUtil.GetIntValue(none, "FC_Known|" + FC_Menu.DishSaveKey(best)) == 0) as int)
	if crit
		CookNote(FC_Menu.TrF("Critical cook! The {0} came out stronger.", dishName))
	endif
	; Cooking skill experience: more stars give more; a new dish gives half again as much
	int xp = 1
	if level > 0
		xp = 4 + 3 * StorageUtil.GetIntValue(none, "FC_Total")
	endif
	if StorageUtil.GetIntValue(none, "FC_Drink") == 1
		xp = xp / 2
		if xp < 1
			xp = 1
		endif
	endif
	if StorageUtil.GetIntValue(none, "FC_Known|" + FC_Menu.DishSaveKey(best)) == 0
		StorageUtil.SetIntValue(none, "FC_Known|" + FC_Menu.DishSaveKey(best), 1)
		xp = xp * 3 / 2
		; the recipe is what is discovered: every seasoning of it is the same recipe (his choice 2026-11-13: "New recipe: X.")
		CookNote(FC_Menu.TrF("New recipe: {0}.", FC_Menu.Tr(FC_Menu.RuleText("dish." + best + ".n"))))  ; the recipe's own name ("Meat Stew"), not the cooked one, in the player's language
	endif
	; variety: cooking the same dish again right away teaches less. Compared by the recipe's saved key, not its number:
	; a food mod added or removed shifts the patched recipes' numbers (Codex review 2026-11-15, L4)
	string dishKey = FC_Menu.DishSaveKey(best)
	if StorageUtil.GetStringValue(none, "FC_LastDishKey") == dishKey
		xp = xp / 2
		if xp < 1
			xp = 1
		endif
	endif
	StorageUtil.SetStringValue(none, "FC_LastDishKey", dishKey)
	CustomSkills.AdvanceSkill("Cooking", xp as float)
	; the cook message reads these: the Recalc below works out the leftovers and overwrites FC_Drink, FC_Level, FC_Total
	StorageUtil.SetIntValue(none, "FC_MadeDrink", StorageUtil.GetIntValue(none, "FC_Drink"))
	StorageUtil.SetIntValue(none, "FC_MadeLevel", StorageUtil.GetIntValue(none, "FC_Level"))
	StorageUtil.SetIntValue(none, "FC_MadeTotal", StorageUtil.GetIntValue(none, "FC_Total"))
	StorageUtil.SetIntValue(none, "FC_Busy", 0)
	Recalc(pot)
EndFunction

; Called by FC_EatEffectScript when a cooked dish is eaten: casts the category's static buff spell.
Function ApplyFood(Actor target, MagicEffect eatFx, int level, int dur) global
	string dataPath = DataFile()
	string cat = FC_Menu.RuleText("eatcat." + KeyOf(eatFx))
	bool dragon = level >= 100
	if dragon
		level -= 100
	endif
	if level >= 40
		; a forbidden dish: the plugin gives it back when eaten without the ring, so no effect either
		level -= 40
		Armor namiraRing = Game.GetFormFromFile(0x2C37B, "Skyrim.esm") as Armor
		if target == Game.GetPlayer() && !target.IsEquipped(namiraRing)
			return
		endif
	endif
	bool garlic = level >= 20
	if garlic
		level -= 20
	endif
	bool drink = level > 10
	if drink
		level -= 10
	endif
	bool baked = level > 5
	if baked
		level -= 5
	endif
	if cat == "" || level < 1
		return
	endif
	; Dishes eaten from the inventory all start together when the menu closes; take turns, so each one replaces
	; the one before it in its slot (otherwise all of them stay active). A lock left in a save is cleared after 3 s.
	; Only a lock taken more than 5 seconds ago (real time) is broken: before, any meal that had waited 3 seconds broke a
	; lock its holder still used, and two buffs could mix (reviews 2026-11-15)
	float waited = 0.0
	while StorageUtil.AdjustIntValue(none, "FC_ApplyLock", 1) != 1
		StorageUtil.AdjustIntValue(none, "FC_ApplyLock", -1)
		Utility.Wait(0.05)
		waited += 0.05
		float since = Utility.GetCurrentRealTime() - StorageUtil.GetFloatValue(none, "FC_ApplyLockAt", -100.0)
		if waited > 1.0 && (since > 5.0 || since < 0.0)
			StorageUtil.SetIntValue(none, "FC_ApplyLock", 0)
			waited = 0.0
		endif
	endwhile
	StorageUtil.SetFloatValue(none, "FC_ApplyLockAt", Utility.GetCurrentRealTime())
	; vampires and werewolves get Earthy meals at half strength; vampires also any dish with garlic
	bool vampire = target.HasKeyword(Keyword.GetKeyword("Vampire"))
	GlobalVariable wolfGlobal = Game.GetFormFromFile(0x0ED06C, "Skyrim.esm") as GlobalVariable
	; a werewolf: the player by the game's own switch; anyone else by the game's werewolf faction or beast race, the two
	; markers werewolf mods give their NPCs (his call 2026-11-17: "add anyway for mod support or future use"; vanilla's
	; Circle members carry neither in human form)
	bool werewolf = false
	if target == Game.GetPlayer()
		werewolf = wolfGlobal && wolfGlobal.GetValue() > 0
	else
		Faction wolfFaction = Game.GetFormFromFile(0x043594, "Skyrim.esm") as Faction  ; WerewolfFaction
		Race wolfRace = Game.GetFormFromFile(0x0CDD84, "Skyrim.esm") as Race  ; WerewolfBeastRace
		werewolf = (wolfFaction && target.IsInFaction(wolfFaction)) || (wolfRace && target.GetRace() == wolfRace)
	endif
	bool reduced = (cat == "Earthy" && (vampire || werewolf)) || (garlic && vampire)
	; a dragon dish sits well with the player's blood (not a follower's)
	if reduced && dragon && target == Game.GetPlayer()
		reduced = false
		if drink
			Debug.Notification(FC_Menu.Tr("The dragon's power lets this drink sit well with your blood."))
		else
			Debug.Notification(FC_Menu.Tr("The dragon's power lets this meal sit well with your blood."))
		endif
	endif
	Spell buff
	if FC_Menu.RuleInt("fxperk." + cat) == 1
		; perk-based categories (weapon damage, spell power, damage taken, detection): one spell per level
		int useLevel = level
		if reduced
			useLevel = (level + 1) / 2
		endif
		string splKey = "fxspl."
		if drink
			splKey = "fxspld."
		elseif baked
			splKey = "fxsplb."
		endif
		buff = Game.GetFormFromFile(FC_Menu.RuleInt(splKey + cat + useLevel), "FreeCooking.esp") as Spell
	else
		; actor-value categories (regen, resistances): one spell, magnitude set per meal
		string spKey = "fxsp."
		if drink
			spKey = "fxspd."
		endif
		buff = Game.GetFormFromFile(FC_Menu.RuleInt(spKey + cat), "FreeCooking.esp") as Spell
		float m = FC_Menu.RuleInt("fxm" + level + "." + cat) as float
		if reduced
			m = m / 2.0
		endif
		buff.SetNthEffectMagnitude(0, m)
	endif
	int k = buff.GetNumEffects()
	while k > 0
		k -= 1
		buff.SetNthEffectDuration(k, dur)
	endwhile
	; one meal, one drink and one baked effect at a time: a new one replaces the previous one in its slot
	string slotKey = "FC_FoodSpell"
	string slotWord = "meal"
	if drink
		slotKey = "FC_DrinkSpell"
		slotWord = "drink"
	elseif baked
		slotKey = "FC_BakedSpell"
		slotWord = "bake"
	endif
	bool isPlayer = target == Game.GetPlayer()
	string note = ""
	Spell prev = StorageUtil.GetFormValue(target, slotKey) as Spell
	if prev && target.HasMagicEffect(prev.GetNthEffectMagicEffect(0))
		string prevCat = CategoryOf(prev)
		if prevCat != "" && prevCat != cat
			if drink
				note = FC_Menu.TrF("Your new drink replaces your {0} drink.", FC_Menu.Tr(prevCat))
			elseif baked
				note = FC_Menu.TrF("Your new baked dish replaces your {0} baked dish.", FC_Menu.Tr(prevCat))
			else
				note = FC_Menu.TrF("Your new meal replaces your {0} meal.", FC_Menu.Tr(prevCat))
			endif
		endif
	endif
	if prev
		target.DispelSpell(prev)
	endif
	target.DispelSpell(buff)
	; a category runs in one slot only: the same flavour in another slot goes, and the message says so ("Your new
	; Mighty drink replaces your Mighty meal."; it went without a word before; manual review 2026-11-15, his "b")
	string[] kinds = new string[3]
	kinds[0] = "FC_FoodSpell"
	kinds[1] = "FC_DrinkSpell"
	kinds[2] = "FC_BakedSpell"
	string[] kindWords = new string[3]
	kindWords[0] = "meal"
	kindWords[1] = "drink"
	kindWords[2] = "baked dish"
	string newWord = "meal"
	if drink
		newWord = "drink"
	elseif baked
		newWord = "baked dish"
	endif
	int kk = 0
	while kk < 3
		if kinds[kk] != slotKey
			Spell same = StorageUtil.GetFormValue(target, kinds[kk]) as Spell
			if same && CategoryOf(same) == cat && target.HasMagicEffect(same.GetNthEffectMagicEffect(0))
				note = FC_Menu.TrF("Your new {0} {1} replaces your {0} {2}.", FC_Menu.Tr(cat), FC_Menu.Tr(newWord), FC_Menu.Tr(kindWords[kk]))
			endif
		endif
		kk += 1
	endwhile
	; remove every meal, drink and baked version of this category first
	DispelCategory(target, dataPath, cat)
	; only one element (Spicy, Chilly, Zesty, Earthy) at a time, in any slot: a new one replaces the old one.
	; With Survival Mode, Spicy stays beside another element and keeps only its warmth (the cold cap and the
	; freezing-water ward; decided 2026-11-04); the plugin turns its fighting parts and frost resistance off. With both
	; of those switched off on the Survival Mode settings page, Spicy has no warmth to keep: one element, as without Survival
	if IsElement(cat)
		; only the player keeps Spicy's warmth: followers don't feel the cold, so for them the one-element rule is plain
		; (his call 2026-11-16: "the survival spice replacement" doesn't make sense for a follower)
		bool spicyStays = target == Game.GetPlayer() && FC_Menu.NeedOwner(3) > 0 && (FC_Menu.FeatureOn(1) || FC_Menu.FeatureOn(2))  ; any mod's cold (2026-11-14)
		string[] slots = new string[3]
		slots[0] = "FC_FoodSpell"
		slots[1] = "FC_DrinkSpell"
		slots[2] = "FC_BakedSpell"
		int e = 0
		while e < 3
			Spell held = StorageUtil.GetFormValue(target, slots[e]) as Spell
			if held && slots[e] != slotKey
				string sCat = CategoryOf(held)
				if sCat != cat && IsElement(sCat) && target.HasMagicEffect(held.GetNthEffectMagicEffect(0))
					if spicyStays && sCat == "Spicy"
						note = FC_Menu.TrF("{0} replaces Spicy's fire, but not its warmth.", FC_Menu.Tr(cat))
					elseif spicyStays && cat == "Spicy"
						note = FC_Menu.TrF("{0} replaces this Spicy dish's fire, but not its warmth.", FC_Menu.Tr(sCat))
					else
						DispelCategory(target, dataPath, sCat)
						StorageUtil.UnsetFormValue(target, slots[e])
						note = FC_Menu.TrF("{0} replaces {1}. Only one element works at a time.", FC_Menu.Tr(cat), FC_Menu.Tr(sCat))
					endif
				endif
			endif
			e += 1
		endwhile
	endif
	; the same category in another slot was just removed: forget it there
	string[] others = new string[3]
	others[0] = "FC_FoodSpell"
	others[1] = "FC_DrinkSpell"
	others[2] = "FC_BakedSpell"
	int o = 0
	while o < 3
		if others[o] != slotKey
			Spell other = StorageUtil.GetFormValue(target, others[o]) as Spell
			if other && CategoryOf(other) == cat
				StorageUtil.UnsetFormValue(target, others[o])
			endif
		endif
		o += 1
	endwhile
	if isPlayer && note != ""
		Debug.Notification(note)
	endif
	StorageUtil.SetStringValue(buff, "FC_Cat", cat)  ; CategoryOf reads it (never the spell's name, which translators change)
	buff.Cast(target, target)
	StorageUtil.SetFormValue(target, slotKey, buff)
	; released by taking our own 1 back, never by setting 0: a waiter between its +1 and -1 was then left at -1, which let
	; two meals in at once or held one back 5 s (reviews 2026-11-16)
	StorageUtil.AdjustIntValue(none, "FC_ApplyLock", -1)
EndFunction

; Shared Table (perk): followers who are following you near you share the meal or drink you just ate, at the same level
; and duration. Each gets the effect best for them (the plugin picks it from their best combat skill: weapons and archery
; Mighty, magic Brainy, armor, Block and Alteration Hearty; never Sneaky); elemental dishes are shared as they are.
Function ShareMeal(MagicEffect eatFx, int mag, int dur) global
	if !HasTreePerk(0x857)
		Debug.Trace("[FC] Shared Table: no perk")
		return
	endif
	; follower form ID, then 0 Mighty / 1 Brainy / 2 Hearty, for each follower (taken when you ate)
	int[] shared = FC_Menu.SharedTableFollowers(eatFx.GetFormID(), false)
	; the plugin takes the list in the equip event; if this script got here first the list is still empty, so wait for
	; it a little (test 2026-11-15: a follower at the table got nothing, with no trace). One entry: taken, nobody there
	int tries = 0
	while shared.Length == 0 && tries < 4
		Utility.Wait(0.25)
		tries += 1
		shared = FC_Menu.SharedTableFollowers(eatFx.GetFormID(), tries == 4)
	endwhile
	Debug.Trace("[FC] Shared Table: effect " + eatFx.GetFormID() + ", list " + shared.Length + " after " + tries + " waits, mag " + mag + ", dur " + dur)
	if shared.Length < 2
		return
	endif
	string dataPath = DataFile()
	string cat = FC_Menu.RuleText("eatcat." + KeyOf(eatFx))
	if cat == ""
		Debug.Trace("[FC] Shared Table: not a meal effect (" + KeyOf(eatFx) + ")")
		return
	endif
	; the level without the markers ApplyFood reads (100 dragon, 40 forbidden, 20 garlic, 10 drink, 5 baked)
	int level = mag
	if level >= 100
		level -= 100
	endif
	if level >= 40
		level -= 40
		; a Namira dish eaten without the ring went back in the bag: nobody shares it (review 2026-11-15)
		Armor namiraRing = Game.GetFormFromFile(0x2C37B, "Skyrim.esm") as Armor
		if !Game.GetPlayer().IsEquipped(namiraRing)
			return
		endif
	endif
	if level >= 20
		level -= 20
	endif
	bool drink = level > 10
	if drink
		level -= 10
	endif
	bool baked = level > 5
	if baked
		level -= 5
	endif
	if level < 1
		Debug.Trace("[FC] Shared Table: level below 1 (magnitude " + mag + ")")
		return
	endif
	string[] best = new string[3]
	best[0] = "Mighty"
	best[1] = "Brainy"
	best[2] = "Hearty"
	string firstName = ""
	int count = 0
	; ranked buffs read the follower's other slots, so shares take turns: dishes eaten together from the inventory
	; would otherwise all see the same free flavour (a lock older than 5 seconds of real time is broken, as FC_ApplyLock)
	float waited = 0.0
	while StorageUtil.AdjustIntValue(none, "FC_ShareLock", 1) != 1
		StorageUtil.AdjustIntValue(none, "FC_ShareLock", -1)
		Utility.Wait(0.05)
		waited += 0.05
		float since = Utility.GetCurrentRealTime() - StorageUtil.GetFloatValue(none, "FC_ShareLockAt", -100.0)
		if waited > 1.0 && (since > 5.0 || since < 0.0)
			StorageUtil.SetIntValue(none, "FC_ShareLock", 0)
			waited = 0.0
		endif
	endwhile
	StorageUtil.SetFloatValue(none, "FC_ShareLockAt", Utility.GetCurrentRealTime())
	int i = 0
	while i + 1 < shared.Length
		; GetFormEx (SKSE): Game.GetForm returns None for forms made in game (FF IDs: spawned or copied NPCs; test 2026-11-15)
		Actor follower = Game.GetFormEx(shared[i]) as Actor
		int which = shared[i + 1]
		if !follower
			Debug.Trace("[FC] Shared Table: follower " + shared[i] + " not found")
		endif
		if follower && which >= 0 && which <= 2
			MagicEffect fx = eatFx
			if !IsElement(cat)
				which = SharedTableRank(follower, drink, baked)
				fx = Game.GetFormFromFile(FC_Menu.RuleInt("fxeatl." + best[which] + level), "FreeCooking.esp") as MagicEffect
			endif
			Debug.Trace("[FC] Shared Table: follower " + shared[i] + " (" + follower.GetDisplayName() + ") gets " + fx + " for " + cat + " level " + level)
			if fx
				ApplyFood(follower, fx, mag, dur)
				; still working: a share with several followers can take over 5 s, and a waiting share would break the lock
				StorageUtil.SetFloatValue(none, "FC_ShareLockAt", Utility.GetCurrentRealTime())
				if count == 0
					firstName = follower.GetDisplayName()
				endif
				count += 1
			endif
		endif
		i += 2
	endwhile
	StorageUtil.AdjustIntValue(none, "FC_ShareLock", -1)  ; (our own 1 back, as FC_ApplyLock)
	string what = "meal"
	if drink
		what = "drink"
	endif
	if count == 1 && drink
		Debug.Notification(FC_Menu.Tr("Your follower shares your drink."))
	elseif count == 1
		Debug.Notification(FC_Menu.Tr("Your follower shares your meal."))
	elseif count > 1 && drink
		Debug.Notification(FC_Menu.Tr("Your followers share your drink."))
	elseif count > 1
		Debug.Notification(FC_Menu.Tr("Your followers share your meal."))
	endif
EndFunction

; Shared Table, ranked buffs (his design 2026-11-16: "maybe we do ranked buffs? their best is always first"): a
; follower ranks Mighty, Brainy and Hearty by their own skills (weapons and archery, magic, Heavy and Light Armor,
; Block and Alteration (his choices 2026-11-22: Light Armor and Alteration, the armor school, Hearty, as in the potion rule); ties
; go to the follower's attack skill (the higher of weapons and magic; weapons on a tie), then Hearty, then the other
; (his rule 2026-11-22: "attack skill, hearty then the other skill", "the attack skill depends on the follower"), as the
; plugin's SharedTableCategory) and gets the best one not already running in another of their
; slots. Before this, a Brainy meal and a Mighty drink both became Mighty for a warrior, and the drink took the meal
; away while the player kept both. A buff keeps its flavour until it ends or the player's dish in that slot is replaced
; (his "a": no switching in the middle of a buff). Returns 0 Mighty, 1 Brainy, 2 Hearty
int Function SharedTableRank(Actor follower, bool drink, bool baked) global
	float[] score = new float[3]
	score[0] = MaxAV(follower, "OneHanded", "TwoHanded", "Marksman", "", "")
	score[1] = MaxAV(follower, "Destruction", "Conjuration", "Illusion", "Restoration", "")
	score[2] = MaxAV(follower, "HeavyArmor", "LightArmor", "Block", "Alteration", "")
	string[] names = new string[3]
	names[0] = "Mighty"
	names[1] = "Brainy"
	names[2] = "Hearty"
	string slotKey = "FC_FoodSpell"
	if drink
		slotKey = "FC_DrinkSpell"
	elseif baked
		slotKey = "FC_BakedSpell"
	endif
	string[] slots = new string[3]
	slots[0] = "FC_FoodSpell"
	slots[1] = "FC_DrinkSpell"
	slots[2] = "FC_BakedSpell"
	int[] tieOrder = new int[3]
	tieOrder[0] = 0
	tieOrder[1] = 2
	tieOrder[2] = 1
	if score[1] > score[0]
		tieOrder[0] = 1  ; a mage: magic is the attack skill
		tieOrder[2] = 0
	endif
	bool[] tried = new bool[3]
	int first = -1
	int round = 0
	while round < 3
		; the best flavour not tried yet (a tie keeps the order attack skill, Hearty, other)
		int pick = -1
		int k = 0
		while k < 3
			int c = tieOrder[k]
			if !tried[c] && (pick < 0 || score[c] > score[pick])
				pick = c
			endif
			k += 1
		endwhile
		tried[pick] = true
		if first < 0
			first = pick
		endif
		bool elsewhere = false
		int e = 0
		while e < 3
			if slots[e] != slotKey
				Spell held = StorageUtil.GetFormValue(follower, slots[e]) as Spell
				if held && CategoryOf(held) == names[pick] && follower.HasMagicEffect(held.GetNthEffectMagicEffect(0))
					elsewhere = true
				endif
			endif
			e += 1
		endwhile
		if !elsewhere
			return pick
		endif
		round += 1
	endwhile
	return first  ; cannot happen with three slots (two others hold at most two flavours); kept as a safe answer
EndFunction

; the highest of up to five actor values (an empty name is skipped)
float Function MaxAV(Actor a, string v1, string v2, string v3, string v4, string v5) global
	float m = a.GetActorValue(v1)
	if v2 != "" && a.GetActorValue(v2) > m
		m = a.GetActorValue(v2)
	endif
	if v3 != "" && a.GetActorValue(v3) > m
		m = a.GetActorValue(v3)
	endif
	if v4 != "" && a.GetActorValue(v4) > m
		m = a.GetActorValue(v4)
	endif
	if v5 != "" && a.GetActorValue(v5) > m
		m = a.GetActorValue(v5)
	endif
	return m
EndFunction

Function DispelCategory(Actor target, string dataPath, string cat) global
	string[] keys = new string[5]
	keys[0] = "fxspl."
	keys[1] = "fxspld."
	keys[2] = "fxsp."
	keys[3] = "fxspd."
	keys[4] = "fxsplb."
	int n = 0
	while n < 5
		int lv = 1
		while lv <= 4
			string k = keys[n] + cat
			if n < 2 || n == 4
				k += lv
			endif
			Spell s = Game.GetFormFromFile(FC_Menu.RuleInt(k), "FreeCooking.esp") as Spell
			if s
				target.DispelSpell(s)
			endif
			if n == 2 || n == 3
				lv = 5
			endif
			lv += 1
		endwhile
		n += 1
	endwhile
EndFunction

; the category of one of our buff spells, from its name: "Spicy (Meal)" -> "Spicy"
string Function CategoryOf(Spell sp) global
	if !sp
		return ""
	endif
	; the category the spell was cast for (ApplyFood). The spell's name ("Spicy (Meal)") is only the fallback for buffs
	; cast before 2026-11-15: a translated esp names it "Picante (Refeicao)" and broke the one-element rule (reviews)
	string tagged = StorageUtil.GetStringValue(sp, "FC_Cat")
	if tagged != ""
		return tagged
	endif
	string n = sp.GetName()
	int cut = StringUtil.Find(n, " (")
	if cut <= 0
		return ""
	endif
	return StringUtil.Substring(n, 0, cut)
EndFunction

bool Function IsElement(string cat) global
	return cat == "Spicy" || cat == "Chilly" || cat == "Zesty" || cat == "Earthy"
EndFunction

; Survival Mode hunger on a cooked dish: food eases the given hunger points (our own effect, handed to Survival's
; hunger); drinks get none (0). Only with Survival Mode installed. Returns the effect's form ID for FC_Menu.MakeDish, or 0
; Survival Mode hunger on a dish: the FreeCooking.esp hunger effect nearest to hungerPoints (FC_Hunger160 ... 360, in
; steps of 40, about 1.3 game hours each; 1000 or more: FC_HungerFull). The amount is set on the effect, as its script
; cannot read a magnitude.
int Function SurvivalHungerEffect(int hungerPoints) global
	; Survival Mode or another survival mod (2026-11-14: the effect finds the mod that runs hunger when eaten)
	if hungerPoints <= 0 || !FC_Menu.SurvivalInstalled()
		return 0
	endif
	int fid = 0xA64  ; FC_HungerFull
	if hungerPoints < 1000
		int stepIndex = (hungerPoints - 140) / 40  ; 160 -> 0 ... 360 -> 5 (nearest step)
		if stepIndex < 0
			stepIndex = 0
		elseif stepIndex > 5
			stepIndex = 5
		endif
		fid = 0xA5E + stepIndex
	endif
	MagicEffect hunger = Game.GetFormFromFile(fid, "FreeCooking.esp") as MagicEffect
	if hunger
		return hunger.GetFormID()
	endif
	return 0
EndFunction

; Survival Mode is on (asked from the plugin, which reads Survival_ModeEnabled; FreeCooking.esp's FC_SurvivalOn global
; now only switches Hearty's Survival regeneration, 2026-11-12)
bool Function SurvivalOn() global
	return FC_Menu.SurvivalOn()
EndFunction

; the hunger Survival Mode gives a vanilla food (its Survival_FoodRestoreHunger* effect, Update.esm 0x2EE1 very small
; ... 0x2EE4 large; 0x24E1-0x24E4 were wrong until 2026-11-16: those are placed references), for a dish cooked with
; the "Cooked food eases hunger" switch off; 0 when the food has none (our own drinks, for example)
int Function VanillaHungerEffect(Form dishBase) global
	Potion base = dishBase as Potion
	if !base || !Game.IsPluginInstalled("ccQDRSSE001-SurvivalMode.esl")
		return 0
	endif
	int n = base.GetNumEffects()
	int i = 0
	while i < n
		MagicEffect e = base.GetNthEffectMagicEffect(i)
		if e
			int k = 0x2EE1
			while k <= 0x2EE4
				if e == Game.GetFormFromFile(k, "Update.esm") as MagicEffect
					return e.GetFormID()
				endif
				k += 1
			endwhile
		endif
		i += 1
	endwhile
	return 0
EndFunction

; the hunger points that vanilla food gives in Survival Mode (Survival_HungerRestoreEffectScript: very small 2, small 18,
; medium 220, large 380), for a Namira dish cooked with the hunger switch off; 0 below medium (too little for one of
; our hunger steps, the smallest of which is 160)
int Function VanillaHungerPoints(Form dishBase) global
	int fid = VanillaHungerEffect(dishBase)
	if fid == 0
		return 0
	endif
	Form e = Game.GetForm(fid)
	if e == Game.GetFormFromFile(0x2EE4, "Update.esm")
		return 380
	elseif e == Game.GetFormFromFile(0x2EE3, "Update.esm")
		return 220
	endif
	return 0
EndFunction

; ---- Spicy's cold cap (Survival Mode; FC_SpicyWarmthEffectScript asks every 2 s while a Spicy effect lasts) ----
; While a Spicy effect lasts the cold can't get past a stage, which depends
; on the place and the Spicy level (Option C, see SpicyColdCeiling); the cold never kills. How: Survival's cold rises on its own timer (every 15 s, up to about 65 points in a
; blizzard at night), so a value held just under a stage would still step over it between two of our checks. So near
; the ceiling the cold's rate is set to nothing (Survival_NeedCold.cachedColdLevelMult, which Survival reads from
; Survival_ColdLevelMult when a game loads) and put back when the effect ends or the cold falls back. Warming up
; near a fire or in a warm place still works as usual. A cold already past the ceiling is brought down to it.
; The ceilings sit 10 under each stage's end: held exactly at the end (299), Survival's tick still nudged the cold to
; 300 every few seconds, so the stage (and its drain) flipped back and forth (test of 2026-11-04).
float Function SpicyColdCeiling(int level, int coldLevel) global
	; Option C (decided 2026-11-04). Spicy makes the place count as 3 cold levels warmer per Spicy level (the mildest cold
	; goes first) and never lets the cold go past a floor stage: Spicy 1 Freezing, 2 Cold,
	; 3 Chilly. Level 4 (a critical cook or a dragon part) is immune: the cold never passes Comfortable.
	; Survival's own rule picks the worst stage a place allows from its cold level (Survival_NeedCold.GetColdStageMaximum:
	; levels 1/4/7/10/13 allow Comfortable/Chilly/Cold/Freezing/Numb). Each ceiling sits 10 below the next stage, the
	; margin that stopped the 299/300 bounce.
	if level >= 4
		return 110.0
	endif
	int effective = coldLevel - 3 * level
	int stage = 0
	if effective >= 13
		stage = 5
	elseif effective >= 10
		stage = 4
	elseif effective >= 7
		stage = 3
	elseif effective >= 4
		stage = 2
	elseif effective >= 1
		stage = 1
	endif
	int floorStage = 5 - level        ; Spicy 1: Freezing (4), 2: Cold (3), 3: Chilly (2)
	if stage > floorStage
		stage = floorStage
	endif
	if stage >= 5
		return 990.0     ; only without Spicy (level 0): the place can reach Numb
	elseif stage == 4
		return 790.0
	elseif stage == 3
		return 490.0
	elseif stage == 2
		return 290.0
	endif
	return 110.0     ; Comfortable or warmer: no cold penalty
EndFunction

; true while the player is in the cold (Chilly or worse)
bool Function SpicyHoldCold(int level) global
	; who runs the cold (FC_Menu.NeedOwner 3): 0 nothing, 1 Survival Mode, 2 Survival Mode Improved / Starfrost,
	; 3 SunHelm, 7 Frostfall. His call 2026-11-14: Spicy keeps real cold protection with every survival mod
	int owner = FC_Menu.NeedOwner(3)
	if owner == 0 || !FC_Menu.FeatureOn(1)  ; the "Spicy holds off the cold" switch (Survival Mode settings page)
		SpicyColdRelease()
		return false
	endif
	if owner == 2
		return SpicyHoldColdSMI(level)
	elseif owner == 3
		return FC_SunHelmBridge.HoldCold(level)
	elseif owner == 7
		return FC_FrostfallBridge.HoldCold(level)
	elseif owner != 1
		return false
	endif
	return FC_SurvivalBridge.HoldCold(level)  ; Survival Mode's own scripts are only touched there (review 2026-11-15)
EndFunction

; ---- Survival Mode Improved (and Starfrost, which runs on it) ----
; It runs Survival's needs in its own plugin and removes the scripts of Survival's need quests, so Survival_NeedCold and
; friends can't be asked there. Its author gave other mods a Papyrus API for this (SurvivalModeImprovedApi: restore
; cold, hunger, tiredness), so Cooking Skill Overhaul uses that and nothing else of it (his rule 2026-11-14: use only the doors a
; mod's author opened). Its values are read from Survival's own globals, which it keeps up to date.
bool Function SMIInstalled() global
	return Game.IsPluginInstalled("SurvivalModeImproved.esp")
EndFunction

; Spicy under Survival Mode Improved (and Starfrost): as under Survival Mode, each Spicy level makes the place count one
; stage warmer (his call 2026-11-15, so the thermometer's blocks can show it). The place's stage comes from SMI's own
; place cold (SMI_CurrentAmbientTemp 0xD1D; 600 or more = all the way) against its own stage values (Starfrost changes
; them). Its cold rises every second, so the ceiling sits 20 under the next stage. Mild places are left alone.
bool Function SpicyHoldColdSMI(int level) global
	GlobalVariable coldNow = Game.GetFormFromFile(0x81B, "ccQDRSSE001-SurvivalMode.esl") as GlobalVariable  ; Survival_ColdNeedValue
	GlobalVariable chilly = Game.GetFormFromFile(0xD21, "SurvivalModeImproved.esp") as GlobalVariable  ; its stage 2 (Chilly) start
	if !coldNow || !chilly
		return false
	endif
	float now = coldNow.GetValue()
	int place = SpicyPlaceStageSMI()
	int stage = place - level  ; the worst stage allowed here (1 = Comfortable)
	if stage < 1
		stage = 1
	endif
	if place <= stage
		return now >= chilly.GetValue()  ; this place can't push the cold past what the spice allows
	endif
	GlobalVariable nextStage = Game.GetFormFromFile(0xD20 + stage, "SurvivalModeImproved.esp") as GlobalVariable
	if !nextStage
		return false
	endif
	float ceiling = nextStage.GetValue() - 20.0
	if now > ceiling
		SurvivalModeImprovedApi.RestoreColdLevel(now - ceiling)
		SpicyFirstHold()
	endif
	return now >= chilly.GetValue()
EndFunction

; the worst stage this place can bring under SMI: 0 warm, 1 Comfortable ... 5 its last stage (unknown: 5, the old rule)
int Function SpicyPlaceStageSMI() global
	GlobalVariable amb = Game.GetFormFromFile(0xD1D, "SurvivalModeImproved.esp") as GlobalVariable  ; SMI_CurrentAmbientTemp
	if !amb
		return 5
	endif
	float a = amb.GetValue()
	if a >= 600.0
		return 5
	endif
	int place = 0
	int k = 1
	while k <= 5
		GlobalVariable start = Game.GetFormFromFile(0xD1F + k, "SurvivalModeImproved.esp") as GlobalVariable  ; stage k's start
		if start && a >= start.GetValue()
			place = k
		endif
		k += 1
	endwhile
	return place
EndFunction

; the first time Spicy holds the cold back under another survival mod: the notice (once per Spicy hold) and the
; one-time help box (per character), as with Survival Mode
Function SpicyFirstHold() global
	if StorageUtil.GetIntValue(none, "FC_SpiceHoldTold") == 0
		StorageUtil.SetIntValue(none, "FC_SpiceHoldTold", 1)
		Debug.Notification(FC_Menu.Tr("The spice keeps the worst of the cold at bay."))
	endif
	if StorageUtil.GetIntValue(none, "FC_SpicyHelpShown") == 0
		StorageUtil.SetIntValue(none, "FC_SpicyHelpShown", 1)
		; the same box as under Survival Mode (FC_SurvivalBridge): Spicy protects from freezing water under every survival
		; mod since 2026-11-15 (his call: "why it shouldn't?" mention it)
		if FC_Menu.FeatureOn(2)
			Debug.MessageBox(FC_Menu.Tr("A Spicy meal or drink keeps the worst of the cold at bay and protects you from freezing water. The higher the dish's level, the less cold you feel."))
		else
			Debug.MessageBox(FC_Menu.Tr("A Spicy meal or drink keeps the worst of the cold at bay. The higher the dish's level, the less cold you feel."))
		endif
	endif
EndFunction

; ---- other survival mods' hunger and thirst (his call 2026-11-14) ----
; hunger points are Survival Mode's (30 a game hour), so a dish takes away the same game hours of hunger in any mod;
; 1000 or more: all of it. owner: FC_Menu.NeedOwner(0)
Function OtherHunger(int owner, float points) global
	float hours = 1000.0
	if points < 1000.0
		hours = points / 30.0
	endif
	if owner == 3
		FC_SunHelmBridge.LowerHunger(hours)
	elseif owner == 4
		FC_LastSeedBridge.LowerHunger(hours)
	elseif owner == 5
		FC_INeedBridge.LowerHunger(hours)
	elseif owner == 6
		RNDLower(0x002884, 0x002DE7, 6.0, hours)  ; RND_HungerPoints, RND_HungerPointsPerHour
	endif
EndFunction

; a brewed drink while another mod runs thirst (FC_EatEffectScript; the hours come from the plugin)
Function OtherThirst(float hours) global
	int owner = FC_Menu.NeedOwner(1)
	if owner == 3
		FC_SunHelmBridge.LowerThirst(hours)
	elseif owner == 4
		FC_LastSeedBridge.LowerThirst(hours)
	elseif owner == 5
		FC_INeedBridge.LowerThirst(hours)
	elseif owner == 6
		RNDLower(0x002E07, 0x002E08, 8.0, hours)  ; RND_ThirstPoints, RND_ThirstPointsPerHour
	endif
EndFunction

; ---- the hold (his design 2026-11-22: "we copy star frost", "lower hunger then hold") ----
; While a meal or baked dish's effect runs, hunger doesn't rise; while a brewed drink's effect runs, thirst doesn't.
; The plugin works out which mod runs each need and sends the mod event FC_HoldNeeds (number: the hunger owner + 16 x the
; thirst owner, 0 where nothing is held; owners as FC_Menu.NeedOwner: 1 Survival Mode, 2 Survival Mode Improved,
; 3 SunHelm, 4 Last Seed, 5 iNeed, 6 Realistic Needs) when that changes and every 10 s. Every needs mod adds the time
; passed x a rate it reads at each update (read from their own scripts 2026-11-22), so the rate is set to 0 and given
; back afterwards. A rate's own value is kept on it in PapyrusUtil storage ("FC_HeldRate", saved with the game) and the
; rates held are listed in "FC_HeldRates"; a rate someone changed during the hold (the player in that mod's menu, or
; Survival Mode Improved putting its own back at a load) is kept as it is now and held again.
; Sleeping, waiting and travel (his design 2026-11-22): the plugin gives the rates back itself when a skip starts, and
; after it gives back the share of the rise the dish covered (FC_SkipGiveBack, SkipGiveBack below). For iNeed it adds
; flags to the request: 256 hunger, 512 thirst: a skip is being counted (FC_INeedBridge.Hold), 1024: the last call, and
; the skip's number x 2048 (so an old request can't settle a newer skip; Codex review 2026-11-22e P3).
Function HoldNeeds(int request) global
	int hungerOwner = request % 16
	int thirstOwner = (request / 16) % 16
	bool skipHunger = (request / 256) % 2 == 1
	bool skipThirst = (request / 512) % 2 == 1
	bool skipFinal = (request / 1024) % 2 == 1
	int skipId = request / 2048
	GlobalVariable wantHunger = HoldRateGlobal(hungerOwner, false)
	GlobalVariable wantThirst = HoldRateGlobal(thirstOwner, true)
	; Last Seed also pays out the updates it saved up while the player was "focused" with its base rate alone
	; (_Seed_AttributeSystem.RunDelayedCycles; Codex review 2026-11-22d M2), so its base rates are held too (his OK 21:41)
	GlobalVariable wantHunger2 = none
	GlobalVariable wantThirst2 = none
	if hungerOwner == 4
		wantHunger2 = Game.GetFormFromFile(0x0029CB, "LastSeed.esp") as GlobalVariable  ; _Seed_HungerRate
	endif
	if thirstOwner == 4
		wantThirst2 = Game.GetFormFromFile(0x0029D3, "LastSeed.esp") as GlobalVariable  ; _Seed_ThirstRate
	endif
	; a copy of the list, and entries removed by value, never by an index the list may have moved under (review 2026-11-22d)
	Form[] heldList = StorageUtil.FormListToArray(none, "FC_HeldRates")
	int i = heldList.Length
	while i > 0
		i -= 1
		GlobalVariable held = heldList[i] as GlobalVariable
		if !held
			StorageUtil.FormListRemove(none, "FC_HeldRates", heldList[i], true)  ; its plugin is gone: nothing to give back
		elseif held != wantHunger && held != wantThirst && held != wantHunger2 && held != wantThirst2
			ReleaseRate(held)
			StorageUtil.FormListRemove(none, "FC_HeldRates", held, true)
		endif
	endwhile
	HoldRate(wantHunger)
	HoldRate(wantThirst)
	HoldRate(wantHunger2)
	HoldRate(wantThirst2)
	; iNeed: its bridge holds the needs' values instead of its rates (his OK 2026-11-22 21:41; only loaded while iNeed is in use)
	; (iNeed removed from the game while something was held: its rates went with it, and the bridge can't run without
	; its script types, so only the flag is cleared; review 2026-11-22d L6)
	if !Game.IsPluginInstalled("iNeed.esp")
		StorageUtil.UnsetIntValue(none, "FC_INeedHeld")
	elseif hungerOwner == 5 || thirstOwner == 5 || StorageUtil.GetIntValue(none, "FC_INeedHeld") != 0
		FC_INeedBridge.Hold(hungerOwner == 5, thirstOwner == 5, skipHunger, skipThirst, skipFinal, skipId)
	endif
EndFunction

; the rate global a hold sets to 0 (none: nothing to hold there, or iNeed)
GlobalVariable Function HoldRateGlobal(int owner, bool thirst) global
	if owner == 1 || owner == 2
		if thirst
			return none  ; Survival Mode has no thirst (ours is held in the plugin)
		endif
		return Game.GetFormFromFile(0x829, "ccQDRSSE001-SurvivalMode.esl") as GlobalVariable  ; Survival_HungerNeedRate
	elseif owner == 3
		if thirst
			return Game.GetFormFromFile(0x05C473, "SunHelmSurvival.esp") as GlobalVariable  ; _SHThirstRate
		endif
		return Game.GetFormFromFile(0x00EAAF, "SunHelmSurvival.esp") as GlobalVariable  ; _SHHungerRate
	elseif owner == 4
		if thirst
			return Game.GetFormFromFile(0x0513F9, "LastSeed.esp") as GlobalVariable  ; _Seed_Setting_RateMulti_Thirst
		endif
		return Game.GetFormFromFile(0x0513F8, "LastSeed.esp") as GlobalVariable  ; _Seed_Setting_RateMulti_Hunger
	elseif owner == 6
		string file = "RealisticNeedsandDiseases.esp"
		if !Game.IsPluginInstalled(file)
			file = "MVC Nirnfeel Immersion.esp"
		endif
		if thirst
			return Game.GetFormFromFile(0x002E08, file) as GlobalVariable  ; RND_ThirstPointsPerHour
		endif
		return Game.GetFormFromFile(0x002DE7, file) as GlobalVariable  ; RND_HungerPointsPerHour
	endif
	return none
EndFunction

Function HoldRate(GlobalVariable rate) global
	if !rate
		return
	endif
	if !StorageUtil.HasFloatValue(rate, "FC_HeldRate") || rate.GetValue() != 0.0
		StorageUtil.SetFloatValue(rate, "FC_HeldRate", rate.GetValue())
		FC_Menu.SetRate(rate, 0.0)  ; through the plugin: Survival_HungerNeedRate is a constant global
	endif
	StorageUtil.FormListAdd(none, "FC_HeldRates", rate, false)
	FC_Menu.NoteHeld(rate, StorageUtil.GetFloatValue(rate, "FC_HeldRate"))  ; every run: also after a load
EndFunction

Function ReleaseRate(GlobalVariable rate) global
	if !rate || !StorageUtil.HasFloatValue(rate, "FC_HeldRate")
		return
	endif
	if rate.GetValue() == 0.0
		FC_Menu.SetRate(rate, StorageUtil.GetFloatValue(rate, "FC_HeldRate"))
	endif
	StorageUtil.UnsetFloatValue(rate, "FC_HeldRate")
	FC_Menu.NoteReleased(rate)
EndFunction

; after a skip (sleep, wait, travel): the share of the rise the dish still covered, in the mod's own points, given back
; through that mod's own functions (the plugin works out the points: FC_SkipGiveBack; Realistic Needs and our own
; thirst it lowers itself). need: 0 hunger, 1 thirst
Function SkipGiveBack(int owner, int need, float points) global
	if points <= 0.0
		return
	endif
	if owner == 1 && need == 0
		FC_SurvivalBridge.FeedHunger(points)
	elseif owner == 2 && need == 0
		SurvivalModeImprovedApi.RestoreHungerLevel(points)
	elseif owner == 3
		FC_SunHelmBridge.GiveBack(need, points)
	elseif owner == 4
		FC_LastSeedBridge.GiveBack(need, points)
	endif
EndFunction

; a rate as its mod set it, also while a hold keeps it at 0 (the bridges turn game hours into points with it)
float Function RateNow(GlobalVariable rate) global
	if !rate
		return 0.0
	endif
	if rate.GetValue() == 0.0 && StorageUtil.HasFloatValue(rate, "FC_HeldRate")
		return StorageUtil.GetFloatValue(rate, "FC_HeldRate")
	endif
	return rate.GetValue()
EndFunction

; Realistic Needs and Diseases has no functions for this: its own food lowers the points global directly
; (rnd_eatfood.psc), and its stage follows on its next check. 1000 hours or more: all of it
Function RNDLower(int pointsId, int rateId, float fallbackRate, float hours) global
	string file = "RealisticNeedsandDiseases.esp"
	if !Game.IsPluginInstalled(file)
		file = "MVC Nirnfeel Immersion.esp"
	endif
	GlobalVariable value = Game.GetFormFromFile(pointsId, file) as GlobalVariable
	GlobalVariable rate = Game.GetFormFromFile(rateId, file) as GlobalVariable
	if !value
		return
	endif
	float perHour = fallbackRate
	if RateNow(rate) > 0.0
		perHour = RateNow(rate)  ; its own rate, also while a dish holds it at 0 (HoldNeeds)
	endif
	float after = 0.0
	if hours < 1000.0
		after = value.GetValue() - hours * perHour
	endif
	if after < 0.0
		after = 0.0
	endif
	value.SetValue(after)
EndFunction


; ---- Frostfall ----
; Frostfall's own door for warming food: its hot soups send the mod events Frost_SoupEffectStart / Frost_SoupEffectStop
; (FrostUtil's soup event emitters), and Frostfall gives +25 warmth while one runs. A Spicy dish is Cooking Skill Overhaul's hot
; dish, so it sends the same events. Nothing is kept in the save: when one Spicy effect ends while another still runs
; (a meal and a drink), the warmth is asked for again.
; Frostfall's handlers take no arguments, so the event goes out the way its own soups send it (SKSE ModEvent with
; nothing attached; Form.SendModEvent adds four arguments and Frostfall refuses the call: tested 2026-11-14)
Function SendFrostfallEvent(string eventName) global
	int handle = ModEvent.Create(eventName)
	if handle
		ModEvent.Send(handle)
	endif
EndFunction

Function SpicyFrostfallStart() global
	if Game.IsPluginInstalled("Frostfall.esp") && FC_Menu.FeatureOn(1)  ; the "Spicy holds off the cold" switch
		SendFrostfallEvent("Frost_SoupEffectStart")
	endif
EndFunction

; Frostfall's own hot soup is on the player (its _Frost_FoodBuff effect, Frostfall.esp 0x066B5D, read from its esp
; 2026-11-16): Frostfall's one food warmth is then still its soup's too
bool Function FrostfallSoupOn() global
	MagicEffect soup = Game.GetFormFromFile(0x066B5D, "Frostfall.esp") as MagicEffect
	return soup && Game.GetPlayer().HasMagicEffect(soup)
EndFunction

Function SpicyFrostfallStop() global
	if !Game.IsPluginInstalled("Frostfall.esp")
		return
	endif
	if FrostfallSoupOn()
		return  ; Frostfall's own soup still warms: its own end clears the warmth (his call 2026-11-16: the later one counts)
	endif
	Actor player = Game.GetPlayer()
	SendFrostfallEvent("Frost_SoupEffectStop")
	Utility.Wait(0.5)
	int[] ids = new int[12]
	ids[0] = 0x914
	ids[1] = 0x915
	ids[2] = 0x916
	ids[3] = 0x917
	ids[4] = 0x934
	ids[5] = 0x935
	ids[6] = 0x936
	ids[7] = 0x937
	ids[8] = 0x9C4
	ids[9] = 0x9C5
	ids[10] = 0x9C6
	ids[11] = 0x9C7
	int i = 0
	while i < 12
		MagicEffect fx = Game.GetFormFromFile(ids[i], "FreeCooking.esp") as MagicEffect
		if fx && player.HasMagicEffect(fx) && FC_Menu.FeatureOn(1)
			SendFrostfallEvent("Frost_SoupEffectStart")
			return
		endif
		i += 1
	endwhile
EndFunction

; the cold rises again as Survival Mode sets it (only if Spicy stopped it)
Function SpicyColdRelease() global
	if StorageUtil.GetIntValue(none, "FC_SpiceHold") != 1
		return
	endif
	StorageUtil.SetIntValue(none, "FC_SpiceHold", 0)
	if SurvivalModeLoaded()
		FC_SurvivalBridge.ReleaseCold()
	endif
EndFunction

; the free Survival Mode Creation is loaded (its scripts exist): only then is FC_SurvivalBridge called
bool Function SurvivalModeLoaded() global
	; (an .esl: GetModByName doesn't list light plugins, GetLightModByName does; 65535 = not loaded)
	return Game.GetLightModByName("ccQDRSSE001-SurvivalMode.esl") != 65535 || Game.GetModByName("ccQDRSSE001-SurvivalMode.esl") != 255
EndFunction

; Dragonfeast: "Akatosh's " for a Dragon Bone meal, "Auri-El's " for a Dragon Scales drink
string Function DragonPrefix(bool isDrink) global
	if isDrink
		return "Auri-El's "
	endif
	return "Akatosh's "
EndFunction

; the cooked item's name, whole for translators (the god's name may go after the dish in another language)
string Function DragonName(bool isDrink, string dishName) global
	if isDrink
		return FC_Menu.TrF("Auri-El's {0}", dishName)
	endif
	return FC_Menu.TrF("Akatosh's {0}", dishName)
EndFunction

; a dragon dish's duration: half a day of game time, in real seconds (TimeScale is game seconds per real second, 20
; unless changed)
int Function DragonDuration() global
	float scale = 20.0
	GlobalVariable timeScale = Game.GetFormFromFile(0x3A, "Skyrim.esm") as GlobalVariable
	if timeScale && timeScale.GetValue() > 0.0
		scale = timeScale.GetValue()
	endif
	return (12.0 * 3600.0 / scale) as int
EndFunction

; A Dragon Scales drink with Survival Mode (FC_DragonWarmthEffectScript): fully warm, and as refreshed as you can be
; without sleeping. Survival's own needs do it: cold through DecreaseCold (as a fire would), tiredness down to its
; Refreshed stage (needStage1Value) and not below, where the Rested bonus starts. DecreaseExhaustion is not used: it
; would also give the Rested and adoption bonuses, which stay rewards for sleeping.
Function DragonWarmth() global
	; another survival mod's cold and tiredness (his call 2026-11-14): each through that mod's own door
	int coldOwner = FC_Menu.NeedOwner(3)
	int tiredOwner = FC_Menu.NeedOwner(2)
	if coldOwner == 3 || tiredOwner == 3
		FC_SunHelmBridge.DragonWarmth(coldOwner == 3, tiredOwner == 3)
	endif
	if coldOwner == 7
		FC_FrostfallBridge.WarmFully()
	endif
	if tiredOwner == 4
		FC_LastSeedBridge.DragonRefresh()
	elseif tiredOwner == 5
		FC_INeedBridge.DragonRefresh()
	elseif tiredOwner == 6
		RNDLower(0x00BAC2, 0x00BAC3, 4.0, 1000.0)  ; RND_SleepPoints to 0 (Not Tired)
	endif
	if coldOwner != 1 && coldOwner != 2 && tiredOwner != 1 && tiredOwner != 2
		return  ; Survival Mode itself isn't running
	endif
	if SMIInstalled()
		; its API: fully warm, and tiredness down to its Refreshed stage (its stage 1), not below
		GlobalVariable coldMax = Game.GetFormFromFile(0x84B, "ccQDRSSE001-SurvivalMode.esl") as GlobalVariable
		if coldMax
			SurvivalModeImprovedApi.RestoreColdLevel(coldMax.GetValue())
		endif
		GlobalVariable tiredNow = Game.GetFormFromFile(0x816, "ccQDRSSE001-SurvivalMode.esl") as GlobalVariable
		GlobalVariable refreshed = Game.GetFormFromFile(0xA17, "SurvivalModeImproved.esp") as GlobalVariable
		if tiredNow && refreshed && tiredNow.GetValue() > refreshed.GetValue()
			SurvivalModeImprovedApi.RestoreExhaustionLevel(tiredNow.GetValue() - refreshed.GetValue())
		endif
		return
	endif
	if SurvivalModeLoaded()
		FC_SurvivalBridge.DragonWarmth()
	endif
EndFunction

; Camp rest (FC_CampRestEffectScript, on brewed drinks while Survival is on): the rest a night outdoors didn't give
; (FC_Menu.TakeCampRest, worked out by the plugin when the player woke) comes back, through Survival's own need, the
; way a bed would have given it but without the Rested bonus, which stays a reward for sleeping in a real bed
Function GiveBackRest(int amount) global
	GiveBackRestTo(amount, FC_Menu.NeedOwner(2))
EndFunction

; owner: the needs mod the plugin worked the amount out for (FC_Menu.NeedOwner numbers; 0: whichever runs tiredness now)
Function GiveBackRestTo(int amount, int owner) global
	if amount <= 0
		return
	endif
	if owner == 0
		owner = FC_Menu.NeedOwner(2)
	endif
	; another survival mod's tiredness (his call 2026-11-14): the plugin worked the amount out in that mod's own scale
	if owner == 3 || owner == 4 || owner == 5
		if owner == 3
			FC_SunHelmBridge.LowerTiredness(amount as float)
		elseif owner == 4
			FC_LastSeedBridge.LowerTiredness(amount as float)
		else
			FC_INeedBridge.LowerTiredness(amount as float)
		endif
		Debug.Notification(FC_Menu.Tr("The brewed drink shakes off the stiffness of sleeping on the ground."))
		return
	endif
	if SurvivalModeLoaded()
		FC_SurvivalBridge.GiveBackRest(amount)
	endif
EndFunction
