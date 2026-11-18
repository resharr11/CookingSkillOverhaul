// Part of CookingMenu.cpp: included in order inside its anonymous namespace (not compiled on its own).
// ---------------------------------------------------------------------------------------------------------------
// Thirst (Survival Mode; his design, built 2026-11-12). Survival Mode has no thirst, so Cooking Skill Overhaul adds one, built
// like Survival's own hunger: the same 0-1000 scale and stage edges, and drinks work like our food (a brewed drink
// keeps you quenched for its effect's duration plus about 5 game hours).
//   Stages: Quenched < 80, Hydrated < 160, Thirsty < 340, Parched < 520, Dry < 770, Dehydrated.
//   Rises 35 points per game hour (Thirsty ~4.6 h after a drink, Dehydrated ~22 h; compared with SunHelm, Last Seed
//   and RND, none of which has thirst arrive later than hunger), x0.75 for the hours slept (SunHelm's rule).
//   Effects (the stage's ability, FreeCooking.esp): Quenched magicka regenerates 10% faster; Thirsty / Parched / Dry /
//   Dehydrated spells cost 10/20/30/40% more; from Parched on shouts recharge 10/25/50% slower (his call: SunHelm's
//   numbers). No bar penalty.
//   Drinks: a brewed drink (a pool dish whose look-alike is a drink) quenches (its duration in game hours + 5) x 35; a
//   Dragon Scales drink quenches fully. A vanilla drink (the alcoholic drinks of the base game and DLCs, and milk)
//   quenches 4 game hours. Alcohol, brewed or vanilla, follows the "Alcohol" setting: like any drink (default), half,
//   or a fifth ("for the crazy purists"). There is nothing to drink from in the world (his call: our drinks are easy
//   to make).
//   Runs only with Survival Mode on and the Thirst switch on, and steps aside while another mod's thirst runs
//   (SunHelm, Last Seed, Realistic Needs and Diseases, iNeed). Then, with the switch on, a brewed drink quenches that
//   mod's thirst instead, by the same game hours (his call 2026-11-14; FC_Core.OtherThirst).
// Save/load: the value is FreeCooking.esp's FC_Thirst global (saved with the game; 0 in a new game); the stage
// abilities are saved like any spell and set again on the first tick after a load. Time is counted from the first tick
// after a load, so nothing is added for the moment of loading.
// ---------------------------------------------------------------------------------------------------------------

	constexpr RE::FormID THIRST_VALUE = 0xA90;        // FC_Thirst
	constexpr RE::FormID THIRST_STAGE = 0xA91;        // FC_ThirstStage (-1 off, 0 Quenched ... 5 Dehydrated)
	constexpr RE::FormID THIRST_QUENCHED = 0xA93;     // FC_ThirstQuenched (ability)
	constexpr RE::FormID THIRST_BAD_FIRST = 0xA9D;    // FC_ThirstThirsty, Parched, Dry, Dehydrated (abilities, in order)
	constexpr float      THIRST_PER_HOUR = 35.0f;
	constexpr float      THIRST_SLEEP_MULT = 0.75f;
	constexpr float      THIRST_MAX = 1000.0f;
	constexpr float      THIRST_STAGE_START[6] = { 0.0f, 80.0f, 160.0f, 340.0f, 520.0f, 770.0f };
	constexpr float      DRINK_HOURS_AFTER = 5.0f;    // a brewed drink: its duration + this
	constexpr float      VANILLA_DRINK_HOURS = 4.0f;
	constexpr const char* THIRST_STAGE_NAMES[6] = { "Quenched", "Hydrated", "Thirsty", "Parched", "Dry", "Dehydrated" };
	// the notice when the stage changes (whole sentences, for translators)
	constexpr const char* THIRST_MESSAGES[6] = { "You are Quenched.", "You are Hydrated.", "You are Thirsty.", "You are Parched.", "You are Dry.",
		"You are Dehydrated." };

	// the base game's alcoholic drinks (every food item that plays the drinking sound ITMPotionUse, read from
	// Skyrim.esm, Update.esm, HearthFires.esm and Dragonborn.esm on 2026-11-12), and milk, the one non-alcoholic drink
	struct DrinkId
	{
		RE::FormID  id;
		const char* file;
	};
	constexpr DrinkId VANILLA_ALCOHOL[] = {
		{ 0x034C5E, "Skyrim.esm" },     // Ale
		{ 0x034C5D, "Skyrim.esm" },     // Nord Mead
		{ 0x02C35A, "Skyrim.esm" },     // Black-Briar Mead
		{ 0x0F693F, "Skyrim.esm" },     // Black-Briar Reserve
		{ 0x0508CA, "Skyrim.esm" },     // Honningbrew Mead
		{ 0x107A8A, "Skyrim.esm" },     // Mead with Juniper Berry
		{ 0x0555E8, "Skyrim.esm" },     // Dragon's Breath Mead
		{ 0x03133C, "Skyrim.esm" },     // Wine
		{ 0x0C5348, "Skyrim.esm" },     // Wine
		{ 0x03133B, "Skyrim.esm" },     // Alto Wine
		{ 0x0C5349, "Skyrim.esm" },     // Alto Wine
		{ 0x0F257E, "Skyrim.esm" },     // Jessica's Wine
		{ 0x085368, "Skyrim.esm" },     // Spiced Wine
		{ 0x01895F, "Skyrim.esm" },     // Firebrand Wine
		{ 0x036D53, "Skyrim.esm" },     // Colovian Brandy
		{ 0x065C37, "Skyrim.esm" },     // Velvet LeChance
		{ 0x065C38, "Skyrim.esm" },     // White-Gold Tower
		{ 0x065C39, "Skyrim.esm" },     // Cliff Racer
		{ 0x0B91D7, "Skyrim.esm" },     // Cyrodilic Brandy
		{ 0x0DC172, "Skyrim.esm" },     // Balmora Blue
		{ 0x09380D, "Skyrim.esm" },     // Argonian Ale
		{ 0x003535, "HearthFires.esm" },  // Wine
		{ 0x003536, "HearthFires.esm" },  // Wine
		{ 0x0207E5, "Dragonborn.esm" },  // Flin
		{ 0x0207E6, "Dragonborn.esm" },  // Sujamma
		{ 0x024E0B, "Dragonborn.esm" },  // Sujamma
		{ 0x0248CC, "Dragonborn.esm" },  // Shein
		{ 0x0248CE, "Dragonborn.esm" },  // Matze
		{ 0x0320DF, "Dragonborn.esm" },  // Emberbrand Wine
		{ 0x03572F, "Dragonborn.esm" },  // Ashfire Mead
	};
	constexpr DrinkId VANILLA_MILK = { 0x003534, "HearthFires.esm" };
	// the looks our brewed drinks borrow (data.json dish ids): alcoholic for mead, ale, wine and spiced wine; Herbal Tea
	// borrows White-Gold Tower's look and Fruit Juice has our own jug, so those two are drinks but not alcohol
	constexpr DrinkId BREW_ALCOHOL_LOOKS[] = {
		{ 0x034C5D, "Skyrim.esm" }, { 0x034C5E, "Skyrim.esm" }, { 0x03133C, "Skyrim.esm" }, { 0x085368, "Skyrim.esm" },
	};
	constexpr DrinkId BREW_SOFT_LOOKS[] = { { 0x065C38, "Skyrim.esm" }, { 0x0009F4, "FreeCooking.esp" } };

	struct ThirstForms
	{
		bool                          looked = false;
		RE::TESGlobal*                value = nullptr;
		RE::TESGlobal*                stage = nullptr;
		std::array<RE::SpellItem*, 6> spells{};  // [1] Hydrated has none
		std::unordered_set<RE::FormID> alcohol, brewAlcohol, brewSoft;
		RE::FormID                    milk = 0;
		// other mods' thirst
		RE::TESGlobal*                sunhelmOn = nullptr;      // _SHEnabled
		RE::TESGlobal*                sunhelmThirstOff = nullptr;  // _SHThirstShouldBeDisabled
		RE::TESGlobal*                lastSeedRunning = nullptr;   // LastSeedRunning (2 = running)
		RE::TESGlobal*                lastSeedThirst = nullptr;    // _Seed_Setting_SystemEnabled_Thirst (2 = on)
		RE::TESGlobal*                rndState = nullptr;          // RND_State (1 = running)
		RE::TESQuest*                 ineedQuest = nullptr;        // _SNQuest
	};
	ThirstForms g_thirst;

	float g_thirstLastHour = -1.0f;  // game hours at the last tick (-1: start counting at the next tick)
	bool  g_thirstSleeping = false;
	int   g_thirstShownStage = -2;   // the stage whose ability is on the player (-2: not checked since the load)
	bool  g_thirstOtherTold = false;

	void ThirstFormsLookup()
	{
		auto& f = g_thirst;
		if (f.looked) {
			return;
		}
		f.looked = true;
		f.value = PluginForm<RE::TESGlobal>(THIRST_VALUE, PLUGIN);
		f.stage = PluginForm<RE::TESGlobal>(THIRST_STAGE, PLUGIN);
		f.spells[0] = PluginForm<RE::SpellItem>(THIRST_QUENCHED, PLUGIN);
		for (int i = 0; i < 4; ++i) {
			f.spells[2 + i] = PluginForm<RE::SpellItem>(THIRST_BAD_FIRST + i, PLUGIN);
		}
		auto data = RE::TESDataHandler::GetSingleton();
		const auto add = [&](std::unordered_set<RE::FormID>& a_set, const DrinkId& a_id) {
			if (auto form = data ? data->LookupForm(a_id.id, a_id.file) : nullptr) {
				a_set.insert(form->GetFormID());
			}
		};
		for (const auto& d : VANILLA_ALCOHOL) {
			add(f.alcohol, d);
		}
		for (const auto& d : BREW_ALCOHOL_LOOKS) {
			add(f.brewAlcohol, d);
		}
		for (const auto& d : BREW_SOFT_LOOKS) {
			add(f.brewSoft, d);
		}
		if (auto milk = data ? data->LookupForm(VANILLA_MILK.id, VANILLA_MILK.file) : nullptr) {
			f.milk = milk->GetFormID();
		}
		if (data) {
			f.sunhelmOn = data->LookupForm<RE::TESGlobal>(0x02EB63, "SunHelmSurvival.esp");
			f.sunhelmThirstOff = data->LookupForm<RE::TESGlobal>(0x752708, "SunHelmSurvival.esp");
			f.lastSeedRunning = data->LookupForm<RE::TESGlobal>(0x00B162, "LastSeed.esp");
			f.lastSeedThirst = data->LookupForm<RE::TESGlobal>(0x007074, "LastSeed.esp");
			f.rndState = data->LookupForm<RE::TESGlobal>(0x012C4C, "RealisticNeedsandDiseases.esp");
			f.ineedQuest = data->LookupForm<RE::TESQuest>(0x000D62, "iNeed.esp");
		}
		int spells = 0;
		for (auto s : f.spells) {
			spells += s ? 1 : 0;
		}
		logger::info("Thirst: value {}, stage {}, {} of 5 stage abilities, {} vanilla alcoholic drinks, milk {}", f.value ? "found" : "MISSING",
			f.stage ? "found" : "MISSING", spells, f.alcohol.size(), f.milk ? "found" : "not loaded");
	}

	// another needs mod runs its own thirst right now: ours steps aside (no second thirst bar to manage)
	const char* OtherThirstMod()
	{
		const auto& f = g_thirst;
		if (f.sunhelmOn && f.sunhelmOn->value >= 1.0f && !(f.sunhelmThirstOff && f.sunhelmThirstOff->value >= 1.0f)) {
			return "SunHelm";
		}
		if (f.lastSeedRunning && f.lastSeedRunning->value >= 2.0f && (!f.lastSeedThirst || f.lastSeedThirst->value >= 2.0f)) {
			return "Last Seed";
		}
		if (f.rndState && f.rndState->value >= 1.0f) {
			return "Realistic Needs and Diseases";
		}
		if (f.ineedQuest && f.ineedQuest->IsRunning()) {
			return "iNeed";
		}
		return nullptr;
	}

	int ThirstStageOf(float a_value)
	{
		int stage = 0;
		for (int i = 1; i < 6; ++i) {
			if (a_value >= THIRST_STAGE_START[i]) {
				stage = i;
			}
		}
		return stage;
	}

	// puts the stage's ability on the player (and only that one); a_stage -1 takes them all off
	void ApplyThirstStage(int a_stage, bool a_announce)
	{
		auto player = Player();
		auto& f = g_thirst;
		if (!player) {
			return;
		}
		for (int i = 0; i < 6; ++i) {
			auto spell = f.spells[i];
			if (!spell) {
				continue;
			}
			const bool want = i == a_stage;
			const bool has = player->HasSpell(spell);
			if (want && !has) {
				player->AddSpell(spell);
			} else if (!want && has) {
				player->RemoveSpell(spell);
			}
		}
		if (f.stage) {
			f.stage->value = static_cast<float>(a_stage);
		}
		if (a_announce && a_stage >= 0) {
			RE::SendHUDMessage::ShowHUDMessage(FC::Tr(THIRST_MESSAGES[a_stage]));
		}
		if (a_stage != g_thirstShownStage) {
			logger::info("Thirst: stage {} ({})", a_stage, a_stage >= 0 ? THIRST_STAGE_NAMES[a_stage] : "off");
		}
		g_thirstShownStage = a_stage;
	}

	void SetThirst(float a_value, bool a_announce)
	{
		auto& f = g_thirst;
		if (!f.value) {
			return;
		}
		f.value->value = std::clamp(a_value, 0.0f, THIRST_MAX);
		const int stage = ThirstStageOf(f.value->value);
		if (stage != g_thirstShownStage) {
			ApplyThirstStage(stage, a_announce && g_thirstShownStage >= -1);
		}
	}

	bool ThirstRunning()
	{
		ThirstFormsLookup();
		return g_thirst.value && SurvivalFeature(FC::Feature::kThirst) && !OtherThirstMod();
	}

	// once a second (StartSurvivalWatch): thirst rises with game time
	void ThirstTick()
	{
		ThirstFormsLookup();
		auto calendar = RE::Calendar::GetSingleton();
		if (!calendar || !g_thirst.value || g_thirstSleeping) {
			return;
		}
		const float now = calendar->GetCurrentGameTime() * 24.0f;
		const auto other = OtherThirstMod();
		if (other && !g_thirstOtherTold) {
			g_thirstOtherTold = true;
			logger::info("Thirst: {} runs its own thirst, Cooking Skill Overhaul's thirst steps aside", other);
		}
		if (!SurvivalFeature(FC::Feature::kThirst) || other) {
			g_thirstLastHour = now;  // nothing builds up while it's off
			if (g_thirstShownStage != -1) {
				ApplyThirstStage(-1, false);
			}
			return;
		}
		if (g_thirstLastHour < 0.0f || now < g_thirstLastHour) {
			g_thirstLastHour = now;  // first tick after a load (or an older save's time)
		}
		const float hours = now - g_thirstLastHour;
		g_thirstLastHour = now;
		const bool firstLook = g_thirstShownStage == -2;
		SetThirst(g_thirst.value->value + hours * THIRST_PER_HOUR, !firstLook);
		if (firstLook) {
			ApplyThirstStage(ThirstStageOf(g_thirst.value->value), false);
		}
	}

	// the sleep watch (Cooking/Survival.inl): no ticks while asleep; the hours slept count at x0.75 on waking
	void ThirstSleepStart()
	{
		ThirstTick();  // count up to the moment you lie down
		g_thirstSleeping = true;
	}

	void ThirstSleepStop()
	{
		g_thirstSleeping = false;
		auto calendar = RE::Calendar::GetSingleton();
		if (!calendar || !ThirstRunning() || g_thirstLastHour < 0.0f) {
			return;
		}
		const float now = calendar->GetCurrentGameTime() * 24.0f;
		const float hours = std::max(0.0f, now - g_thirstLastHour);
		g_thirstLastHour = now;
		SetThirst(g_thirst.value->value + hours * THIRST_PER_HOUR * THIRST_SLEEP_MULT, true);
		logger::info("Thirst: slept {:.1f} h, thirst {:.0f}", hours, g_thirst.value->value);
	}

	// the Alcohol setting's share of a drink: like any drink, half, or a fifth
	float AlcoholShare()
	{
		switch (FC::GetOptions().alcohol) {
		case 1:
			return 0.5f;
		case 2:
			return 0.2f;
		default:
			return 1.0f;
		}
	}

	// the player drank or ate something (EatWatcher): a drink quenches
	// a brewed drink while another mod runs thirst (his call 2026-11-14: the Thirst switch on means our drinks quench that
	// mod's thirst). Until 2026-11-16 the hours waited for the drink's FC_CampRest effect to hand them on, so drinks
	// cooked without it (thirst switch off at the time, or before the needs mod was installed) quenched nothing and their
	// hours piled up for the next drink (reviews 2026-11-16). Now every drink hands its own hours on when it is drunk,
	// through FC_Core.OtherThirst (his call 2026-11-16: old drinks quench when drunk). Kept only for TakeOtherThirst
	// (old scripts in a save still ask): always 0 now
	float g_otherThirstHours = 0.0f;

	// the mod that runs thirst lowers it by those game hours (1000: fully): an SKSE mod event FC_OtherThirst with the hours as
	// its number, heard by FC_PotContainerScript, which calls FC_Core.OtherThirst. A mod event, not a direct call into the
	// scripts: his fixed rule, C++ never calls into the Papyrus engine (DispatchStaticCall for a day, 2026-11-16). Main thread
	void DispatchOtherThirst(float a_hours)
	{
		auto source = SKSE::GetModCallbackEventSource();
		if (!source || a_hours <= 0.0f) {
			return;
		}
		SKSE::ModCallbackEvent event{ "FC_OtherThirst", RE::BSFixedString(), a_hours, nullptr };
		source->SendEvent(&event);
	}

	bool NamiraDish(const RE::TESForm* a_item);  // Cooking/Hooks.inl

	void ThirstOnConsume(RE::TESForm* a_item)
	{
		if (!a_item) {
			return;
		}
		ThirstFormsLookup();
		const bool ours = ThirstRunning();
		// a Namira drink is kept out of the needs mod's own drink detection whatever the switch (Needs.inl NeedsChoiceFor:
		// a refused one must do nothing), so one drunk with the ring quenches through here even with the switch off
		// (before: nothing at all; Codex review 2026-11-16 afternoon, M4). Refused ones never get here (Hooks.inl)
		const bool other = !ours && (FC::FeatureSwitch(FC::Feature::kThirst) || NamiraDish(a_item)) && NeedOwner(Need::kThirst) != NeedMod::kNone;
		if (!ours && !other) {
			return;
		}
		auto& f = g_thirst;
		float hours = 0.0f;
		bool  alcohol = false;
		bool  full = false;
		const char* kind = nullptr;
		auto file = a_item->GetFile(0);
		if (file && file->GetFilename() == PLUGIN) {
			// a brewed drink from the dish pool: its look-alike says it's a drink, its effects how long it lasts
			std::scoped_lock lock(g_dishLock);
			const auto it = g_dishDefs.find(a_item->GetLocalFormID());
			if (it == g_dishDefs.end()) {
				return;
			}
			const auto& def = it->second;
			// our brews by their vanilla look-alikes, and drinks the patcher made from other mods' recipes (before, those
			// quenched nothing: reviews 2026-11-15)
			const int patched = PatchedDrinkKind(def.base);
			const bool soft = f.brewSoft.contains(def.base) || patched == 1;
			alcohol = f.brewAlcohol.contains(def.base) || patched == 2;
			if (!soft && !alcohol) {
				return;  // food
			}
			float seconds = 0.0f;
			for (const auto& e : def.effects) {
				seconds = std::max(seconds, static_cast<float>(e.duration));
				full = full || e.magnitude >= 100.0f;  // the dragon dish marker (+100 on the eat effect): a Dragon Scales drink
			}
			const auto timeScale = RE::Calendar::GetSingleton() ? RE::Calendar::GetSingleton()->GetTimescale() : 20.0f;
			hours = seconds * timeScale / 3600.0f + DRINK_HOURS_AFTER;
			kind = "brewed drink";
		} else if (other) {
			return;  // the other mod's own drinks are its own business
		} else if (f.alcohol.contains(a_item->GetFormID())) {
			hours = VANILLA_DRINK_HOURS;
			alcohol = true;
			kind = "vanilla drink";
		} else if (a_item->GetFormID() == f.milk) {
			hours = VANILLA_DRINK_HOURS;
			kind = "milk";
		} else {
			return;
		}
		if (alcohol) {
			hours *= AlcoholShare();
		}
		if (other) {
			DispatchOtherThirst(full ? 1000.0f : hours);
			logger::info("Thirst: {} {} quenches the other mod's thirst ({}{:.1f} h)", kind, a_item->GetName(), alcohol ? "alcohol, " : "",
				full ? 1000.0f : hours);
			return;
		}
		const float before = f.value->value;
		SetThirst(full ? 0.0f : before - hours * THIRST_PER_HOUR, true);
		logger::info("Thirst: {} {} ({}{:.1f} h): {:.0f} -> {:.0f}", kind, a_item->GetName(), alcohol ? "alcohol, " : "", hours, before, f.value->value);
	}

	// a brewed drink without the camp rest effect (brewed before it was put on every brew: with no needs mod then, or
	// both switches off) still gives back the rest owed when drunk, as one with it would (Codex review 2026-11-16e, L8).
	// Paid the same way as a claimed night: the mod event FC_CampRestPay. Main thread
	void CampRestOnConsume(RE::TESForm* a_item)
	{
		auto drink = a_item ? a_item->As<RE::AlchemyItem>() : nullptr;
		auto file = drink ? drink->GetFile(0) : nullptr;
		if (!file || file->GetFilename() != PLUGIN) {
			return;
		}
		ThirstFormsLookup();
		{
			std::scoped_lock lock(g_dishLock);
			const auto it = g_dishDefs.find(drink->GetLocalFormID());
			if (it == g_dishDefs.end()) {
				return;
			}
			const int patched = PatchedDrinkKind(it->second.base);
			if (!g_thirst.brewSoft.contains(it->second.base) && !g_thirst.brewAlcohol.contains(it->second.base) && patched == 0) {
				return;  // food
			}
		}
		for (const auto effect : drink->effects) {
			const auto base = effect ? effect->baseEffect : nullptr;
			const auto baseFile = base ? base->GetFile(0) : nullptr;
			if (baseFile && baseFile->GetFilename() == PLUGIN && base->GetLocalFormID() == 0xA6A) {
				return;  // FC_CampRest: its own script asks for the rest
			}
		}
		const auto given = TakeCampRest();
		if (given > 0) {
			logger::info("Camp rest: {} given back by a brewed drink from before every brew carried camp rest", given);
			QueueCampRestPay(given, g_campRestOwner);
		}
	}

	// a save is loading or a new game starts: count time afresh, look at the stage again on the first tick
	// Papyrus: FC_Menu.TakeOtherThirst() - the hours the last brewed drink quenches in another mod's thirst, given out once
	float PapyrusTakeOtherThirst(RE::StaticFunctionTag*)
	{
		const float hours = g_otherThirstHours;
		g_otherThirstHours = 0.0f;
		return hours;
	}

	void ThirstReset()
	{
		g_otherThirstHours = 0.0f;
		g_thirstLastHour = -1.0f;
		g_thirstSleeping = false;
		g_thirstShownStage = -2;
		g_thirstOtherTold = false;
	}
