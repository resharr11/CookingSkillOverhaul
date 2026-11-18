// Part of CookingMenu.cpp: included in order inside its anonymous namespace (not compiled on its own).
// Fortify Cooking and the enchanted Chef's Hat (added 2026-11-11, his design). Fortify Cooking works like vanilla's
// Fortify skill enchantments: it doesn't add Cooking skill, it makes what the skill does stronger. Cooking's skill makes
// dishes last longer, so this does too: "Dishes you cook last <mag>% longer." It can go on head and chest pieces.
// Fortified time counts in full up to the normal 20:00 limit; past it, half of the extra counts, and never more than a
// quarter over (25:00; a dragon dish's half day becomes 15 game hours at most). His numbers: two max-enchanted pieces
// (~50%) turn a perfect 20:00 meal into 25:00; the found hat (10%) into 21:00.
// The hat (a vanilla Chef's Hat with 10%) is sold by the three Khajiit caravans: a 10% chance each time their stock
// resets. It is added to the caravans' merchant chests when the game loads, not by editing those chests in the esp, so
// other mods that change the chests keep their changes.

	constexpr const char* CHEF_HAT_PLUGIN = "FreeCooking.esp";
	constexpr RE::FormID FORTIFY_COOKING_EFFECT = 0xA7C;  // FC_FortifyCookingEffect (MGEF)
	constexpr RE::FormID CHEF_HAT_LIST = 0xA81;           // FC_LItemChefHatCaravan (LVLI: the hat, 90% chance of nothing)
	constexpr std::array<RE::FormID, 3> CARAVAN_CHESTS{ 0x7434B, 0x7434D, 0x7434E };  // Skyrim.esm MerchantCaravanA/B/CChest
	constexpr std::int32_t COOK_LIMIT = 1200;             // 20:00, the normal longest dish (seconds)

	// the player's Fortify Cooking, in percent: every worn piece's effect added up (main thread or a frame-synced native)
	float LiveFortifyCookingPercent()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto base = PluginForm<RE::EffectSetting>(FORTIFY_COOKING_EFFECT, CHEF_HAT_PLUGIN);
		auto effects = player && base ? player->GetActiveEffectList() : nullptr;
		float total = 0.0f;
		if (effects) {
			for (auto effect : *effects) {
				if (effect && effect->GetBaseObject() == base && effect->flags.none(RE::ActiveEffect::Flag::kInactive, RE::ActiveEffect::Flag::kDispelled)) {
					total += effect->magnitude;
				}
			}
		}
		return std::max(total, 0.0f);
	}

	// any thread: off the main thread, the copy the main thread keeps (Cooking/PlayerView.inl)
	float FortifyCookingPercent()
	{
		return OnMainThread() ? LiveFortifyCookingPercent() : ViewFortifyCooking();
	}

	// a dish's duration with the player's Fortify Cooking (seconds). a_dragon: a dragon dish's half day, always past the limit
	std::int32_t FortifyDuration(std::int32_t a_dur, bool a_dragon)
	{
		const float pct = FortifyCookingPercent();
		if (a_dur <= 0 || pct <= 0.0f) {
			return a_dur;
		}
		const float limit = a_dragon ? static_cast<float>(a_dur) : static_cast<float>(std::max(a_dur, COOK_LIMIT));
		const float raw = a_dur * (100.0f + pct) / 100.0f;
		float result = raw <= limit ? raw : limit + (raw - limit) / 2.0f;
		result = std::min(result, limit * 1.25f);
		if (a_dragon) {
			return static_cast<std::int32_t>(result);
		}
		// meals and drinks keep their 30-second steps, rounded to the nearest one (rounding down took a short dish's whole
		// bonus: 4:30 + 10% = 4:57 fell back to 4:30; found in game 2026-11-11)
		return static_cast<std::int32_t>(std::lround(result / 30.0f)) * 30;
	}

	// at kDataLoaded: the hat's list goes into each caravan's merchant chest (base container; a chest picks it up the next
	// time its stock resets)
	void ChefHatSetup()
	{
		auto list = PluginForm<RE::TESLevItem>(CHEF_HAT_LIST, CHEF_HAT_PLUGIN);
		if (!list) {
			logger::error("Chef's Hat: its list (FreeCooking.esp {:X}) is missing; the caravans won't sell it", CHEF_HAT_LIST);
			return;
		}
		std::int32_t added = 0;
		for (const auto id : CARAVAN_CHESTS) {
			auto chest = RE::TESForm::LookupByID<RE::TESObjectCONT>(id);
			if (!chest) {
				continue;
			}
			bool already = false;
			chest->ForEachContainerObject([&](RE::ContainerObject& a_entry) {
				already = already || a_entry.obj == list;
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (!already && chest->AddObjectToContainer(list, 1, nullptr)) {
				++added;
			}
		}
		logger::info("Chef's Hat: added to {} of 3 Khajiit caravan chests (10% each time their stock resets)", added);
	}
