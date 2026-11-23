// Part of CookingMenu.cpp: included in order inside its anonymous namespace (not compiled on its own). Pot and eating watchers, item cards, ID self-test, brewing, menu keys.
	// Namira dishes eaten without the ring (they go back in the bag): when, and which of our scripted effects the dish
	// carried (0 hunger, 1 dragon warmth, 2 camp rest), for TakeForbiddenRefund below. One entry per effect the dish
	// really has, so a dish without one leaves nothing behind for the next dish to take (reviews 2026-11-16)
	std::mutex g_forbiddenRefundLock;
	std::vector<std::pair<std::chrono::steady_clock::time_point, std::int32_t>> g_forbiddenRefunds;

	// which of our scripted effects a dish carries, as TakeForbiddenRefund kinds
	std::vector<std::int32_t> RefundKindsOf(const RE::AlchemyItem* a_dish)
	{
		std::vector<std::int32_t> kinds;
		for (const auto effect : a_dish->effects) {
			const auto base = effect ? effect->baseEffect : nullptr;
			const auto file = base ? base->GetFile(0) : nullptr;
			if (!file || file->GetFilename() != PLUGIN) {
				continue;
			}
			const auto local = base->GetLocalFormID();
			if (local == 0x9F0 || (local >= 0xA5E && local <= 0xA64)) {
				kinds.push_back(0);  // FC_SurvivalHunger, FC_Hunger160 ... FC_HungerFull
			} else if (local == 0xA66) {
				kinds.push_back(1);  // FC_DragonWarmth
			} else if (local == 0xA6A) {
				kinds.push_back(2);  // FC_CampRest
			}
		}
		return kinds;
	}

	// 0: not at a cooking place, 1: at a pot or spit, 2: at a campfire (open fire: spit cooking only), 3: at an oven
	int CookingPlace()
	{
		auto player = Player();
		auto furniture = player ? player->GetOccupiedFurniture().get() : nullptr;
		auto base = furniture ? furniture->GetBaseObject() : nullptr;
		auto keywords = base ? base->As<RE::BGSKeywordForm>() : nullptr;
		if (!keywords) {
			return 0;
		}
		if (keywords->HasKeywordID(KEYWORD_COOKPOT)) {
			return 1;
		}
		if (auto oven = PluginForm<RE::BGSKeyword>(0x117F7, "HearthFires.esm"); oven && keywords->HasKeyword(oven)) {
			return 3;  // BYOHCraftingOven: Hearthfire ovens bake
		}
		auto campfire = PluginForm<RE::BGSKeyword>(KEYWORD_CAMPFIRE, "Update.esm");
		return campfire && keywords->HasKeyword(campfire) ? 2 : 0;
	}

	bool PlayerAtCookingPot() { return CookingPlace() != 0; }

	// at a campfire, only grilled and roasted dishes; at an oven, only baked ones; a pot makes everything else
	bool NeedsPot(const CookingMenu::Dish& a_dish) { return !AllowedHere(a_dish.index); }

	// why a recipe can't be made here, for its card
	std::string WrongPlaceText()
	{
		if (g_place == 2) {
			return FC::Tr("Over an open fire you can only grill and roast. Stews, soups and drinks need a cooking pot, and baked goods an oven.");
		}
		if (g_place == 3) {
			return FC::Tr("An oven is for baking. Stews, soups, grilled food and drinks need a cooking pot.");
		}
		return FC::Tr("Bread, pies and other baked goods need an oven.");
	}

	// The player sits at a pot or spit like in vanilla (animation and all) and the game opens its crafting
	// menu; we hide that menu and show the cooking screen on top. Closing either closes both, so the player
	// stands up from the pot exactly like vanilla.
	class PotWatcher : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static PotWatcher* GetSingleton()
		{
			static PotWatcher singleton;
			return &singleton;
		}
		// a cooking station's crafting menu is open: hide it and open the cooking screen on top (UI thread). a_place
		// was worked out on the main thread (CookingPlace reads the player's live furniture)
		static void ShowOverVanilla(int a_place)
		{
			const auto place = a_place;
			if (place == 0) {
				return;
			}
			auto ui = RE::UI::GetSingleton();
			if (!ui || !ui->IsMenuOpen(RE::CraftingMenu::MENU_NAME)) {
				return;  // (a moment after the menu opened: it may be closed already)
			}
			g_place = place;
			if (auto vanilla = ui->GetMenu(RE::CraftingMenu::MENU_NAME); vanilla && vanilla->uiMovie) {
				vanilla->uiMovie->SetVisible(false);
			}
			g_overVanilla = true;
			logger::info("{}'s crafting menu opened: showing the cooking screen over it", place == 2 ? "Campfire" : place == 3 ? "Oven" : "Pot");
			// a fresh copy of the inventory first (main thread), so the screen's first list is this one (2026-11-13
			// review: it showed the last visit's or an empty list)
			SKSE::GetTaskInterface()->AddTask([]() {
				CaptureView(true);
				CookingMenu::Open();
			});
		}

		static inline std::atomic<RE::FormID> g_craftSeat{ 0 };

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (!a_event || std::string_view(a_event->menuName.c_str()) != RE::CraftingMenu::MENU_NAME) {
				return RE::BSEventNotifyControl::kContinue;
			}
			auto ui = RE::UI::GetSingleton();
			if (a_event->opening) {
				// this event comes on another thread (seen in the log 2026-11-13): which station the player sits at is
				// read on the main thread (the player's furniture changes there; Codex review 2026-11-15, H1), then
				// the screen opens on the UI thread. Solid Ground Camping's camp pot also needs its fire checked (a
				// scan of the objects around it): over a cold fire nothing cooks (its camp code says why and closes
				// the menu)
				SKSE::GetTaskInterface()->AddTask([]() {
					auto player = Player();
					auto seat = player ? player->GetOccupiedFurniture().get() : nullptr;
					g_craftSeat = seat ? seat->GetFormID() : 0;  // (the station this menu was opened at: its close stands up from it)
					if (seat && IsCampPot(seat->GetBaseObject()) && CampPotUnlit(seat.get())) {
						return;
					}
					const auto place = CookingPlace();
					SKSE::GetTaskInterface()->AddUITask([place]() { ShowOverVanilla(place); });
				});
			} else {
				if (g_overVanilla) {
					g_overVanilla = false;
					g_place = 0;
					if (ui && ui->IsMenuOpen(CookingMenu::MENU_NAME)) {
						CookingMenu::Close();
					}
				}
				// The pot's menu has closed: stand up right away, like leaving any crafting station. Without this
				// the stirring loop kept playing for a while after the screen closed.
				{
					// (only from the station this menu was opened at: a menu closing as the player already sat at another
					// station stood them up from that one; Codex whole-system review 2026-11-17)
					SKSE::GetTaskInterface()->AddTask([]() {
						const auto opened = g_craftSeat.exchange(0);
						auto player = Player();
						auto seat = player ? player->GetOccupiedFurniture().get() : nullptr;
						if (player && seat && opened && seat->GetFormID() == opened && PlayerAtCookingPot()) {
							player->NotifyAnimationGraph("IdleFurnitureExit");
							logger::info("Left the pot: sent IdleFurnitureExit");
						}
					});
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// What an ingredient is in cooking, short enough for a HUD message or a card title: "Spicy seasoning", "vegetable"
	std::string ShortRole(const Ing& a_ing)
	{
		if (a_ing.potion) {
			return FC::TrF("{0} potion", FC::Tr(a_ing.flavour.c_str()));
		}
		if (a_ing.dragon == 1) {
			return FC::Tr("Dragonfeast, Meals");
		}
		if (a_ing.dragon == 2) {
			return FC::Tr("Dragonfeast, Drinks");
		}
		if (a_ing.garlic) {
			return FC::Tr("Earthy seasoning, Neutralizes poison");
		}
		if (a_ing.alcohol) {
			return FC::Tr("Drink, Masks bad tastes");
		}
		if (a_ing.filler == "veg") {
			return FC::Tr("Vegetable");
		}
		if (a_ing.filler == "staple") {
			return FC::Tr("Staple");
		}
		if (!a_ing.flavour.empty() && a_ing.Food()) {
			return FC::TrF("Food, {0}", FC::Tr(a_ing.flavour.c_str()));
		}
		if (!a_ing.flavour.empty()) {
			return FC::TrF("{0} seasoning", FC::Tr(a_ing.flavour.c_str()));
		}
		return FC::Tr("Food");
	}

	// Eating: an ingredient eaten raw is tasted (its flavour becomes known); with Cookbook Memory, eating a
	// dish you have never cooked (an inn stew, a sweetroll from a barrel) teaches its recipe.
	// the spouse's Homecooked Meal (Skyrim.esm FoodMarriageMeal): Hearty level 1 for 10 minutes, on top of any meal
	constexpr RE::FormID HOMECOOKED_MEAL = 0x000CD614;
	void CastHomecooked()
	{
		SKSE::GetTaskInterface()->AddTask([]() {
			auto data = RE::TESDataHandler::GetSingleton();
			auto spell = data ? data->LookupForm<RE::SpellItem>(0x97D, PLUGIN) : nullptr;
			auto player = Player();
			auto caster = player ? player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant) : nullptr;
			if (spell && caster) {
				caster->CastSpellImmediate(spell, false, player, 1.0f, false, 0.0f, player);
				RE::SendHUDMessage::ShowHUDMessage(FC::Tr("Home cooking makes you Hearty."));
			}
		});
	}

	// forbidden dishes: Namira's own (a recipe slot only forbidden ingredients fill: Long Pork Roast, Reachcliff Stew,
	// and their seasoned copies) and any dish cooked with a forbidden ingredient (marked by the scripts)
	bool NamiraDish(const RE::TESForm* a_item)
	{
		// a dish the pot made with a forbidden ingredient carries one of our effects with magnitude 40 or more
		// (not Survival Mode's hunger effect, FC_SurvivalHunger 0x9F0: its magnitude is the hunger points, often over 40)
		constexpr RE::FormID SURVIVAL_HUNGER_EFFECT = 0x9F0;
		constexpr RE::FormID DISH_DURATION_EFFECT = 0xA67;  // FC_DishDuration: its magnitude is the minutes the dish lasts
		if (a_item && a_item->Is(RE::FormType::AlchemyItem)) {
			for (const auto effect : static_cast<const RE::AlchemyItem*>(a_item)->effects) {
				if (!effect) {
					continue;
				}
				const auto base = effect->baseEffect;
				const auto file = base ? base->GetFile(0) : nullptr;
				// (+100 marks a dragon dish: take it off first)
				const float mark = effect->effectItem.magnitude >= 100.0f ? effect->effectItem.magnitude - 100.0f : effect->effectItem.magnitude;
				if (file && file->GetFilename() == PLUGIN && base->GetLocalFormID() != SURVIVAL_HUNGER_EFFECT && base->GetLocalFormID() != DISH_DURATION_EFFECT && mark >= 40.0f) {
					return true;
				}
			}
		}
		if (!a_item) {
			return false;
		}
		// Namira's own dishes as their recipe's own item (by form, not by name: names are translated, and a part of a
		// name could match another item; Claude review 2026-11-15, L4), worked out once (item cards ask every frame)
		static std::unordered_set<std::int32_t> namira;
		static bool ready = false;
		CookLock lock(g_cookLock);  // item cards (UI thread) and eating (main thread) both get here
		if (!ready && !g_dishes.empty()) {
			for (const auto& dish : g_dishes) {
				if (std::ranges::any_of(dish.slots, [](const CookingMenu::Slot& a_slot) {
						return !a_slot.options.empty() && std::ranges::all_of(a_slot.options, [](RE::TESBoundObject* a_option) { return Hidden(a_option, false); });
					})) {
					namira.insert(dish.index);
				}
			}
			ready = true;
		}
		const auto it = g_dishByBase.find(a_item->GetFormID());
		return it != g_dishByBase.end() && namira.contains(it->second);
	}

	class EatWatcher : public RE::BSTEventSink<RE::TESEquipEvent>
	{
	public:
		static EatWatcher* GetSingleton()
		{
			static EatWatcher singleton;
			return &singleton;
		}
		RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>*) override
		{
			auto player = Player();
			if (!a_event || !player || a_event->actor.get() != player) {
				return RE::BSEventNotifyControl::kContinue;
			}
			// anything the player equips or takes off (the Ring of Namira in the inventory, where the game is paused and the
			// timer doesn't run): item cards read the copy, so refresh it
			SKSE::GetTaskInterface()->AddTask([]() { CaptureView(false); });
			if (!a_event->equipped) {
				return RE::BSEventNotifyControl::kContinue;
			}
			auto form = RE::TESForm::LookupByID(a_event->baseObject);
			if (!form || !form->Is(RE::FormType::Ingredient, RE::FormType::AlchemyItem)) {
				return RE::BSEventNotifyControl::kContinue;
			}
			auto item = static_cast<RE::TESBoundObject*>(form);
			if (const auto supplies = CampingForm(CAMP_SUPPLIES); supplies && form == supplies) {
				return RE::BSEventNotifyControl::kContinue;  // Camping's own item, not food
			}
			// only Namira's followers can stomach her dishes: without the ring, the dish goes back in the bag
			if (form->Is(RE::FormType::AlchemyItem) && NamiraDish(form) && !RingWorn()) {
				{
					// its hunger, warmth and camp rest effects are already on their way: each asks TakeForbiddenRefund and
					// does nothing (before, the dish came back and still fed you: endless food from one dish; reviews
					// 2026-11-15; warmth and rest too, his call 2026-11-16: a refused dish does nothing at all)
					std::scoped_lock lock(g_forbiddenRefundLock);
					const auto now = std::chrono::steady_clock::now();
					for (const auto kind : RefundKindsOf(static_cast<RE::AlchemyItem*>(form))) {
						if (g_forbiddenRefunds.size() < 32) {
							g_forbiddenRefunds.emplace_back(now, kind);
						}
					}
				}
				// by form ID: a load before the task runs can free a player-made dish (2026-11-13 review)
				SKSE::GetTaskInterface()->AddTask([id = item->GetFormID()]() {
					auto dish = RE::TESForm::LookupByID<RE::AlchemyItem>(id);
					if (auto player = Player(); player && dish && !g_loadingSave) {
						player->AddObjectToContainer(dish, nullptr, 1, nullptr);
						RE::SendHUDMessage::ShowHUDMessage(FC::Tr("Only Namira's followers can stomach this."));
					}
				});
				return RE::BSEventNotifyControl::kContinue;
			}
			if (item->GetFormID() == HOMECOOKED_MEAL) {
				CastHomecooked();
			}
			// Shared Table: who is near and following right now, for the eat script (Cooking/SharedTable.inl)
			if (form->Is(RE::FormType::AlchemyItem) && static_cast<RE::AlchemyItem*>(form)->IsFood()) {
				SnapshotSharedTable(static_cast<RE::AlchemyItem*>(form));
				SharedTableIdles(form);
				PlayerEatIdle(form);
				// a drink quenches (Cooking/Thirst.inl); outside the event, since it adds and removes abilities
				SKSE::GetTaskInterface()->AddTask([id = form->GetFormID(), game = g_gameGeneration.load()]() {
					if (game != g_gameGeneration.load()) {
						return;  // drunk in the game left by a load: nothing of it reaches the loaded one
					}
					if (auto drink = RE::TESForm::LookupByID(id); drink && !g_loadingSave) {
						ThirstOnConsume(drink);
						CampRestOnConsume(drink);
					}
				});
			}
			const auto ing = IngOf(item);
			CookLock lock(g_cookLock);
			// the first time you eat something the pot uses, you learn what it does in cooking (like Alchemy
			// reveals an ingredient's first effect); seasonings also reveal their flavour
			if (!Tasted(item, ing)) {
				g_tasted.insert(item->GetFormID());
				g_sessionTasted.insert(item->GetFormID());
				SendToScripts("FC_MenuTaste", { item });
				const auto text = FC::TrF("{0}: {1}.", item->GetName(), ShortRole(ing));
				if (!Hidden(item, RingWorn())) {  // a forbidden ingredient eaten without the ring teaches quietly
					RE::SendHUDMessage::ShowHUDMessage(text.c_str());
				}
				logger::info("Tasted by eating: {}", text);
			}
			if (const auto it = g_dishByBase.find(item->GetFormID()); it != g_dishByBase.end() && !g_known.contains(it->second) && HasTreePerk(PERK_COOKBOOK)) {
				g_known.insert(it->second);
				g_sessionLearned.insert(it->second);
				SendLearned(it->second);
				RE::SendHUDMessage::ShowHUDMessage(FC::TrF("You remember how this was made. {0} joins your recipes.", DishName(it->second, "")).c_str());
				logger::info("Recipe learned by eating: {}", Str(std::format("dish.{}.n", it->second)));
			}
			return RE::BSEventNotifyControl::kContinue;
		}
		static void SendLearned(std::int32_t a_index)
		{
			// sent from the main thread (the cooking screen's Open can run elsewhere)
			SKSE::GetTaskInterface()->AddTask([a_index]() {
				if (auto source = SKSE::GetModCallbackEventSource()) {
					SKSE::ModCallbackEvent event{ "FC_MenuLearnDish", RE::BSFixedString(std::to_string(a_index).c_str()), 0.0f, nullptr };
					source->SendEvent(&event);
				}
			});
		}
	};

	// The inventory, barter and container screens: the item card's title gets a cooking tag, like Alchemy shows
	// "Unknown" for effects you have not found yet: "Fire Salts (untasted)", "Fire Salts (Spicy seasoning)".
	// Only the card of the highlighted item changes; the list stays as it is.
	std::string CardTag(const RE::TESBoundObject* a_item)
	{
		if (a_item && a_item->GetFormID() == RingFormID()) {
			return FC::Tr("Unlocks forbidden ingredients");
		}
		// a dish cooked with a forbidden ingredient
		if (a_item && a_item->Is(RE::FormType::AlchemyItem) && NamiraDish(a_item)) {
			return FC::Tr("Namira's dish");
		}
		if (!a_item || !a_item->Is(RE::FormType::Ingredient, RE::FormType::AlchemyItem, RE::FormType::Misc)) {
			return {};
		}
		const auto ing = IngOf(a_item);
		if (!ing.Known() || (ing.dragon > 0 && !HasTreePerk(PERK_DRAGONFEAST))) {
			return {};  // (a dragon part says nothing until the Dragonfeast perk)
		}
		if (ing.need == "ring" && !RingWorn()) {
			return FC::Tr("Needs the Ring of Namira");
		}
		return Tasted(a_item, ing) ? ShortRole(ing) : std::string(FC::Tr("Untasted"));
	}

	// the card's title text field; the menus differ in where the card sits, so try the known places
	bool FindCardTitle(RE::GFxValue& a_root, RE::GFxValue& a_title)
	{
		static const std::vector<std::vector<const char*>> paths{
			{ "itemCardFadeHolder", "ItemCard_mc" },
			{ "ItemCardFadeHolder_mc", "ItemCard_mc" },
			{ "itemCard" },
			{ "ItemCard_mc" },
		};
		for (const auto& path : paths) {
			RE::GFxValue node = a_root;
			bool ok = true;
			for (const char* name : path) {
				RE::GFxValue next;
				if (!node.IsObject() || !node.GetMember(name, &next) || !next.IsObject()) {
					ok = false;
					break;
				}
				node = next;
			}
			RE::GFxValue text;
			RE::GFxValue field;
			if (ok && node.GetMember("ItemText", &text) && text.IsObject() && text.GetMember("ItemTextField", &field) && field.IsObject()) {
				a_title = field;
				return true;
			}
		}
		return false;
	}

	void TagCard(RE::GFxValue& a_root, RE::ItemList* a_list, std::string& a_lastSet)
	{
		if (!a_list || !a_root.IsObject()) {
			return;
		}
		auto selected = a_list->GetSelectedItem();
		auto entry = selected ? selected->data.objDesc : nullptr;
		const auto tag = CardTag(entry ? entry->object : nullptr);  // not GetObject(): Windows renames it
		if (tag.empty()) {
			return;
		}
		RE::GFxValue title;
		if (!FindCardTitle(a_root, title)) {
			return;
		}
		RE::GFxValue current;
		if (!title.GetMember("text", &current) || !current.IsString()) {
			return;
		}
		const std::string text = current.GetString();
		const std::string suffix = " (" + tag + ")";
		if (text.empty() || text == a_lastSet || text.ends_with(suffix)) {
			return;
		}
		a_lastSet = text + suffix;
		title.SetMember("text", a_lastSet.c_str());
	}

	template <class T>
	struct CardHook
	{
		static void AdvanceMovie(T* a_menu, float a_interval, std::uint32_t a_time)
		{
			original(a_menu, a_interval, a_time);
			static std::string lastSet;
			TagCard(a_menu->root, a_menu->itemList, lastSet);
		}
		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ T::VTABLE[0] };
			original = vtbl.write_vfunc(0x5, AdvanceMovie);
		}
		static inline REL::Relocation<decltype(AdvanceMovie)> original;
	};

	// ---- ID self-test (console: fcidtest, or bat fcidtest) ----
	// The plugin checks every item and dish the rules name (menu.tsv, data.json): is it found, and does it get the key
	// the rules use for it? Content that is not installed is skipped, not failed. Then it hands every item it found to
	// the scripts the way the cooking screen does (FC_IdTest), so they can check they get the same item and key back.
	void PapyrusIdTest(RE::StaticFunctionTag*)
	{
		auto data = RE::TESDataHandler::GetSingleton();
		if (!data) {
			return;
		}
		const auto installed = [&](const std::string& a_plugin) {
			return !a_plugin.empty() && (data->LookupLoadedModByName(a_plugin) || data->LookupLoadedLightModByName(a_plugin));
		};
		std::int32_t items = 0, itemsOk = 0, dishes = 0, dishesOk = 0, others = 0, othersOk = 0, absent = 0;
		std::set<std::string> seen;
		std::string cases;
		const auto checkItem = [&](const std::string& a_id, const std::string& a_where) {
			if (!seen.insert(Lower(a_id)).second) {
				return;
			}
			const auto colon = a_id.rfind(':');
			const std::string plugin = colon == std::string::npos ? std::string() : a_id.substr(0, colon);
			if (!installed(plugin)) {
				++absent;
				return;
			}
			++items;
			auto item = LookupItem(a_id);
			if (!item) {
				logger::error("ID test: {} ({}) was not found", a_id, a_where);
				return;
			}
			std::string want = plugin.substr(0, plugin.find('.'));
			want = Lower(want + "_" + a_id.substr(colon + 1));
			if (const auto got = PlainKeyOf(item); got != want) {
				logger::error("ID test: {} ({}) has the key {}, the rules expect {}", a_id, a_where, got, want);
				return;
			}
			++itemsOk;
			if (!cases.empty()) {
				cases += ',';
			}
			cases += IdText(item) + "=" + want;
		};
		// the cooking screen's lists: every ingredient line and every recipe slot
		std::ifstream file(MENU_FILE);
		std::string line;
		while (std::getline(file, line)) {
			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			const auto f = Split(line, '\t');
			if (f.size() >= 4 && f[0] == "I") {
				checkItem(f[1], f[2]);
			} else if (f.size() >= 7 && f[0] == "D") {
				for (std::size_t i = 6; i < f.size(); ++i) {
					const auto tilde = f[i].find('~');
					for (const auto& id : Split(tilde == std::string::npos ? f[i] : f[i].substr(tilde + 1), '|')) {
						checkItem(id, "recipe " + f[2]);
					}
				}
			}
		}
		// the dishes cooking makes (data.json), as food or drink
		for (std::int32_t i = 0; g_str.contains(Lower(std::format("dish.{}.n", i))); ++i) {
			const auto plugin = Str(std::format("dish.{}.p", i));
			if (Int(std::format("dish.{}.off", i)) == 1) {
				continue;
			}
			if (!installed(plugin)) {
				++absent;
				continue;
			}
			++dishes;
			auto form = data->LookupForm(static_cast<RE::FormID>(Int(std::format("dish.{}.id", i))), plugin);
			if (form && form->Is(RE::FormType::AlchemyItem)) {
				++dishesOk;
			} else {
				logger::error("ID test: dish {} ({}) was not found in {}", i, Str(std::format("dish.{}.n", i)), plugin);
			}
		}
		// the mod's own effects, spells and perk (data.json: FreeCooking.esp IDs), and the Ring of Namira
		for (const auto& [k, v] : g_int) {
			const auto prefix = k.substr(0, k.find('.'));
			static const std::set<std::string> own{ "fxspl", "fxspld", "fxsplb", "fxeatl", "fxeat", "fxsp", "fxspd", "plain", "perk" };
			if (!own.contains(prefix)) {
				continue;
			}
			++others;
			if (data->LookupForm(static_cast<RE::FormID>(v), PLUGIN)) {
				++othersOk;
			} else {
				logger::error("ID test: {} ({:X} in {}) was not found", k, v, PLUGIN);
			}
		}
		// items named by their key: which effect is which flavour ("eatcat.<key>"), themed potions ("pov.<key>")
		std::unordered_map<std::string, std::string> stems;  // plugin name without extension -> file name
		for (auto file : data->files) {
			if (file) {
				const std::string name(file->GetFilename());
				stems.emplace(Lower(name.substr(0, name.find('.'))), name);
			}
		}
		const auto byKey = [&](const std::string& a_key) -> RE::TESForm* {
			const auto us = a_key.rfind('_');
			const auto it = us == std::string::npos ? stems.end() : stems.find(a_key.substr(0, us));
			return it == stems.end() ? nullptr : data->LookupForm(static_cast<RE::FormID>(std::strtoul(a_key.c_str() + us + 1, nullptr, 10)), it->second);
		};
		for (const auto& [k, v] : g_str) {
			const auto dot = k.find('.');
			const auto prefix = k.substr(0, dot);
			if (prefix == "eatcat" || prefix == "pov") {
				++others;
				if (byKey(k.substr(dot + 1))) {
					++othersOk;
				} else {
					logger::error("ID test: {} was not found", k);
				}
			} else if (prefix == "pfx") {
				// potion effects by full form ID
				++others;
				if (auto fx = RE::TESForm::LookupByID(static_cast<RE::FormID>(std::strtoul(k.c_str() + dot + 1, nullptr, 10))); fx && fx->Is(RE::FormType::MagicEffect)) {
					++othersOk;
				} else {
					logger::error("ID test: the potion effect {} was not found", k);
				}
			}
		}
		++others;
		if (auto ring = data->LookupForm(static_cast<RE::FormID>(Int("ring.id")), Str("ring.p")); ring && ring->Is(RE::FormType::Armor)) {
			++othersOk;
		} else {
			logger::error("ID test: the Ring of Namira was not found");
		}
		logger::info("ID test (plugin): {} of {} items, {} of {} dishes, {} of {} effects, spells and other forms; {} skipped (content not installed)",
			itemsOk, items, dishesOk, dishes, othersOk, others, absent);
		SendTextToScripts("FC_IdTest", std::format("{}/{}/{}/{}/{}/{}/{};{}", itemsOk, items, dishesOk, dishes, othersOk, others, absent, cases));
	}

	// the console command "fcidtest" (bat fcidtest): an unused developer command of the game's, renamed
	bool ConsoleIdTest(const RE::SCRIPT_PARAMETER*, RE::SCRIPT_FUNCTION::ScriptData*, RE::TESObjectREFR*, RE::TESObjectREFR*, RE::Script*, RE::ScriptLocals*, double&, std::uint32_t&)
	{
		if (auto console = RE::ConsoleLog::GetSingleton()) {
			console->Print("%s", FC::Tr("Cooking Skill Overhaul: testing the item IDs. Close the console to see the results."));
			console->Print("%s", FC::Tr("If no message box appears, open the cooking screen once and try again."));
		}
		PapyrusIdTest(nullptr);
		return true;
	}

	void RegisterConsoleCommand()
	{
		auto command = RE::SCRIPT_FUNCTION::LocateConsoleCommand("ToggleHeapTracking");
		if (!command) {
			logger::error("Could not add the console command fcidtest");
			return;
		}
		command->functionName = "FCIdTest";
		command->shortName = "fcidtest";
		static const std::string help = FC::Tr("Cooking Skill Overhaul: checks that every item and dish the mod uses can be read");
		command->helpString = help.c_str();
		command->referenceFunction = false;
		command->numParams = 0;
		command->params = nullptr;
		command->executeFunction = &ConsoleIdTest;
		command->conditionFunction = nullptr;
		logger::info("Console command added: fcidtest");
	}

	// Papyrus: FC_Menu.DishCooked(int dish) - FC_Core.Cook tells the recipe book what was cooked (Most used, Recent)
	void PapyrusDishCooked(RE::StaticFunctionTag*, std::int32_t a_dish)
	{
		if (a_dish < 0) {
			return;
		}
		// under the cooking lock: the cooking screen reads the counts on the UI thread, the save writes them
		CookLock lock(g_cookLock);
		++g_dishUses[a_dish];
		g_dishLastCooked[a_dish] = ++g_cookClock;
	}

	// Brewing: mead, ale and wine come out of the pot young ("Young Nord Mead", a quality step lower) and become the
	// finished brew at the first 6 AM after brewing (sleeping, waiting or travelling through it counts). Aging (his rules,
	// 2026-11-15): every later morning a finished brew gains a star until it has 7 ("cap at seven stars"); the star
	// table sets the level, and a critical brew keeps its extra level, so only it can reach level 4.
	// Each step is its own pool dish ("Nord Mead II", "Nord Mead I", "Vintage Nord Mead"): the game can't keep an
	// age on stacked bottles. The aged steps are made here, from the step before, only when a bottle reaches them.
	// Bottles in chests: the morning they went in is kept per chest and brew; they catch up when the chest is opened or
	// when they are taken out. Merchant chests never age (a sold bottle comes back as it was sold). Kept in the save:
	// the young brews, each brew step, the chests' mornings and the last morning counted
	std::unordered_map<RE::FormID, RE::FormID> g_youngBrews;  // young -> finished
	std::int64_t g_lastMorning = -1;

	constexpr RE::FormID BREW_AGING_EFFECT = 0x813;  // FC_BrewAging: "Mornings of aging left: <mag>."
	constexpr RE::FormID BREW_AGED_EFFECT = 0x814;   // FC_BrewAged: "Fully aged."
	constexpr RE::FormID BREW_FINE_EFFECT = 0x81B;   // FC_BrewFine: "Fine brew: fully aged, it reaches level 4."
	constexpr RE::FormID BREW_STARS_EFFECT = 0x81C;  // FC_BrewStars: hidden, its magnitude is the brew's stars (keeps
	                                                 // brews of different stars apart: they age differently)
	constexpr RE::FormID BREW_DURATION_EFFECT = 0xA67;  // FC_DishDuration: "Lasts <mag> minutes."

	// one finished or aged brew (keyed by its dish's form ID)
	struct BrewAge
	{
		std::string   cat;           // its flavour ("Mighty")
		std::string   name;          // its seasoned name without "Young", the countdown or "(House)" ("Mighty Nord Mead")
		std::int32_t  stars{ 0 };    // its stars now (the finished brew's stars + its step)
		std::int32_t  step{ 0 };     // 0 finished, then one more each morning it aged
		std::int32_t  baseValue{ 0 };  // the finished brew's value
		float         scale{ 1.0f };   // how its brewer's skill, Cook perk and gear scaled the duration
		bool          crit{ false };   // a critical cook: one level above its stars
		bool          house{ false };
		bool          water{ false };  // water in it: one minute more (raw)
		RE::FormID    next{ 0 };       // the next step, once made
	};
	std::unordered_map<RE::FormID, BrewAge> g_brewAges;
	// (chest, brew) -> the last morning bottles of that brew went into that chest (0 = dropped in the world)
	std::map<std::pair<RE::FormID, RE::FormID>, std::int64_t> g_brewStamps;
	// our own swaps send container events too: those are skipped (counts per brew, main thread only)
	std::unordered_map<RE::FormID, std::int32_t> g_brewSwapAdds;
	std::unordered_map<RE::FormID, std::int32_t> g_brewSwapRemoves;
	std::atomic<bool> g_bartering{ false };

	// which morning it is: counts up by one each 6 AM
	std::int64_t Morning()
	{
		auto calendar = RE::Calendar::GetSingleton();
		if (!calendar || !calendar->gameDaysPassed || !calendar->gameHour) {
			return -1;
		}
		const float hour = calendar->gameHour->value;
		const auto day = static_cast<std::int64_t>(std::llround(calendar->gameDaysPassed->value - hour / 24.0f));
		return hour >= 6.0f ? day : day - 1;
	}

	std::int32_t BrewStarLevel(std::int32_t a_stars) { return a_stars >= 7 ? 3 : a_stars >= 5 ? 2 : 1; }
	std::int32_t BrewLevel(const BrewAge& a_brew) { return (std::min)(4, BrewStarLevel(a_brew.stars) + (a_brew.crit ? 1 : 0)); }
	// mornings of aging still to come: a star a morning until 7 stars ([stated] 2026-11-15: every brew ages to 7 stars,
	// "player agency"; the countdown in its name is these mornings)
	std::int32_t BrewAgingLeft(const BrewAge& a_brew) { return (std::max)(0, 7 - a_brew.stars); }
	// a drink's raw duration by its star level (FC_Core.Recalc: 4:00 / 6:00 / 8:20), plus a minute for water
	std::int32_t BrewRawDuration(std::int32_t a_starLevel, bool a_water)
	{
		return (a_starLevel >= 3 ? 500 : a_starLevel == 2 ? 360 : 240) + (a_water ? 60 : 0);
	}

	// a finished or aged brew's name counts down the mornings of aging left ("Mighty Mead III", "II", "I"); done, it is
	// "Vintage Mighty Mead" ([stated] 2026-11-15: "countdown"; FC_Core.MakeDish names the finished brew the same way)
	std::string BrewName(const std::string& a_name, std::int32_t a_left, bool a_house)
	{
		const char* counts[] = { "Vintage {0}", "{0} I", "{0} II", "{0} III", "{0} IV", "{0} V", "{0} VI" };
		auto name = FC::TrF(counts[std::clamp(a_left, 0, 6)], a_name);
		return a_house ? FC::TrF("{0} (House)", name) : name;
	}

	bool IsPoolDish(RE::FormID a_id)
	{
		const auto form = RE::TESForm::LookupByID(a_id);
		const auto file = form ? form->GetFile(0) : nullptr;
		const auto local = form ? form->GetLocalFormID() : 0u;
		return file && file->GetFilename() == PLUGIN && local >= DISH_POOL_FIRST && local <= DISH_POOL_LAST;
	}

	bool IsBrewForm(RE::FormID a_id) { return g_youngBrews.contains(a_id) || g_brewAges.contains(a_id); }

	// Papyrus: int FC_Menu.YoungBrew(int young, int finished, string cat, string name, int stars, bool crit, bool house,
	// bool water) - FC_Core.Cook made a young brew and its finished one (cat "" for a brew without seasoning: it doesn't
	// age). Returns the young dish to give: a_young, or a young dish of its own when an earlier batch made the same young
	// dish and matures into another finished one (two meads at Cooking 29 and 30 round to the same young drink but not the
	// same finished one: both matured into the second; Codex review 2026-11-16, M1). Frame-synced: runs on the main thread
	std::int32_t PapyrusYoungBrew(RE::StaticFunctionTag*, std::int32_t a_young, std::int32_t a_finished, std::string a_cat, std::string a_name,
		std::int32_t a_stars, bool a_crit, bool a_house, bool a_water)
	{
		auto young = static_cast<RE::FormID>(a_young);
		const auto finished = static_cast<RE::FormID>(a_finished);
		// both must be two different dish slots of ours: with the pool full, a dish falls back to its plain vanilla
		// look-alike, and bought Nord Mead then turned into the cooked brew every morning (review 2026-11-15)
		if (young == finished || !IsPoolDish(young) || !IsPoolDish(finished)) {
			return a_young;
		}
		if (const auto it = g_youngBrews.find(young); it != g_youngBrews.end() && it->second != finished) {
			// the finished brew goes into the young dish's identity (NeedsRecipeKey leaves it out)
			bool own = false;
			if (auto def = DishDefOf(young)) {
				def->needsKey += std::format("{}{:08X}", YOUNG_FOR, finished & 0x00FFFFFF);
				const auto made = static_cast<RE::FormID>(AddDish(*def));
				if (made != 0 && made != young && IsPoolDish(made)) {
					young = made;
					own = true;
				}
			}
			if (!own) {
				// the pool full: this batch shares the young dish and matures as the earlier one does; the bottles already
				// fermenting keep what they become (before, the new batch's finished brew replaced it for all of them;
				// Codex review 2026-11-16 afternoon, L2)
				logger::warn("Brewing: the dish pool is full; this batch matures as the earlier one of the same young drink");
				return static_cast<std::int32_t>(young);
			}
		}
		g_youngBrews[young] = finished;
		if (a_cat.empty() || a_stars <= 0 || g_brewAges.contains(finished)) {
			return static_cast<std::int32_t>(young);
		}
		// how the brewer's skill, Cook perk and gear scaled the finished brew's duration: its aged steps keep it
		const auto def = DishDefOf(finished);
		const auto raw = BrewRawDuration(BrewStarLevel(a_stars), a_water);
		const auto dur = def && !def->effects.empty() ? def->effects.front().duration : 0u;
		BrewAge brew;
		brew.cat = a_cat;
		brew.name = a_name;
		brew.stars = a_stars;
		brew.crit = a_crit;
		brew.house = a_house;
		brew.water = a_water;
		brew.baseValue = def ? def->value : 0;
		brew.scale = dur > 0 && raw > 0 ? static_cast<float>(dur) / static_cast<float>(raw) : 1.0f;
		g_brewAges[finished] = brew;
		return static_cast<std::int32_t>(young);
	}

	// the brew one morning later: the finished brew for a young one, the next aged step for a finished or aged one (made
	// here the first time a bottle reaches it), 0 when it doesn't change any more (or the dish pool is full). Main thread
	RE::FormID BrewNext(RE::FormID a_form)
	{
		if (const auto it = g_youngBrews.find(a_form); it != g_youngBrews.end()) {
			return it->second;
		}
		const auto it = g_brewAges.find(a_form);
		if (it == g_brewAges.end() || BrewAgingLeft(it->second) == 0) {
			return 0;
		}
		if (it->second.next != 0 && RE::TESForm::LookupByID(it->second.next)) {
			return it->second.next;
		}
		auto def = DishDefOf(a_form);
		if (!def || def->effects.empty()) {
			return 0;
		}
		const BrewAge& from = it->second;
		BrewAge to = from;
		to.step += 1;
		to.stars += 1;
		to.next = 0;
		const auto oldLevel = BrewLevel(from), newLevel = BrewLevel(to);
		auto dur = static_cast<std::int32_t>(std::lround(BrewRawDuration(BrewStarLevel(to.stars), to.water) * to.scale));
		dur = (std::min)(1500, (dur + 59) / 60 * 60);  // whole minutes, as every dish (25:00 at most, as Fortify Cooking)
		def->name = BrewName(to.name, BrewAgingLeft(to), to.house);
		def->value = (to.baseValue * (10 + to.step) + 5) / 10;  // +10% a step (his call 2026-11-15)
		auto eat = PluginForm(static_cast<RE::FormID>(Int(std::format("fxeatl.{}{}", to.cat, newLevel))), PLUGIN);
		const auto agingFx = PluginForm(BREW_AGING_EFFECT, PLUGIN), agedFx = PluginForm(BREW_AGED_EFFECT, PLUGIN);
		const auto starsFx = PluginForm(BREW_STARS_EFFECT, PLUGIN), durFx = PluginForm(BREW_DURATION_EFFECT, PLUGIN);
		if (!eat || !agingFx || !agedFx || !starsFx) {
			return 0;
		}
		const auto left = BrewAgingLeft(to);
		for (std::size_t i = 0; i < def->effects.size(); ++i) {
			auto& e = def->effects[i];
			if (i == 0) {
				// the meal itself: magnitude = level plus its marks (drink +10, garlic +20...), duration = the buff's
				e.effect = eat->GetFormID();
				e.magnitude = e.magnitude - static_cast<float>(oldLevel) + static_cast<float>(newLevel);
				e.duration = static_cast<std::uint32_t>(dur);
			} else if (durFx && e.effect == durFx->GetFormID()) {
				e.magnitude = static_cast<float>((dur + 30) / 60);
			} else if (e.effect == agingFx->GetFormID() || e.effect == agedFx->GetFormID()) {
				e.effect = left > 0 ? agingFx->GetFormID() : agedFx->GetFormID();
				e.magnitude = static_cast<float>(left);
			} else if (e.effect == starsFx->GetFormID()) {
				e.magnitude = static_cast<float>(to.stars);
			}
		}
		const auto id = static_cast<RE::FormID>(AddDish(*def));
		if (id == 0) {
			return 0;  // every blank dish is used: the brew stays as it is
		}
		it->second.next = id;
		g_brewAges.try_emplace(id, to);
		logger::info("Brewing: {} ages into {:X} ({} stars, level {})", def->name, id, to.stars, newLevel);
		return id;
	}

	// a brew a_mornings mornings later (as far as it still changes)
	RE::FormID BrewAfter(RE::FormID a_form, std::int64_t a_mornings)
	{
		auto form = a_form;
		for (std::int64_t i = 0; i < a_mornings && i < 8; ++i) {
			const auto next = BrewNext(form);
			if (next == 0) {
				break;
			}
			form = next;
		}
		return form;
	}

	// swaps a_count bottles in a_ref from one brew step to another; the container events this sends are skipped
	void SwapBrew(RE::TESObjectREFR* a_ref, RE::FormID a_from, RE::FormID a_to, std::int32_t a_count)
	{
		auto from = RE::TESForm::LookupByID<RE::AlchemyItem>(a_from);
		auto to = RE::TESForm::LookupByID<RE::AlchemyItem>(a_to);
		if (!a_ref || !from || !to || a_count <= 0) {
			return;
		}
		const bool player = a_ref == Player();
		if (player) {
			g_brewSwapRemoves[a_from] += a_count;
			g_brewSwapAdds[a_to] += a_count;
		}
		a_ref->RemoveItem(from, a_count, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
		a_ref->AddObjectToContainer(to, nullptr, a_count, nullptr);
	}

	// every brew in a chest (or a follower's pack) catches up with the mornings since it went in. Main thread
	void CatchUpBrews(RE::TESObjectREFR* a_ref)
	{
		const auto morning = Morning();
		if (!a_ref || morning < 0) {
			return;
		}
		const auto id = a_ref->GetFormID();
		std::vector<std::pair<RE::FormID, std::int64_t>> due;
		for (auto it = g_brewStamps.lower_bound({ id, 0 }); it != g_brewStamps.end() && it->first.first == id; ++it) {
			if (morning > it->second) {
				due.emplace_back(it->first.second, it->second);
			}
		}
		// count every due brew before any swap: a bottle that ages into another due brew's form must not be aged again with
		// that stack (Codex review 2026-11-16: a 5-star and a 6-star bottle both became Vintage in one morning)
		std::vector<std::int32_t> counts;
		for (const auto& [form, stamp] : due) {
			counts.push_back(ItemCount(a_ref, RE::TESForm::LookupByID(form)));
		}
		// every due stamp goes before any is written again: a bottle aged into another due brew's form keeps the stamp
		// written for it (erasing each in its own turn dropped it, so that bottle stopped aging there; Claude review
		// 2026-11-16b, M1)
		for (const auto& [form, stamp] : due) {
			g_brewStamps.erase({ id, form });
		}
		for (std::size_t k = 0; k < due.size(); ++k) {
			const auto& [form, stamp] = due[k];
			const auto have = counts[k];
			if (have <= 0) {
				continue;  // all taken out since
			}
			const auto to = BrewAfter(form, morning - stamp);
			if (to != form) {
				SwapBrew(a_ref, form, to, have);
			}
			g_brewStamps[{ id, to }] = morning;
		}
	}

	// bottles moving in and out of the player's inventory (Main thread, through a task)
	void BrewMoved(RE::FormID a_old, RE::FormID a_new, RE::FormID a_form, std::int32_t a_count)
	{
		constexpr RE::FormID PLAYER = 0x14;
		const auto morning = Morning();
		if (morning < 0 || !IsBrewForm(a_form)) {
			return;
		}
		if (a_old == PLAYER && a_new != PLAYER) {
			if (auto& n = g_brewSwapRemoves[a_form]; n > 0) {
				n = (std::max)(0, n - a_count);  // our own swap
				return;
			}
			if (a_new == 0) {
				return;  // dropped (or drunk): bottles in the world don't age (his call 2026-11-16)
			}
			if (a_new != 0 && g_bartering) {
				return;  // sold: merchant chests never age
			}
			g_brewStamps[{ a_new, a_form }] = morning;  // (a newer deposit makes older bottles there late, never early)
			return;
		}
		if (a_new == PLAYER && a_old != PLAYER) {
			if (auto& n = g_brewSwapAdds[a_form]; n > 0) {
				n = (std::max)(0, n - a_count);  // our own swap
				return;
			}
			if (a_old == 0) {
				// picked up from the world, or new (the pot's own bottles come from nowhere too): as it was. Before, a
				// dropped bottle's stamp stayed, and every later cook of that brew came out of the pot aged (reviews 2026-11-16)
				return;
			}
			const auto it = g_brewStamps.find({ a_old, a_form });
			if (it == g_brewStamps.end() || morning <= it->second) {
				return;
			}
			const auto to = BrewAfter(a_form, morning - it->second);
			auto container = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_old);
			if (!container || ItemCount(container, RE::TESForm::LookupByID(a_form)) <= 0) {
				g_brewStamps.erase(it);  // none left there
			}
			if (to != a_form) {
				auto player = Player();
				SwapBrew(player, a_form, to, (std::min)(a_count, ItemCount(player, RE::TESForm::LookupByID(a_form))));
			}
		}
	}

	class BrewWatcher : public RE::BSTEventSink<RE::TESContainerChangedEvent>, public RE::BSTEventSink<RE::TESActivateEvent>,
						public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static BrewWatcher* GetSingleton()
		{
			static BrewWatcher singleton;
			return &singleton;
		}
		RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
		{
			constexpr RE::FormID PLAYER = 0x14;
			if (a_event && (a_event->oldContainer == PLAYER || a_event->newContainer == PLAYER) && a_event->oldContainer != a_event->newContainer) {
				const auto o = a_event->oldContainer, n = a_event->newContainer, f = a_event->baseObj;
				const auto c = a_event->itemCount;
				SKSE::GetTaskInterface()->AddTask([o, n, f, c]() { BrewMoved(o, n, f, c); });
			}
			return RE::BSEventNotifyControl::kContinue;
		}
		RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* a_event, RE::BSTEventSource<RE::TESActivateEvent>*) override
		{
			// a chest opened: its brews catch up before the player sees them
			if (a_event && a_event->actionRef && a_event->actionRef->IsPlayerRef() && a_event->objectActivated) {
				auto target = a_event->objectActivated.get();
				const auto base = target ? target->GetBaseObject() : nullptr;
				if (base && base->Is(RE::FormType::Container)) {
					const auto handle = target->GetHandle();
					SKSE::GetTaskInterface()->AddTask([handle]() {
						if (auto ref = handle.get()) {
							CatchUpBrews(ref.get());
						}
					});
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event && a_event->menuName == RE::BarterMenu::MENU_NAME) {
				g_bartering = a_event->opening;
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	void MatureBrews()
	{
		auto player = Player();
		const auto morning = Morning();
		if (!player || morning < 0) {
			return;
		}
		if (g_lastMorning < 0 || morning < g_lastMorning) {
			g_lastMorning = morning;  // a new game or an older save: start counting from now
			return;
		}
		if (morning == g_lastMorning) {
			return;
		}
		const auto mornings = morning - g_lastMorning;  // a long sleep or wait can pass several
		g_lastMorning = morning;
		std::vector<RE::FormID> brews;
		for (const auto& [young, finished] : g_youngBrews) {
			brews.push_back(young);
		}
		for (const auto& [form, brew] : g_brewAges) {
			brews.push_back(form);
		}
		// count every carried brew before any swap: a young bottle that matures in this pass must not be aged again in
		// the same pass when the loop reaches its new form (in-game test 2026-11-15: one morning took a young wine to Vintage)
		std::vector<std::pair<RE::FormID, std::int32_t>> carried;
		for (const auto form : brews) {
			const auto n = ItemCount(player, RE::TESForm::LookupByID(form));
			if (n > 0) {
				carried.emplace_back(form, n);
			}
		}
		std::int32_t matured = 0, aged = 0;
		for (const auto& [form, n] : carried) {
			const auto to = BrewAfter(form, mornings);
			if (to == form) {
				continue;
			}
			SwapBrew(player, form, to, n);
			(g_youngBrews.contains(form) ? matured : aged) += n;
		}
		if (matured > 0) {
			RE::SendHUDMessage::ShowHUDMessage(FC::Tr("Your brews have finished fermenting overnight."));
		} else if (aged > 0) {
			RE::SendHUDMessage::ShowHUDMessage(FC::Tr("Your brews have aged overnight."));
		}
		if (matured + aged > 0) {
			logger::info("Brewing: {} young brews matured, {} brews aged", matured, aged);
		}
	}

	void WatchBrews()
	{
		RepeatAlways(2000, []() {
			if (!g_loadingSave) {
				MatureBrews();
			}
			return true;
		});
	}

	// Papyrus: int[] FC_Menu.ChooseDish(string[] keys, int count): FC_Core.Recalc asks which dish its pot makes (the
	// rules' keys of the pot entries, one per copy; the first count are used). Returns the dish index (-1: none), 1 if an
	// insect egg in its egg slot needs the drink, then the entries filling the recipe's slots. Only strings and ints
	// cross (ChooseDish, Cooking/Rules.inl).
	// FC_Menu.TakeForbiddenRefund(int kind): true once for each Namira dish just eaten without the ring (it went back in
	// the bag) that carried that effect (0 hunger, 1 dragon warmth, 2 camp rest), so it does nothing either
	// (FC_HungerEffectScript, FC_DragonWarmthEffectScript, FC_CampRestEffectScript). Only within a few seconds of eating it
	bool PapyrusTakeForbiddenRefund(RE::StaticFunctionTag*, std::int32_t a_kind)
	{
		std::scoped_lock lock(g_forbiddenRefundLock);
		const auto now = std::chrono::steady_clock::now();
		std::erase_if(g_forbiddenRefunds, [&](const auto& a_entry) { return now - a_entry.first > std::chrono::seconds(10); });
		const auto it = std::ranges::find_if(g_forbiddenRefunds, [&](const auto& a_entry) { return a_entry.second == a_kind; });
		if (it == g_forbiddenRefunds.end()) {
			return false;
		}
		g_forbiddenRefunds.erase(it);
		return true;
	}

	// FC_Menu.EmptierWater(formId): water used in a brew comes back the way its own mod gives it back ([stated] 2026-11-15
	// "waterskin should follow mod logic"): SunHelm's Waterskin 3/3 -> 2/3 -> 1/3 -> Empty Waterskin (SunHelmSurvival.esp
	// _SHWaterskin_3/2/1 0x4DE9B0/AF/AE, _SHWaterskin_Empty 0x4DE9AD, read from its esp 2026-11-15). 0: nothing comes back
	std::int32_t PapyrusEmptierWater(RE::StaticFunctionTag*, std::int32_t a_form)
	{
		const auto used = RE::TESForm::LookupByID(static_cast<RE::FormID>(a_form));
		const auto file = used ? used->GetFile(0) : nullptr;
		if (!file || file->GetFilename() != "SunHelmSurvival.esp"sv) {
			return 0;
		}
		RE::FormID next = 0;
		switch (used->GetLocalFormID()) {
		case 0x4DE9B0: next = 0x4DE9AF; break;  // 3/3 -> 2/3
		case 0x4DE9AF: next = 0x4DE9AE; break;  // 2/3 -> 1/3
		case 0x4DE9AE: next = 0x4DE9AD; break;  // 1/3 -> empty
		default: return 0;
		}
		const auto back = PluginForm(next, "SunHelmSurvival.esp");
		return back ? static_cast<std::int32_t>(back->GetFormID()) : 0;
	}

	// camp rest: the rest the last outdoor sleep didn't give, given out once (FC_CampRestEffectScript; Cooking/Survival.inl)
	std::int32_t PapyrusTakeCampRest(RE::StaticFunctionTag*)
	{
		return TakeCampRest();
	}

	// FC_Menu.TakeCampRestPay(): rest a drink already took, waiting for the scripts to give it (Cooking/Survival.inl)
	std::vector<std::int32_t> PapyrusTakeCampRestPay(RE::StaticFunctionTag*)
	{
		return TakeCampRestPay();
	}

	// FC_Menu.SurvivalOn(): Survival Mode is on
	bool PapyrusSurvivalOn(RE::StaticFunctionTag*)
	{
		return SurvivalOn();
	}

	// FC_Menu.FeatureOn(feature): a Survival feature's switch on the settings page (Settings.h FC::Feature numbers)
	bool PapyrusFeatureOn(RE::StaticFunctionTag*, std::int32_t a_feature)
	{
		return FC::FeatureSwitch(static_cast<FC::Feature>(a_feature));
	}

	// a dish's duration with the player's Fortify Cooking (FC_Core.ScaleDuration and the dragon dishes): seconds in and out,
	// dragon 1 for a dragon dish's half day (Cooking/ChefHat.inl)
	std::int32_t PapyrusFortifyDuration(RE::StaticFunctionTag*, std::int32_t a_dur, std::int32_t a_dragon)
	{
		return FortifyDuration(a_dur, a_dragon != 0);
	}

	std::vector<std::int32_t> PapyrusChooseDish(RE::StaticFunctionTag*, std::vector<RE::BSFixedString> a_keys, std::int32_t a_count)
	{
		std::vector<std::string> keys;
		const auto count = std::clamp<std::size_t>(static_cast<std::size_t>((std::max)(a_count, 0)), 0, a_keys.size());
		keys.reserve(count);
		for (std::size_t i = 0; i < count; ++i) {
			keys.push_back(Lower(a_keys[i].c_str() ? a_keys[i].c_str() : ""));
		}
		t_scriptChoice = true;  // the running cook's station (Cooking/Rules.inl AllowedHere)
		const auto choice = ChooseDish(keys, HasTreePerk(PERK_LONG_SIMMER), true);  // the cook's own main ingredient
		t_scriptChoice = false;
		std::vector<std::int32_t> result{ choice.dish, choice.needAlc ? 1 : 0 };
		result.insert(result.end(), choice.entries.begin(), choice.entries.end());
		return result;
	}

	bool RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm)
	{
		a_vm->RegisterFunction("YoungBrew", "FC_Menu", PapyrusYoungBrew);  // (young, finished, cat, name, stars, crit, house, water)
		a_vm->RegisterFunction("MakeDish", "FC_Menu", PapyrusMakeDish);
		a_vm->RegisterFunction("ChooseDish", "FC_Menu", PapyrusChooseDish);
		a_vm->RegisterFunction("FortifyDuration", "FC_Menu", PapyrusFortifyDuration);
		a_vm->RegisterFunction("TakeCampRest", "FC_Menu", PapyrusTakeCampRest);
		a_vm->RegisterFunction("TakeCampRestPay", "FC_Menu", PapyrusTakeCampRestPay);
		a_vm->RegisterFunction("INeedRestNow", "FC_Menu", PapyrusINeedRestNow);
		a_vm->RegisterFunction("TakeForbiddenRefund", "FC_Menu", PapyrusTakeForbiddenRefund);
		a_vm->RegisterFunction("EmptierWater", "FC_Menu", PapyrusEmptierWater);
		a_vm->RegisterFunction("SurvivalOn", "FC_Menu", PapyrusSurvivalOn);
		a_vm->RegisterFunction("NeedOwner", "FC_Menu", PapyrusNeedOwner);  // Cooking/OtherMods.inl
		a_vm->RegisterFunction("SurvivalInstalled", "FC_Menu", PapyrusSurvivalInstalled);
		a_vm->RegisterFunction("TakeOtherThirst", "FC_Menu", PapyrusTakeOtherThirst);  // Cooking/Thirst.inl
		a_vm->RegisterFunction("SetRate", "FC_Menu", PapyrusSetRate);  // Cooking/Thirst.inl: the hold (frame-synced)
		a_vm->RegisterFunction("NoteHeld", "FC_Menu", PapyrusNoteHeld);  // Cooking/Thirst.inl: the hold's skips (frame-synced)
		a_vm->RegisterFunction("NoteReleased", "FC_Menu", PapyrusNoteReleased);
		a_vm->RegisterFunction("SkipShare", "FC_Menu", PapyrusSkipShare);
		a_vm->RegisterFunction("SkipDone", "FC_Menu", PapyrusSkipDone);
		a_vm->RegisterFunction("SkipPending", "FC_Menu", PapyrusSkipPending);
		a_vm->RegisterFunction("SkipEndDay", "FC_Menu", PapyrusSkipEndDay);
		a_vm->RegisterFunction("HoldSession", "FC_Menu", PapyrusHoldSession);
		a_vm->RegisterFunction("FeatureOn", "FC_Menu", PapyrusFeatureOn);
		a_vm->RegisterFunction("SharedTableFollowers", "FC_Menu", PapyrusSharedTableFollowers);
		a_vm->RegisterFunction("ShareIdle", "FC_Menu", PapyrusShareIdle);
		a_vm->RegisterFunction("DishCooked", "FC_Menu", PapyrusDishCooked);
		a_vm->RegisterFunction("IdTest", "FC_Menu", PapyrusIdTest);
		a_vm->RegisterFunction("Open", "FC_Menu", PapyrusOpen);
		a_vm->RegisterFunction("SetKnown", "FC_Menu", PapyrusSetKnown);
		a_vm->RegisterFunction("Refresh", "FC_Menu", PapyrusRefresh);
		a_vm->RegisterFunction("SetTasted", "FC_Menu", PapyrusSetTasted);
		a_vm->RegisterFunction("Cooked", "FC_Menu", PapyrusCooked);
		a_vm->RegisterFunction("CookBusy", "FC_Menu", PapyrusCookBusy);
		a_vm->RegisterFunction("RuleInt", "FC_Menu", PapyrusRuleInt, true);  // read-only after kDataLoaded: no need to wait for the frame (FC_Core asks often)
		a_vm->RegisterFunction("RuleText", "FC_Menu", PapyrusRuleText, true);
		a_vm->RegisterFunction("Tr", "FC_Menu", PapyrusTr, true);
		a_vm->RegisterFunction("TrF", "FC_Menu", PapyrusTrF, true);
		a_vm->RegisterFunction("HasRuleText", "FC_Menu", PapyrusHasRuleText, true);
		a_vm->RegisterFunction("DishSaveKey", "FC_Menu", PapyrusDishSaveKey, true);
		// (frame-synced: it reads the live form's plugin; Codex review 2026-11-15, L3)
		a_vm->RegisterFunction("ItemKey", "FC_Menu", PapyrusItemKey);
		a_vm->RegisterFunction("PotionFlavourOf", "FC_Menu", PapyrusPotionFlavour);  // (frame-synced: it reads the live form's effects)
		a_vm->RegisterFunction("DishName", "FC_Menu", PapyrusDishName, true);
		return true;
	}

	// Keys the vanilla crafting menu gets from the game's own menu code, which a custom menu has to handle
	// itself while the cooking screen is open: R Cook, F one more, Q sort, Tab or Esc Exit; on a controller X Cook,
	// Y the third button, B Exit (A selects, through the movie)
	class MenuKeys : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static MenuKeys* GetSingleton()
		{
			static MenuKeys singleton;
			return &singleton;
		}
		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
		{
			for (auto event = a_event ? *a_event : nullptr; event; event = event->next) {
				const auto device = event->GetDevice();
				if (device != RE::INPUT_DEVICE::kKeyboard && device != RE::INPUT_DEVICE::kMouse && device != RE::INPUT_DEVICE::kGamepad) {
					continue;
				}
				// like vanilla menus, the buttons shown follow the device last used: controller art after a controller
				// input, keyboard art after a key or the mouse
				const bool pad = device == RE::INPUT_DEVICE::kGamepad;
				if (g_padInUse.exchange(pad) != pad) {
					if (auto ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(CookingMenu::MENU_NAME)) {
						SKSE::GetTaskInterface()->AddUITask([]() {
							if (auto menu = GetOpenMenu()) {
								menu->ShowPlatform();
							}
						});
					}
				}
				auto button = event->AsButtonEvent();
				if (!button || !button->IsDown()) {
					continue;
				}
				// 1 cook (R, X), 2 add another (F), 3 sort (Q), 4 the third button as it shows now (Y), 5 exit (Tab, Esc, B)
				const auto code = button->GetIDCode();
				int action = 0;
				if (device == RE::INPUT_DEVICE::kKeyboard) {
					action = code == KEY_R ? 1 : code == KEY_F ? 2 : code == KEY_Q ? 3 : (code == KEY_TAB || code == KEY_ESC) ? 5 : 0;
				} else if (pad) {
					action = code == PAD_X ? 1 : code == PAD_Y ? 4 : code == PAD_B ? 5 : 0;
				}
				if (action == 0) {
					continue;
				}
				auto ui = RE::UI::GetSingleton();
				if (ui && ui->IsMenuOpen(CookingMenu::MENU_NAME) && !ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME)) {
					if (action != 5) {
						// a fresh copy of the inventory first (main thread), then the screen acts on it (UI thread)
						SKSE::GetTaskInterface()->AddTask([action]() {
							CaptureView(true);
							SKSE::GetTaskInterface()->AddUITask([action]() {
								if (auto menu = GetOpenMenu()) {
									if (action == 1) {
										menu->Cook();
									} else if (action == 3 || (action == 4 && g_auxArt != 0)) {
										menu->CycleSort();  // Y does what the third button shows: Sort, or Add another
									} else {
										menu->AddAnother();
									}
								}
							});
						});
					} else {
						RE::PlaySound("UIMenuCancel");
						CookingMenu::Close();
					}
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
