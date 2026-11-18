// Part of CookingMenu.cpp: included in order inside its anonymous namespace (not compiled on its own).
// The Misc section of the cooking screen (designed 2026-11-14, his calls): recipes at the vanilla cooking pot and the
// Hearthfire oven that our rules don't cook. Our screen covers those stations' crafting menu, so without this they could
// not be made at all. They are made exactly as their mod made them (its items, its conditions, its result), one at a
// time with R, like vanilla crafting: no pot, no buff, no discovery, no Cooking experience. "We only help a mod if it
// uses the cooking pot or the baking oven": other mods' stations (campfires included) keep their own menus.
// What goes in: from mods, every recipe at those two stations whose result our rules don't cook (the patcher's Misc
// verdicts: halves, slices, jerky...; non-food: tallow, bandages, potions mixed at the pot...); from vanilla and
// Creations, which count as vanilla, only the hand list below. The player hides the ones he doesn't want on the
// patcher's Misc recipes page (Menu.cpp; MISC lines in the edits file).

	// the vanilla and Creation recipes our rules don't cook (read from the plugins 2026-11-14; every other vanilla
	// recipe at a pot or oven makes one of our dishes, or a hot variant of one, which our rules cook without the heat)
	struct HandMisc
	{
		const char*   file;
		std::uint32_t local;
	};
	constexpr HandMisc VANILLA_MISC[] = {
		{ "ccBGSSSE037-Curios.esl", 0x000D9A },     // Rare Curios: poisoned Red Apple
		{ "ccBGSSSE037-Curios.esl", 0x00084A },     // Rare Curios: poisoned Green Apple
		{ "ccKRTSSE001_Altar.esl", 0x000F7F },      // Bittercup (quest "A Dying Wish"): Ironwood Nut soup
		{ "ccKRTSSE001_Altar.esl", 0x000CE7 },      // Bittercup: the same soup, hot
		{ "ccKRTSSE001_Altar.esl", 0x000F84 },      // Bittercup: poisoned Sujamma
		{ "ccVSVSSE004-BeAFarmer.esl", 0x00088E },  // Farming: cheese wheel from milk and salt
		{ "ccVSVSSE004-BeAFarmer.esl", 0x00088F },  // Farming: the other cheese wheel
	};

	// 1: the vanilla cooking pot (CraftingCookpot), 3: the Hearthfire oven (BYOHCraftingOven), 0: any other station
	int MiscPlaceOf(const RE::BGSConstructibleObject* a_recipe)
	{
		const auto* bench = a_recipe ? a_recipe->benchKeyword : nullptr;
		if (!bench) {
			return 0;
		}
		if (bench->GetFormID() == KEYWORD_COOKPOT) {
			return 1;
		}
		auto oven = PluginForm<RE::BGSKeyword>(0x117F7, "HearthFires.esm");
		return oven && bench == oven ? 3 : 0;
	}

	// the Misc recipes page (Menu.cpp): one row per made item, filled with g_misc
	std::vector<FC::MiscRow> g_miscRows;

	// at kDataLoaded, after the patcher. Every recipe at the pot and oven that our rules don't cook goes in; the player
	// decides on the patcher's Misc recipes page which ones the cooking screen shows (his call 2026-11-15, replacing the
	// 2026-11-14 preparation-step rule, which hid the recipes for anything one of our dishes asks for and so hid SunHelm's
	// Bottle of Water). Dishes the patcher left out (no mesh, a slot nothing fills, left out by hand) stay out, as before.
	void BuildMisc()
	{
		g_misc.clear();
		g_miscRows.clear();
		auto data = RE::TESDataHandler::GetSingleton();
		if (!data) {
			return;
		}
		// every form our rules cook: our own dishes and the patcher's new ones (dish.N), and the patcher's covered foods
		std::unordered_set<RE::FormID> cooked = g_patchCooked;
		for (std::int32_t n = 0; g_str.contains(Lower(std::format("dish.{}.n", n))); ++n) {
			if (auto form = PluginForm(static_cast<RE::FormID>(Int(std::format("dish.{}.id", n))), Str(std::format("dish.{}.p", n)).c_str())) {
				cooked.insert(form->GetFormID());
			}
		}
		// shown or hidden: food mods' patch files first, the player's own choices on top
		auto edits = g_patchFileEdits;
		edits.Replace(LoadPlayerEdits());
		std::unordered_set<RE::FormID> handList;
		for (const auto& h : VANILLA_MISC) {
			if (auto form = data->LookupForm(h.local, h.file)) {
				handList.insert(form->GetFormID());
			}
		}
		std::size_t fromMods = 0, fromVanilla = 0, hidden = 0;
		std::unordered_map<std::string, std::size_t> rowOf;
		for (auto* recipe : data->GetFormArray<RE::BGSConstructibleObject>()) {
			if (!recipe || recipe->IsDeleted() || !recipe->createdItem || !recipe->createdItem->IsBoundObject()) {
				continue;
			}
			const int place = MiscPlaceOf(recipe);
			if (place == 0) {
				continue;
			}
			const auto* file = recipe->GetFile(0);  // the plugin that added the recipe (a later mod's edit keeps that)
			const std::string_view fileName = file ? file->GetFilename() : std::string_view{};
			if (fileName.empty() || Lower(std::string(fileName)) == Lower(PLUGIN)) {
				continue;
			}
			if (IsVanillaFile(fileName)) {
				// a vanilla recipe a mod has edited to make a quest item behind a quest stays too, as a mod's quest item
				// recipe does (Codex review 2026-11-16d, M9), with its own conditions checked when made (MiscMake). One a
				// quest only unlocks makes a vanilla-look dish our rules already cook: nothing unique to keep (his rule
				// 2026-11-16, "respect design and intent")
				const auto* last = recipe->GetFile();  // the winning edit's plugin
				const bool modLocked = last && !IsVanillaFile(last->GetFilename()) && QuestConditions(recipe) && QuestItemRecipe(recipe);
				if (!handList.contains(recipe->GetFormID()) && !modLocked) {
					continue;
				}
				++fromVanilla;
			} else {
				// our rules cook it, or the patcher left that dish out (not offered here either). A quest item's recipe
				// stays, with its own conditions. One a quest only unlocks is cooked by our rules, a new dish of ours
				// unlocking with the same quest (his rule 2026-11-16, Patcher/Dishes.cpp)
				const bool questItem = QuestItemRecipe(recipe);
				if (!questItem && (cooked.contains(recipe->createdItem->GetFormID()) || g_patchHidden.contains(recipe->createdItem->GetFormID()))) {
					continue;
				}
				++fromMods;
			}
			const auto* made = recipe->createdItem;
			const auto  key = FCPatch::ItemKey(FileOf(made), made->GetLocalFormID());
			const auto  it = edits.misc.find(key);
			const bool  off = it != edits.misc.end() && it->second;
			hidden += off ? 1 : 0;
			g_misc.emplace_back(recipe, place, key, off);
			// the page's row: one per made item (SunHelm boils water three ways: one row)
			if (const auto r = rowOf.find(key); r != rowOf.end()) {
				auto& row = g_miscRows[r->second];
				++row.recipes;
				row.oven = row.oven || place == 3;
				row.pot = row.pot || place == 1;
			} else {
				FC::MiscRow row;
				row.key = key;
				row.name = NameOf(made);
				row.plugin = std::string(fileName);
				row.pot = place == 1;
				row.oven = place == 3;
				row.hiddenAtStart = off;
				row.byPatchFile = g_patchFileEdits.misc.contains(key);
				rowOf.emplace(key, g_miscRows.size());
				g_miscRows.push_back(std::move(row));
			}
		}
		std::ranges::sort(g_miscRows, [](const FC::MiscRow& a, const FC::MiscRow& b) { return a.name < b.name; });
		logger::info("Misc: {} recipes at the pot and oven our rules don't cook ({} from mods, {} vanilla and Creations); {} hidden on the Misc recipes page",
			g_misc.size(), fromMods, fromVanilla, hidden);
	}

	// R on a Misc recipe (main thread): the game's own crafting steps, checked again here because the inventory may have
	// changed since the screen drew the list. Quest copies never count, like vanilla crafting.
	bool MiscMakeable(std::size_t a_index, const std::unordered_map<RE::TESBoundObject*, std::int32_t>& a_counts);  // below

	bool MakeMisc(std::size_t a_index)
	{
		if (a_index >= g_misc.size()) {
			return false;
		}
		// the screen checked the items against its copy of the inventory (up to 100 ms old): check them again here,
		// live (Claude review 2026-11-15, L10)
		if (!MiscMakeable(a_index, UsableCounts())) {
			return false;
		}
		const auto* recipe = g_misc[a_index].recipe;
		auto        player = Player();
		if (!player || (recipe->conditions.head && !recipe->conditions.IsTrue(player, player))) {
			return false;
		}
		if (!recipe->CreateItem()) {
			return false;
		}
		RE::ItemCrafted::Event event{};
		event.item = recipe->createdItem;
		if (auto source = RE::ItemCrafted::GetEventSource()) {
			source->SendEvent(&event);  // mods listening to vanilla crafting see it too
		}
		logger::info("Misc: made {} x{}", NameOf(recipe->createdItem), recipe->data.numConstructed);
		return true;
	}

	// the exact items a Misc recipe takes (its mod's list; a count of 0 or less counts as 1, like the game)
	std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> MiscNeeds(std::size_t a_index)
	{
		std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> needs;
		if (a_index >= g_misc.size()) {
			return needs;
		}
		const auto& items = g_misc[a_index].recipe->requiredItems;
		for (std::uint32_t i = 0; i < items.numContainerObjects; ++i) {
			const auto* entry = items.containerObjects ? items.containerObjects[i] : nullptr;
			if (entry && entry->obj) {
				needs.emplace_back(entry->obj, (std::max)(1, std::abs(entry->count)));
			}
		}
		return needs;
	}

	// whether what you carry (quest copies left out, like vanilla crafting) covers a Misc recipe
	bool MiscMakeable(std::size_t a_index, const std::unordered_map<RE::TESBoundObject*, std::int32_t>& a_counts)
	{
		for (const auto& [item, need] : MiscNeeds(a_index)) {
			const auto it = a_counts.find(item);
			if (it == a_counts.end() || it->second < need) {
				return false;
			}
		}
		return a_index < g_misc.size();
	}
