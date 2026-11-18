// Part of CookingMenu.cpp: included in order inside its anonymous namespace (not compiled on its own).
// ---------------------------------------------------------------------------------------------------------------
// Our dishes in other needs mods' food lists (his call 2026-11-15). SunHelm, Last Seed and iNeed stop the game with a
// box asking the player what an unknown food is, every time a dish slot is eaten for the first time; Realistic Needs
// and Diseases silently counts unknown food as nothing. Our cooked dishes are reused slots of FreeCooking.esp, so a
// hand-made answer would also stick to whatever dish the slot holds later. Cooking Skill Overhaul answers for the player:
//   - while Cooking Skill Overhaul feeds that need itself (the "Cooked food eases hunger" switch for meals, the thirst switch for
//     brewed drinks: both reach the other mod through its own functions, Cooking/OtherMods.inl), every dish slot is
//     "not food" there (no box, nothing counted twice);
//   - with that switch off, the slot is that mod's food of the dish's size (light, medium, heavy meal, or drink: the size
//     the dish carries, DishPool.inl), or what the player picked for the recipe on the patcher's Needs mods page.
// Only each mod's own doors (read from their scripts 2026-11-15): SunHelm's ignore and size keywords, Last Seed's
// _Seed_NotFood and food lists, iNeed's ignore and food lists, RND's food lists. His call: spoilage is left alone (a
// player who turned it on in Last Seed or iNeed wants it, so no preserved / no-spoil lists).
// Save safety: the entries go into the lists' own record arrays in memory (the lists' Revert() and the save only cover
// entries scripts added), so nothing is written to the save or to Data, and removing Cooking Skill Overhaul leaves nothing behind.
// Every slot is filled in again at load, when it gets a new dish, and when a switch or a choice changes.
// ---------------------------------------------------------------------------------------------------------------

	enum NeedsChoice : int
	{
		kNeedsNone = -1,  // the slot is blank: in none of the lists
		kNeedsNotFood = 0,
		kNeedsLight = 1,
		kNeedsMedium = 2,
		kNeedsHeavy = 3,
		kNeedsDrink = 4,
	};
	constexpr const char* NEEDS_WORDS[] = { "NOTFOOD", "LIGHT", "MEDIUM", "HEAVY", "DRINK" };

	struct NeedsForms
	{
		bool looked = false;
		bool sunhelm = false, lastSeed = false, ineed = false, rnd = false;
		// SunHelm (SunHelmSurvival.esp ignore keyword 0xABE495; its size keywords are Bethesda's, in Update.esm)
		RE::BGSKeyword* shIgnore = nullptr;
		std::array<RE::BGSKeyword*, 3> shSize{};  // light, medium, heavy
		RE::BGSKeyword* shDrink = nullptr;
		RE::BGSKeyword* shAlcohol = nullptr;
		RE::BGSListForm* shAlcoholList = nullptr;  // _SHAlcoholList 0x33F759: SunHelm quenches alcohol only for drinks on it
		// Last Seed (LastSeed.esp)
		RE::BGSListForm* lsNotFood = nullptr;
		std::array<RE::BGSListForm*, 3> lsHunger{};  // Minor, Major, Superior
		RE::BGSListForm* lsDrinkSoft = nullptr;
		RE::BGSListForm* lsDrinkAlcohol = nullptr;
		RE::BGSListForm* lsAle = nullptr;           // how strong a drink is (it gets drunk like ale)
		RE::BGSListForm* lsMeatCooked = nullptr;     // the food kind when the look-alike is in none of its lists
		std::vector<RE::BGSListForm*> lsKinds;       // its food kinds (bread, stews, cooked meat...): the look-alike's one is used
		// iNeed (iNeed.esp)
		RE::BGSListForm* inIgnore = nullptr;
		std::array<RE::BGSListForm*, 3> inSize{};
		RE::BGSListForm* inDrinkAlcohol = nullptr;
		RE::BGSListForm* inDrinkSoft = nullptr;
		// Realistic Needs and Diseases (or Nirnfeel, same local IDs)
		std::array<RE::BGSListForm*, 3> rndSize{};  // snack, medium, abundant
		RE::BGSListForm* rndDrink = nullptr;
		RE::BGSListForm* rndAlcohol = nullptr;
		std::vector<RE::BGSListForm*> lists;  // every list we may put a slot in (to take it out again)
		std::vector<RE::BGSKeyword*>  keywords;
	};
	NeedsForms g_needs;
	std::mutex g_needsLock;
	std::unordered_map<std::string, int> g_needsChoice;  // the player's (or a patch file's) choice per recipe dish key
	std::vector<FC::NeedsRow>            g_needsRows;

	void NeedsLookup()
	{
		auto& f = g_needs;
		if (f.looked) {
			return;
		}
		f.looked = true;
		auto data = RE::TESDataHandler::GetSingleton();
		if (!data) {
			return;
		}
		const auto loaded = [&](const char* a_name) { return data->LookupLoadedModByName(a_name) || data->LookupLoadedLightModByName(a_name); };
		const auto list = [&](RE::FormID a_id, const char* a_file) {
			auto l = data->LookupForm<RE::BGSListForm>(a_id, a_file);
			if (l) {
				f.lists.push_back(l);
			}
			return l;
		};
		const auto keyword = [&](RE::FormID a_id, const char* a_file) {
			auto k = data->LookupForm<RE::BGSKeyword>(a_id, a_file);
			if (k) {
				f.keywords.push_back(k);
			}
			return k;
		};
		if (loaded("SunHelmSurvival.esp")) {
			f.shIgnore = keyword(0xABE495, "SunHelmSurvival.esp");
			f.shSize = { keyword(0xF3DD6D, "Update.esm"), keyword(0xF3DD6E, "Update.esm"), keyword(0xF3DD6F, "Update.esm") };
			f.shDrink = keyword(0xF3DD72, "Update.esm");
			f.shAlcohol = keyword(0xF3DD73, "Update.esm");
			f.shAlcoholList = list(0x33F759, "SunHelmSurvival.esp");  // its _SHDrinkDetection checks the list, not the keyword
			f.sunhelm = f.shIgnore != nullptr;
		}
		if (loaded("LastSeed.esp")) {
			constexpr const char* LS = "LastSeed.esp";
			f.lsNotFood = list(0x13A268, LS);
			f.lsHunger = { list(0x707B, LS), list(0x707C, LS), list(0x707D, LS) };
			f.lsDrinkSoft = list(0xC71B, LS);
			f.lsDrinkAlcohol = list(0xC719, LS);
			f.lsAle = list(0x108EB, LS);
			f.lsMeatCooked = list(0x9658, LS);
			// bread, cheese, stews, cooked meat, small game, fish, seafood, treats, pastries, cheese bowls, milk
			for (const RE::FormID id : { 0x9666, 0x9663, 0x9667, 0x9658, 0x965A, 0x965C, 0x965E, 0x9664, 0x9665, 0xC718, 0xC71A }) {
				if (auto l = data->LookupForm<RE::BGSListForm>(id, LS)) {
					f.lsKinds.push_back(l);
					if (std::ranges::find(f.lists, l) == f.lists.end()) {
						f.lists.push_back(l);
					}
				}
			}
			f.lastSeed = f.lsNotFood != nullptr;
		}
		if (loaded("iNeed.esp")) {
			constexpr const char* INEED = "iNeed.esp";  // not IN: windows.h defines IN as nothing
			f.inIgnore = list(0x3E351, INEED);
			f.inSize = { list(0xD6B, INEED), list(0xD6C, INEED), list(0xD69, INEED) };
			f.inDrinkAlcohol = list(0xD68, INEED);
			f.inDrinkSoft = list(0xBA28, INEED);
			f.ineed = f.inIgnore != nullptr;
		}
		for (const char* file : { "RealisticNeedsandDiseases.esp", "MVC Nirnfeel Immersion.esp" }) {
			if (!f.rnd && loaded(file)) {
				f.rndSize = { list(0x137797, file), list(0x137798, file), list(0x137799, file) };
				f.rndDrink = list(0x13779B, file);
				f.rndAlcohol = list(0x13779D, file);
				f.rnd = f.rndSize[0] != nullptr;
			}
		}
		logger::info("Needs mods' food lists: SunHelm {}, Last Seed {}, iNeed {}, Realistic Needs {} ({} lists, {} keywords)",
			f.sunhelm ? "found" : "-", f.lastSeed ? "found" : "-", f.ineed ? "found" : "-", f.rnd ? "found" : "-", f.lists.size(), f.keywords.size());
	}

	bool NeedsModLoaded()
	{
		NeedsLookup();
		return g_needs.sunhelm || g_needs.lastSeed || g_needs.ineed || g_needs.rnd;
	}

	// the list's own record array (not the entries scripts add, which the save keeps)
	void NeedsListAdd(RE::BGSListForm* a_list, RE::TESForm* a_form)
	{
		if (!a_list || !a_form) {
			return;
		}
		for (auto form : a_list->forms) {
			if (form == a_form) {
				return;
			}
		}
		if (a_list->forms.size() == a_list->forms.capacity()) {
			a_list->forms.reserve(a_list->forms.size() + 64);  // our slots come in ones: fewer moves of the array
		}
		a_list->forms.push_back(a_form);
	}

	void NeedsListRemove(RE::BGSListForm* a_list, RE::TESForm* a_form)
	{
		if (!a_list || !a_form) {
			return;
		}
		for (auto it = a_list->forms.begin(); it != a_list->forms.end(); ++it) {
			if (*it == a_form) {
				a_list->forms.erase(it);
				return;
			}
		}
	}

	// a recipe's key on the Needs mods page: the item it makes (its dish form), except a general dish (Meat Stew, Fish
	// Pie...: it borrows a vanilla dish's look, so the look's key would be shared with that dish), which keeps its own
	// "f:<English name>" key (Codex review 2026-11-15, M17)
	std::string NeedsKeyOfDish(std::int32_t a_dish)
	{
		if (const auto it = g_dishSaveKeys.find(a_dish); it != g_dishSaveKeys.end() && it->second.starts_with("f:")) {
			return it->second;
		}
		const auto plugin = Str(std::format("dish.{}.p", a_dish));
		auto data = RE::TESDataHandler::GetSingleton();
		auto form = data && !plugin.empty() ? data->LookupForm(static_cast<RE::FormID>(Int(std::format("dish.{}.id", a_dish))), plugin) : nullptr;
		return form ? FCPatch::ItemKey(FileOf(form), form->GetLocalFormID()) : std::string();
	}

	// the key of the recipe a slot's dish belongs to: its recipe's, or (an older save's dish) the dish it looks like
	// ("" for a blank slot)
	std::string NeedsRecipeKey(const DishDef* a_def)
	{
		// (a young brew of its own carries its finished brew after YOUNG_FOR: still the same recipe)
		if (const auto key = a_def ? a_def->needsKey.substr(0, a_def->needsKey.find(YOUNG_FOR)) : std::string{}; !key.empty()) {
			return key;
		}
		if (!a_def || !a_def->base) {
			return {};
		}
		const auto base = RE::TESForm::LookupByID(a_def->base);
		return base ? FCPatch::ItemKey(FileOf(base), base->GetLocalFormID()) : std::string();
	}

	// the dish's size from its size keyword (DishPool.inl SetDishSizeKeyword): light, medium, heavy or drink
	int NeedsSizeOf(const RE::AlchemyItem* a_dish)
	{
		constexpr RE::FormID IDS[] = { 0xAA4, 0xAA5, 0xAA6, 0xAA7 };
		for (int i = 0; i < 4; ++i) {
			auto keyword = PluginForm<RE::BGSKeyword>(IDS[i], PLUGIN);
			if (keyword && a_dish->HasKeyword(keyword)) {
				return kNeedsLight + i;
			}
		}
		return kNeedsMedium;  // no size (an older esp): a plain meal
	}

	bool NeedsIsAlcohol(const DishDef* a_def)
	{
		ThirstFormsLookup();
		return a_def && (g_thirst.brewAlcohol.contains(a_def->base) || PatchedDrinkKind(a_def->base) == 2);
	}

	// the dish carries one of our hunger effects (FC_HungerEffect 0x9F0, FC_Hunger160..FC_HungerFull 0xA5E-0xA64): it feeds
	// the hunger itself, whatever the switch says now (it was baked in when the dish was cooked)
	bool DishFeedsHunger(const DishDef* a_def)
	{
		for (const auto& e : a_def->effects) {
			const auto form = RE::TESForm::LookupByID(e.effect);
			const auto file = form ? form->GetFile(0) : nullptr;
			const auto local = form ? form->GetLocalFormID() : 0u;
			if (file && file->GetFilename() == PLUGIN && (local == 0x9F0 || (local >= 0xA5E && local <= 0xA64))) {
				return true;
			}
		}
		return false;
	}

	// what the slot is in the needs mods now. Cooking Skill Overhaul feeds the need itself: not food (a meal by the hunger effect
	// it was cooked with, a drink by the thirst switch now: drinks are handled when drunk). Else the player's choice for
	// the recipe, or the dish's own size. Called with g_dishLock held
	// a Namira dish by its definition (as Cooking/Hooks.inl NamiraDish reads the live dish, whose effects aren't set yet
	// when its needs slot is filled): one of our effects with the forbidden mark, magnitude 40 or more (+100 on a dragon
	// dish), not FC_SurvivalHunger 0x9F0 or FC_DishDuration 0xA67, whose magnitudes are amounts
	bool DefIsNamira(const DishDef* a_def)
	{
		for (const auto& e : a_def->effects) {
			const auto form = RE::TESForm::LookupByID(e.effect);
			const auto file = form ? form->GetFile(0) : nullptr;
			const auto local = form ? form->GetLocalFormID() : 0u;
			const float mark = e.magnitude >= 100.0f ? e.magnitude - 100.0f : e.magnitude;
			if (file && file->GetFilename() == PLUGIN && local != 0x9F0 && local != 0xA67 && mark >= 40.0f) {
				return true;
			}
		}
		return false;
	}

	int NeedsChoiceFor(const RE::AlchemyItem* a_dish, const DishDef* a_def)
	{
		if (!a_def) {
			return kNeedsNone;
		}
		// a Namira dish feeds only through our own effects, which know whether the ring was worn: never the needs mod's own
		// food detection (his call 2026-11-16: a dish refused without the ring does nothing at all)
		if (DefIsNamira(a_def)) {
			return kNeedsNotFood;
		}
		const int  size = NeedsSizeOf(a_dish);
		const bool drink = size == kNeedsDrink;
		const auto options = FC::GetOptions();
		// (before: the hunger switch alone decided, so meals cooked before a switch change fed twice or not at all;
		// reviews 2026-11-15)
		if (drink ? options.thirst : DishFeedsHunger(a_def)) {
			return kNeedsNotFood;
		}
		{
			std::scoped_lock lock(g_needsLock);
			if (const auto it = g_needsChoice.find(NeedsRecipeKey(a_def)); it != g_needsChoice.end()) {
				return it->second;
			}
		}
		return size;
	}

	// fills one slot into the needs mods' lists (a_def nullptr: takes it out of all of them)
	void NeedsApplySlot(RE::AlchemyItem* a_dish, const DishDef* a_def)
	{
		if (!a_dish || !NeedsModLoaded()) {
			return;
		}
		auto& f = g_needs;
		for (auto l : f.lists) {
			NeedsListRemove(l, a_dish);
		}
		std::vector<RE::BGSKeyword*> old;
		for (auto k : f.keywords) {
			if (a_dish->HasKeyword(k)) {
				old.push_back(k);
			}
		}
		if (!old.empty()) {
			a_dish->RemoveKeywords(old);
		}
		const int choice = NeedsChoiceFor(a_dish, a_def);
		if (choice == kNeedsNone) {
			return;
		}
		const bool alcohol = choice == kNeedsDrink && NeedsIsAlcohol(a_def);
		const auto addKeyword = [&](RE::BGSKeyword* a_keyword) {
			if (a_keyword) {
				a_dish->AddKeyword(a_keyword);
			}
		};
		if (choice == kNeedsNotFood) {
			if (f.sunhelm) addKeyword(f.shIgnore);
			NeedsListAdd(f.lsNotFood, a_dish);
			NeedsListAdd(f.inIgnore, a_dish);
			return;  // Realistic Needs: in none of its lists is "not food" already
		}
		if (choice == kNeedsDrink) {
			if (f.sunhelm) addKeyword(alcohol ? f.shAlcohol : f.shDrink);
			if (f.sunhelm && alcohol) NeedsListAdd(f.shAlcoholList, a_dish);  // (reviews 2026-11-15: the keyword alone quenched nothing)
			NeedsListAdd(alcohol ? f.lsDrinkAlcohol : f.lsDrinkSoft, a_dish);
			if (alcohol) NeedsListAdd(f.lsAle, a_dish);
			NeedsListAdd(alcohol ? f.inDrinkAlcohol : f.inDrinkSoft, a_dish);
			NeedsListAdd(alcohol ? f.rndAlcohol : f.rndDrink, a_dish);
			return;
		}
		const auto i = static_cast<std::size_t>(choice - kNeedsLight);
		if (f.sunhelm) addKeyword(f.shSize[i]);
		if (f.lastSeed) {
			// the kind of food is the look-alike's own in Last Seed (a stew stays a stew, a pie a pastry), cooked meat if none
			RE::BGSListForm* kind = nullptr;
			if (const auto base = RE::TESForm::LookupByID(a_def->base)) {
				for (auto l : f.lsKinds) {
					if (l->HasForm(base)) {
						kind = l;
						break;
					}
				}
			}
			NeedsListAdd(kind ? kind : f.lsMeatCooked, a_dish);
			NeedsListAdd(f.lsHunger[i], a_dish);
		}
		NeedsListAdd(f.inSize[i], a_dish);
		NeedsListAdd(f.rndSize[i], a_dish);
	}

	// every used slot again (a switch or a choice changed). Main thread, through a task
	void NeedsRefreshAll()
	{
		if (!NeedsModLoaded()) {
			return;
		}
		std::scoped_lock lock(g_dishLock);
		std::size_t done = 0;
		for (const auto& [local, def] : g_dishDefs) {
			if (auto dish = DishSlotForm(local)) {
				NeedsApplySlot(dish, &def);
				++done;
			}
		}
		logger::info("Needs mods: {} cooked dishes sorted again", done);
	}

	// the recipes for the page, and the choices from patch files and the player's edits. At kDataLoaded, after the rules
	// and the patcher (dish.N holds our dishes and the patcher's new ones)
	void BuildNeedsRows()
	{
		g_needsRows.clear();
		if (!NeedsModLoaded()) {
			return;
		}
		auto edits = g_patchFileEdits;
		edits.Replace(LoadPlayerEdits());
		{
			std::scoped_lock lock(g_needsLock);
			g_needsChoice.clear();
			for (const auto& [key, word] : edits.needs) {
				for (int c = 0; c < 5; ++c) {
					if (word == NEEDS_WORDS[c]) g_needsChoice[key] = c;
				}
			}
		}
		std::unordered_set<std::string> seen;
		for (std::int32_t n = 0; g_str.contains(Lower(std::format("dish.{}.n", n))); ++n) {
			if (Int(std::format("dish.{}.off", n)) == 1) {
				continue;
			}
			FC::NeedsRow row;
			row.key = NeedsKeyOfDish(n);
			if (row.key.empty()) {
				continue;
			}
			if (!seen.insert(row.key).second) {
				continue;
			}
			row.name = FC::Tr(Str(std::format("dish.{}.n", n)).c_str());
			row.drink = Int(std::format("dish.{}.dr", n)) == 1;
			row.byPatchFile = g_patchFileEdits.needs.contains(row.key);
			g_needsRows.push_back(std::move(row));
		}
		std::ranges::sort(g_needsRows, [](const FC::NeedsRow& a, const FC::NeedsRow& b) { return a.name < b.name; });
		logger::info("Needs mods page: {} recipes, {} set by hand or by patch files", g_needsRows.size(), g_needsChoice.size());
	}
