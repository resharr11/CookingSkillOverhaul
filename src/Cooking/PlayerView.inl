// Part of CookingMenu.cpp: included in order inside its anonymous namespace (not compiled on its own).
// The player as the cooking screen and the item cards see it (added 2026-11-13 after the two code reviews).
// The cooking screen and the item cards run on the UI thread, and at a pot the game keeps running behind the screen
// (kPausesGame is off), so reading the inventory, perks or active effects from there races the main thread changing
// them (a cooked dish going in, a spell ending). The main thread copies what the screen needs into this view a few
// times a second, and right before every screen update the scripts ask for (FC_Menu.Open, Refresh, Cooked: those
// natives wait for the frame's sync point, so they read the game safely). Code on any other thread reads the copy.

	std::thread::id g_mainThread{};  // set at kDataLoaded (CookingMenu::Register runs on the main thread)
	bool OnMainThread() { return std::this_thread::get_id() == g_mainThread; }

	// one lock for the cooking state the screen, the scripts and the eat watcher all touch: what is known and tasted,
	// the recipe book's counts, the flavour cache. Recursive: the screen's code calls helpers that take it again.
	std::recursive_mutex g_cookLock;
	using CookLock = std::scoped_lock<std::recursive_mutex>;

	constexpr std::array<RE::FormID, 6> VIEW_PERKS{ 0x862, 0x854, 0x85D, 0x85A, 0x85E, 0x857 };  // the tree perks the code asks about

	// the Misc section's recipes (Cooking/Misc.inl fills it at kDataLoaded; only "hidden" changes after): other mods' recipes at the
	// vanilla pot (place 1) and the Hearthfire oven (place 3) that our rules don't cook
	struct MiscRecipe
	{
		RE::BGSConstructibleObject* recipe{ nullptr };
		int                         place{ 0 };
		std::string                 key;  // the made item's "<plugin>|<local ID>" (FCPatch::ItemKey): what the patcher page sets
		std::atomic<bool>           hidden{ false };  // hidden by the player on the Misc recipes page (set from the menu's thread)
		MiscRecipe(RE::BGSConstructibleObject* a_recipe, int a_place, std::string a_key, bool a_hidden) :
			recipe(a_recipe), place(a_place), key(std::move(a_key)), hidden(a_hidden) {}
		MiscRecipe(const MiscRecipe& a_o) :
			recipe(a_o.recipe), place(a_o.place), key(a_o.key), hidden(a_o.hidden.load()) {}
		MiscRecipe& operator=(const MiscRecipe& a_o)
		{
			recipe = a_o.recipe;
			place = a_o.place;
			key = a_o.key;
			hidden = a_o.hidden.load();
			return *this;
		}
	};
	std::vector<MiscRecipe> g_misc;
	// mod foods the patcher made part of our rules (a vanilla look, a look already used, a variant, or a new dish):
	// filled by RunPatcher (Cooking/Patcher.inl), so their recipes don't show again in Misc
	std::unordered_set<RE::FormID> g_patchCooked;
	// mod dishes the patcher left out (no mesh, a slot nothing fills, waiting for an ingredient's group, left out by hand):
	// kept out of Misc too (his call 2026-11-14: "if they aren't quest items they aren't important")
	std::unordered_set<RE::FormID> g_patchHidden;
	// items quests hold in quest object aliases (Cooking/Patcher.inl BuildQuestItems, kDataLoaded; read-only after)
	std::unordered_set<RE::FormID> g_questItems;
	// our dishes that unlock with a mod's quest-locked recipes: dish index -> those recipes (open once any of them passes
	// for the player, checked when the screen's view is copied; Rules.inl ChooseDish). Filled by RunPatcher, read-only after
	std::unordered_map<std::int32_t, std::vector<RE::BGSConstructibleObject*>> g_lockedDishes;

	struct PlayerView
	{
		std::array<bool, VIEW_PERKS.size()> perks{};
		bool  ringWorn = false;
		float fortifyCooking = 0.0f;
		// the Cooking skill, its progress to the next level (0-1) and the game's time scale: the scripts change the
		// skill on their own thread, so the screen reads them from here (Codex review 2026-11-15, H1)
		float skill = 15.0f;
		float skillRatio = 0.0f;
		float timescale = 20.0f;
		std::unordered_map<RE::TESBoundObject*, std::int32_t> counts;  // everything carried (not filtered)
		std::vector<bool> miscShown;  // per g_misc entry: its own conditions pass for the player (a perk, a learned recipe...)
		std::unordered_set<std::int32_t> openDishes;  // dishes of g_lockedDishes whose mod's quest has unlocked them
	};
	std::mutex g_viewLock;
	PlayerView g_view;

	bool ViewPerk(RE::FormID a_id)
	{
		for (std::size_t i = 0; i < VIEW_PERKS.size(); ++i) {
			if (VIEW_PERKS[i] == a_id) {
				std::scoped_lock lock(g_viewLock);
				return g_view.perks[i];
			}
		}
		return false;
	}

	bool ViewRingWorn()
	{
		std::scoped_lock lock(g_viewLock);
		return g_view.ringWorn;
	}

	float ViewFortifyCooking()
	{
		std::scoped_lock lock(g_viewLock);
		return g_view.fortifyCooking;
	}

	PlayerView ViewSkillAndTime()
	{
		std::scoped_lock lock(g_viewLock);
		PlayerView v;
		v.skill = g_view.skill;
		v.skillRatio = g_view.skillRatio;
		v.timescale = g_view.timescale;
		return v;
	}

	bool ViewMiscShown(std::size_t a_index)
	{
		std::scoped_lock lock(g_viewLock);
		return a_index < g_view.miscShown.size() && g_view.miscShown[a_index];
	}

	std::unordered_map<RE::TESBoundObject*, std::int32_t> ViewCounts()
	{
		std::scoped_lock lock(g_viewLock);
		return g_view.counts;
	}

	// what the player carries, without quest items (a quest's poisoned cheese or a heart a quest asks for): those copies are
	// never offered to the pot or the cooking screen, like the game's own crafting (BGSConstructibleObject::CreateItem
	// counts only copies that aren't quest objects). Main thread only.
	std::unordered_map<RE::TESBoundObject*, std::int32_t> UsableCounts()
	{
		std::unordered_map<RE::TESBoundObject*, std::int32_t> counts;
		auto                                                   player = Player();
		if (!player) {
			return counts;
		}
		for (const auto& [item, data] : player->GetInventory()) {
			auto count = data.first;
			bool questCopy = false;
			if (const auto& entry = data.second; entry && entry->extraLists) {
				for (auto* xList : *entry->extraLists) {
					if (xList && xList->HasQuestObjectAlias()) {
						count -= xList->GetCount();
						questCopy = true;
					}
				}
			}
			// while a quest copy is carried, the item isn't offered at all: the scripts move items to the pot by kind
			// (ObjectReference.RemoveItem), and which copy the game takes then can't be chosen, so a quest copy could
			// be cooked (reviews 2026-11-15; rare: only items a quest marks)
			if (questCopy) {
				continue;
			}
			if (item && count > 0) {
				counts[item] = count;
			}
		}
		return counts;
	}

	// everything the player carries except quest items: live on the main thread, else the copy
	std::unordered_map<RE::TESBoundObject*, std::int32_t> CarriedCounts()
	{
		if (!OnMainThread()) {
			return ViewCounts();
		}
		return UsableCounts();
	}
