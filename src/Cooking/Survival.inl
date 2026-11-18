// Part of CookingMenu.cpp: included in order inside its anonymous namespace (not compiled on its own).
// ---------------------------------------------------------------------------------------------------------------
// Hearty's small health regeneration in Survival Mode is a record condition, and a condition can't read Survival's own
// Survival_ModeEnabled (its plugin isn't one of our masters), so this sets FreeCooking.esp's FC_SurvivalOn once a second:
// 1 while Survival Mode is on and the "Hearty heals over time" switch is on (Survival Mode settings page), else 0. (Until
// 2026-11-12 it meant Survival Mode on; scripts now ask FC_Menu.SurvivalOn instead. To be renamed FC_HeartyRegenOn.)
//
// Save/load: FC_SurvivalOn is a global, so it is saved with the game; it is set again on the first tick after a load.
// ---------------------------------------------------------------------------------------------------------------

	constexpr RE::FormID SURVIVAL_ON_GLOBAL = 0xA6B;     // FreeCooking.esp FC_SurvivalOn
	constexpr RE::FormID SURVIVAL_MODE_ENABLED = 0x826;  // ccqdrsse001-survivalmode.esl Survival_ModeEnabled
	constexpr RE::FormID SURVIVAL_LOWER_REGEN = 0x982;   // ccqdrsse001-survivalmode.esl Survival_abLowerRegenSpell

	void SpicyWardTick(bool a_survivalOn);  // below

	void SurvivalTick()
	{
		static RE::TESGlobal* ours = nullptr;
		static RE::TESGlobal* theirs = nullptr;
		static bool looked = false;
		if (!looked) {
			looked = true;
			ours = PluginForm<RE::TESGlobal>(SURVIVAL_ON_GLOBAL, PLUGIN);
			theirs = PluginForm<RE::TESGlobal>(SURVIVAL_MODE_ENABLED, "ccqdrsse001-survivalmode.esl");
			logger::info("Survival Mode: {} (FC_SurvivalOn {})", theirs ? "installed" : "not installed", ours ? "found" : "missing");
		}
		if (!ours) {
			return;
		}
		const bool survival = theirs && theirs->value >= 1.0f;
		static int lastSurvival = -1;
		if (lastSurvival != static_cast<int>(survival)) {
			lastSurvival = survival;
			logger::info("Survival Mode is {}", survival ? "on" : "off");
		}
		// Hearty's regeneration only where natural health regeneration is stopped (his rule 2026-11-16: "only needed if
		// it's relevant, aka the mod removes base health regen"): Survival's own Survival_abLowerRegenSpell on the player.
		// Survival Mode gives it, Survival Mode Improved (and Starfrost on it) keep it; SunHelm, Frostfall, Last Seed,
		// iNeed and Realistic Needs leave health regeneration alone, so Hearty adds nothing there
		static RE::SpellItem* lowerRegen = nullptr;
		static bool lookedRegen = false;
		if (!lookedRegen) {
			lookedRegen = true;
			lowerRegen = PluginForm<RE::SpellItem>(SURVIVAL_LOWER_REGEN, "ccqdrsse001-survivalmode.esl");
		}
		auto player = RE::PlayerCharacter::GetSingleton();
		const bool noRegen = player && lowerRegen && player->HasSpell(lowerRegen);
		if (player) {
			FC::SetRegenStopped(noRegen);  // for the settings page (render thread)
		}
		const float hearty = noRegen && FC::FeatureSwitch(FC::Feature::kHeartyRegen) ? 1.0f : 0.0f;
		if (ours->value != hearty) {
			ours->value = hearty;
			logger::info("Hearty's Survival regeneration: {}", hearty > 0.0f ? "on" : "off");
		}

		SunHelmWaterTick(RE::PlayerCharacter::GetSingleton());  // Cooking/OtherMods.inl: Spicy and SunHelm's freezing water (before the ward tick reads it)
		SpicyWardTick(survival);
		FrostfallTick(RE::PlayerCharacter::GetSingleton());  // Cooking/OtherMods.inl: Spicy and its cold water, frostbite
	}

// ---------------------------------------------------------------------------------------------------------------
// Spicy and freezing water: while any Spicy effect lasts (meal, drink or baked, any level) and Survival Mode is on,
// the player carries FC_SpicyWaterWard (FreeCooking.esp 0xA7A), an ability holding Survival's own
// Survival_FireCloakFreezingWaterDesc (Update.esm 0x2EE9). Our copy of Survival_FreezingWaterCheck counts that effect
// as a Flame Cloak, so Spicy keeps the freezing water's damage, its forced cold and its message away.
// Save/load: the ability is saved with the game like any spell, and this tick (every second, also right after a
// load) adds or removes it to match the Spicy effects actually running, so it can never be left behind.
// ---------------------------------------------------------------------------------------------------------------

	constexpr RE::FormID SPICY_WATER_WARD = 0xA7A;
	// for HUD mods (NeedsHUD reads them through its FreeCooking config file): how many cold stages Spicy moves every
	// place right now (0-4: the highest Spicy level running, 0 when no Spicy runs, no survival mod's cold runs or the
	// "Spicy holds off the cold" switch is off), and 1 while Spicy keeps freezing water harmless (with the "Spicy protects
	// from freezing water" switch on: Survival Mode, Survival Mode Improved, Frostfall and SunHelm)
	constexpr RE::FormID SPICY_SHIFT = 0xAA8;       // FreeCooking.esp FC_SpicyShift
	constexpr RE::FormID SPICY_WATER_SAFE = 0xAA9;  // FreeCooking.esp FC_SpicyWaterSafe
	constexpr RE::FormID SPICY_WARMTH_ONLY = 0xA7B;  // FreeCooking.esp FC_SpicyWarmthOnly
	constexpr RE::FormID SPICY_EFFECTS[] = {
		0x914, 0x915, 0x916, 0x917,  // FC_Fx_Spicy1-4 (meal)
		0x934, 0x935, 0x936, 0x937,  // FC_FxD_Spicy1-4 (drink)
		0x9C4, 0x9C5, 0x9C6, 0x9C7,  // FC_FxB_Spicy1-4 (baked)
	};

	void SpicyWardTick(bool a_survivalOn)
	{
		static RE::SpellItem* ward = nullptr;
		static RE::TESGlobal* warmthOnly = nullptr;
		static RE::TESGlobal* shiftGlobal = nullptr;
		static RE::TESGlobal* waterSafeGlobal = nullptr;
		static std::vector<RE::EffectSetting*> spicy;
		static std::vector<RE::EffectSetting*> others;  // Earthy, Chilly and Zesty meal, drink and baked effects
		static bool looked = false;
		if (!looked) {
			looked = true;
			ward = PluginForm<RE::SpellItem>(SPICY_WATER_WARD, PLUGIN);
			warmthOnly = PluginForm<RE::TESGlobal>(SPICY_WARMTH_ONLY, PLUGIN);
			shiftGlobal = PluginForm<RE::TESGlobal>(SPICY_SHIFT, PLUGIN);
			waterSafeGlobal = PluginForm<RE::TESGlobal>(SPICY_WATER_SAFE, PLUGIN);
			for (auto id : SPICY_EFFECTS) {
				if (auto effect = PluginForm<RE::EffectSetting>(id, PLUGIN)) {
					spicy.push_back(effect);
				}
			}
			for (const RE::FormID first : { 0x910u, 0x930u, 0x9C0u }) {
				for (RE::FormID n = 0; n < 16; ++n) {
					if (n / 4 == 1) {
						continue;  // Spicy
					}
					if (auto effect = PluginForm<RE::EffectSetting>(first + n, PLUGIN)) {
						others.push_back(effect);
					}
				}
			}
			logger::info("Spicy water ward: {}, {} ({} Spicy effects, {} other element effects)", ward ? "ready" : "MISSING FC_SpicyWaterWard",
				warmthOnly ? "warmth-only switch ready" : "MISSING FC_SpicyWarmthOnly", spicy.size(), others.size());
		}
		auto player = Player();
		if (!ward || !player) {
			return;
		}
		const auto any = [&](const std::vector<RE::EffectSetting*>& a_list) {
			for (auto effect : a_list) {
				if (player->HasMagicEffect(effect)) {
					return true;
				}
			}
			return false;
		};
		const bool spicyOn = any(spicy);
		// the switches on the Survival Mode settings page: the water ward, and Spicy's warmth (cold cap or water ward)
		const bool water = FC::FeatureSwitch(FC::Feature::kSpicyWater);
		const bool warmth = water || FC::FeatureSwitch(FC::Feature::kSpicyCold);
		const bool wanted = a_survivalOn && spicyOn && water;
		// Spicy beside another element keeps only its warmth: its Resist Frost entries are conditioned on this global
		// being 0 (FreeCooking.esp), and its weapon and spell parts give way in HolyDamage.inl ReadElementMeal. With
		// both warmth switches off there is no warmth to keep, so the one-element rule applies as without Survival
		if (warmthOnly) {
			const bool cold = a_survivalOn || NeedOwner(Need::kCold) != NeedMod::kNone;  // any mod's cold (2026-11-14)
			const float value = (cold && spicyOn && warmth && any(others)) ? 1.0f : 0.0f;
			if (warmthOnly->value != value) {
				warmthOnly->value = value;
				logger::info("Spicy: {}", value > 0.0f ? "warmth only (another element is active)" : "full");
			}
		}
		// the HUD globals: the highest Spicy level running (SPICY_EFFECTS is in level order inside each group of four)
		{
			int level = 0;
			for (std::size_t i = 0; i < spicy.size(); ++i) {
				if (player->HasMagicEffect(spicy[i])) {
					level = std::max(level, static_cast<int>(spicy[i]->GetLocalFormID() & 0x3) + 1);
				}
			}
			const auto owner = NeedOwner(Need::kCold);
			const bool coldRuns = a_survivalOn || owner != NeedMod::kNone;
			const float shift = (coldRuns && FC::FeatureSwitch(FC::Feature::kSpicyCold)) ? static_cast<float>(level) : 0.0f;
			// the ward (Survival Mode, SMI), Frostfall's own potion keyword or SunHelm's flame-cloak keyword
			const bool waterSafe = level > 0 && water && (wanted || owner == NeedMod::kFrostfall || (owner == NeedMod::kSunHelm && g_shSpicyWard));
			if (shiftGlobal && shiftGlobal->value != shift) {
				shiftGlobal->value = shift;
				logger::info("Spicy moves the cold by {} stage(s)", static_cast<int>(shift));
			}
			if (waterSafeGlobal && waterSafeGlobal->value != (waterSafe ? 1.0f : 0.0f)) {
				waterSafeGlobal->value = waterSafe ? 1.0f : 0.0f;
				logger::info("Spicy and freezing water: {}", waterSafe ? "harmless" : "as the survival mod sets it");
			}
		}
		const bool has = player->HasSpell(ward);
		if (wanted && !has) {
			player->AddSpell(ward);
			logger::info("Spicy water ward: on (a Spicy effect is running)");
		} else if (!wanted && has) {
			player->RemoveSpell(ward);
			logger::info("Spicy water ward: off");
		}
	}

// ---------------------------------------------------------------------------------------------------------------
// Camp rest (his design, 2026-11-12; replaces Refreshing): Survival never lets a night outdoors take tiredness below
// the start of Drained, so every camper wakes up Drained (Survival_ExhaustionSleepDetection: 120 per hour slept,
// Survival_ExhaustionRestorePerHour; quality sleep = an interior or a worldspace in Survival_InteriorAreas, else
// NeedExhaustion.DecreaseExhaustion stops at needStage2Value, 160, and does nothing below it). Here an outdoor sleep
// notes the rest a bed would have given for those hours; the next brewed drink gives it back (FC_CampRestEffectScript
// asks FC_Menu.TakeCampRest, and FC_Core.GiveBackRest lowers tiredness through Survival's own need, without the
// Rested bonus). Nothing to give back after an indoor sleep, after a drink already gave it, or without sleeping: a
// drink never replaces sleep, and drinking more never helps. The first outdoor sleep of a character shows a one-time
// help box on waking (his pick: the moment the player meets the problem).
// Save/load: what is owed, its needs mod, whether the help was shown, and a Last Seed, iNeed or SunHelm night still
// being worked out (with a drink's claim on it) are saved ('REST', version 5); loading a save or a new game clears them
// first, then the loaded game's own values come back.
// ---------------------------------------------------------------------------------------------------------------

	constexpr RE::FormID SURVIVAL_TIREDNESS = 0x816;       // Survival_ExhaustionNeedValue
	constexpr RE::FormID SURVIVAL_REST_PER_HOUR = 0x80E;   // Survival_ExhaustionRestorePerHour (120)
	constexpr RE::FormID SURVIVAL_INTERIOR_AREAS = 0x92F;  // Survival_InteriorAreas (worldspaces that count as indoors)
	constexpr float DRAINED_FROM = 160.0f;  // Survival's needStage2Value: an outdoor sleep can't go lower

	struct SurvivalForms
	{
		bool looked = false;
		RE::TESGlobal* modeOn = nullptr;
		RE::TESGlobal* tiredness = nullptr;
		RE::TESGlobal* restPerHour = nullptr;
		RE::BGSListForm* interiorAreas = nullptr;
		RE::SpellItem* brownRot[3] = {};  // Survival_DiseaseBrownRot, 2, 3: sleep restores 75%, 50%, 25%
	};
	SurvivalForms g_survival;

	float g_campRestOwed = 0.0f;       // the rest the last outdoor sleep didn't give (tiredness points, in the scale of the
	                                   // mod that ran tiredness that night: Survival's, SunHelm's or iNeed's)
	// Last Seed works its floor out when the player wakes, after our sleep event: the night is kept, and a few seconds
	// later (LastSeedNightSettled) the owed rest is worked out once from where Last Seed left the player and kept in
	// g_campRestOwed, which is saved. Worked out at the drink instead, a reload lost it and fatigue gathered after
	// waking was refunded too (Codex review 2026-11-15, M11)
	float g_campLsBefore = -1.0f;      // its fatigue when the player lay down (-1: no Last Seed night waiting)
	float g_campLsHours = 0.0f;
	float g_campLsNeedsFloor = 0.0f;   // Last Seed's hunger and thirst floor that night (0, 25 or 50): a bed would stop there too
	std::uint32_t g_campLsNight = 0;   // which night the waiting job belongs to (a new sleep replaces it)
	bool g_campRestHelpShown = false;  // the one-time help box, per character
	float g_sleepStartDay = -1.0f;     // game days when the sleep started (-1: not sleeping)
	float g_sleepStartTiredness = 0.0f;
	NeedMod g_sleepOwner = NeedMod::kNone;  // the mod that ran tiredness when the player lay down
	bool g_sleepShBedroll = false;          // SunHelm: this sleep is in a bedroll (its own flag, read when the sleep starts)
	// the ground's own penalty, read when the sleep starts (his call 2026-11-16: a drink gives back only what sleeping
	// outdoors or on a bedroll cost, never the needs mod's hunger or thirst penalty; reviews 2026-11-16, M6):
	bool g_sleepINeedBedroll = false;       // iNeed: in one of its bedrolls with no tent within 384 units (_SNNeeds)
	// iNeed decides a bedroll night by the activation, not by furniture the player lies in: its activator perk sets
	// _SNQuest.IsBedRoll and clears it after Utility.Wait(5.0), five seconds of running game (the sleep menu doesn't
	// count). Without a lie-down animation the player never occupies the bedroll, so the occupied furniture missed it
	// (in-game test 2026-11-16). The same here: set when the player activates one of its bedrolls, cleared after five
	// seconds of running game (main-thread timers don't advance in menus)
	bool g_ineedBedrollActivated = false;
	std::atomic<std::uint32_t> g_ineedBedrollActivation{ 0 };  // (atomic: the scripts' answer is checked against it on their thread)
	// An iNeed bedroll night is worked out a few seconds after waking, like Last Seed's: the plugin then asks the scripts
	// (mod event FC_INeedWoke) for iNeed's rest and stomach rot factor, and they answer through FC_Menu.INeedRestNow.
	// Asked when the bedroll was used instead, the answer came only after the sleep menu had closed, too late for the
	// night (in-game test 2026-11-16). iNeed caps rest at 110, so a night that filled it lost nothing (Codex review 2026-11-16c)
	float g_ineedNightBed = -1.0f;     // a bed's rest that night before stomach rot (-1: no iNeed night waiting)
	float g_ineedNightHours = 0.0f;
	std::uint32_t g_ineedNight = 0;    // which night the waiting job belongs to (a new sleep or a load replaces it)
	// SunHelm: its bedroll gives 75% of a bed's night, and a bed's night stops at 0 tiredness, so what the bedroll held
	// back is known once SunHelm has woken the player: the 25% at most, and never more than the tiredness left then
	// (Codex review 2026-11-16d, M5: a night that would have rested fully either way still paid the 25%)
	float g_shNightOwed = -1.0f;       // the 25% of that night (-1: no SunHelm night waiting)
	std::uint32_t g_shNight = 0;
	// a brewed drink drunk while a night is still waiting (iNeed's or SunHelm's) claims it: it is given back by a mod
	// event as soon as it is worked out, so that first drink isn't wasted (Codex review 2026-11-16d, M1)
	bool g_campRestClaimed = false;
	// the needs mod the rest owed is in (its scale and its tiredness): given only to that one (Codex review 2026-11-16d,
	// M7: a refund saved under iNeed was paid into SunHelm). kNone: from a save before this was kept
	NeedMod g_campRestOwner = NeedMod::kNone;
	// rest already taken by a drink but not yet given by the scripts (a claimed night, an old bottle without the effect):
	// kept here until the pot's script collects it (FC_Menu.TakeCampRestPay), so a script not listening yet loses
	// nothing; collected once, and only into the needs mod it is in (Codex review 2026-11-16f, M3). Saved ('REST' v6)
	float   g_campRestPay = 0.0f;
	NeedMod g_campRestPayOwner = NeedMod::kNone;
	bool g_sleepLsPlace = false;            // Last Seed: its location penalty is on and the player has no Outdoorsman

	void SurvivalFormsLookup()
	{
		auto& f = g_survival;
		if (f.looked) {
			return;
		}
		constexpr auto SURV = "ccqdrsse001-survivalmode.esl";
		f.modeOn = PluginForm<RE::TESGlobal>(SURVIVAL_MODE_ENABLED, SURV);
		f.tiredness = PluginForm<RE::TESGlobal>(SURVIVAL_TIREDNESS, SURV);
		f.restPerHour = PluginForm<RE::TESGlobal>(SURVIVAL_REST_PER_HOUR, SURV);
		f.interiorAreas = PluginForm<RE::BGSListForm>(SURVIVAL_INTERIOR_AREAS, SURV);
		constexpr RE::FormID BROWN_ROT[3] = { 0x914, 0x98B, 0x986 };
		for (int i = 0; i < 3; ++i) {
			f.brownRot[i] = PluginForm<RE::SpellItem>(BROWN_ROT[i], SURV);
		}
		f.looked = true;  // only once every form is filled in
		logger::info("Camp rest: Survival {}, tiredness {}, rest per hour {}, interior areas {}", f.modeOn ? "installed" : "not installed",
			f.tiredness ? "found" : "MISSING", f.restPerHour ? "found" : "MISSING", f.interiorAreas ? "found" : "MISSING");
	}

	bool SurvivalOn();  // below

	// Brown Rot's share of a night's rest, as Survival works it out (Survival_NeedExhaustion.GetBrownRotEffectMult:
	// the first stage it finds, in this order): 1 without it. A bed's night and so the camp rest both shrink by it
	float BrownRotMult(RE::Actor* a_player)
	{
		constexpr float MULT[3] = { 0.75f, 0.5f, 0.25f };
		for (int i = 0; i < 3; ++i) {
			if (g_survival.brownRot[i] && a_player->HasSpell(g_survival.brownRot[i])) {
				return MULT[i];
			}
		}
		return 1.0f;
	}

	// a Cooking Skill Overhaul Survival feature is running: Survival Mode is on and the feature's switch on the Survival Mode
	// settings page is on (Settings.h FC::Feature; Papyrus asks through FC_Menu.SurvivalFeature)
	bool SurvivalFeature(FC::Feature a_feature)
	{
		return SurvivalOn() && FC::FeatureSwitch(a_feature);
	}

	bool SurvivalOn()
	{
		SurvivalFormsLookup();
		return g_survival.modeOn && g_survival.modeOn->value >= 1.0f;
	}

	// Survival's own test for a good night's sleep (Survival_ExhaustionSleepDetection.OnSleepStop)
	bool QualitySleepHere(RE::PlayerCharacter* a_player)
	{
		auto cell = a_player ? a_player->GetParentCell() : nullptr;
		if (cell && cell->IsInteriorCell()) {
			return true;
		}
		auto world = a_player ? a_player->GetWorldspace() : nullptr;
		return world && g_survival.interiorAreas && g_survival.interiorAreas->HasForm(world);
	}

	// thirst (Cooking/Thirst.inl, included after this file) counts the hours slept on its own
	void ThirstSleepStart();
	void ThirstSleepStop();
	void ThirstTick();
	void LastSeedNightSettled();  // below
	void INeedNightSettled(float a_rest, float a_stomach);  // below
	void SunHelmNightSettled();                             // below
	void CampRestSettled(NeedMod a_owner);                  // below
	void WaitSunHelmNight(bool a_cast = true);              // below
	void WaitINeedNight(bool a_cast = true);                // below
	bool OwedFitsNow();                                     // below
	void QueueCampRestPay(std::int32_t a_given, NeedMod a_owner);  // below
	float SunHelmSlumber();                                 // below
	float INeedSleepMult(const RE::TESGlobal* a_penalty);
	bool INeedUnsafeHere(RE::PlayerCharacter* a_player);
	float LastSeedNeedsFloor();
	void ThirstReset();
	// afflictions (Cooking/Afflictions.inl, included after Thirst.inl)
	void AfflictionsTick();
	void AfflictionsReset();

	// the one-time help box, after the first night that really owes rest (not after a night that rested fully: the box
	// would then be spent before a real camp night; Claude review 2026-11-16b, L8)
	void ShowCampRestHelpOnce()
	{
		if (g_campRestHelpShown) {
			return;
		}
		g_campRestHelpShown = true;
		SKSE::GetTaskInterface()->AddTask([]() {
			RE::DebugMessageBox(FC::Tr(
				"Sleeping outdoors never leaves you fully rested. After a night outdoors, a brewed drink gives back the rest the hard ground took."));
		});
	}

	struct SleepWatch final : RE::BSTEventSink<RE::TESSleepStartEvent>, RE::BSTEventSink<RE::TESSleepStopEvent>,
							  RE::BSTEventSink<RE::TESActivateEvent>
	{
		RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* a_event, RE::BSTEventSource<RE::TESActivateEvent>*) override
		{
			if (!a_event || !a_event->actionRef || !a_event->actionRef->IsPlayerRef() || !a_event->objectActivated) {
				return RE::BSEventNotifyControl::kContinue;
			}
			OtherModsLookup();
			const auto base = a_event->objectActivated->GetBaseObject();
			if (base && g_other.ineedBedrolls && g_other.ineedBedrolls->HasForm(base)) {
				g_ineedBedrollActivated = true;
				const auto activation = ++g_ineedBedrollActivation;
				After(5000, [activation]() {
					if (activation == g_ineedBedrollActivation) {
						g_ineedBedrollActivated = false;
					}
				});
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESSleepStartEvent*, RE::BSTEventSource<RE::TESSleepStartEvent>*) override
		{
			ThirstSleepStart();
			auto calendar = RE::Calendar::GetSingleton();
			// a night still waiting to be worked out (iNeed's or SunHelm's) is dropped by a new sleep: its answer would
			// be read from the new night (Codex review 2026-11-16d, M4)
			if (g_ineedNightBed >= 0.0f || g_shNightOwed >= 0.0f) {
				logger::info("Camp rest: a new sleep before the last night was worked out: that night is dropped");
			}
			g_ineedNightBed = -1.0f;
			++g_ineedNight;
			g_shNightOwed = -1.0f;
			++g_shNight;
			g_campRestClaimed = false;
			g_sleepOwner = NeedOwner(Need::kTiredness);
			g_sleepStartDay = -1.0f;
			if (!FC::FeatureSwitch(FC::Feature::kCampRest) || !calendar) {
				return RE::BSEventNotifyControl::kContinue;
			}
			switch (g_sleepOwner) {
			case NeedMod::kSurvival:
				if (!g_survival.tiredness) {
					return RE::BSEventNotifyControl::kContinue;
				}
				g_sleepStartTiredness = g_survival.tiredness->value;
				break;
			case NeedMod::kLastSeed:
				if (!g_other.lsFatigue) {
					return RE::BSEventNotifyControl::kContinue;
				}
				g_sleepStartTiredness = g_other.lsFatigue->value;
				// _Seed_FatigueSystem.OnSleepStop: outdoors without Outdoorsman, the place floors fatigue at 25 more
				g_sleepLsPlace = AtLeast(g_other.lsSleepByPlace, 2.0f) && (!g_other.lsOutdoorsman || g_other.lsOutdoorsman->value < 1.0f);
				break;
			case NeedMod::kSunHelm:
				// SunHelm cuts the rest by its own bedroll flag, not by where the player sleeps (an ordinary bed outdoors
				// rests fully, a bedroll indoors doesn't): the flag is read now, before SunHelm clears it on waking
				// (_SHFatigueSystem.DetectSleepStop; Codex review 2026-11-15, M15)
				g_sleepShBedroll = g_other.shBedroll && g_other.shBedroll->value >= 1.0f;
				g_sleepStartTiredness = 0.0f;
				break;
			case NeedMod::kINeed:
				{
					g_sleepStartTiredness = 0.0f;  // its own rest per hour is all that's needed
					// _SNNeeds: a bedroll rests 15% less unless a tent stands within 384 units
					auto player = Player();
					auto furniture = player ? player->GetOccupiedFurniture().get() : nullptr;
					const auto base = furniture ? furniture->GetBaseObject() : nullptr;
					// one of its bedrolls: just activated (how iNeed itself decides), or lain in
					g_sleepINeedBedroll = g_ineedBedrollActivated || (base && g_other.ineedBedrolls && g_other.ineedBedrolls->HasForm(base));
					if (g_sleepINeedBedroll && g_other.ineedTents) {
						if (auto tes = RE::TES::GetSingleton()) {
							tes->ForEachReferenceInRange(player, 384.0f, [&](RE::TESObjectREFR* a_ref) {
								const auto refBase = a_ref ? a_ref->GetBaseObject() : nullptr;
								if (refBase && g_other.ineedTents->HasForm(refBase)) {
									g_sleepINeedBedroll = false;
									return RE::BSContainer::ForEachResult::kStop;
								}
								return RE::BSContainer::ForEachResult::kContinue;
							});
						}
					}
					break;
				}
			default:
				// Survival Mode Improved gives full rest anywhere and Realistic Needs rests the same everywhere: nothing
				// to give back
				return RE::BSEventNotifyControl::kContinue;
			}
			g_sleepStartDay = calendar->GetCurrentGameTime();
			return RE::BSEventNotifyControl::kContinue;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESSleepStopEvent*, RE::BSTEventSource<RE::TESSleepStopEvent>*) override
		{
			ThirstSleepStop();
			auto calendar = RE::Calendar::GetSingleton();
			auto player = Player();
			const float started = g_sleepStartDay;
			g_sleepStartDay = -1.0f;
			if (started < 0.0f || !calendar || !player || !FC::FeatureSwitch(FC::Feature::kCampRest)) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const float hours = std::max(0.0f, (calendar->GetCurrentGameTime() - started) * 24.0f);
			// SunHelm: its bedroll flag decides, indoors or out (a SunHelm without the flag: where the player slept). iNeed
			// too cuts a bedroll night indoors (a dungeon camp), so its own bedroll check decides, below (Claude review
			// 2026-11-16d, M1)
			const bool sunHelmBed = g_sleepOwner == NeedMod::kSunHelm && (g_other.shBedroll ? !g_sleepShBedroll : QualitySleepHere(player));
			const bool ownCheck = g_sleepOwner == NeedMod::kSunHelm || g_sleepOwner == NeedMod::kINeed;
			if (sunHelmBed || (!ownCheck && QualitySleepHere(player))) {
				g_campRestOwed = 0.0f;  // a real bed (or indoors): nothing missing
				g_campLsBefore = -1.0f;
				logger::info("Camp rest: slept {:.1f} h in a bed or indoors, nothing owed", hours);
				return RE::BSEventNotifyControl::kContinue;
			}
			switch (g_sleepOwner) {
			case NeedMod::kSurvival:
				{
					// the same night, as a bed and as the ground (Survival's numbers)
					const float mult = BrownRotMult(player);  // his call 2026-11-12: Brown Rot keeps its effect on the camp rest too
					const float rest = hours * (g_survival.restPerHour ? g_survival.restPerHour->value : 120.0f) * mult;
					const float before = g_sleepStartTiredness;
					const float bed = std::max(0.0f, before - rest);
					const float ground = before >= DRAINED_FROM ? std::max(DRAINED_FROM, before - rest) : before;
					g_campRestOwed = std::max(0.0f, ground - bed);
					g_campRestOwner = g_sleepOwner;
					logger::info("Camp rest: slept {:.1f} h outdoors from {:.0f} (Brown Rot x{:.2f}): a bed {:.0f}, the ground {:.0f}, owed {:.0f}",
						hours, before, mult, bed, ground, g_campRestOwed);
					break;
				}
			case NeedMod::kSunHelm:
				{
					// SunHelm: a bedroll gives 75% of a bed's rest (_SHFatigueSystem.DetectSleepStop; whole hours, its
					// _SHFatigueSleepRestoreAmount a game hour): the other 25% is owed
					const float perHour = g_other.shRestPerHour ? g_other.shRestPerHour->value : 50.0f;
					g_campRestOwed = 0.0f;
					// its Slumber perk adds 10 or 15% to every rest it gives (_SHFatigueSystem.DecreaseFatigueLevel): a bed's
					// night too, so the 25% held back is counted with it, in real tiredness points (Codex review 2026-11-16e, M5)
					g_shNightOwed = std::round(hours) * perHour * 0.25f * (1.0f + SunHelmSlumber());
					logger::info("Camp rest (SunHelm): slept {:.1f} h in a bedroll, 25% of the night {:.0f}, worked out once SunHelm has woken you",
						hours, g_shNightOwed);
					WaitSunHelmNight();
					break;
				}
			case NeedMod::kINeed:
				{
					// iNeed: a bedroll without a tent rests 15% less. Its night (_SNNeeds) is 12.5 a whole game hour times its
					// hunger and thirst multipliers and 0.85 in an uncleared dungeon; the bedroll's 0.85 comes on top, so what
					// it held back is 15% of that night, not of a full one (Codex review 2026-11-16 afternoon, M3). Its stomach
					// rot (a script value) is not read: with it the drink gives a little more than the night lost
					const float needs = INeedSleepMult(g_other.ineedHungerPenalty) * INeedSleepMult(g_other.ineedThirstPenalty);
					const float unsafe = INeedUnsafeHere(player) ? 0.85f : 1.0f;
					g_campRestOwed = 0.0f;
					if (!g_sleepINeedBedroll) {
						g_ineedNightBed = -1.0f;
						logger::info("Camp rest (iNeed): slept {:.1f} h, not in a bedroll or by a tent, nothing owed", hours);
						break;
					}
					// its stomach rot and where the night left its rest come from the scripts once iNeed has woken the player
					g_ineedNightBed = std::round(hours) * 12.5f * needs * unsafe;
					g_ineedNightHours = hours;
					logger::info("Camp rest (iNeed): slept {:.1f} h in a bedroll, needs x{:.2f}, place x{:.2f}, worked out once iNeed has woken you",
						hours, needs, unsafe);
					WaitINeedNight();
					break;
				}
			case NeedMod::kLastSeed:
				{
					if (!g_sleepLsPlace) {
						// no place penalty (its option off, or Outdoorsman): its floors are hunger and thirst only
						g_campRestOwed = 0.0f;
						g_campLsBefore = -1.0f;
						logger::info("Camp rest (Last Seed): slept {:.1f} h outdoors, no place penalty, nothing owed", hours);
						break;
					}
					g_campLsBefore = g_sleepStartTiredness;
					g_campLsHours = hours;
					g_campLsNeedsFloor = LastSeedNeedsFloor();
					g_campRestOwed = 0.0f;
					const auto night = ++g_campLsNight;
					logger::info("Camp rest (Last Seed): slept {:.1f} h outdoors from {:.0f}, worked out once Last Seed has woken you", hours, g_campLsBefore);
					After(5000, [night]() {
						if (night == g_campLsNight) {
							LastSeedNightSettled();
						}
					});
					break;
				}
			default:
				return RE::BSEventNotifyControl::kContinue;
			}
			// (a Last Seed night is worked out a few seconds later: LastSeedNightSettled shows the box then)
			// (at least half a point: the drink gives whole points, so less than that is nothing; Claude review 2026-11-16c)
			if (hours > 0.0f && g_campRestOwed >= 0.5f) {
				ShowCampRestHelpOnce();
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// SunHelm's Slumber perk: what it adds to every rest SunHelm gives (_SHFatigueSystem.DecreaseFatigueLevel: rank 1
	// 10%, rank 2 15%)
	float SunHelmSlumber()
	{
		const float rank = g_other.shSlumber ? g_other.shSlumber->value : 0.0f;
		return rank == 2.0f ? 0.15f : rank == 1.0f ? 0.10f : 0.0f;
	}

	// a SunHelm bedroll night waits a few seconds of running game for SunHelm to wake the player, then is worked out
	// (after the sleep, or after loading a save made in those seconds)
	void WaitSunHelmNight(bool a_cast)
	{
		const auto night = ++g_shNight;
		// (a drink drunk meanwhile is paid through the pot's script: it listens again now; see iNeed below)
		if (a_cast) {
			SKSE::GetTaskInterface()->AddTask([]() { RegisterMenuListener(); });
		}
		After(5000, [night]() {
			if (night == g_shNight && g_shNightOwed >= 0.0f) {
				SunHelmNightSettled();
			}
		});
	}

	// an iNeed bedroll night the same: after a few seconds the scripts are asked where iNeed left the rest
	void WaitINeedNight(bool a_cast)
	{
		const auto night = ++g_ineedNight;
		// the scripts' answer comes through the pot's script: it listens again now, as the cast when the game started
		// didn't always take (in-game test 2026-11-16: in a new game it never answered until it was cast)
		if (a_cast) {
			SKSE::GetTaskInterface()->AddTask([]() { RegisterMenuListener(); });
		}
		After(5000, [night]() {
			if (night != g_ineedNight || g_ineedNightBed < 0.0f) {
				return;
			}
			// ask the scripts (a mod event: C++ never calls into them); no answer within 10 s of running game
			// (FC_INeedBridge missing or the scripts busy): 15% of the night, stomach rot left out
			if (auto source = SKSE::GetModCallbackEventSource()) {
				SKSE::ModCallbackEvent event{ "FC_INeedWoke", RE::BSFixedString(), static_cast<float>(night), nullptr };
				source->SendEvent(&event);
			}
			After(10000, [night]() {
				if (night == g_ineedNight && g_ineedNightBed >= 0.0f) {
					INeedNightSettled(-1.0f, 1.0f);
				}
			});
		});
	}

	// the rest owed is in the scale of the needs mod running tiredness now (Survival and Survival Mode Improved share
	// one). One switched since the night: not given (Codex review 2026-11-16d, M7)
	bool FitsNow(NeedMod a_owner)
	{
		const auto owner = NeedOwner(Need::kTiredness);
		return a_owner == NeedMod::kNone || a_owner == owner ||
			   ((a_owner == NeedMod::kSurvival || a_owner == NeedMod::kSMI) && (owner == NeedMod::kSurvival || owner == NeedMod::kSMI));
	}
	bool OwedFitsNow() { return FitsNow(g_campRestOwner); }

	// rest a drink took, to be given by the scripts: kept until they collect it, then a mod event asks them to (main thread)
	void QueueCampRestPay(std::int32_t a_given, NeedMod a_owner)
	{
		if (a_given <= 0) {
			return;
		}
		if (g_campRestPay > 0.0f && g_campRestPayOwner != a_owner) {
			g_campRestPay = 0.0f;  // (an older amount in another needs mod's scale: not mixed in)
		}
		g_campRestPay += static_cast<float>(a_given);
		g_campRestPayOwner = a_owner;
		if (auto source = SKSE::GetModCallbackEventSource()) {
			SKSE::ModCallbackEvent event{ "FC_CampRestPay", RE::BSFixedString(), 0.0f, nullptr };
			source->SendEvent(&event);
		}
	}

	// Papyrus: FC_Menu.TakeCampRestPay() - the rest waiting to be given, once, as [whole points, needs mod]: the scripts
	// give it to that mod, not to whichever runs tiredness when they get to it (Codex review 2026-11-16g, 6). [0, 0] when
	// none, or when it is in another needs mod's scale than the one running tiredness now (then dropped)
	std::vector<std::int32_t> TakeCampRestPay()
	{
		const float pay = g_campRestPay;
		const auto  owner = g_campRestPayOwner;
		const bool  fits = FitsNow(owner);
		g_campRestPay = 0.0f;
		if (pay <= 0.0f) {
			return { 0, 0 };
		}
		if (!fits || !FC::FeatureSwitch(FC::Feature::kCampRest)) {
			logger::info("Camp rest: {:.0f} waiting for the scripts dropped (needs mod switched, or camp rest off)", pay);
			return { 0, 0 };
		}
		logger::info("Camp rest: the scripts collect {:.0f} (needs mod {})", pay, static_cast<int>(owner));
		return { static_cast<std::int32_t>(std::round(pay)), static_cast<std::int32_t>(owner) };
	}

	// iNeed's sleep multiplier for one need (_SNNeeds.NeedsCheck)
	float INeedSleepMult(const RE::TESGlobal* a_penalty)
	{
		switch (a_penalty ? static_cast<int>(a_penalty->value) : 0) {
		case -2: return 1.08f;
		case 1:  return 0.95f;
		case 2:  return 0.90f;
		case 3:  return 0.80f;
		default: return 1.0f;
		}
	}

	// iNeed's unsafe place (_SNQuestScript.IsUnsafeLocation): an uncleared location that can be cleared, except a vampire
	// in a vampire lair (its bandit, Forsworn and warlock exceptions need the player in those factions: not checked)
	bool INeedUnsafeHere(RE::PlayerCharacter* a_player)
	{
		static const auto clearable = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeClearable");
		static const auto vampireLair = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeVampireLair");
		static const auto vampire = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("Vampire");
		auto loc = a_player ? a_player->GetCurrentLocation() : nullptr;
		if (!loc || !clearable || loc->IsCleared() || !loc->HasKeyword(clearable)) {
			return false;
		}
		return !(vampire && vampireLair && a_player->HasKeyword(vampire) && loc->HasKeyword(vampireLair));
	}

	// Last Seed's hunger and thirst floor for a night (_Seed_FatigueSystem.OnSleepStop): 25 each when its needs option is
	// on and the player is Hungry or Thirsty or worse
	float LastSeedNeedsFloor()
	{
		if (!g_other.lsSleepByNeeds || g_other.lsSleepByNeeds->value != 2.0f) {
			return 0.0f;
		}
		return (AtLeast(g_other.lsHungerLevel, 2.0f) ? 25.0f : 0.0f) + (AtLeast(g_other.lsThirstLevel, 2.0f) ? 25.0f : 0.0f);
	}

	void StartSleepWatch()
	{
		static SleepWatch watch;
		if (auto events = RE::ScriptEventSourceHolder::GetSingleton()) {
			events->AddEventSink<RE::TESSleepStartEvent>(&watch);
			events->AddEventSink<RE::TESSleepStopEvent>(&watch);
			events->AddEventSink<RE::TESActivateEvent>(&watch);
			logger::info("Camp rest: sleep watched");
		}
	}

	// Last Seed's night, once it has woken the player: the night as a bed would have left it (12 a game hour,
	// _Seed_FatigueSystem SLEEP_RESTORE_RATE) against where Last Seed left the player; its outdoor floor is 25, so never
	// more than that. Kept as a fixed amount (saved with the game), given by the next brewed drink
	void LastSeedNightSettled()
	{
		if (g_campLsBefore < 0.0f) {
			return;
		}
		if (g_other.lsFatigue) {
			// a bed stops at the hunger and thirst floor too (DecreaseAttributeSleep: never lowered below it, left alone
			// if already there); only the place's 25 is the ground's (Codex review 2026-11-16 afternoon, M3)
			// (a night saved by version 2 has no floor of its own: the one now, as near as can be known; Codex review
			// 2026-11-16c: 0 there repeated the old overpayment)
			const float floor = g_campLsNeedsFloor >= 0.0f ? g_campLsNeedsFloor : LastSeedNeedsFloor();
			const float bed = g_campLsBefore <= floor ? g_campLsBefore : std::max(floor, g_campLsBefore - g_campLsHours * 12.0f);
			g_campRestOwed = std::clamp(g_other.lsFatigue->value - bed, 0.0f, 25.0f);
			g_campRestOwner = NeedMod::kLastSeed;
			// (fatigue keeps rising in the seconds before this: a fraction of a point is that, not the ground. The help box
			// showed after a night that owed 0.03, logged as 0; in-game test 2026-11-16, Claude and Codex reviews)
			logger::info("Camp rest (Last Seed): woke at {:.2f}, a bed would give {:.2f}, owed {:.2f}", g_other.lsFatigue->value, bed, g_campRestOwed);
			if (g_campRestOwed >= 0.5f) {
				ShowCampRestHelpOnce();
			}
		}
		g_campLsBefore = -1.0f;
	}

	// an iNeed bedroll night, a few seconds after waking: a_rest is iNeed's rest where the night left it (0..110, higher is
	// better; -1: the scripts didn't answer) and a_stomach its stomach rot factor. The bedroll gave 85% of a bed's night,
	// both capped at 110: what a bed would have added on top is owed, never more than the room left below 110
	void INeedNightSettled(float a_rest, float a_stomach)
	{
		if (NeedOwner(Need::kTiredness) != NeedMod::kINeed) {
			// a night saved while it waited, loaded with iNeed gone or its tiredness off: nothing to give back into
			// (in-game test 2026-11-16 21:03, the same with SunHelm)
			g_ineedNightBed = -1.0f;
			g_campRestOwed = 0.0f;
			g_campRestClaimed = false;
			logger::info("Camp rest (iNeed): the waiting night dropped (iNeed doesn't run tiredness now)");
			return;
		}
		const float bed = g_ineedNightBed * std::clamp(a_stomach, 0.0f, 1.0f);
		g_ineedNightBed = -1.0f;
		float owed = bed * 0.15f;
		if (a_rest >= 0.0f) {
			owed = (std::min)(owed, (std::max)(0.0f, 110.0f - a_rest));
		}
		g_campRestOwed = (std::max)(0.0f, owed);
		logger::info("Camp rest (iNeed): woke at {}, stomach x{:.2f}, a bed would give {:.1f}, owed {:.1f}",
			a_rest >= 0.0f ? std::format("{:.1f}", a_rest) : std::string("? (not reported)"), a_stomach, bed, g_campRestOwed);
		CampRestSettled(NeedMod::kINeed);
	}

	// a SunHelm bedroll night, a few seconds after waking: the 25% the bedroll held back, but no more than the tiredness
	// SunHelm left (a bed stops at 0)
	void SunHelmNightSettled()
	{
		if (NeedOwner(Need::kTiredness) != NeedMod::kSunHelm) {
			// a night saved while it waited, loaded with SunHelm gone or its fatigue off: before, the night was counted
			// in full and the help box showed (in-game test 2026-11-16 21:03)
			g_shNightOwed = -1.0f;
			g_campRestOwed = 0.0f;
			g_campRestClaimed = false;
			logger::info("Camp rest (SunHelm): the waiting night dropped (SunHelm doesn't run tiredness now)");
			return;
		}
		const float part = g_shNightOwed;
		g_shNightOwed = -1.0f;
		const float left = g_other.shFatigueNow ? (std::max)(0.0f, g_other.shFatigueNow->value) : part;
		g_campRestOwed = (std::max)(0.0f, (std::min)(part, left));
		logger::info("Camp rest (SunHelm): woke at {:.0f} tiredness, 25% of the night {:.0f}, owed {:.0f}", left, part, g_campRestOwed);
		CampRestSettled(NeedMod::kSunHelm);
	}

	// a night just worked out (iNeed's or SunHelm's): kept for the next drink, or given now when a drink already claimed
	// it. Given through a mod event (C++ never calls into the scripts): FC_PotContainerScript passes it to
	// FC_Core.GiveBackRest, as the drink itself would have
	void CampRestSettled(NeedMod a_owner)
	{
		g_campRestOwner = a_owner;
		if (g_campRestClaimed) {
			g_campRestClaimed = false;
			const float owed = g_campRestOwed;
			g_campRestOwed = 0.0f;
			// (the needs mod switched while the night was worked out: not paid into the other one; Codex review 2026-11-16e, M2)
			const auto given = FC::FeatureSwitch(FC::Feature::kCampRest) && OwedFitsNow() ? static_cast<std::int32_t>(std::round(owed)) : 0;
			if (given > 0) {
				logger::info("Camp rest: {} given back to the drink drunk while the night was being worked out", given);
				QueueCampRestPay(given, a_owner);
			}
			return;
		}
		if (g_campRestOwed >= 0.5f) {
			ShowCampRestHelpOnce();
		}
	}

	// Papyrus: FC_Menu.INeedRestNow(float fatigue, float stomach, int night) - iNeed's rest and stomach rot factor after an
	// iNeed bedroll night (FC_INeedBridge.ReportRest answering the mod event FC_INeedWoke); kept only for that use. Called
	// on a script thread: the night is settled on the game's thread
	void PapyrusINeedRestNow(RE::StaticFunctionTag*, float a_fatigue, float a_stomach, std::int32_t a_night)
	{
		if (!std::isfinite(a_fatigue) || !std::isfinite(a_stomach)) {
			return;
		}
		const float rest = std::clamp(a_fatigue, 0.0f, 110.0f);
		const float stomach = std::clamp(a_stomach, 0.0f, 1.0f);
		SKSE::GetTaskInterface()->AddTask([rest, stomach, a_night]() {
			if (static_cast<std::uint32_t>(a_night) == g_ineedNight && g_ineedNightBed >= 0.0f) {
				INeedNightSettled(rest, stomach);
			}  // else a late answer about an earlier night, or one from before a load
		});
	}

	// Papyrus: FC_Menu.TakeCampRest() - the rest owed (whole points), given out once: the next call returns 0
	std::int32_t TakeCampRest()
	{
		// a drink in the first seconds after waking: Last Seed's night is settled now
		LastSeedNightSettled();
		// iNeed's or SunHelm's night still waiting for the needs mod: this drink claims it, given as soon as it is known
		if (g_ineedNightBed >= 0.0f || g_shNightOwed >= 0.0f) {
			g_campRestClaimed = true;
			logger::info("Camp rest: a drink while the night is still being worked out: it gets that night's rest when it is");
			return 0;
		}
		// rest owed in another needs mod's scale than the one running tiredness now (one switched since): not given
		if (g_campRestOwed > 0.0f && !OwedFitsNow()) {
			logger::info("Camp rest: {:.0f} owed in another needs mod's tiredness than the one running now: dropped", g_campRestOwed);
			g_campRestOwed = 0.0f;
		}
		// switched off since the night (Survival Mode settings page): nothing is given, and nothing kept for later
		const float owed = g_campRestOwed;
		g_campRestOwed = 0.0f;
		const auto given = FC::FeatureSwitch(FC::Feature::kCampRest) ? static_cast<std::int32_t>(std::round(owed)) : 0;
		if (given > 0) {
			logger::info("Camp rest: {} given back by a brewed drink", given);
		}
		return given;
	}

	void CampRestSave(SKSE::SerializationInterface* a_intfc)
	{
		// version 2: a Last Seed night not settled yet is kept too (a save right after waking lost it; reviews 2026-11-16);
		// version 3: with that night's hunger and thirst floor (-1: not known, a night from a version 2 save);
		// version 4: the needs mod the rest owed is in;
		// version 5: an iNeed or SunHelm night not worked out yet, and whether a drink claimed it (a quicksave in the
		// seconds after waking lost it; Codex review 2026-11-16e, M3);
		// version 6: rest a drink took that the scripts haven't collected yet, and its needs mod
		if (a_intfc->OpenRecord('REST', 6)) {
			a_intfc->WriteRecordData(g_campRestOwed);
			a_intfc->WriteRecordData(static_cast<std::uint8_t>(g_campRestHelpShown ? 1 : 0));
			a_intfc->WriteRecordData(g_campLsBefore);
			a_intfc->WriteRecordData(g_campLsHours);
			a_intfc->WriteRecordData(g_campLsNeedsFloor);
			a_intfc->WriteRecordData(static_cast<std::int32_t>(g_campRestOwner));
			a_intfc->WriteRecordData(g_shNightOwed);
			a_intfc->WriteRecordData(g_ineedNightBed);
			a_intfc->WriteRecordData(g_ineedNightHours);
			a_intfc->WriteRecordData(static_cast<std::uint8_t>(g_campRestClaimed ? 1 : 0));
			a_intfc->WriteRecordData(g_campRestPay);
			a_intfc->WriteRecordData(static_cast<std::int32_t>(g_campRestPayOwner));
		}
	}

	bool CampRestLoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, std::uint32_t a_version)
	{
		if (a_type != 'REST') {
			return false;
		}
		if (a_version >= 1 && a_version <= 6) {
			std::uint8_t shown = 0;
			float owed = 0.0f;
			if (!ReadAll(a_intfc, owed) || !ReadAll(a_intfc, shown)) {
				return true;  // cut short: nothing owed
			}
			// a damaged value never turns into a huge or broken amount of rest (Codex review 2026-11-15)
			g_campRestOwed = std::isfinite(owed) ? std::clamp(owed, 0.0f, 1000.0f) : 0.0f;
			g_campRestHelpShown = shown != 0;
			float before = -1.0f, hours = 0.0f, floor = 0.0f;
			std::int32_t owner = 0;
			// every field read first (each version writes them all), then checked
			const bool lsRead = a_version >= 2 && ReadAll(a_intfc, before) && ReadAll(a_intfc, hours) && (a_version < 3 || ReadAll(a_intfc, floor));
			const bool ownerRead = a_version >= 4 && lsRead && ReadAll(a_intfc, owner);
			if (ownerRead && owner >= 0 && owner <= static_cast<std::int32_t>(NeedMod::kFrostfall)) {
				g_campRestOwner = static_cast<NeedMod>(owner);
			} else if (a_version >= 4) {
				// a version 4 record cut short or with no needs mod it knows: the rest has no owner to go to, so none is
				// kept (a save from before version 4 keeps the old rule; Codex review 2026-11-16e, L6)
				g_campRestOwed = 0.0f;
			}
			// version 5: the iNeed or SunHelm night still waiting, worked out a few seconds after the load (iNeed's and
			// SunHelm's own scripts finish that wake in the loaded game)
			float shOwed = -1.0f, inBed = -1.0f, inHours = 0.0f;
			std::uint8_t claimed = 0;
			if (a_version >= 5 && ownerRead && ReadAll(a_intfc, shOwed) && ReadAll(a_intfc, inBed) && ReadAll(a_intfc, inHours) && ReadAll(a_intfc, claimed)) {
				const bool shOk = std::isfinite(shOwed) && shOwed >= 0.0f && shOwed <= 10000.0f;
				const bool inOk = std::isfinite(inBed) && std::isfinite(inHours) && inBed >= 0.0f && inBed <= 10000.0f && inHours >= 0.0f && inHours <= 1000.0f;
				if (shOk) {
					g_shNightOwed = shOwed;
					WaitSunHelmNight(false);  // (no cast from the load: the load's own one, 1 s after it, covers it; review 2026-11-16f, L2)
				} else if (inOk) {
					g_ineedNightBed = inBed;
					g_ineedNightHours = inHours;
					WaitINeedNight(false);
				}
				g_campRestClaimed = claimed != 0 && (shOk || inOk);
			}
			// version 6: rest waiting for the scripts; they collect it when they listen again after the load
			float pay = 0.0f;
			std::int32_t payOwner = 0;
			if (a_version >= 6 && ReadAll(a_intfc, pay) && ReadAll(a_intfc, payOwner) && std::isfinite(pay) && pay > 0.0f && pay <= 10000.0f &&
				payOwner >= 0 && payOwner <= static_cast<std::int32_t>(NeedMod::kFrostfall)) {
				g_campRestPay = pay;
				g_campRestPayOwner = static_cast<NeedMod>(payOwner);
			}
			if (lsRead && std::isfinite(before) && std::isfinite(hours) && before >= 0.0f && before <= 1000.0f && hours >= 0.0f && hours <= 1000.0f &&
				// (-1: a version 2 night saved again before it was worked out; Codex review 2026-11-16d, L13)
				(a_version < 3 || (std::isfinite(floor) && (floor == -1.0f || (floor >= 0.0f && floor <= 50.0f))))) {
				// a Last Seed night still waiting (saved within seconds of waking): settled from where the save left Last Seed,
				// a few seconds after the load, as the sleep event does (not by the next drink, hours later; Claude review
				// 2026-11-16b, L2). (Jobs added during the load survive: the old game's jobs are dropped before it)
				g_campLsBefore = before;
				g_campLsHours = hours;
				g_campLsNeedsFloor = a_version < 3 ? -1.0f : floor;  // a version 2 night: no floor known (settled with the one then)
				const auto night = ++g_campLsNight;
				After(5000, [night]() {
					if (night == g_campLsNight) {
						LastSeedNightSettled();
					}
				});
			}
		}
		return true;
	}

// ---------------------------------------------------------------------------------------------------------------
// Stepped drains (his design, 2026-11-03): Survival shrinks the stamina (hunger), health (cold) and magicka
// (tiredness) bars a little more with every need point, so the red end creeps; here each bar takes one fixed step
// per stage instead, changing at the moment Survival changes stage (its message and stage effects), so the bar reads
// as a clear signal. Survival keeps everything else (stages, messages, effects); only the drain's size is replaced.
// Steps: 10 / 20 / 35 / 55% from the first stage that drains (no bar ever empties). Stage limits are Survival's own
// (its scripts' SetNeedStageValues): hunger 160/340/520/770, cold 120/300/500/800, tiredness 160/340/560/800.
//
// How: Survival recalculates on every need change: it writes its penalty (Variable02/04/03), then the HUD's percent
// (Update.esm 0x2EDF/0x2EDE/0x2EE0), then recasts its penalty spell (0xA05/0xA07/0xA06, sized by Variable0x through
// its perk). When the HUD percent differs from the last one written here, Survival has just recalculated: the step
// is put in its place (Variable0x = full bar x step, HUD = step x 100) and the spell recast so the real loss matches
// the bar. The full bar is max + the penalty the spell takes now (read from the spell's active effect). A 0 Survival
// wrote inside a draining stage is its own (beast races never get cold's drain) and is left alone.
//
// Save/load: nothing of its own is saved; the steps live in Survival's own saved numbers, and after a load (or with
// this switched off, or the mod removed) Survival's next recalculation is read afresh or puts its own drain back.
// ---------------------------------------------------------------------------------------------------------------

	constexpr float DRAIN_STEPS[4] = { 0.10f, 0.20f, 0.35f, 0.55f };

	struct SteppedNeed
	{
		const char*     name;
		RE::FormID      needValue;  // ccqdrsse001-survivalmode.esl
		float           maxValue;   // Survival_*NeedMaxValue: hunger and cold 1000, tiredness 960
		float           stages[4];  // where each draining stage starts
		RE::ActorValue  penaltyAV;  // the penalty Survival's perk sizes its spell from
		RE::ActorValue  attribute;  // the bar
		RE::FormID      hudGlobal;  // Update.esm
		RE::FormID      spellId;    // ccqdrsse001-survivalmode.esl
		RE::TESGlobal*  need = nullptr;
		RE::TESGlobal*  hud = nullptr;
		RE::SpellItem*  spell = nullptr;
		float           lastHud = -1.0f;  // the HUD percent written here last (-1: none yet in this game)
		float           lastStep = -1.0f;  // for the log: only stage changes are written down
	};
	SteppedNeed g_steppedNeeds[3] = {
		{ "hunger", 0x81A, 1000.0f, { 160.0f, 340.0f, 520.0f, 770.0f }, RE::ActorValue::kVariable02, RE::ActorValue::kStamina, 0x2EDF, 0xA05 },
		{ "cold", 0x81B, 1000.0f, { 120.0f, 300.0f, 500.0f, 800.0f }, RE::ActorValue::kVariable04, RE::ActorValue::kHealth, 0x2EDE, 0xA07 },
		{ "tiredness", 0x816, 960.0f, { 160.0f, 340.0f, 560.0f, 800.0f }, RE::ActorValue::kVariable03, RE::ActorValue::kMagicka, 0x2EE0, 0xA06 },
	};

	// the drain's step for a need value: 0 before the first draining stage
	float DrainStep(const SteppedNeed& a_need, float a_value)
	{
		float step = 0.0f;
		for (int i = 0; i < 4; ++i) {
			if (a_value >= a_need.stages[i]) {
				step = DRAIN_STEPS[i];
			}
		}
		return step;
	}

	void RecastPenaltySpell(RE::PlayerCharacter* a_player, RE::SpellItem* a_spell)
	{
		auto caster = a_player ? a_player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant) : nullptr;
		if (caster && a_spell) {
			caster->CastSpellImmediate(a_spell, false, a_player, 1.0f, false, 0.0f, a_player);
		}
	}

	// the penalty Survival's spell takes from the bar right now (its active effect on the player; 0 if none)
	float PenaltySpellMagnitude(RE::Actor* a_actor, RE::SpellItem* a_spell)
	{
		auto effects = a_actor && a_spell ? a_actor->GetActiveEffectList() : nullptr;
		if (!effects) {
			return 0.0f;
		}
		for (auto effect : *effects) {
			if (effect && effect->spell == a_spell && effect->flags.none(RE::ActiveEffect::Flag::kInactive, RE::ActiveEffect::Flag::kDispelled)) {
				return effect->magnitude;
			}
		}
		return 0.0f;
	}

	// The attribute's full value (as if Survival took nothing). Survival has just set the penalty actor value to
	// full x its linear share and only then the HUD global (ApplyAttributePenalty), while its penalty spell is recast
	// a moment later; so max + the spell's magnitude can be out of date here (the first test read 84 instead of 100).
	// Survival's own numbers are exact: full = penalty / share, with the share worked out from the need value the way
	// Survival does. The spell way is kept only for a share of 0 (nothing to divide by).
	float FullAttribute(RE::PlayerCharacter* a_player, const SteppedNeed& a_need)
	{
		const float penalty = a_player->GetActorValue(a_need.penaltyAV);
		const float from = a_need.stages[0] - 1.0f;
		const float share = std::clamp((a_need.need->value - from) / (a_need.maxValue - from), 0.0f, 1.0f);
		if (share > 0.0f && penalty > 0.0f) {
			return penalty / share;
		}
		return a_player->GetActorValueMax(a_need.attribute) + PenaltySpellMagnitude(a_player, a_need.spell);
	}

	void StepDrainsTick()
	{
		// the "Penalties in steps" switch (Survival Mode settings page). Turned off, Survival's next recalculation puts
		// its own penalty back; turned on again, the current penalty is read afresh first (as after a load)
		static bool wasOn = true;
		const bool on = FC::FeatureSwitch(FC::Feature::kSteppedDrains);
		if (on != wasOn) {
			wasOn = on;
			logger::info("Stepped drains: {}", on ? "on" : "off (Survival Mode's own penalties)");
			for (auto& n : g_steppedNeeds) {
				n.lastHud = -1.0f;
				n.lastStep = -1.0f;
			}
		}
		auto player = Player();
		// Survival Mode only: Survival Mode Improved re-applies its own penalties every half second, so steps can't hold
		// there (the switch shows "has no effect" with it); the needs mods already work in steps
		if (!on || !player || !SurvivalOn() || NeedOwner(Need::kHunger) != NeedMod::kSurvival) {
			return;
		}
		static bool looked = false;
		if (!looked) {
			looked = true;
			for (auto& n : g_steppedNeeds) {
				n.need = PluginForm<RE::TESGlobal>(n.needValue, "ccqdrsse001-survivalmode.esl");
				n.spell = PluginForm<RE::SpellItem>(n.spellId, "ccqdrsse001-survivalmode.esl");
				n.hud = PluginForm<RE::TESGlobal>(n.hudGlobal, "Update.esm");
				logger::info("Stepped drains: {} {}", n.name, n.need && n.spell && n.hud ? "ready" : "MISSING forms (left to Survival)");
			}
		}
		for (int i = 0; i < 3; ++i) {
			auto& n = g_steppedNeeds[i];
			if (!n.need || !n.spell || !n.hud) {
				continue;
			}
			if (n.hud->value == n.lastHud) {
				continue;  // nothing new from Survival
			}
			if (n.lastHud < 0.0f) {
				// the first look after a load: the saved penalty is kept as it is (our own step, or Survival's share
				// in a save from before), and the next change Survival makes is stepped as usual. Working the full
				// value out here went wrong in the test of 2026-11-04 (28 instead of 100): the saved penalty was ours,
				// not Survival's share.
				n.lastHud = n.hud->value;
				n.lastStep = DrainStep(n, n.need->value);
				continue;
			}
			const float step = DrainStep(n, n.need->value);
			if (n.hud->value <= 0.0f && step > 0.0f) {
				n.lastHud = n.hud->value;  // Survival's own 0 (a beast race's cold, a stopped need): left alone
				continue;
			}
			const float full = FullAttribute(player, n);
			player->SetActorValue(n.penaltyAV, full * step);
			n.hud->value = step * 100.0f;
			n.lastHud = n.hud->value;
			RecastPenaltySpell(player, n.spell);
			if (step != n.lastStep) {
				n.lastStep = step;
				logger::info("Stepped drains: {} {:.0f} -> {:.0f}% of {:.0f}", n.name, n.need->value, step * 100.0f, full);
			}
		}
	}

	// a save is loading or a new game starts: the loaded game's numbers are read afresh, nothing owed until its own
	// saved value comes back (CampRestLoadRecord)
	void SurvivalReset()
	{
		for (auto& n : g_steppedNeeds) {
			n.lastHud = -1.0f;
			n.lastStep = -1.0f;
		}
		g_campRestOwed = 0.0f;
		g_campRestHelpShown = false;
		g_sleepStartDay = -1.0f;
		g_campLsBefore = -1.0f;
		// an iNeed bedroll activated just before a load is not carried into the loaded game (its 5 s job is dropped;
		// Claude review 2026-11-16b, L1)
		g_ineedBedrollActivated = false;
		++g_ineedBedrollActivation;
		// (and an iNeed or SunHelm night still waiting: the loaded game's own one comes back from 'REST' version 5)
		g_ineedNightBed = -1.0f;
		++g_ineedNight;
		g_shNightOwed = -1.0f;  // (a SunHelm night the same)
		++g_shNight;
		g_campRestClaimed = false;
		g_campRestOwner = NeedMod::kNone;
		g_campRestPay = 0.0f;
		g_campRestPayOwner = NeedMod::kNone;
		ThirstReset();
		AfflictionsReset();
	}

// ---------------------------------------------------------------------------------------------------------------
// Test aid: bars always shown (bat fcbars / fcbarsoff set FreeCooking.esp's FC_ShowBars 0xA79 to 1 / 0).
// A full bar fades out, so a test can't tell a clean bar from a hidden one. While FC_ShowBars is 1, the HUD's own
// RunMeterAnim is called on the health, magicka and stamina meters every tick, the same call the HUD makes every
// frame for a bar with a Survival penalty (hudmenu.swf HUDMenu.onEnterFrame), so the bars stay up.
// Save/load: nothing of ours is saved; FC_ShowBars is a global saved with the game (0 unless a test bat set it).
// ---------------------------------------------------------------------------------------------------------------

	constexpr RE::FormID SHOW_BARS_GLOBAL = 0xA79;  // FreeCooking.esp FC_ShowBars

	void ShowBarsTick()
	{
		static RE::TESGlobal* global = nullptr;
		static bool looked = false;
		static bool wasOn = false;
		if (!looked) {
			looked = true;
			global = PluginForm<RE::TESGlobal>(SHOW_BARS_GLOBAL, PLUGIN);
			if (!global) {
				logger::error("Test bars: FC_ShowBars not found");
			}
		}
		const bool on = global && global->value >= 1.0f;
		if (on != wasOn) {
			wasOn = on;
			logger::info("Test bars: {}", on ? "always shown" : "back to normal");
		}
		if (!on) {
			return;
		}
		SKSE::GetTaskInterface()->AddUITask([]() {
			auto ui = RE::UI::GetSingleton();
			if (!ui) {
				return;
			}
			auto hud = ui->GetMenu(RE::HUDMenu::MENU_NAME);
			if (!hud || !hud->uiMovie) {
				return;
			}
			for (const char* meter : { "Health", "Magica", "Stamina" }) {  // "Magica" is the HUD's own spelling
				RE::GFxValue clip;
				const std::string path = std::string("_root.HUDMovieBaseInstance.") + meter;
				if (hud->uiMovie->GetVariable(&clip, path.c_str()) && clip.IsDisplayObject()) {
					hud->uiMovie->Invoke("_root.HUDMovieBaseInstance.RunMeterAnim", nullptr, &clip, 1);
				}
			}
		});
	}

	void StartSurvivalWatch()
	{
		RepeatAlways(1000, []() {
			if (!g_loadingSave) {
				SurvivalTick();
				ThirstTick();
				AfflictionsTick();
			}
			return true;
		});
		// stepped drains: fast enough that Survival's own creeping red end is replaced before it shows
		RepeatAlways(100, []() {
			if (!g_loadingSave) {
				StepDrainsTick();
				ShowBarsTick();
			}
			return true;
		});
	}
