	// ---------------------------------------------------------------------------------------------------------------
	// The dish pool: FreeCooking.esp carries 1280 blank dishes (FC_DishB00 to FC_DishFFF, "Cooked Dish"). When the pot
	// makes a dish nobody made before in this game, it takes the next blank one and fills it in: the base dish's look,
	// weight and sounds, the name, the value and the effects. The game doesn't save changes made to records, so what
	// each used dish is lives in our part of the save ('DISH') and is filled in again on every load. Used dishes are
	// never given back (an old one may still sit in a chest somewhere), and two pots that make the same thing share one.
	// ---------------------------------------------------------------------------------------------------------------

	constexpr RE::FormID DISH_POOL_FIRST = 0xB00;
	constexpr RE::FormID DISH_POOL_LAST = 0xFFF;
	// whether the save being loaded had Cooking Skill Overhaul's SKSE data (its .skse file): false after the revert, true once the
	// load callback runs (SKSE runs it only when the save has a .skse file)
	bool g_cosaveRead{ false };
	// this save's dish record was read whole (the co-save is there and holds our dishes); false on a new game, a save
	// without its .skse file, or a damaged record
	bool g_dishRecordRead{ false };
	// FC_DishPoolNext (FreeCooking.esp GLOB 0x809): the next dish slot never handed out in this game. A global lives
	// in the .ess itself, so it survives a lost .skse file: new dishes then never reuse a slot a stored dish already
	// has (before, a dish in a chest could turn into another dish; Codex review 2026-11-15)
	constexpr RE::FormID DISH_POOL_NEXT_GLOBAL = 0x809;
	RE::TESGlobal* DishPoolNextGlobal()
	{
		static RE::TESGlobal* global = nullptr;
		if (!global) {
			if (auto data = RE::TESDataHandler::GetSingleton()) {
				global = data->LookupForm<RE::TESGlobal>(DISH_POOL_NEXT_GLOBAL, PLUGIN);
			}
		}
		return global;
	}
	RE::FormID DishPoolNextSaved()
	{
		auto global = DishPoolNextGlobal();
		const auto v = global ? global->value : 0.0f;
		return v >= static_cast<float>(DISH_POOL_FIRST) && v <= static_cast<float>(DISH_POOL_LAST + 1) ? static_cast<RE::FormID>(v) : 0;
	}
	void DishPoolNextStore(RE::FormID a_next)
	{
		if (auto global = DishPoolNextGlobal(); global && static_cast<float>(a_next) > global->value) {
			global->value = static_cast<float>(a_next);
		}
	}
	constexpr std::uint32_t DISH_POOL_RECORD = 'DISH';
	constexpr std::size_t   MAX_DISH_EFFECTS = 16;  // a dish's effects, at most (the save keeps the count in one byte)

	struct DishEffectDef
	{
		RE::FormID    effect = 0;  // the magic effect (load-order form ID)
		float         magnitude = 0.0f;
		std::uint32_t duration = 0;
	};

	// in a dish's needsKey, after the recipe's key: the finished brew a young brew matures into, for two batches whose young
	// dishes are otherwise the same (Cooking/Hooks.inl PapyrusYoungBrew)
	constexpr std::string_view YOUNG_FOR = "|young>";

	struct DishDef
	{
		RE::FormID                 base = 0;  // the dish it looks like (load-order form ID); 0: keep the blank look
		std::string                name;
		std::int32_t               value = 0;
		std::vector<DishEffectDef> effects;
		// the recipe's key on the Needs mods page (Needs.inl NeedsKeyOfDish): two recipes with the same look (Meat Stew
		// and Venison Stew, Fish Pie and Apple Pie) keep their own needs setting ("": an older save's dish, by its look;
		// Codex review 2026-11-15, M17)
		std::string needsKey;

		[[nodiscard]] std::string Key() const
		{
			std::string key = std::format("{:08X}|{}|{}|{}", base, name, value, needsKey);
			for (const auto& e : effects) {
				key += std::format("|{:08X}:{:.3f}:{}", e.effect, e.magnitude, e.duration);
			}
			return key;
		}
	};

	// how a blank dish looked when we first touched it, and the effect objects it uses (kept and reused, never freed:
	// an effect running on someone may still point at one)
	struct DishSlotState
	{
		bool                                   captured = false;
		RE::BSFixedString                      model;
		RE::BSResource::ID*                    textures = nullptr;
		std::uint32_t*                         addons = nullptr;
		std::uint16_t                          numTextures = 0;
		std::uint16_t                          numAddons = 0;
		RE::TESModelTextureSwap::AlternateTexture* swaps = nullptr;
		std::uint32_t                          numSwaps = 0;
		RE::BSFixedString                      name;
		float                                  weight = 0.0f;
		RE::BGSSoundDescriptorForm*            pickup = nullptr;
		RE::BGSSoundDescriptorForm*            putdown = nullptr;
		RE::BGSSoundDescriptorForm*            consume = nullptr;
		std::int32_t                           value = 0;
		RE::EffectSetting*                     blankEffect = nullptr;
		std::vector<RE::Effect*>               owned;
		std::string                            applied;  // the Key() of what the record holds now ("": blank)
	};

	std::mutex                               g_dishLock;
	std::map<RE::FormID, DishDef>            g_dishDefs;      // this game's used dishes, by local ID in our plugin
	std::unordered_map<std::string, RE::FormID> g_dishByKey;   // the same, by what they are
	std::map<RE::FormID, DishSlotState>      g_dishSlots;     // every slot this session touched, any game
	bool                                     g_dishPoolFullTold = false;

	RE::AlchemyItem* DishSlotForm(RE::FormID a_local)
	{
		auto data = RE::TESDataHandler::GetSingleton();
		return data ? data->LookupForm<RE::AlchemyItem>(a_local, PLUGIN) : nullptr;
	}

	void CaptureDishSlot(RE::AlchemyItem* a_dish, DishSlotState& a_state)
	{
		if (a_state.captured) {
			return;
		}
		a_state.captured = true;
		a_state.model = a_dish->model;
		a_state.textures = a_dish->textures;
		a_state.addons = a_dish->addons;
		a_state.numTextures = a_dish->numTextures;
		a_state.numAddons = a_dish->numAddons;
		a_state.swaps = a_dish->alternateTextures;
		a_state.numSwaps = a_dish->numAlternateTextures;
		a_state.name = a_dish->fullName;
		a_state.weight = a_dish->weight;
		a_state.pickup = a_dish->pickupSound;
		a_state.putdown = a_dish->putdownSound;
		a_state.consume = a_dish->data.consumptionSound;
		a_state.value = a_dish->data.costOverride;
		for (auto effect : a_dish->effects) {
			if (effect) {
				a_state.owned.push_back(effect);
			}
		}
		if (!a_state.owned.empty()) {
			a_state.blankEffect = a_state.owned.front()->baseEffect;
		}
	}

	// fills the record in (a_def nullptr: back to the blank dish); runs again only when the record holds something else
	// Eating Animations and Sounds (Nexus SE 42602): it animates a food, with that food in hand, by keywords KID gives the
	// vanilla foods at load (EASKID_All, a group, and one EASkey_<food> that picks the animation and model). A pool dish
	// takes the EAS keywords of the dish it looks like, so EAS plays that dish's animation with that dish in hand. The
	// old dish's EAS keywords go first (a slot changes look between games). Without EAS no form carries them: nothing
	bool IsEasKeyword(const RE::BGSKeyword* a_keyword)
	{
		const std::string_view id = a_keyword ? a_keyword->GetFormEditorID() : "";
		return id.starts_with("EASKID_") || id.starts_with("EASkey_");
	}

	void CopyEatingAnimationKeywords(RE::AlchemyItem* a_dish, const RE::AlchemyItem* a_base)
	{
		std::vector<RE::BGSKeyword*> old, wanted;
		a_dish->ForEachKeyword([&](RE::BGSKeyword* a_keyword) {
			if (IsEasKeyword(a_keyword)) {
				old.push_back(a_keyword);
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		if (!old.empty()) {
			a_dish->RemoveKeywords(old);
		}
		if (a_base) {
			a_base->ForEachKeyword([&](RE::BGSKeyword* a_keyword) {
				if (IsEasKeyword(a_keyword)) {
					wanted.push_back(a_keyword);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}
		if (!wanted.empty()) {
			a_dish->AddKeywords(wanted);
			logger::info("Dish pool: {} takes {} Eating Animations keywords from {}", a_dish->GetName(), wanted.size(), a_base->GetName());
		}
	}

	// Dish-size keywords (FreeCooking.esp 0xAA4 FC_DishLight, 0xAA5 FC_DishMedium, 0xAA6 FC_DishHeavy, 0xAA7 FC_DishDrink).
	// Other needs mods (SunHelm, Last Seed, Realistic Needs...) know foods by their own lists, and our cooked dishes are
	// in none. His rule (2026-11-14): we don't patch other mods, we give their patch makers something to hook into, so
	// each cooked dish carries one of these, set from its Survival hunger: our steps 160 light, 200-240 medium, 280 and up
	// (or a full Dragon Bone meal) heavy; with "Dishes keep you fed" off, from the vanilla food's own hunger effect
	// (Update.esm 0x2EE1 very small, 0x2EE2 small, 0x2EE3 medium, 0x2EE4 large; 0x24E1-0x24E4 until 2026-11-16, which are
	// placed references, reviews 2026-11-16); brewed drinks get Drink.
	// Called with g_dishLock held (ApplyDishSlot).
	void SetDishSizeKeyword(RE::AlchemyItem* a_dish, const DishDef* a_def)
	{
		static std::array<RE::BGSKeyword*, 4> keywords{};
		static bool looked = false;
		if (!looked) {
			looked = true;
			constexpr RE::FormID IDS[] = { 0xAA4, 0xAA5, 0xAA6, 0xAA7 };
			for (std::size_t i = 0; i < keywords.size(); ++i) {
				keywords[i] = PluginForm<RE::BGSKeyword>(IDS[i], PLUGIN);
			}
			if (!keywords[0]) {
				logger::warn("Dish pool: the dish-size keywords aren't in FreeCooking.esp (older esp?)");
			}
		}
		std::vector<RE::BGSKeyword*> old;
		for (auto keyword : keywords) {
			if (keyword && a_dish->HasKeyword(keyword)) {
				old.push_back(keyword);
			}
		}
		if (!old.empty()) {
			a_dish->RemoveKeywords(old);
		}
		if (!a_def) {
			return;
		}
		int size = -1;  // 0 light, 1 medium, 2 heavy, 3 drink
		for (const auto& e : a_def->effects) {
			const auto form = RE::TESForm::LookupByID(e.effect);
			const auto file = form ? form->GetFile(0) : nullptr;
			if (!file) {
				continue;
			}
			const auto local = form->GetLocalFormID();
			int found = -1;
			if (file->GetFilename() == PLUGIN) {
				if (local == 0xA64 || (local >= 0xA61 && local <= 0xA63)) {
					found = 2;  // 280-360, or fully sated
				} else if (local == 0xA5F || local == 0xA60 || local == 0x9F0) {
					found = 1;  // 200-240 (0x9F0: the old single hunger effect, 240)
				} else if (local == 0xA5E) {
					found = 0;  // 160
				}
			} else if (file->GetFilename() == "Update.esm") {
				if (local == 0x2EE4) {
					found = 2;
				} else if (local == 0x2EE3) {
					found = 1;
				} else if (local == 0x2EE1 || local == 0x2EE2) {
					found = 0;
				}
			}
			size = std::max(size, found);
		}
		if (size < 0) {
			const auto it = g_dishByBase.find(a_def->base);
			if (it != g_dishByBase.end() && Int(std::format("dish.{}.dr", it->second)) == 1) {
				size = 3;
			}
		}
		if (size >= 0 && keywords[static_cast<std::size_t>(size)]) {
			a_dish->AddKeywords({ keywords[static_cast<std::size_t>(size)] });
		}
	}

	void NeedsApplySlot(RE::AlchemyItem* a_dish, const DishDef* a_def);  // Cooking/Needs.inl: other needs mods' food lists

	void ApplyDishSlot(RE::FormID a_local, const DishDef* a_def)
	{
		auto dish = DishSlotForm(a_local);
		if (!dish) {
			return;
		}
		auto& state = g_dishSlots[a_local];
		CaptureDishSlot(dish, state);
		const auto key = a_def ? a_def->Key() : std::string{};
		if (state.applied == key) {
			return;
		}
		state.applied = key;

		// a player-made potion as the base: its model buffers are only borrowed and go when that potion does (2026-11-13
		// review), so it gets the blank look instead
		const auto found = a_def && a_def->base ? RE::TESForm::LookupByID<RE::AlchemyItem>(a_def->base) : nullptr;
		const auto base = found && !found->IsDynamicForm() ? found : nullptr;
		if (base) {
			dish->SetModel(base->GetModel());
			dish->textures = base->textures;
			dish->addons = base->addons;
			dish->numTextures = base->numTextures;
			dish->numAddons = base->numAddons;
			dish->alternateTextures = base->alternateTextures;
			dish->numAlternateTextures = base->numAlternateTextures;
			dish->weight = base->weight;
			dish->pickupSound = base->pickupSound;
			dish->putdownSound = base->putdownSound;
			dish->data.consumptionSound = base->data.consumptionSound;
		} else {
			dish->SetModel(state.model.c_str());
			dish->textures = state.textures;
			dish->addons = state.addons;
			dish->numTextures = state.numTextures;
			dish->numAddons = state.numAddons;
			dish->alternateTextures = state.swaps;
			dish->numAlternateTextures = state.numSwaps;
			dish->weight = state.weight;
			dish->pickupSound = state.pickup;
			dish->putdownSound = state.putdown;
			dish->data.consumptionSound = state.consume;
		}
		CopyEatingAnimationKeywords(dish, base);
		SetDishSizeKeyword(dish, a_def);
		NeedsApplySlot(dish, a_def);  // after the size: SunHelm, Last Seed, iNeed and Realistic Needs see the slot's new dish
		dish->fullName = a_def ? RE::BSFixedString(a_def->name.c_str()) : state.name;
		dish->data.costOverride = a_def ? a_def->value : state.value;
		dish->data.flags.set(RE::AlchemyItem::AlchemyFlag::kCostOverride, RE::AlchemyItem::AlchemyFlag::kFoodItem);

		// the effects: the same effect objects are changed in place, so anything already pointing at them stays valid
		std::vector<DishEffectDef> wanted;
		if (a_def) {
			for (const auto& e : a_def->effects) {
				if (RE::TESForm::LookupByID<RE::EffectSetting>(e.effect)) {
					wanted.push_back(e);
				}
			}
		}
		if (wanted.empty()) {
			// never an empty effect list: the item card crashes on one
			wanted.push_back({ state.blankEffect ? state.blankEffect->GetFormID() : 0, 0.0f, 0 });
		}
		if (wanted.size() > MAX_DISH_EFFECTS) {
			wanted.resize(MAX_DISH_EFFECTS);
		}
		while (state.owned.size() < wanted.size()) {
			state.owned.push_back(new RE::Effect());
		}
		// The item card can be reading this list on the UI thread while a native changes it (2026-11-13 review): the
		// list never moves (room for the most it can hold, made once) and is never empty for a moment (entries are
		// replaced in place, extra ones dropped at the end)
		dish->effects.reserve(MAX_DISH_EFFECTS);
		for (std::size_t i = 0; i < wanted.size(); ++i) {
			auto effect = state.owned[i];
			auto setting = wanted[i].effect ? RE::TESForm::LookupByID<RE::EffectSetting>(wanted[i].effect) : nullptr;
			effect->baseEffect = setting ? setting : state.blankEffect;
			effect->effectItem.magnitude = wanted[i].magnitude;
			effect->effectItem.area = 0;
			effect->effectItem.duration = wanted[i].duration;
			effect->cost = 0.0f;
			if (i < dish->effects.size()) {
				dish->effects[static_cast<std::uint32_t>(i)] = effect;
			} else {
				dish->effects.push_back(effect);
			}
		}
		while (dish->effects.size() > wanted.size()) {
			dish->effects.pop_back();
		}
		dish->hostileCount = 0;
		dish->avEffectSetting = dish->effects.front()->baseEffect;
	}

	// makes every record match this game: its dishes filled in, the ones another game used blank again
	void ReconcileDishPool()
	{
		std::scoped_lock lock(g_dishLock);
		for (const auto& [local, def] : g_dishDefs) {
			ApplyDishSlot(local, &def);
		}
		for (auto& [local, state] : g_dishSlots) {
			if (!state.applied.empty() && !g_dishDefs.contains(local)) {
				ApplyDishSlot(local, nullptr);
			}
		}
	}

	std::string NeedsKeyOfDish(std::int32_t a_dish);  // Cooking/Needs.inl
	std::int32_t AddDish(const DishDef& def);

	// Papyrus: int FC_Menu.MakeDish(int base, string name, int value, int[] effects, float[] magnitudes, int[] durations,
	// int recipe) returns the dish's form ID, or 0 when every blank dish is used
	std::int32_t PapyrusMakeDish(RE::StaticFunctionTag*, std::int32_t a_base, RE::BSFixedString a_name, std::int32_t a_value,
		std::vector<std::int32_t> a_effects, std::vector<float> a_magnitudes, std::vector<std::int32_t> a_durations, std::int32_t a_recipe)
	{
		DishDef def;
		def.base = static_cast<RE::FormID>(a_base);
		def.needsKey = NeedsKeyOfDish(a_recipe);
		def.name = a_name.c_str() ? a_name.c_str() : "";
		def.value = std::max(0, a_value);
		for (std::size_t i = 0; i < a_effects.size(); ++i) {
			if (a_effects[i] == 0) {
				continue;  // an unused place in the arrays
			}
			const auto magnitude = i < a_magnitudes.size() ? a_magnitudes[i] : 0.0f;
			// rounded up to the whole minute the dish's card shows: the duration is part of what makes a dish its own slot,
			// and small skill or gear changes (30-second steps) used up slots (about 1280 in a long game; reviews 2026-11-15)
			auto duration = i < a_durations.size() ? std::max(0, a_durations[i]) : 0;
			if (duration > 0) {
				duration = (duration + 59) / 60 * 60;
			}
			def.effects.push_back({ static_cast<RE::FormID>(a_effects[i]), magnitude, static_cast<std::uint32_t>(duration) });
			if (def.effects.size() == MAX_DISH_EFFECTS) {
				break;  // the most a dish holds (the save keeps the count in one byte)
			}
		}
		return AddDish(def);
	}

	// a dish from its definition: the one already made the same way, or the next blank dish filled in. Returns its form
	// ID, or 0 when every blank dish is used. Also used for aged brews (Cooking/Hooks.inl BrewNext), which the plugin
	// makes itself from the step before
	std::int32_t AddDish(const DishDef& def)
	{
		std::scoped_lock lock(g_dishLock);
		const auto key = def.Key();
		RE::FormID local = 0;
		if (const auto it = g_dishByKey.find(key); it != g_dishByKey.end()) {
			local = it->second;
		} else {
			const RE::FormID next = (std::max)(g_dishDefs.empty() ? DISH_POOL_FIRST : g_dishDefs.rbegin()->first + 1, DishPoolNextSaved());
			if (next > DISH_POOL_LAST) {
				if (!g_dishPoolFullTold) {
					g_dishPoolFullTold = true;
					logger::warn("Dish pool: all {} dishes are used in this game", DISH_POOL_LAST - DISH_POOL_FIRST + 1);
				}
				return 0;
			}
			local = next;
			g_dishDefs[local] = def;
			g_dishByKey[key] = local;
			DishPoolNextStore(local + 1);
			logger::info("Dish pool: {:03X} is now {}", local, def.name);
		}
		ApplyDishSlot(local, &g_dishDefs[local]);
		const auto dish = DishSlotForm(local);
		return dish ? static_cast<std::int32_t>(dish->GetFormID()) : 0;
	}

	// a pool dish's definition by its form ID (a copy), or none for any other form
	std::optional<DishDef> DishDefOf(RE::FormID a_id)
	{
		const auto form = RE::TESForm::LookupByID(a_id);
		const auto file = form ? form->GetFile(0) : nullptr;
		if (!file || file->GetFilename() != PLUGIN) {
			return std::nullopt;
		}
		std::scoped_lock lock(g_dishLock);
		const auto it = g_dishDefs.find(form->GetLocalFormID());
		return it != g_dishDefs.end() ? std::optional<DishDef>(it->second) : std::nullopt;
	}

	void DishPoolSave(SKSE::SerializationInterface* a_intfc)
	{
		std::scoped_lock lock(g_dishLock);
		// version 2 (2026-11-15): each dish also keeps its recipe's needs key
		if (!a_intfc->OpenRecord(DISH_POOL_RECORD, 2)) {
			return;
		}
		a_intfc->WriteRecordData(static_cast<std::uint32_t>(g_dishDefs.size()));
		for (const auto& [local, def] : g_dishDefs) {
			a_intfc->WriteRecordData(local);
			a_intfc->WriteRecordData(def.base);
			const auto length = static_cast<std::uint16_t>(std::min<std::size_t>(def.name.size(), 0xFFFF));
			a_intfc->WriteRecordData(length);
			a_intfc->WriteRecordData(def.name.data(), length);
			const auto keyLength = static_cast<std::uint16_t>(std::min<std::size_t>(def.needsKey.size(), 0xFFFF));
			a_intfc->WriteRecordData(keyLength);
			a_intfc->WriteRecordData(def.needsKey.data(), keyLength);
			a_intfc->WriteRecordData(def.value);
			a_intfc->WriteRecordData(static_cast<std::uint8_t>(def.effects.size()));
			for (const auto& e : def.effects) {
				a_intfc->WriteRecordData(e.effect);
				a_intfc->WriteRecordData(e.magnitude);
				a_intfc->WriteRecordData(e.duration);
			}
		}
	}

	bool DishPoolLoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, std::uint32_t a_version)
	{
		if (a_type != DISH_POOL_RECORD) {
			return false;
		}
		if (a_version != 1 && a_version != 2) {
			return true;
		}
		{
			// read into a copy first and take it only when the whole record read cleanly: a record cut short or damaged
			// must not leave half a pool, whose missing slots new dishes would then reuse (Codex review 2026-11-15)
			std::map<RE::FormID, DishDef> defs;
			std::uint32_t count = 0;
			bool whole = ReadAll(a_intfc, count);
			for (std::uint32_t k = 0; whole && k < count; ++k) {
				RE::FormID local = 0, base = 0;
				std::uint16_t length = 0;
				DishDef def;
				if (!ReadAll(a_intfc, local) || !ReadAll(a_intfc, base) || !ReadAll(a_intfc, length)) {
					whole = false;
					break;
				}
				def.name.resize(length);
				if (length && !ReadAll(a_intfc, def.name.data(), length)) {
					whole = false;
					break;
				}
				if (a_version >= 2) {
					std::uint16_t keyLength = 0;
					if (!ReadAll(a_intfc, keyLength)) {
						whole = false;
						break;
					}
					def.needsKey.resize(keyLength);
					if (keyLength && !ReadAll(a_intfc, def.needsKey.data(), keyLength)) {
						whole = false;
						break;
					}
				}
				std::uint8_t effects = 0;
				if (!ReadAll(a_intfc, def.value) || !ReadAll(a_intfc, effects) || effects > MAX_DISH_EFFECTS) {
					whole = false;
					break;
				}
				RE::FormID resolved = 0;
				def.base = base && a_intfc->ResolveFormID(base, resolved) ? resolved : 0;  // its mod gone: the blank look
				bool ok = true;
				for (std::uint8_t e = 0; e < effects; ++e) {
					DishEffectDef effect;
					if (!ReadAll(a_intfc, effect.effect) || !ReadAll(a_intfc, effect.magnitude) || !ReadAll(a_intfc, effect.duration)) {
						ok = false;
						break;
					}
					if (a_intfc->ResolveFormID(effect.effect, resolved)) {
						effect.effect = resolved;
						def.effects.push_back(effect);
					}
				}
				if (!ok) {
					whole = false;
					break;
				}
				if (defs.contains(local)) {
					whole = false;  // one slot twice: our writer never does that, so the record is damaged (Codex review 2026-11-16)
					break;
				}
				if (local >= DISH_POOL_FIRST && local <= DISH_POOL_LAST) {
					defs[local] = std::move(def);
				}
			}
			std::scoped_lock lock(g_dishLock);
			if (whole) {
				for (auto& [local, def] : defs) {
					g_dishByKey[def.Key()] = local;
					g_dishDefs[local] = std::move(def);
				}
				g_dishRecordRead = true;
				if (!g_dishDefs.empty()) {
					DishPoolNextStore(g_dishDefs.rbegin()->first + 1);  // saves from before the global: set it now
				}
				logger::info("Dish pool: {} dishes in this save", g_dishDefs.size());
			} else {
				logger::error("Dish pool: this save's dish record is damaged; its dishes are not loaded");
			}
		}
		ReconcileDishPool();  // as early as we can: before anything in the world looks at the dishes
		return true;
	}

	void DishPoolRevert()
	{
		std::scoped_lock lock(g_dishLock);
		g_dishDefs.clear();
		g_dishByKey.clear();
		g_dishPoolFullTold = false;
		g_dishRecordRead = false;
	}
