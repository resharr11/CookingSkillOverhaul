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

// ---------------------------------------------------------------------------------------------------------------
// The hold (his design 2026-11-22: "we copy star frost", "lower hunger then hold"; "if a player toggle our mod they want
// our effect"). Eating still lowers hunger once, as before; then, while a meal or baked dish's effect runs, hunger does not
// rise at all, and while a brewed drink's effect runs, thirst does not rise. The idea is Starfrost's (its
// MAG_HungerController_Script sets Survival Mode Improved's hunger rate multiplier to 0 while a food buff runs). Every
// needs mod raises its needs by the time passed x a rate value it reads at each update (read from each mod's own files
// 2026-11-22), so the hold sets that rate to 0 and gives it back afterwards:
//   Survival Mode and Survival Mode Improved (Starfrost too): Survival_HungerNeedRate (Survival Mode's esl 0x829). Not
//     Starfrost's own SMI_HungerRateMult, so the two holds never save each other's 0.
//   SunHelm _SHHungerRate / _SHThirstRate; Last Seed _Seed_Setting_RateMulti_Hunger / _Thirst (the multiplier also
//   covers what it adds while health regenerates, and its base rates _Seed_HungerRate / _Seed_ThirstRate for the updates
//   it saved up); Realistic Needs RND_HungerPointsPerHour / RND_ThirstPointsPerHour. Cooking Skill Overhaul's own thirst
//   simply doesn't rise.
// Which effects count: the player's active effects whose spell is one of our dish buffs (data.json fxsp/fxspl/fxsplb:
// meals and baked dishes; fxspd/fxspld: drinks). Switches: "Dishes keep you fed" for hunger, Thirst for thirst; off,
// nothing is held (his call: a player who doesn't want it turns the switch off).
// The rates are set by the scripts (FC_Core.HoldNeeds, through the mod event FC_HoldNeeds: his rule, C++ never calls into
// Papyrus). They keep each rate's own value in PapyrusUtil storage (saved with the game) and give it back when the hold
// ends; a rate the player changed during the hold keeps the player's value. The plugin sends what it wants when that
// changes and every 10 s (also after a load), so a lost event or a load can't leave a rate at 0 longer than that.
// iNeed's rates are not touched: its bridge holds the needs' values instead (FC_INeedBridge.Hold).
// Sleeping, waiting and travel (his design 2026-11-22: "sleep and waiting needs to make sense", "it's how we handle
// rested outdoors"). A dish's effect keeps running in real time through a skip (its timer counts real seconds, and the
// skipped hours pass in a few of them), so a held rate would hold the whole skip whatever the dish had left. Instead the
// hold lets go: when the Sleep/Wait menu opens, or when fast travel ends (map, carriage, boat: the game reports the
// hours, and the needs mods count the trip at their next update), the rates get their own values back at once. Each
// mod then counts the skipped hours its own way. Once its value has settled, the share the dish still covered is given
// back through that mod's own functions: rise x min(1, dish time left when the skip started / hours skipped). Then the
// hold starts again if the dish still runs. iNeed's value hold does the same in its bridge (only scripts can read
// iNeed's values). Our own thirst counts it directly: only the hours past the drink's end rise (ThirstTick), which also
// covers time skips no event reports (jail). Tested 2026-11-22 under Survival Mode: its rise is in within 3 s.
// ---------------------------------------------------------------------------------------------------------------

	bool                      g_holdHunger = false;     // a meal or baked dish's effect runs on the player (any switch)
	bool                      g_holdThirst = false;     // a brewed drink's effect runs on the player
	bool                      g_holdOwnThirst = false;  // our own thirst is held right now (ThirstTick reads it, with the drink's hours left)
	std::atomic<std::int32_t> g_holdSent{ -1 };         // the last request sent to the scripts (-1: none yet; also set from natives)
	std::chrono::steady_clock::time_point g_holdSentAt{};

	// The skip code is reached from three threads: the 1 s tick (main thread), the scripts' natives (Papyrus threads at
	// the frame sync point, seen in the 2026-11-22 test), the Sleep/Wait menu's event and the fast travel event (other
	// threads: menu events are sent from the UI side, and queued tasks run on job threads in normal play;
	// skyrim-skse-patterns, verified 2026-11-16 and seen again 2026-11-22). So everything below is kept under one lock;
	// nothing under it calls the engine or logs (Codex review 2026-11-22e L1): forms are looked up and values read by the
	// main tick before it locks (g_holdSnap), log lines are kept (SkipLog) and written after the lock is let go, and the
	// give-backs (RND's global, the mod events) are decided and paid only by the main tick, after it.
	std::mutex g_skipLock;

	// log lines made under the lock, written after it (SkipLogFlush). Fixed-size slots written with format_to_n: nothing
	// is allocated under the lock (Codex review 2026-11-22f L3); a line that doesn't fit is cut, a 17th line is dropped
	struct SkipLogLine
	{
		bool                   warn = false;
		std::size_t            size = 0;
		std::array<char, 240>  text{};
	};
	std::array<SkipLogLine, 16> g_skipLog{};
	std::size_t                 g_skipLogCount = 0;

	template <class... Args>
	void SkipLogAs(bool a_warn, std::format_string<Args...> a_fmt, Args&&... a_args)  // under the lock
	{
		if (g_skipLogCount >= g_skipLog.size()) {
			return;
		}
		auto& line = g_skipLog[g_skipLogCount++];
		line.warn = a_warn;
		line.size = std::format_to_n(line.text.data(), line.text.size(), a_fmt, std::forward<Args>(a_args)...).out - line.text.data();
	}

	template <class... Args>
	void SkipLog(std::format_string<Args...> a_fmt, Args&&... a_args)  // under the lock
	{
		SkipLogAs(false, a_fmt, std::forward<Args>(a_args)...);
	}

	template <class... Args>
	void SkipWarn(std::format_string<Args...> a_fmt, Args&&... a_args)  // under the lock
	{
		SkipLogAs(true, a_fmt, std::forward<Args>(a_args)...);
	}

	void SkipLogFlush()  // after the lock
	{
		std::array<SkipLogLine, 16> lines;
		std::size_t                 count = 0;
		{
			std::scoped_lock lock(g_skipLock);
			lines = g_skipLog;
			count = g_skipLogCount;
			g_skipLogCount = 0;
		}
		for (std::size_t i = 0; i < count; ++i) {
			const std::string_view text(lines[i].text.data(), std::min(lines[i].size, lines[i].text.size()));
			if (lines[i].warn) {
				logger::warn("{}", text);
			} else {
				logger::info("{}", text);
			}
		}
	}

	// held rate globals and their own values, told by the scripts (FC_Menu.NoteHeld / NoteReleased from FC_Core.HoldRate /
	// ReleaseRate), so a skip can give them back at once
	std::unordered_map<RE::TESGlobal*, float> g_heldRates;

	// one need during a skip. Each need keeps its own skipped and covered hours (Claude review 2026-11-22e L5, Codex P1, his
	// OK 2026-11-22 23:48): an iNeed give-back still settling when a second skip starts carries on over both skips, with
	// each skip's own dish hours, and a fresh dish eaten between them counts for the second one
	struct SkipNeed
	{
		bool           on = false;
		std::int32_t   owner = 0;        // NeedOwner number
		RE::TESGlobal* value = nullptr;  // the need's value (none for iNeed: its bridge reads it)
		float          last = 0.0f;      // the value at the last reading
		float          risen = 0.0f;     // the rise so far, counted only from the steps up (food only lowers a need, the
		                                 // skip's count only raises it: Codex review 2026-11-22e M3, his pick 2026-11-22 23:48)
		float          base = 0.0f;      // the rise when this skip started: it settles only after a rise past it, so a
		                                 // give-back carried on can't settle on the last skip's rise before the needs mod
		                                 // counted this one (Claude review 2026-11-22f F1)
		float          prevBase = 0.0f;  // the base before this skip: back to it if this skip took no game time (Claude 22g F2)
		float          lastRise = -1.0f;
		int            still = 0;        // seconds the rise hasn't changed
		float          left = 0.0f;      // the dish's game hours left when this skip started
		float          startDay = 0.0f;  // game days when this skip started
		bool           open = false;     // this skip's hours aren't known yet (the menu is open)
		float          covered = 0.0f;   // hours the dishes covered, over the skips this give-back spans
		float          total = 0.0f;     // hours skipped, over the same skips
	};
	struct SkipState
	{
		int           menus = 0;          // 1 while the Sleep/Wait menu is open
		bool          pending = false;    // a skip took the hold off; the give-back isn't done yet
		float         startDay = 0.0f;    // game days when the newest skip started
		float         hours = -1.0f;      // hours of the newest skip (-1: the menu is still open)
		float         endDay = 0.0f;      // game days when the newest skip ended (iNeed's bridge: FC_Menu.SkipEndDay)
		int           ticks = 0;          // seconds since it ended
		std::uint32_t generation = 0;
		std::int32_t  id = 0;             // the newest skip's number, sent to iNeed's bridge with its flags and given back
		                                  // with SkipShare / SkipDone, so an old request can't settle a newer skip
		                                  // (Codex review 2026-11-22e P3)
		SkipNeed      hunger, thirst;
	};
	SkipState    g_skip;
	std::int32_t g_skipCount = 0;  // the last skip number used (1..4095: the request is sent as a float)

	// what the main thread saw at its last tick: who holds each need, the dishes' hours left, the needs' value globals and
	// their values then
	struct HoldSnap
	{
		std::int32_t   hunger = 0;      // held now (a dish runs): the mod's number, else 0
		std::int32_t   thirst = 0;
		std::int32_t   runHunger = 0;   // the mod running the need, dish or not
		std::int32_t   runThirst = 0;
		float          mealLeft = 0.0f;
		float          drinkLeft = 0.0f;
		RE::TESGlobal* hungerValue = nullptr;  // the running mod's value globals
		RE::TESGlobal* thirstValue = nullptr;
		float          hungerNow = 0.0f;
		float          thirstNow = 0.0f;
	};
	HoldSnap g_holdSnap;

	// a give-back decided under the lock, paid by the main tick after the lock (SkipPayOut). The sinks pay nothing
	// themselves (Claude review 2026-11-22e L4: RND's global was written from the sink's thread): what a sink decides waits
	// in g_skipPays for the next tick
	struct SkipPay
	{
		std::int32_t   owner = 0;
		int            need = 0;  // 0 hunger, 1 thirst
		float          points = 0.0f;
		RE::TESGlobal* value = nullptr;
	};
	std::vector<SkipPay> g_skipPays;  // decided by a sink (a need's mod changed between two skips), paid at the next tick
	void SkipTickLocked(std::vector<SkipPay>& a_pays);         // below
	std::int32_t SkipFlagsLocked(std::int32_t& a_hunger, std::int32_t& a_thirst, bool& a_fast);  // below
	void SkipPayOut(const std::vector<SkipPay>& a_pays);       // below
	float g_thirstHeldLeft = 0.0f;   // our own thirst: the drink's hours left at the last tick while it held (ThirstTick)
	float g_thirstDrinkLeft = 0.0f;  // the drink's hours left at this tick (HoldTick)

	// our dish buff spells (data.json fxsp... keys hold their FreeCooking.esp local IDs); filled once the rules are read
	struct HoldSpells
	{
		bool                           looked = false;
		std::unordered_set<RE::FormID> food;
		std::unordered_set<RE::FormID> drink;
	};
	HoldSpells g_holdSpells;

	void HoldSpellsLookup()
	{
		auto& s = g_holdSpells;
		if (s.looked || g_int.empty()) {
			return;
		}
		s.looked = true;
		for (const auto& [k, v] : g_int) {
			const auto prefix = k.substr(0, k.find('.'));
			const bool food = prefix == "fxsp" || prefix == "fxspl" || prefix == "fxsplb";
			const bool drink = prefix == "fxspd" || prefix == "fxspld";
			if (!food && !drink) {
				continue;
			}
			if (auto spell = PluginForm<RE::SpellItem>(static_cast<RE::FormID>(v), PLUGIN)) {
				(food ? s.food : s.drink).insert(spell->GetFormID());
			}
		}
		logger::info("Hold: {} meal and {} drink buff spells", s.food.size(), s.drink.size());
	}

	// game hours left on the longest of the player's dish effects of one kind (meal or drink). Main thread
	float DishHoursLeft(bool a_drink)
	{
		auto player = Player();
		auto list = player ? player->GetActiveEffectList() : nullptr;
		const auto calendar = RE::Calendar::GetSingleton();
		const float timeScale = calendar ? calendar->GetTimescale() : 20.0f;
		float seconds = 0.0f;
		if (list) {
			const auto& set = a_drink ? g_holdSpells.drink : g_holdSpells.food;
			for (auto effect : *list) {
				if (!effect || !effect->spell || effect->flags.any(RE::ActiveEffect::Flag::kInactive, RE::ActiveEffect::Flag::kDispelled) ||
					!set.contains(effect->spell->GetFormID())) {
					continue;
				}
				seconds = std::max(seconds, effect->duration - effect->elapsedSeconds);
			}
		}
		return std::max(0.0f, seconds) * timeScale / 3600.0f;
	}

	// the value global of a need in the mod that runs it (none for iNeed: only scripts read its values). Main thread;
	// looked up once per owner
	RE::TESGlobal* SkipValueGlobal(std::int32_t a_owner, bool a_thirst)
	{
		static std::array<RE::TESGlobal*, 16> cache{};
		static std::array<bool, 16>           looked{};
		const auto slot = static_cast<std::size_t>(std::clamp(a_owner, 0, 7) * 2 + (a_thirst ? 1 : 0));
		if (looked[slot]) {
			return cache[slot];
		}
		looked[slot] = true;
		RE::TESGlobal* g = nullptr;
		switch (static_cast<NeedMod>(a_owner)) {
		case NeedMod::kSurvival:
		case NeedMod::kSMI:
			g = a_thirst ? nullptr : PluginForm<RE::TESGlobal>(0x81A, "ccqdrsse001-survivalmode.esl");  // Survival_HungerNeedValue
			break;
		case NeedMod::kSunHelm:
			g = PluginForm<RE::TESGlobal>(a_thirst ? 0x05C472 : 0x00EAAE, "SunHelmSurvival.esp");  // _SHCurrentThirstLevel / Hunger
			break;
		case NeedMod::kLastSeed:
			g = PluginForm<RE::TESGlobal>(a_thirst ? 0x0029CE : 0x0029CD, "LastSeed.esp");  // _Seed_AttributeThirst / Hunger
			break;
		case NeedMod::kRND:
			{
				// RND_ThirstPoints / RND_HungerPoints (also in the Nirnfeel merge, under the same IDs)
				const RE::FormID id = a_thirst ? 0x002E07 : 0x002884;
				g = PluginForm<RE::TESGlobal>(id, "RealisticNeedsandDiseases.esp");
				if (!g) {
					g = PluginForm<RE::TESGlobal>(id, "MVC Nirnfeel Immersion.esp");
				}
				break;
			}
		default:
			break;
		}
		cache[slot] = g;
		return g;
	}

	void SendHoldRequest(std::int32_t a_request)
	{
		if (auto source = SKSE::GetModCallbackEventSource()) {
			SKSE::ModCallbackEvent event{ "FC_HoldNeeds", RE::BSFixedString(), static_cast<float>(a_request), nullptr };
			source->SendEvent(&event);
		}
	}

	// once a second (StartSurvivalWatch), before ThirstTick; also when waking (ThirstSleepStop). Main thread
	void HoldTick()
	{
		HoldSpellsLookup();
		auto player = Player();
		auto list = player ? player->GetActiveEffectList() : nullptr;
		bool meal = false;
		bool drink = false;
		if (list && g_holdSpells.looked) {
			for (auto effect : *list) {
				if (!effect || !effect->spell || effect->flags.any(RE::ActiveEffect::Flag::kInactive, RE::ActiveEffect::Flag::kDispelled)) {
					continue;
				}
				const auto id = effect->spell->GetFormID();
				meal = meal || g_holdSpells.food.contains(id);
				drink = drink || g_holdSpells.drink.contains(id);
			}
		}
		if (meal != g_holdHunger || drink != g_holdThirst) {
			logger::info("Hold: meal effect {}, drink effect {}", meal ? "runs" : "none", drink ? "runs" : "none");
		}
		g_holdHunger = meal;
		g_holdThirst = drink;
		// who runs each need (OtherMods.inl NeedOwner: 1 Survival Mode, 2 Survival Mode Improved, 3 SunHelm, 4 Last Seed,
		// 5 iNeed, 6 Realistic Needs); 0 in the request: nothing held there
		// runHunger / runThirst: the mod running the need whether a dish runs or not (a skip's record carries on while the
		// same mod runs it, also after the dish ran out: Claude review 2026-11-22g F1, Codex M1)
		std::int32_t runHunger = 0;
		if (FC::FeatureSwitch(FC::Feature::kHunger)) {
			const auto owner = NeedOwner(Need::kHunger);
			if (owner >= NeedMod::kSurvival && owner <= NeedMod::kRND) {
				runHunger = static_cast<std::int32_t>(owner);
			}
		}
		const std::int32_t hunger = meal ? runHunger : 0;
		std::int32_t       runThirst = 0;
		bool               own = false;
		if (FC::FeatureSwitch(FC::Feature::kThirst)) {
			const auto owner = NeedOwner(Need::kThirst);
			if (owner >= NeedMod::kSunHelm && owner <= NeedMod::kRND) {
				runThirst = static_cast<std::int32_t>(owner);
			} else if (drink && owner == NeedMod::kNone && ThirstRunning()) {
				own = true;
			}
		}
		const std::int32_t thirst = drink ? runThirst : 0;
		// note what the sinks may need, then the skip's own work (SkipTickLocked) and its flags (SkipFlagsLocked)
		const float        mealLeft = meal ? DishHoursLeft(false) : 0.0f;
		const float        drinkLeft = drink ? DishHoursLeft(true) : 0.0f;
		std::int32_t       flags = 0;
		bool               fast = false;
		std::vector<SkipPay> pays;
		pays.reserve(12);  // the tick's give-backs (2) and a sink's (up to 8)
		// forms looked up and values read before the lock (Codex review 2026-11-22e L1)
		std::int32_t reqHunger = hunger;  // the request's owners (a need whose skip is being counted is named or left out)
		std::int32_t reqThirst = thirst;
		const auto   hungerValue = SkipValueGlobal(runHunger, false);
		const auto   thirstValue = SkipValueGlobal(runThirst, true);
		const float hungerNow = hungerValue ? hungerValue->value : 0.0f;
		const float thirstNow = thirstValue ? thirstValue->value : 0.0f;
		{
			std::scoped_lock lock(g_skipLock);
			g_holdSnap = { hunger, thirst, runHunger, runThirst, mealLeft, drinkLeft, hungerValue, thirstValue, hungerNow, thirstNow };
			pays.insert(pays.end(), g_skipPays.begin(), g_skipPays.end());  // (reserved: no allocation here)
			g_skipPays.clear();  // keeps its capacity for the next sink
			if (g_skip.pending) {
				SkipTickLocked(pays);
			}
			flags = SkipFlagsLocked(reqHunger, reqThirst, fast);
		}
		SkipLogFlush();
		SkipPayOut(pays);
		g_thirstDrinkLeft = drinkLeft;
		if (own != g_holdOwnThirst) {
			logger::info("Hold: Cooking Skill Overhaul's thirst {}", own ? "held" : "rises again");
		}
		g_holdOwnThirst = own;
		const std::int32_t request = reqHunger + reqThirst * 16 + flags;
		const auto         now = std::chrono::steady_clock::now();
		if (request == g_holdSent && now - g_holdSentAt < std::chrono::seconds(fast ? 2 : 10)) {
			return;
		}
		if (request != g_holdSent) {
			logger::info("Hold: hunger held in mod {}, thirst held in mod {} (0: none){}", reqHunger, reqThirst, flags ? " (a skip is settling)" : "");
		}
		g_holdSent = request;
		g_holdSentAt = now;
		SendHoldRequest(request);
	}

	// Survival Mode's Survival_HungerNeedRate is a constant global, and a constant global's value is not stored in saves
	// (CK wiki "Global": "will not be saved with save game data"). The hold's 0 would stay in memory across a load or a
	// new game, where the loaded game's script storage has nothing to give it back (Claude review 2026-11-22d, M1). So
	// its value is read once at kDataLoaded and written back before every load and new game, as Survival Mode Improved
	// does for the same global; if the loaded game has a meal running, the scripts hold it again within 10 s.
	RE::TESGlobal* g_survivalRate = nullptr;
	float          g_survivalRateOwn = 0.0f;

	std::uint32_t HoldSessionStart();  // below

	void HoldRememberSurvivalRate()
	{
		{
			std::scoped_lock lock(g_skipLock);
			g_skipPays.reserve(8);  // a sink adds at most 2 (one per need) before the next tick takes them
		}
		HoldSessionStart();  // the session seed is made here, at kDataLoaded, not in a Papyrus call
		g_survivalRate = PluginForm<RE::TESGlobal>(0x829, "ccqdrsse001-survivalmode.esl");  // Survival_HungerNeedRate
		if (g_survivalRate) {
			g_survivalRateOwn = g_survivalRate->value;
			logger::info("Hold: Survival_HungerNeedRate is {:.2f} in the plugins", g_survivalRateOwn);
		}
	}

	// kPreLoadGame and kNewGame (CookingMenu.cpp). Main thread
	void HoldResetForLoad()
	{
		if (g_survivalRate) {
			if (std::abs(g_survivalRate->value - g_survivalRateOwn) > 0.001f) {
				logger::info("Hold: Survival_HungerNeedRate {:.2f} -> {:.2f} before the game loads", g_survivalRate->value, g_survivalRateOwn);
			}
			g_survivalRate->value = g_survivalRateOwn;  // always (Codex review 2026-11-22f L4)
		}
		g_holdSent = -1;  // the next tick tells the loaded game's scripts what is held
		g_holdOwnThirst = false;
		g_thirstHeldLeft = 0.0f;
		// the game we leave: its held rates and its skip (the loaded game's scripts tell their own
		// again). The menu count too: a Sleep/Wait close that came during the load was only counted (Claude review
		// 2026-11-22e L3), and no menu is open in a game being loaded
		std::scoped_lock lock(g_skipLock);
		g_heldRates.clear();
		g_skip = SkipState{};
		g_skipPays.clear();
		g_holdSnap = {};
	}

	// Papyrus: FC_Menu.SetRate(rate, value): the hold's rate globals are set through here (FC_Core.HoldRate / ReleaseRate).
	// Survival Mode's Survival_HungerNeedRate is a constant global and Papyrus SetValue refuses it ("Cannot set the value of
	// a constant GlobalVariable", seen in game 2026-11-22), so the value is written directly. Frame-synced
	void PapyrusSetRate(RE::StaticFunctionTag*, RE::TESGlobal* a_rate, float a_value)
	{
		if (!a_rate) {
			return;
		}
		{
			std::scoped_lock lock(g_skipLock);
			// a script call from before a skip, run late, can't hold a rate again while the skip's hours are being counted
			if (a_value == 0.0f && g_skip.pending && g_heldRates.contains(a_rate) && a_rate->value != 0.0f) {
				SkipLog("Hold: {:08X} not set to 0 again while a skip is being counted", a_rate->GetFormID());
			} else {
				a_rate->value = a_value;
			}
		}
		SkipLogFlush();
	}

	// Papyrus: FC_Menu.NoteHeld(rate, own) / NoteReleased(rate): FC_Core.HoldRate / ReleaseRate tell the plugin which rate
	// globals are held and their own values, so a skip can give them back at once. Frame-synced
	void PapyrusNoteHeld(RE::StaticFunctionTag*, RE::TESGlobal* a_rate, float a_own)
	{
		if (a_rate) {
			std::scoped_lock lock(g_skipLock);
			g_heldRates[a_rate] = a_own;
		}
	}

	void PapyrusNoteReleased(RE::StaticFunctionTag*, RE::TESGlobal* a_rate)
	{
		std::scoped_lock lock(g_skipLock);
		g_heldRates.erase(a_rate);
	}

	// the share of a need's skipped hours the dishes covered: each skip counts min(dish hours left when it started, hours
	// skipped); over the skips one give-back spans
	float SkipShareLocked(const SkipNeed& a_need)
	{
		if (a_need.total <= 0.0f) {
			return 0.0f;
		}
		return std::clamp(a_need.covered / a_need.total, 0.0f, 1.0f);
	}

	// Papyrus: FC_Menu.SkipShare(need, skip) (0 hunger, 1 thirst; skip: the number its request carried): for iNeed's
	// bridge. -1: that skip isn't the one being settled any more (a newer one started, or it ended): give nothing back
	// now, the next request asks again. Frame-synced
	float PapyrusSkipShare(RE::StaticFunctionTag*, std::int32_t a_need, std::int32_t a_skip)
	{
		std::scoped_lock lock(g_skipLock);
		const auto& need = a_need == 0 ? g_skip.hunger : g_skip.thirst;
		if (!g_skip.pending || a_skip != g_skip.id || !need.on || need.open) {
			return -1.0f;
		}
		return SkipShareLocked(need);
	}

	// Papyrus: FC_Menu.SkipEndDay(need, skip): game days when the newest skip ended, for iNeed's bridge: it settles once
	// iNeed's own clock (_SNQuest.TimeUpdate, stamped each time iNeed counts time) has reached it, so iNeed has counted
	// every skip the give-back spans (his pick 2026-11-23 03:06, after test 22h: value-based guesses failed three times).
	// -1: the skip is still running, or the number is old. Frame-synced
	float PapyrusSkipEndDay(RE::StaticFunctionTag*, std::int32_t a_need, std::int32_t a_skip)
	{
		std::scoped_lock lock(g_skipLock);
		const auto& need = a_need == 0 ? g_skip.hunger : g_skip.thirst;
		if (!g_skip.pending || a_skip != g_skip.id || !need.on || g_skip.hours < 0.0f) {
			return -1.0f;
		}
		return g_skip.endDay;
	}

	// Papyrus: FC_Menu.SkipPending(need): a skip of this need is being counted for iNeed, so its bridge must not give a
	// plain hold's loss back (Codex review 2026-11-22e P2: a plain request that was already running when the skip
	// started). Frame-synced
	bool PapyrusSkipPending(RE::StaticFunctionTag*, std::int32_t a_need)
	{
		std::scoped_lock lock(g_skipLock);
		const auto& need = a_need == 0 ? g_skip.hunger : g_skip.thirst;
		return g_skip.pending && need.on && need.owner == static_cast<std::int32_t>(NeedMod::kINeed);
	}

	// Papyrus: FC_Menu.HoldSession(): the number of the game running now (it changes at every load), so the pot script can
	// tell a hold run of this game from one whose stack the load lost (Claude review 2026-11-22e L6)
	// (a random start per Skyrim start, so a number saved in one run can't match the first load of the next: Codex
	// review 2026-11-22f L1)
	std::uint32_t HoldSessionStart()
	{
		static const std::uint32_t start = []() -> std::uint32_t {
			std::uint32_t seed = 0;
			try {
				seed = std::random_device{}();
			} catch (...) {  // (an RNG failure throws: Codex review 2026-11-22g L2) fall back to the clock
			}
			seed ^= static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
			return seed & 0x3FFFFFFF;
		}();
		return start;
	}

	std::int32_t PapyrusHoldSession(RE::StaticFunctionTag*)
	{
		return static_cast<std::int32_t>((HoldSessionStart() + g_gameGeneration.load()) & 0x7FFFFFFF);
	}

	void SkipNeedDoneLocked(SkipNeed& a_need)
	{
		a_need.on = false;
		if (g_skip.pending && !g_skip.hunger.on && !g_skip.thirst.on) {
			g_skip.pending = false;
			g_holdSent = -1;  // the next tick holds again what still runs
			SkipLog("Hold: the skip's give-back is done; the hold starts again where a dish still runs");
		}
	}

	// Papyrus: FC_Menu.SkipDone(need, skip): iNeed's bridge gave back its share of that skip. Frame-synced
	void PapyrusSkipDone(RE::StaticFunctionTag*, std::int32_t a_need, std::int32_t a_skip)
	{
		{
			std::scoped_lock lock(g_skipLock);
			auto& need = a_need == 0 ? g_skip.hunger : g_skip.thirst;
			if (need.on && need.owner == static_cast<std::int32_t>(NeedMod::kINeed) && a_skip == g_skip.id) {
				SkipLog("Hold: iNeed's {} share given back by its bridge", a_need == 0 ? "hunger" : "thirst");
				SkipNeedDoneLocked(need);
			}
		}
		SkipLogFlush();
	}

	// one need's give-back: the dish's share of its rise (logged here, paid by SkipPayOut)
	SkipPay SkipPayFor(const SkipNeed& a_need, int a_index, float a_rise)
	{
		const float share = SkipShareLocked(a_need);
		const float give = a_rise * share;
		SkipLog("Hold: after the skip, {} rose {:.2f} in mod {} ({:.2f} of {:.2f} h covered); the dish covered {:.0f}%: {:.2f} given back",
			a_index == 0 ? "hunger" : "thirst", a_rise, a_need.owner, a_need.covered, a_need.total, share * 100.0f, give);
		return { a_need.owner, a_index, give, a_need.value };
	}

	// main thread, after the lock: Realistic Needs is lowered directly, as its own food does (rnd_eatfood.psc); the others
	// through the scripts, which call each mod's own functions (FC_Core.SkipGiveBack): mod event FC_SkipGiveBack, "owner,need"
	void SkipPayOut(const std::vector<SkipPay>& a_pays)
	{
		for (const auto& pay : a_pays) {
			if (pay.points <= 0.0f) {
				continue;
			}
			if (pay.owner == static_cast<std::int32_t>(NeedMod::kRND)) {
				if (pay.value) {
					pay.value->value = std::max(0.0f, pay.value->value - pay.points);
				}
			} else if (auto source = SKSE::GetModCallbackEventSource()) {
				const std::string what = std::to_string(pay.owner) + (pay.need == 0 ? ",0" : ",1");
				SKSE::ModCallbackEvent event{ "FC_SkipGiveBack", RE::BSFixedString(what), pay.points, nullptr };
				source->SendEvent(&event);
			}
		}
	}

	// the rise of a need so far: the steps up it took, up to the value a_now
	float SkipRiseTo(const SkipNeed& a_need, float a_now)
	{
		return a_need.risen + std::max(0.0f, a_now - a_need.last);
	}

	// a skip's hours are known: each need it took off the hold counts them, and the dish hours that covered them
	void SkipCloseNeedsLocked(float a_nowDay)
	{
		for (auto* need : { &g_skip.hunger, &g_skip.thirst }) {
			if (need->on && need->open) {
				const float hours = std::max(0.0f, (a_nowDay - need->startDay) * 24.0f);
				need->covered += std::min(need->left, hours);
				need->total += hours;
				need->open = false;
				if (hours < 0.01f) {
					need->base = need->prevBase;  // no game time: nothing new to wait for
				}
			}
		}
	}

	// a skip starts: the Sleep/Wait menu opened (a_travelHours < 0), or fast travel ended (a carriage and a boat are fast
	// travel too) after a_travelHours. Any thread, under the lock; only g_holdSnap is read (the main tick's values).
	// Returns true when something was held
	bool SkipStartLocked(float a_nowDay, float a_travelHours)
	{
		const bool  travel = a_travelHours >= 0.0f;
		const float startDay = travel ? a_nowDay - a_travelHours / 24.0f : a_nowDay;
		const auto& snap = g_holdSnap;
		if (g_skip.pending) {
			// a second skip before the first one's give-back finished (a wait, then travel within 30 s): every need still
			// settling carries on over both skips, each skip counting its own skipped and dish hours. Paying the first
			// skip at the second's start gave 0 when the needs mod hadn't counted it yet, and its rise then fell into the
			// second skip's share (test 2026-11-23 01:10: 1 h fully covered, then 2 h with 0.59 h left: 90 risen, 30%)
			SkipLog("Hold: a new skip before the last one settled: its give-back carries on over both");
		}
		if (!g_skip.pending) {
			g_skip = SkipState{ g_skip.menus };
			if (snap.hunger == 0 && snap.thirst == 0) {
				return false;  // nothing held
			}
			g_skip.generation = g_gameGeneration.load();
		}
		g_skipCount = g_skipCount % 4095 + 1;
		g_skip.id = g_skipCount;
		g_skip.startDay = startDay;
		g_skip.hours = -1.0f;
		g_skip.ticks = 0;
		// a_held: the mod holding the need now (a dish runs), a_run: the mod running it (dish or not)
		const auto setUp = [startDay](SkipNeed& a_need, std::int32_t a_held, std::int32_t a_run, RE::TESGlobal* a_value, float a_now, float a_left) {
			if (a_need.on && a_need.owner == a_run && a_need.value == a_value) {
				// carried on from the last skip (the same mod runs the need): this skip counts with its own dish hours (0 if
				// the dish ran out meanwhile); the rise watch starts over and must pass the rise so far
				a_need.risen = SkipRiseTo(a_need, a_now);
				a_need.last = a_now;
				a_need.prevBase = a_need.base;
				a_need.base = a_need.risen;
				a_need.left = a_held == a_run ? a_left : 0.0f;
				a_need.startDay = startDay;
				a_need.open = true;
				a_need.still = 0;
				a_need.lastRise = -1.0f;
				return;
			}
			if (a_need.on) {
				// another mod runs the need now (or none: its switch went off): the old record is paid what came in so far,
				// at the next tick, and a new one starts for this skip (Codex review 2026-11-22f M2). An old iNeed record
				// just ends (its bridge moves its mark when no skip is flagged)
				if (a_need.owner != static_cast<std::int32_t>(NeedMod::kINeed)) {
					g_skipPays.push_back(SkipPayFor(a_need, &a_need == &g_skip.hunger ? 0 : 1, a_need.risen));
				}
				a_need.on = false;
			}
			if (a_held == 0) {
				return;
			}
			a_need = SkipNeed{};
			a_need.on = true;
			a_need.owner = a_held;
			a_need.value = a_value;
			a_need.last = a_now;  // the main tick's value (at most 1 s old); for travel the needs mods count after this event
			a_need.left = a_left;
			a_need.startDay = startDay;
			a_need.open = true;
		};
		setUp(g_skip.hunger, snap.hunger, snap.runHunger, snap.hungerValue, snap.hungerNow, snap.mealLeft);
		setUp(g_skip.thirst, snap.thirst, snap.runThirst, snap.thirstValue, snap.thirstNow, snap.drinkLeft);
		g_skip.pending = g_skip.hunger.on || g_skip.thirst.on;
		// the held rates get their own values back now (the scripts' kept values stay; FC_Core sees the rate isn't 0 any
		// more and lets it be)
		for (const auto& [rate, value] : g_heldRates) {
			if (rate && rate->value == 0.0f) {
				rate->value = value;
			}
		}
		SkipLog("Hold: skip {} starts ({}): {} rates given back; hunger owner {} ({:.2f} h of dish left), thirst owner {} ({:.2f} h left)",
			g_skip.id, travel ? "fast travel" : "sleep or wait", g_heldRates.size(), g_skip.hunger.on ? g_skip.hunger.owner : 0,
			g_skip.hunger.left, g_skip.thirst.on ? g_skip.thirst.owner : 0, g_skip.thirst.left);
		if (travel) {
			g_skip.hours = std::max(0.0f, a_travelHours);
			g_skip.endDay = a_nowDay;
			SkipCloseNeedsLocked(a_nowDay);
			SkipLog("Hold: the trip took {:.2f} game hours; waiting for the needs mods to count them", g_skip.hours);
		}
		return g_skip.pending;
	}

	// the Sleep/Wait menu closed. Any thread, under the lock
	void SkipMenuClosedLocked(float a_nowDay)
	{
		if (!g_skip.pending || g_skip.hours >= 0.0f) {
			return;
		}
		g_skip.hours = std::max(0.0f, (a_nowDay - g_skip.startDay) * 24.0f);
		g_skip.endDay = a_nowDay;
		g_skip.ticks = 0;
		SkipCloseNeedsLocked(a_nowDay);
		SkipLog("Hold: the skip ended after {:.2f} game hours; waiting for the needs mods to count them", g_skip.hours);
	}

	// the request's skip part (HoldTick and the sinks): a need whose skip is being counted isn't held again until its
	// give-back is done. iNeed stays named, with its skip flag (256 hunger, 512 thirst), also if the dish ran out during
	// the skip (its covered share is still owed): its bridge waits for iNeed to count the skipped hours and gives back the
	// covered share; 1024: the last call (30 s), give back what has come in; the skip's number x 2048 goes with the flags.
	// Under the lock
	std::int32_t SkipFlagsLocked(std::int32_t& a_hunger, std::int32_t& a_thirst, bool& a_fast)
	{
		std::int32_t flags = 0;
		if (!g_skip.pending) {
			return 0;
		}
		const auto ineed = static_cast<std::int32_t>(NeedMod::kINeed);
		if (g_skip.hunger.on) {
			if (g_skip.hunger.owner == ineed) {
				a_hunger = ineed;
				flags |= 256;
			} else {
				a_hunger = 0;
			}
		}
		if (g_skip.thirst.on) {
			if (g_skip.thirst.owner == ineed) {
				a_thirst = ineed;
				flags |= 512;
			} else {
				a_thirst = 0;
			}
		}
		a_fast = (flags != 0) && g_skip.hours >= 0.0f;
		if (a_fast && g_skip.ticks >= 30) {
			flags |= 1024;  // kept until the bridge reports (FC_Menu.SkipDone), so a plain request can't overtake it
		}
		if (flags != 0) {
			flags += g_skip.id * 2048;
		}
		return flags;
	}

	// once a second from HoldTick while a skip is pending: after it ended, each need's rise is watched until it stops
	// changing (3 s), at most 30 s; iNeed's bridge does its own (SkipFlagsLocked). Main thread, under the lock (the
	// value globals are plain floats: no engine call)
	void SkipTickLocked(std::vector<SkipPay>& a_pays)
	{
		if (g_skip.generation != g_gameGeneration.load()) {
			g_skip = SkipState{ g_skip.menus };  // another game was loaded
			return;
		}
		if (g_skip.hours < 0.0f) {
			return;  // the menu is still open
		}
		++g_skip.ticks;
		for (auto* need : { &g_skip.hunger, &g_skip.thirst }) {
			if (!need->on) {
				continue;
			}
			const int index = need == &g_skip.hunger ? 0 : 1;
			if (need->total < 0.01f) {
				SkipLog("Hold: no game time passed for {}: nothing to give back", index == 0 ? "hunger" : "thirst");
				SkipNeedDoneLocked(*need);
				continue;
			}
			if (need->owner == static_cast<std::int32_t>(NeedMod::kINeed)) {
				if (g_skip.ticks >= 60) {
					SkipWarn("Hold: iNeed's bridge never reported its skip give-back; the hold starts again");
					SkipNeedDoneLocked(*need);
				}
				continue;
			}
			if (!need->value) {
				SkipNeedDoneLocked(*need);
				continue;
			}
			const float now = need->value->value;
			need->risen = SkipRiseTo(*need, now);
			need->last = now;
			const float rise = need->risen;
			if (std::abs(rise - need->lastRise) <= std::max(0.01f, rise * 0.005f)) {
				++need->still;
			} else {
				need->still = 0;
			}
			need->lastRise = rise;
			const bool settled = rise > need->base && need->still >= 3;
			if (settled || g_skip.ticks >= 30) {
				if (!settled) {
					SkipLog("Hold: mod {} hadn't settled 30 s after the skip", need->owner);
				}
				a_pays.push_back(SkipPayFor(*need, index, rise));
				SkipNeedDoneLocked(*need);
			}
		}
	}

	std::int32_t SkipRequestLocked()
	{
		std::int32_t hunger = g_holdSnap.hunger;
		std::int32_t thirst = g_holdSnap.thirst;
		bool         fast = false;
		const auto   flags = SkipFlagsLocked(hunger, thirst, fast);
		return hunger + thirst * 16 + flags;
	}

	// The Sleep/Wait menu (sleeping, waiting, serving... anything the game runs through it) and fast travel (map travel,
	// carriages, boats: Survival Mode's own watcher logs "Player traveled using a boat or carriage" on this event, and
	// counts the trip at its next update, as SunHelm does). Loading screens are not watched: a door's takes no game time,
	// and the 2026-11-22 test showed work queued during one runs too late to see it. The sinks only decide under the lock
	// and tell the scripts at once (iNeed's bridge must not give a skip's whole loss back on a plain request that was
	// already on its way); the give-backs are worked out and paid by the main tick
	class SkipWatcher :
		public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
		public RE::BSTEventSink<RE::TESFastTravelEndEvent>
	{
	public:
		static SkipWatcher* GetSingleton()
		{
			static SkipWatcher singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (!a_event || std::string_view(a_event->menuName.c_str()) != RE::SleepWaitMenu::MENU_NAME) {
				return RE::BSEventNotifyControl::kContinue;
			}
			auto        calendar = RE::Calendar::GetSingleton();
			const bool  counts = calendar && !g_loadingSave;
			const float now = counts ? calendar->GetCurrentGameTime() : 0.0f;
			std::int32_t request = -1;
			{
				std::scoped_lock lock(g_skipLock);
				if (a_event->opening) {
					if (counts && g_skip.menus == 0) {
						g_skip.menus = 1;
						if (SkipStartLocked(now, -1.0f)) {
							request = SkipRequestLocked();
						}
					}
				} else if (g_skip.menus != 0) {
					g_skip.menus = 0;  // also during a load: only the count (Claude review 2026-11-22e L3)
					if (counts) {
						SkipMenuClosedLocked(now);
					}
				}
			}
			SkipLogFlush();
			if (request >= 0) {
				SendHoldRequest(request);
			}
			return RE::BSEventNotifyControl::kContinue;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::TESFastTravelEndEvent* a_event, RE::BSTEventSource<RE::TESFastTravelEndEvent>*) override
		{
			auto calendar = RE::Calendar::GetSingleton();
			if (!a_event || !calendar || g_loadingSave) {
				return RE::BSEventNotifyControl::kContinue;
			}
			const float  now = calendar->GetCurrentGameTime();
			std::int32_t request = -1;
			{
				std::scoped_lock lock(g_skipLock);
				SkipLog("Hold: fast travel ended ({:.2f} game hours)", a_event->fastTravelEndHours);
				if (SkipStartLocked(now, std::max(0.0f, a_event->fastTravelEndHours))) {
					request = SkipRequestLocked();
				}
			}
			SkipLogFlush();
			if (request >= 0) {
				SendHoldRequest(request);
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
	void StartSkipWatch()
	{
		if (auto ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(SkipWatcher::GetSingleton());
		}
		if (auto events = RE::ScriptEventSourceHolder::GetSingleton()) {
			events->AddEventSink<RE::TESFastTravelEndEvent>(SkipWatcher::GetSingleton());
		}
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
		// a brewed drink held thirst for the hours it still had at the last tick (HoldTick); a skip (wait, travel, jail)
		// shows here as one big step, and only the hours past the drink's end count
		const float rise = std::max(0.0f, hours - g_thirstHeldLeft) * THIRST_PER_HOUR;
		g_thirstHeldLeft = g_holdOwnThirst ? g_thirstDrinkLeft : 0.0f;
		SetThirst(g_thirst.value->value + rise, !firstLook);
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
		// the drink held only the hours it still had when the player lay down (ThirstSleepStart's tick); then look again
		const float rise = std::max(0.0f, hours - g_thirstHeldLeft) * THIRST_PER_HOUR * THIRST_SLEEP_MULT;
		HoldTick();
		g_thirstHeldLeft = g_holdOwnThirst ? g_thirstDrinkLeft : 0.0f;
		SetThirst(g_thirst.value->value + rise, true);
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
		g_thirstHeldLeft = 0.0f;
	}
