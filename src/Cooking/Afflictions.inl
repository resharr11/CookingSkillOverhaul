// Part of CookingMenu.cpp: included in order inside its anonymous namespace (not compiled on its own).
// ---------------------------------------------------------------------------------------------------------------
// Afflictions (Survival Mode; his design, 2026-11-12). Survival gives three afflictions, each an ability that its own
// TimedAbilityScript removes after 24 game hours (read from ccQDRSSE001-SurvivalMode.esl and its scripts):
//   Weakened 0x910     melee weapons and blocking 30% less effective; rolled 20% every hunger update (0.25 h) while
//                      Starving (Survival_NeedHunger, Survival_AfflictionHungerChance 0x8E5).
//   Addled 0x911       magicka and stamina regenerate 30% slower; rolled 30% every tiredness update (0.5 h) while
//                      Debilitated (Survival_NeedExhaustion, 0x8E4).
//   Frostbitten 0x913  lockpicking and pickpocketing 30% harder, bows 30% less damage; rolled 16% every cold update
//                      while Numb (Survival_NeedCold, 0x8E6).
// Eating, resting or warming up never removes them, and Cure Disease can't touch abilities. His rule (2026-11-09):
// "our cooking should fix it, but we should not change how vanilla works". So Survival's afflictions stay exactly as
// they are, and Cooking Skill Overhaul adds a cure: the flavour that counters what the affliction takes away clears it (his
// mapping, 2026-11-12): Mighty (weapons) clears Weakened, Brainy (magicka) clears Addled, Spicy (the cold safety
// net) clears Frostbitten. Any meal, baked dish or brewed drink of that flavour counts, at any level, while its effect
// lasts (checked every second, so it also clears an affliction that arrives while the dish is still working).
// No extra condition is needed for the cause: while the player is still starving, exhausted or freezing, Survival
// keeps rolling and the affliction soon comes back; it only stays away once the need itself is fixed.
// Teaching: a one-time help box the first time the player has an affliction (his rule: teach once, when it matters),
// and a short message each time a dish clears one. No line on the dish cards.
// Switch: "Flavours clear afflictions" on the Survival Mode settings page (FC::Feature::kAfflictions, on by default).
// Works the same under Survival Mode Improved, which rolls the same affliction spells. Frostfall's frostbite, its own
// Frostbitten, is cleared by Spicy the same way (Cooking/OtherMods.inl FrostfallTick).
// Save/load: only whether the help box was shown is ours ('AFFL'); loading a save or a new game clears it until the
// loaded game's own record comes back. The afflictions themselves are Survival's spells, saved with the game.
// ---------------------------------------------------------------------------------------------------------------

	constexpr const char* SURVIVAL_FILE = "ccQDRSSE001-SurvivalMode.esl";

	struct AfflictionCure
	{
		RE::FormID  affliction;  // Survival's ability
		RE::FormID  meal[4];     // the flavour's meal effects (levels 1-4), FreeCooking.esp
		RE::FormID  baked[4];
		RE::FormID  drink[4];
		const char* mealText;    // whole sentences, for translators
		const char* drinkText;
		const char* name;        // for the log
	};
	constexpr AfflictionCure AFFLICTION_CURES[] = {
		{ 0x910, { 0x830, 0x831, 0x832, 0x833 }, { 0x990, 0x991, 0x992, 0x993 }, { 0x8C0, 0x8C1, 0x8C2, 0x8C3 },
			"The Mighty meal restores your strength. You are no longer Weakened.",
			"The Mighty drink restores your strength. You are no longer Weakened.", "Weakened (Mighty)" },
		{ 0x911, { 0x834, 0x835, 0x836, 0x837 }, { 0x994, 0x995, 0x996, 0x997 }, { 0x8C4, 0x8C5, 0x8C6, 0x8C7 },
			"The Brainy meal clears your head. You are no longer Addled.",
			"The Brainy drink clears your head. You are no longer Addled.", "Addled (Brainy)" },
		{ 0x913, { 0x914, 0x915, 0x916, 0x917 }, { 0x9C4, 0x9C5, 0x9C6, 0x9C7 }, { 0x934, 0x935, 0x936, 0x937 },
			"The Spicy meal warms your frozen limbs. You are no longer Frostbitten.",
			"The Spicy drink warms your frozen limbs. You are no longer Frostbitten.", "Frostbitten (Spicy)" },
	};

	struct AfflictionForms
	{
		RE::SpellItem*                     affliction = nullptr;
		std::vector<RE::EffectSetting*>    food;   // meal and baked effects
		std::vector<RE::EffectSetting*>    drink;
	};
	std::array<AfflictionForms, std::size(AFFLICTION_CURES)> g_afflictionForms;
	bool g_afflictionHelpShown = false;  // the one-time help box, per character

	void AfflictionFormsLookup()
	{
		static bool looked = false;
		if (looked) {
			return;
		}
		looked = true;
		auto data = RE::TESDataHandler::GetSingleton();
		if (!data || !data->LookupLoadedLightModByName(SURVIVAL_FILE)) {
			logger::info("Afflictions: Survival Mode not installed");
			return;
		}
		std::size_t found = 0, effects = 0;
		for (std::size_t i = 0; i < std::size(AFFLICTION_CURES); ++i) {
			const auto& cure = AFFLICTION_CURES[i];
			auto& forms = g_afflictionForms[i];
			forms.affliction = data->LookupForm<RE::SpellItem>(cure.affliction, SURVIVAL_FILE);
			found += forms.affliction ? 1 : 0;
			for (int level = 0; level < 4; ++level) {
				for (auto [id, list] : { std::pair{ cure.meal[level], &forms.food }, std::pair{ cure.baked[level], &forms.food },
						 std::pair{ cure.drink[level], &forms.drink } }) {
					if (auto effect = PluginForm<RE::EffectSetting>(id, PLUGIN)) {
						list->push_back(effect);
						++effects;
					}
				}
			}
		}
		logger::info("Afflictions: {} of 3 Survival afflictions found, {} of 36 flavour effects", found, effects);
	}

	// once a second (StartSurvivalWatch)
	void AfflictionsTick()
	{
		AfflictionFormsLookup();
		auto player = Player();
		if (!player || !SurvivalFeature(FC::Feature::kAfflictions)) {
			return;
		}
		const auto running = [&](const std::vector<RE::EffectSetting*>& a_list) {
			for (auto effect : a_list) {
				if (player->HasMagicEffect(effect)) {
					return true;
				}
			}
			return false;
		};
		bool afflicted = false;
		for (std::size_t i = 0; i < std::size(AFFLICTION_CURES); ++i) {
			const auto& forms = g_afflictionForms[i];
			if (!forms.affliction || !player->HasSpell(forms.affliction)) {
				continue;
			}
			afflicted = true;
			const bool food = running(forms.food);
			const bool drink = !food && running(forms.drink);
			if (!food && !drink) {
				continue;
			}
			player->RemoveSpell(forms.affliction);
			const auto& cure = AFFLICTION_CURES[i];
			RE::SendHUDMessage::ShowHUDMessage(FC::Tr(food ? cure.mealText : cure.drinkText));
			logger::info("Afflictions: {} cleared by a {}", cure.name, food ? "meal or baked dish" : "drink");
		}
		if (afflicted && !g_afflictionHelpShown) {
			g_afflictionHelpShown = true;
			SKSE::GetTaskInterface()->AddTask([]() {
				RE::DebugMessageBox(FC::Tr(
					"Survival Mode's afflictions last a day, even after you have eaten, rested or warmed up. A dish of the right flavour clears one sooner: Mighty clears Weakened, Brainy clears Addled and Spicy clears Frostbitten. A meal, a baked dish and a brewed drink all count."));
			});
			logger::info("Afflictions: help shown");
		}
	}

	void AfflictionsSave(SKSE::SerializationInterface* a_intfc)
	{
		if (a_intfc->OpenRecord('AFFL', 2)) {
			a_intfc->WriteRecordData(static_cast<std::uint8_t>(g_afflictionHelpShown ? 1 : 0));
			a_intfc->WriteRecordData(static_cast<std::uint8_t>(g_frostbiteHelpShown ? 1 : 0));  // v2: Frostfall's frostbite help (OtherMods.inl)
		}
	}

	bool AfflictionsLoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, std::uint32_t a_version)
	{
		if (a_type != 'AFFL') {
			return false;
		}
		if (a_version >= 1) {
			std::uint8_t shown = 0;
			if (!ReadAll(a_intfc, shown)) {
				return true;  // cut short: keep the defaults
			}
			g_afflictionHelpShown = shown != 0;
		}
		if (a_version >= 2) {
			std::uint8_t shown = 0;
			if (!ReadAll(a_intfc, shown)) {
				return true;
			}
			g_frostbiteHelpShown = shown != 0;
		}
		return true;
	}

	void AfflictionsReset()
	{
		g_afflictionHelpShown = false;
		g_frostbiteHelpShown = false;
	}
