// Part of CookingMenu.cpp: included in order inside its anonymous namespace (not compiled on its own).
// ---------------------------------------------------------------------------------------------------------------
// The spell side of the element meals: while an Earthy, Spicy, Chilly or Zesty meal, drink or baked dish is active,
// every hit of the player's spells, and of a follower's when it shares the meal (Shared Table), hand-cast and staff,
// concentration ticks included, whatever its own element,
// adds a share of its damage as the meal's element (decided 2026-11-03):
//   Spicy  3/5/7/10% as fire (no side effect, so a little more than the others)
//   Chilly 2/4/6/8% as frost, which also drains twice that in stamina
//   Zesty  2/4/6/8% as shock, which also drains twice that in magicka
//   Earthy 1/2/3/4% as holy damage, 3/6/9/12% against undead, daedra, vampires, ghosts and werewolves
// So a spell of the meal's own element simply comes out stronger, and a spell of another element gains a second one.
//
// Where the hits are seen: ValueModifierEffect::ModifyActorValue (vtable slot 0x20), the point where a magic effect
// changes an actor value, hooked on every subclass's own vtable. That is where the game applies each tick of a damage
// effect, after the target's resistances (the idea comes from Floating Damage NG's notes; none of its code is used).
// The share is taken of the damage before the target's resistances (the hit is divided back by them), because the
// extra element then meets the target's own resistance to it: a fire spell on a fire-proof foe still carries its
// full frost, and nothing is reduced twice. The game calls the hook from several threads, so it only notes plain
// numbers (target, effect, damage, element, level) in a queue. On the main thread (a timer every 0.25 s) the hits are
// added up per target and the extra is cast from the player at the target, as soon as it reaches 1 point or 0.5 s
// after the first hit, so small concentration ticks add up instead of being lost:
//   FreeCooking.esp FC_ElementStrike_Spicy/Chilly/Zesty (the vanilla fire and frost enchantment effects and our
//   FC_ZestyShock, so the target's element resistance applies once) or FC_HolyStrike (no school, no resistance).
// Our own strikes and the weapon hit spells (FC_Hit_*) are recognised by their spell and never add more; the strikes
// that land are logged, so a test can check the damage that actually arrived.
//
// The weapon side: every weapon hit of the player or of a follower who shares an element meal adds the same share of
// its damage as a spell hit does (his decision 2026-11-23: "lets do it % based. enchants are flat because enchants can
// be increased with skills level and perks"; until then a flat 3/4/5/6-style hit scaled by the weapon). The share is
// taken of the hit's damage without the sneak and power attack multipliers (his pick the same day: element bonuses are
// never multiplied by sneak or power attacks), cast as FreeCooking.esp FC_Hit_Spicy/Chilly/Zesty<level> or FC_HolyStrike
// with that amount. It used to be a perk's Apply Combat Hit Spell entry, but the game applies only one spell
// per hit through that entry point (Scrambled Bugs, Nexus SE 43532; SkyrimMOD作成wiki Tips: only the perk whose priority
// is nearest 0 counts; TESAll), so vanilla's bleed and paralysis perks and our hit knocked each other out. Now the plugin
// watches the call where the game sends an actor's weapon hit (AE: ID 38586 + 0xFA, the same place powerof3's Papyrus
// Extender takes its OnWeaponHit from, unchanged in its 1.7.99 update): once per weapon hit and per target, melee and
// arrows, with the hit's flags, fists, claws and bashes too; blocked hits still count.
// What the hit's damage holds at that call (SkyrimSE.exe 1.7.104 read 2026-11-23, HitData::Populate and its helper
// 0x1407F17C0): the weapon damage (skill, smithing) times the attack's Damage Mult (a power attack's, attack data
// +0x18, passed into the weapon damage function), Mod Power Attack Damage perks (entry 28) when the hit has the power
// flag, Calculate Weapon Damage, Mod Attack Damage and Mod Incoming Damage, then the sneak multiplier (the weapon
// type's fCombatSneak*Mult through Mod Sneak Attack Mult perks, kept at HitData +0x70 with the sneak flag), the
// critical damage added, and the attacker's Attack Damage Mult. Armour: the in-game test 2026-11-23 showed totalDamage at
// this call is already after the target's armour (bandit 7.2 against physicalDamage 8.4; no-armour dummy 8.4 = 8.4), while
// physicalDamage holds the same damage before armour (power x2: 16.8, sneak x3: 25.2). His call the same night: "doest make
// sense that armor blocks our elementals hits", so the base is physicalDamage (totalDamage if a hit carries none), divided
// back by the sneak multiplier, and on a power attack by its Damage Mult and its Mod Power Attack Damage perks (vanilla's
// Savage Strike and Devastating Blow multiply; a mod's perk that adds a flat amount is undone as a multiplier).
// The hook only notes plain numbers; the cast happens on the main thread in one shared task. The hook is placed only
// if the game has a call instruction at that spot (otherwise the weapon side stays off and the log says so).
//
// Save/load: nothing is saved. A load or a new game drops the queue and the totals of the game being left (HolyReset);
// the element and level are read again from the player's active effects on the next tick.
// ---------------------------------------------------------------------------------------------------------------

	enum Element : std::int32_t { kEarthy = 0, kSpicy = 1, kChilly = 2, kZesty = 3 };
	constexpr std::array<const char*, 4> ELEMENT_NAME{ "Earthy", "Spicy", "Chilly", "Zesty" };
	constexpr std::array<std::array<float, 4>, 4> ELEMENT_PCT{ {
		{ 1.0f, 2.0f, 3.0f, 4.0f },    // Earthy (holy), anyone
		{ 3.0f, 5.0f, 7.0f, 10.0f },   // Spicy (fire)
		{ 2.0f, 4.0f, 6.0f, 8.0f },    // Chilly (frost)
		{ 2.0f, 4.0f, 6.0f, 8.0f },    // Zesty (shock)
	} };
	constexpr std::array<float, 4> HOLY_FOE_PCT{ 3.0f, 6.0f, 9.0f, 12.0f };
	constexpr RE::FormID HOLY_SPELL = 0xA5D;                                    // FC_HolyStrike
	constexpr std::array<RE::FormID, 4> STRIKE_SPELL{ HOLY_SPELL, 0xA6E, 0xA6F, 0xA70 };  // FC_ElementStrike_*
	constexpr RE::FormID HIT_SPELL_FIRST = 0x950, HIT_SPELL_LAST = 0x95F;       // FC_Hit_* (weapon side)
	constexpr float MAX_RESIST_UNDONE = 85.0f;  // a resistance above this counts as this when undoing it (never / 0)
	constexpr std::size_t HOLY_QUEUE_MAX = 1024;  // hits waiting for the main thread (a big fight stays far below this)
	constexpr float HOLY_FLUSH_POINTS = 1.0f;
	constexpr auto HOLY_FLUSH_AFTER = std::chrono::milliseconds(500);

	constexpr std::int32_t READ_LATER = -2;  // a follower's hit: its meal is read on the main thread

	struct HolyHit
	{
		RE::FormID   target = 0;
		RE::FormID   caster = 0;     // the player or a follower
		RE::FormID   effect = 0;     // the magic effect that did it (for the log)
		float        damage = 0.0f;  // health the hit would have taken before the target's resistances
		float        taken = 0.0f;   // health it took (for the log)
		std::int32_t element = -1;   // the meal's element when it landed; -1: one of our strikes landing (logged only);
		                             // READ_LATER: a follower's hit
		std::int32_t level = 0;      // the meal's level when it landed
	};

	struct HolyTotal
	{
		float                                 bonus = 0.0f;
		float                                 damage = 0.0f;  // the spell damage it came from (for the log)
		RE::FormID                            effect = 0;     // the last magic effect in it (for the log)
		std::int32_t                          element = 0;
		bool                                  foe = false;    // undead, daedra, vampire, ghost or werewolf (Earthy)
		std::chrono::steady_clock::time_point first{};
	};

	// the meal's element (Element, -1: none) and its level 1-4, in one value (element * 8 + level; -1 none) so a hit never
	// reads one meal's element with another's level while a meal changes (Codex review 2026-11-15). Main thread writes
	std::atomic<std::int32_t> g_elementMealPacked{ -1 };
	// who can strike with an element now: the player and the followers with an element meal running, written by the
	// main thread's tick (HolyTick, every 250 ms) and read by the weapon-hit capture, so a big fight between other
	// actors queues no work at all (Codex review 2026-11-15, M14). 0: an empty place
	constexpr std::size_t                    ELEMENT_FIGHTERS_MAX = 16;
	std::array<std::atomic<RE::FormID>, ELEMENT_FIGHTERS_MAX> g_elementFighters{};
	std::atomic<RE::TESFile*> g_ourFile{ nullptr };   // FreeCooking.esp, to recognise our own spells
	std::atomic<std::uint32_t> g_playerHandle{ 0 };  // the player's handle, to compare against a hit's caster
	std::mutex                g_holyLock;
	std::vector<HolyHit>      g_holyQueue;           // hook -> main thread
	std::size_t               g_holyDropped = 0;     // hits lost to a full queue (logged)
	std::map<std::tuple<RE::FormID, RE::FormID, std::int32_t>, HolyTotal> g_holyTotals;  // main thread only; key: target, caster, element

	// Our strikes landing, for the log only: summed per target and element and written once they stop landing. A fire
	// strike uses the vanilla fire enchantment effect, whose afterburn (taper weight 0.2 over 1 s, Skyrim.esm) lands in
	// tiny pieces every frame; one line per piece wrote ~1,700 lines in two minutes (2026-11-03).
	struct StrikeLanded
	{
		float                                 total = 0.0f;
		int                                   pieces = 0;
		RE::FormID                            effect = 0;
		std::chrono::steady_clock::time_point last{};
	};
	std::map<std::pair<RE::FormID, std::int32_t>, StrikeLanded> g_strikesLanded;  // main thread only; key: target, element
	constexpr auto STRIKE_LOG_QUIET = std::chrono::milliseconds(1500);

	// 0-3: one of our element strikes (by element); 4: a weapon hit spell; -1: not ours
	std::int32_t OurSpellKind(const RE::MagicItem* a_spell)
	{
		const auto file = g_ourFile.load(std::memory_order_relaxed);
		if (!file || a_spell->GetFile(0) != file) {
			return -1;
		}
		const auto local = a_spell->GetLocalFormID();
		for (std::int32_t k = 0; k < 4; ++k) {
			if (local == STRIKE_SPELL[static_cast<std::size_t>(k)]) {
				return k;
			}
		}
		return local >= HIT_SPELL_FIRST && local <= HIT_SPELL_LAST ? 4 : -1;
	}

	// what's left after the target's resistance (an actor value in percent; a weakness is negative)
	float AfterResist(RE::Actor* a_target, RE::ActorValue a_av)
	{
		if (a_av == RE::ActorValue::kNone) {
			return 1.0f;
		}
		const auto resist = std::min(a_target->GetActorValue(a_av), MAX_RESIST_UNDONE);
		return 1.0f - resist / 100.0f;
	}

	// any thread: one tick of a magic effect changed an actor value
	void HolyCapture(RE::ValueModifierEffect* a_effect, RE::Actor* a_target, float a_value, RE::ActorValue a_av)
	{
		if (a_value >= 0.0f || !a_effect || !a_target) {
			return;
		}
		const auto av = a_av == RE::ActorValue::kNone ? a_effect->actorValue : a_av;
		if (av != RE::ActorValue::kHealth) {
			return;
		}
		// the player's spells, or a follower's (who may share an element meal: Shared Table); not hits on the player
		const auto player = g_playerHandle.load(std::memory_order_relaxed);
		if (player == 0 || a_target->IsPlayerRef()) {
			return;
		}
		const bool byPlayer = a_effect->caster.native_handle() == player;
		RE::FormID casterId = 0;
		if (!byPlayer) {
			// a follower with an element meal, from the main thread's list (HolyTick): their own flags are not read here,
			// on another actor's hit job (Codex review 2026-11-15, M1)
			const auto caster = a_effect->caster.get();
			if (!caster || caster.get() == a_target) {
				return;
			}
			casterId = caster->GetFormID();
			if (std::ranges::none_of(g_elementFighters, [casterId](const auto& a_id) { return a_id.load(std::memory_order_relaxed) == casterId; })) {
				return;
			}
		}
		const auto base = a_effect->GetBaseObject();
		const auto spell = a_effect->spell;
		if (!base || !spell) {
			return;
		}
		const auto ours = OurSpellKind(spell);
		if (ours == 4) {
			return;  // a weapon hit's element: weapons have their own fixed extra
		}
		if (ours >= 0) {
			std::scoped_lock lock(g_holyLock);  // one of our strikes landing: for the log only
			if (g_holyQueue.size() < HOLY_QUEUE_MAX) {
				g_holyQueue.push_back({ a_target->GetFormID(), casterId, base->GetFormID(), -a_value, -a_value, -1, ours });
			}
			return;
		}
		auto element = READ_LATER;
		std::int32_t level = 0;
		if (byPlayer) {
			const auto packed = g_elementMealPacked.load(std::memory_order_relaxed);
			element = packed < 0 ? -1 : packed / 8;
			level = packed < 0 ? 0 : packed % 8;
			if (element < 0 || element > 3 || level < 1 || level > 4) {
				return;
			}
		}
		const auto type = spell->GetSpellType();
		if (type != RE::MagicSystem::SpellType::kSpell && type != RE::MagicSystem::SpellType::kStaffEnchantment) {
			return;  // powers, shouts, scrolls, potions, poisons, weapon enchantments: not spells
		}
		// back to the damage before the target's resistances: magic resistance on any hostile spell, then its element
		// resistance, the two multiplied as the game does (UESP, Skyrim:Resist Magic: 85% magic and 85% fire leave
		// 2.25 of 100). NPCs have no 85% cap, but a fully immune target takes no damage and never gets here.
		auto factor = AfterResist(a_target, base->data.resistVariable);
		if (base->IsHostile() && base->data.resistVariable != RE::ActorValue::kResistMagic) {
			factor *= AfterResist(a_target, RE::ActorValue::kResistMagic);
		}
		const auto damage = factor > 0.01f ? -a_value / factor : -a_value;
		std::scoped_lock lock(g_holyLock);
		if (g_holyQueue.size() >= HOLY_QUEUE_MAX) {
			++g_holyDropped;
			return;
		}
		g_holyQueue.push_back({ a_target->GetFormID(), casterId, base->GetFormID(), damage, -a_value, element, level });
	}

	// one hook per effect class: each keeps its own original function
	template <std::size_t N>
	struct HolyHook
	{
		static void ModifyActorValue(RE::ValueModifierEffect* a_this, RE::Actor* a_actor, float a_value, RE::ActorValue a_av)
		{
			original(a_this, a_actor, a_value, a_av);
			HolyCapture(a_this, a_actor, a_value, a_av);
		}
		static inline REL::Relocation<decltype(ModifyActorValue)> original;
		static void Install(REL::ID a_vtable)
		{
			REL::Relocation<std::uintptr_t> vtbl{ a_vtable };
			original = vtbl.write_vfunc(0x20, ModifyActorValue);
		}
	};

	void InstallHolyHooks()
	{
		HolyHook<0>::Install(RE::VTABLE_ValueModifierEffect[0]);
		HolyHook<1>::Install(RE::VTABLE_DualValueModifierEffect[0]);
		HolyHook<2>::Install(RE::VTABLE_PeakValueModifierEffect[0]);
		HolyHook<3>::Install(RE::VTABLE_AbsorbEffect[0]);
		HolyHook<4>::Install(RE::VTABLE_AccumulatingValueModifierEffect[0]);
		HolyHook<5>::Install(RE::VTABLE_TargetValueModifierEffect[0]);
		HolyHook<6>::Install(RE::VTABLE_ValueAndConditionsEffect[0]);
		logger::info("Elements: spell hits watched (7 magic effect types)");
	}

	// the element meal effects (FC_Fx_, FC_FxD_, FC_FxB_ <element>1-4), looked up once (main thread)
	std::array<RE::EffectSetting*, 48> g_elementEffects{};
	bool                               g_elementEffectsFound = false;

	// the element meal on an actor (the player, or a follower sharing it): its meal, drink or baked effect (only one
	// element runs at a time); main thread
	std::pair<std::int32_t, std::int32_t> ReadElementMeal(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return { -1, 0 };
		}
		if (!g_elementEffectsFound) {
			auto data = RE::TESDataHandler::GetSingleton();
			if (!data) {
				return { -1, 0 };
			}
			std::size_t i = 0;
			for (const RE::FormID first : { 0x910u, 0x930u, 0x9C0u }) {
				for (RE::FormID n = 0; n < 16; ++n) {
					g_elementEffects[i++] = data->LookupForm<RE::EffectSetting>(first + n, PLUGIN);
				}
			}
			g_elementEffectsFound = true;
		}
		// Spicy (element 1) can run beside another element in Survival Mode, keeping only its warmth (decided
		// 2026-11-04: "Spicy fights only when it's your only element"), so the other elements are looked for first
		for (const bool spicyPass : { false, true }) {
			for (std::int32_t level = 4; level >= 1; --level) {
				for (std::int32_t element = 0; element < 4; ++element) {
					if ((element == 1) != spicyPass) {
						continue;
					}
					for (std::size_t book = 0; book < 3; ++book) {
						auto effect = g_elementEffects[book * 16 + static_cast<std::size_t>(element * 4 + level - 1)];
						if (effect && a_actor->HasMagicEffect(effect)) {
							return { element, level };
						}
					}
				}
			}
		}
		return { -1, 0 };
	}

	// undead, daedra, vampires, ghosts, werewolves and the raised dead (weapon and spell side alike); main thread
	bool HolyFoe(RE::Actor* a_actor)
	{
		for (const RE::FormID id : UNHOLY_KEYWORDS) {  // Earthy's list (Rules.inl), shared with the potion rule
			if (auto keyword = RE::TESForm::LookupByID<RE::BGSKeyword>(id); keyword && a_actor->HasKeyword(keyword)) {
				return true;
			}
		}
		const auto race = a_actor->GetRace();
		if (race && race->GetFormID() == WEREWOLF_BEAST_RACE) {
			return true;
		}
		// the raised dead (Raise Zombie, Reanimate Corpse, Dead Thrall, a vampire lord's Raise Dead...): the corpse keeps
		// its own race and keywords, so look for an active reanimate effect on it
		bool raised = false;
		if (auto target = a_actor->GetMagicTarget()) {
			if (auto effects = target->GetActiveEffectList()) {
				for (auto effect : *effects) {
					const auto base = effect ? effect->GetBaseObject() : nullptr;
					if (base && base->GetArchetype() == RE::EffectSetting::Archetype::kReanimate && effect->flags.none(RE::ActiveEffect::Flag::kInactive)) {
						raised = true;
						break;
					}
				}
			}
		}
		return raised;
	}

	// main thread
	// Our strikes are spells, so the caster's Mod Spell Magnitude perks (Augmented Flames/Frost/Shock, Necromage, our
	// own Brainy, any mod's) would scale them again: measured 2026-11-03, Augmented Flames made a 3.29 Spicy strike land
	// 4.12 (x1.25) on top of a share already taken from the boosted spell. So each strike's amount is divided by the
	// multiplier the game itself reports for this caster, strike spell and target (decided 2026-11-03, his option 1).
	// The call shape, HandleEntryPoint(kModSpellMagnitude, owner, spell, target, float* in/out), comes from UESP's
	// PERK format page (conditions Perk Owner, Spell, Target), Perk Entry Point Extender's dispatcher (owner, 2 forms,
	// out) and Lodestone's in-game trace of this entry point's arguments (owner, the spell, a third slot). As a guard,
	// the game's own table must list 3 arguments for it, or the correction stays off (and says so once).
	float StrikePerkMult(RE::Actor* a_caster, RE::SpellItem* a_spell, RE::Actor* a_target)
	{
		static const bool usable = []() {
			auto entry = RE::BGSEntryPoint::GetEntryPoint(RE::BGSEntryPoint::ENTRY_POINT::kModSpellMagnitude);
			if (!entry || entry->parameters.count != 3 || !entry->parameters.data) {
				logger::warn("Elements: the game's Mod Spell Magnitude entry point doesn't take the expected 3 arguments ({}); perk boosts on our strikes are not undone",
					entry ? entry->parameters.count : 0u);
				return false;
			}
			std::string names;
			for (std::uint32_t i = 0; i < entry->parameters.count; ++i) {
				const char* name = entry->parameters.data[i].name;
				names += std::format("{}{}", i ? ", " : "", name ? name : "?");
			}
			logger::info("Elements: perk boosts on our strikes are undone (Mod Spell Magnitude takes: {})", names);
			return true;
		}();
		if (!usable || !a_caster || !a_spell) {
			return 1.0f;
		}
		float mult = 1.0f;
		RE::BGSEntryPoint::HandleEntryPoint(RE::BGSEntryPoint::ENTRY_POINT::kModSpellMagnitude, a_caster, static_cast<RE::MagicItem*>(a_spell), a_target, &mult);
		return mult > 0.01f && mult < 100.0f ? mult : 1.0f;
	}

	void HolyTick()
	{
		auto player = Player();
		if (!player) {
			return;
		}
		g_playerHandle.store(player->GetHandle().native_handle(), std::memory_order_relaxed);
		auto data = RE::TESDataHandler::GetSingleton();
		if (!g_ourFile.load(std::memory_order_relaxed) && data) {
			if (auto file = data->LookupModByName(PLUGIN)) {
				g_ourFile.store(const_cast<RE::TESFile*>(file), std::memory_order_relaxed);
				logger::info("Elements: our spells are recognised by {}", PLUGIN);
			}
		}
		const auto [element, level] = ReadElementMeal(player);
		g_elementMealPacked.store(element < 0 || level < 0 || level > 7 ? -1 : element * 8 + level, std::memory_order_relaxed);
		{
			std::size_t n = 0;
			if (element >= 0) {
				g_elementFighters[n++].store(player->GetFormID(), std::memory_order_relaxed);
			}
			if (auto lists = RE::ProcessLists::GetSingleton()) {
				for (const auto& h : lists->highActorHandles) {
					if (n >= ELEMENT_FIGHTERS_MAX) {
						break;
					}
					auto a = h.get();
					if (a && a.get() != player && a->IsPlayerTeammate() && ReadElementMeal(a.get()).first >= 0) {
						g_elementFighters[n++].store(a->GetFormID(), std::memory_order_relaxed);
					}
				}
			}
			for (; n < ELEMENT_FIGHTERS_MAX; ++n) {
				g_elementFighters[n].store(0, std::memory_order_relaxed);
			}
		}

		std::vector<HolyHit> hits;
		std::size_t dropped = 0;
		{
			std::scoped_lock lock(g_holyLock);
			hits.swap(g_holyQueue);
			dropped = std::exchange(g_holyDropped, 0);
		}
		if (dropped > 0) {
			logger::warn("Elements: {} spell hit(s) missed (queue full)", dropped);
		}
		const auto now = std::chrono::steady_clock::now();
		for (const auto& hit : hits) {
			if (hit.element == -1) {
				auto& landed = g_strikesLanded[{ hit.target, hit.level }];
				landed.total += hit.taken;
				++landed.pieces;
				landed.effect = hit.effect;
				landed.last = now;
				continue;
			}
			auto target = RE::TESForm::LookupByID<RE::Actor>(hit.target);
			if (!target) {
				continue;
			}
			auto element = hit.element;
			auto level = hit.level;
			if (element == READ_LATER) {  // a follower's spell: its own meal, if it shares one
				const auto meal = ReadElementMeal(RE::TESForm::LookupByID<RE::Actor>(hit.caster));
				element = meal.first;
				level = meal.second;
				if (element < 0 || element > 3 || level < 1 || level > 4) {
					continue;
				}
			}
			// each hit at its own element and rate, so a meal ending mid-fight changes only the hits after it
			const bool foe = element == kEarthy && HolyFoe(target);
			const auto& rates = foe ? HOLY_FOE_PCT : ELEMENT_PCT[static_cast<std::size_t>(element)];
			auto& total = g_holyTotals[{ hit.target, hit.caster, element }];
			if (total.bonus <= 0.0f) {
				total.first = now;
			}
			total.bonus += hit.damage * rates[static_cast<std::size_t>(level - 1)] / 100.0f;
			total.damage += hit.damage;
			total.element = element;
			total.foe = foe;
			total.effect = hit.effect;
		}
		for (auto it = g_strikesLanded.begin(); it != g_strikesLanded.end();) {
			const auto& [key, landed] = *it;
			if (now - landed.last < STRIKE_LOG_QUIET) {
				++it;
				continue;
			}
			logger::info("Elements: {} strike landed {:.2f} damage on {:08X} (effect {:08X}, {} piece{})", ELEMENT_NAME[static_cast<std::size_t>(key.second)],
				landed.total, key.first, landed.effect, landed.pieces, landed.pieces == 1 ? "" : "s");
			it = g_strikesLanded.erase(it);
		}
		if (g_holyTotals.empty()) {
			return;
		}
		for (auto it = g_holyTotals.begin(); it != g_holyTotals.end();) {
			auto& [key, total] = *it;
			if (total.bonus < HOLY_FLUSH_POINTS && now - total.first < HOLY_FLUSH_AFTER) {
				++it;
				continue;
			}
			const auto id = std::get<0>(key);
			const auto casterId = std::get<1>(key);
			RE::Actor* by = casterId == 0 ? static_cast<RE::Actor*>(player) : RE::TESForm::LookupByID<RE::Actor>(casterId);
			auto caster = by ? by->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant) : nullptr;
			auto target = RE::TESForm::LookupByID<RE::Actor>(id);
			auto spell = data ? data->LookupForm<RE::SpellItem>(STRIKE_SPELL[static_cast<std::size_t>(total.element)], PLUGIN) : nullptr;
			if (spell && caster && target && !target->IsDead() && target->Is3DLoaded()) {
				const auto mult = StrikePerkMult(by, spell, target);
				caster->CastSpellImmediate(spell, true, target, 1.0f, false, total.bonus / mult, by);
				logger::info("Elements: {} {:.2f} to {:08X} by {} ({}, from {:.1f} spell damage before resistances, effect {:08X}: {:.1f}%; perks x{:.2f} undone)",
					ELEMENT_NAME[static_cast<std::size_t>(total.element)], total.bonus, id, casterId == 0 ? "the player"s : std::format("{:08X}", casterId),
					total.foe ? "unholy foe" : "anyone", total.damage, total.effect,
					total.damage > 0.0f ? total.bonus * 100.0f / total.damage : 0.0f, mult);
			}
			it = g_holyTotals.erase(it);
		}
	}

	// ---- the weapon side ----
	// No element during a brawl: the brawl quest ends on any magic from the player, and a fair fistfight has no magic.
	constexpr RE::FormID BRAWL_FACTION = 0x4CFA6;  // DGIntimidateFaction: the player and the brawlers, for the whole brawl

	bool InBrawl(RE::Actor* a_aggressor, RE::Actor* a_target)
	{
		static auto faction = RE::TESForm::LookupByID<RE::TESFaction>(BRAWL_FACTION);
		return faction && (a_aggressor->IsInFaction(faction) || a_target->IsInFaction(faction));
	}

	// the Mod Power Attack Damage multiplier the game uses on a hit (entry 28: owner, weapon, target). Read in the hook,
	// before the game applies the hit (Codex review 2026-11-24a R2, his pick "proposed"): read later, a perk whose
	// condition the hit itself changes (Adamant's Execute: target at half Health or less; Cleave: target staggered) was
	// counted for a hit it did not apply to. The game reads the same entry on that thread for that hit. Measured in game
	// 2026-11-24 22:40 (probe, 11 power attacks): the target's Health is unchanged right after the hooked call and lower
	// when the task runs, and a target went from not staggered to staggered in between; the value read at the hit matched
	// what the game applied on every hit (physical x1.00 and x1.50 with Adamant's Cleave).
	float PowerPerkMult(RE::Actor* a_aggressor, RE::TESObjectWEAP* a_weapon, RE::Actor* a_target)
	{
		float mult = 1.0f;
		RE::BGSEntryPoint::HandleEntryPoint(RE::BGSEntryPoint::ENTRY_POINT::kModPowerAttackDamage, a_aggressor, a_weapon, a_target, &mult);
		return mult > 0.01f && mult < 100.0f ? mult : 1.0f;
	}
	constexpr RE::FormID HIT_SPELL_FIRST_ELEMENT = 0x950;  // FC_Hit_<element><level>: 0x950 + element * 4 + level - 1

	struct WeaponHit
	{
		RE::FormID aggressor = 0;
		RE::FormID target = 0;
		RE::FormID weapon = 0;   // 0: unarmed
		bool       bash = false;
		float      total = 0.0f;       // the hit's damage after armour (HitData totalDamage, before the call): log and fallback
		float      totalAfter = 0.0f;  // the same field after the call, and physicalDamage: for the log (test 2026-11-23)
		float      physical = 0.0f;    // the hit's damage before armour (HitData physicalDamage): the element's base
		float      sneakMult = 1.0f;   // the sneak multiplier in it (1: not a sneak attack)
		float      powerMult = 1.0f;   // a power attack's Damage Mult (1: not a power attack)
		bool       power = false;
		float      perkAtHit = 0.0f;  // the power perks' multiplier read before the hit was applied (0: not read)
		std::uint32_t game = 0;  // g_hitGame when it landed: a hit from before a load is dropped
	};
	std::mutex             g_weaponHitLock;
	std::vector<WeaponHit> g_weaponHits;            // hook -> main thread
	std::atomic<bool>      g_weaponTaskQueued{ false };
	std::atomic<std::uint32_t> g_hitGame{ 0 };  // counts up at every load or new game (HolyReset)

	// main thread: add each hit's element (the attacker's meal may differ per hit, so it is read now)
	void ApplyWeaponHits()
	{
		g_weaponTaskQueued.store(false);
		std::vector<WeaponHit> hits;
		{
			std::scoped_lock lock(g_weaponHitLock);
			hits.swap(g_weaponHits);
		}
		if (g_loadingSave) {
			return;
		}
		// hits noted before a load belong to the game being left (2026-11-13 review: a reset flag used to drop the
		// first batch of real hits after every load instead)
		const auto game = g_hitGame.load();
		std::erase_if(hits, [game](const WeaponHit& a_hit) { return a_hit.game != game; });
		static std::array<RE::SpellItem*, 13> spells{};  // FC_Hit_* 0x954-0x95F (Spicy, Chilly, Zesty 1-4), then FC_HolyStrike
		static bool                           spellsFound = false;
		if (!spellsFound) {
			auto data = RE::TESDataHandler::GetSingleton();
			if (!data) {
				return;
			}
			for (RE::FormID n = 0; n < 12; ++n) {
				spells[n] = data->LookupForm<RE::SpellItem>(HIT_SPELL_FIRST_ELEMENT + 4 + n, PLUGIN);
			}
			spells[12] = data->LookupForm<RE::SpellItem>(HOLY_SPELL, PLUGIN);
			spellsFound = true;
		}
		for (const auto& hit : hits) {
			auto aggressor = RE::TESForm::LookupByID<RE::Actor>(hit.aggressor);
			auto target = RE::TESForm::LookupByID<RE::Actor>(hit.target);
			if (!aggressor || !target || target->IsDead() || !target->Is3DLoaded()) {
				continue;
			}
			if (!aggressor->IsPlayerRef() && !aggressor->IsPlayerTeammate()) {
				continue;  // only the player and followers eat our meals
			}
			const auto [element, level] = ReadElementMeal(aggressor);
			if (element < 0 || level < 1 || level > 4) {
				continue;
			}
			const char* kind = hit.bash ? "bash" : (hit.weapon ? "weapon hit" : "unarmed hit");
			if (InBrawl(aggressor, target)) {
				logger::info("Elements: {} by {:08X} on {:08X} skipped (brawl)", kind, hit.aggressor, hit.target);
				continue;  // a fair fistfight: no magic
			}
			auto caster = aggressor->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
			if (!caster) {
				continue;
			}
			// the hit's damage without its sneak and power attack multipliers (see the top of this file)
			float base = (hit.physical > 0.0f ? hit.physical : hit.total) / hit.sneakMult;  // before armour (his call 2026-11-23)
			float perk = 1.0f;
			if (hit.power) {
				if (hit.perkAtHit > 0.0f) {
					perk = hit.perkAtHit;
				} else {
					// never reached since review 2026-11-24 R2-A: every accepted power hit carries the value read at the
					// hit; this line is the log trap for it (no read after the hit: its perks may judge a changed target)
					logger::info("Elements: power hit by {:08X} on {:08X} came without its perk value from the hit (no perk applied)", hit.aggressor, hit.target);
				}
				base /= hit.powerMult * perk;
			}
			if (base <= 0.0f) {
				continue;  // a hit with no damage (a bash may carry none): no element
			}
			const bool foe = element == kEarthy && HolyFoe(target);
			const auto& rates = foe ? HOLY_FOE_PCT : ELEMENT_PCT[static_cast<std::size_t>(element)];
			const auto pct = rates[static_cast<std::size_t>(level - 1)];
			const auto amount = base * pct / 100.0f;
			auto spell = element == kEarthy ? spells[12] : spells[static_cast<std::size_t>((element - 1) * 4 + level - 1)];
			if (!spell) {
				continue;
			}
			const auto mult = StrikePerkMult(aggressor, spell, target);
			caster->CastSpellImmediate(spell, element == kEarthy, target, 1.0f, false, amount / mult, aggressor);
			logger::info("Elements: {} by {:08X} on {:08X} (weapon {:08X}): {} level {}{}, {:.1f}% of {:.1f} = {:.2f} damage (hit {:.1f}, after the call {:.1f}, physical {:.1f}, sneak x{:.2f}, power x{:.2f} x perks {:.2f}; strike perks x{:.2f} undone)",
				kind, hit.aggressor, hit.target, hit.weapon, ELEMENT_NAME[static_cast<std::size_t>(element)], level, foe ? " (unholy foe)" : "", pct, base, amount,
				hit.total, hit.totalAfter, hit.physical, hit.sneakMult, hit.power ? hit.powerMult : 1.0f, perk, mult);
		}
	}

	// the aggressor of a hit, when it is the player or someone else with an element meal (else null)
	RE::NiPointer<RE::Actor> ElementFighterOf(const RE::HitData& a_data, RE::Actor* a_target)
	{
		auto aggressor = a_data.aggressor.get();
		if (!aggressor || aggressor.get() == a_target) {
			return {};
		}
		const auto attacker = aggressor->GetFormID();
		if (std::ranges::none_of(g_elementFighters, [attacker](const auto& a_id) { return a_id.load(std::memory_order_relaxed) == attacker; })) {
			return {};
		}
		return aggressor;
	}

	// any thread (inside the game's hit handling, before the game applies the hit): a power attack's Mod Power Attack
	// Damage multiplier for this fighter; 0 for any other hit (nothing read)
	float PowerPerkAtHit(RE::Actor* a_fighter, RE::Actor* a_target, const RE::HitData& a_data)
	{
		if (!a_fighter || !a_target || a_data.flags.none(RE::HitData::Flag::kPowerAttack)) {
			return 0.0f;
		}
		auto weapon = a_data.weapon && !a_data.weapon->IsHandToHandMelee() ? a_data.weapon : nullptr;  // as ApplyWeaponHits
		return PowerPerkMult(a_fighter, weapon, a_target);
	}

	// any thread (inside the game's hit handling): note the hit, nothing else. a_fighter is the hook's one decision,
	// taken before the game applied the hit, that the aggressor has an element meal (review 2026-11-24 R2-A: a second
	// look at the fighter list here could see a refresh the first missed, and that hit then had no perk value)
	void WeaponHitCapture(RE::Actor* a_target, const RE::HitData& a_data, float a_totalBefore, float a_perkAtHit, RE::Actor* a_fighter)
	{
		if (!a_target || !a_fighter) {
			return;  // every melee hit counts: weapons, fists and claws (weapon 0 or Unarmed), and bashes
		}
		// only a weapon's hit (fists carry the Unarmed weapon 0x1F4) or a bash. Measured in game 2026-11-14 (a probe
		// logged every field at this call): punches came with weapon 1F4, swords and bows with their weapon, bashes
		// with no weapon and the bash flag; Lightning Storm's hits came with no weapon, no attack data, 0 damage and
		// its explosion as the source, and were counted as punches. Firebolt, Sparks and Lightning Bolt never reach
		// this call. (The game's own melee and explosion flags are not set here: a filter on them dropped every punch.)
		if (!a_data.weapon && a_data.flags.none(RE::HitData::Flag::kBash, RE::HitData::Flag::kTimedBash)) {
			return;
		}
		{
			std::scoped_lock lock(g_weaponHitLock);
			if (g_weaponHits.size() >= HOLY_QUEUE_MAX) {
				return;
			}
			const bool unarmed = !a_data.weapon || a_data.weapon->IsHandToHandMelee();
			WeaponHit hit;
			hit.aggressor = a_fighter->GetFormID();
			hit.target = a_target->GetFormID();
			hit.weapon = unarmed ? 0 : a_data.weapon->GetFormID();
			hit.bash = a_data.flags.any(RE::HitData::Flag::kBash, RE::HitData::Flag::kTimedBash);
			hit.game = g_hitGame.load();
			hit.total = a_totalBefore;
			hit.totalAfter = a_data.totalDamage;
			hit.physical = a_data.physicalDamage;
			// the sneak multiplier sits at +0x70 (CommonLib calls it bonusHealthDamageMult; the exe stores the sneak
			// multiplier there and sets the sneak flag when it is above 1)
			if (a_data.flags.any(RE::HitData::Flag::kSneakAttack) && a_data.bonusHealthDamageMult > 1.0f && a_data.bonusHealthDamageMult < 1000.0f) {
				hit.sneakMult = a_data.bonusHealthDamageMult;
			}
			if (a_data.flags.any(RE::HitData::Flag::kPowerAttack) && a_data.attackData) {
				const auto dm = a_data.attackData->data.damageMult;
				hit.power = true;
				hit.powerMult = dm > 0.01f && dm < 100.0f ? dm : 1.0f;
				hit.perkAtHit = a_perkAtHit;
			}
			g_weaponHits.push_back(hit);
		}
		if (!g_weaponTaskQueued.exchange(true)) {
			SKSE::GetTaskInterface()->AddTask([]() { ApplyWeaponHits(); });  // one shared task for every hit waiting
		}
	}

#ifdef SKYRIM_SUPPORT_AE
	struct WeaponHitHook
	{
		static void thunk(RE::AIProcess* a_targetProcess, RE::HitData& a_data)
		{
			const float before = a_data.totalDamage;  // the damage as Populate left it (the call may change it)
			auto        target = a_targetProcess ? a_targetProcess->GetUserData() : nullptr;
			// one decision before the call applies the hit: is the aggressor a fighter with an element meal (an actor
			// who just ate is seen within 250 ms), and its power attack perks (review 2026-11-24a R2, R2-A)
			const auto  fighter = target ? ElementFighterOf(a_data, target) : RE::NiPointer<RE::Actor>{};
			const float perk = PowerPerkAtHit(fighter.get(), target, a_data);
			func(a_targetProcess, a_data);
			WeaponHitCapture(target, a_data, before, perk, fighter.get());
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
#endif

	void InstallWeaponHitHook()
	{
#ifdef SKYRIM_SUPPORT_AE
		REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(37633, 38586), 0xFA };
		if (*reinterpret_cast<const std::uint8_t*>(target.address()) != 0xE8) {
			logger::error("Elements: the weapon hit call was not found in this game version; weapon element hits are off");
			return;
		}
		WeaponHitHook::func = target.write_call<5>(WeaponHitHook::thunk);
		logger::info("Elements: weapon hits watched");
#else
		logger::error("Elements: weapon element hits need the AE build; they are off");
#endif
	}

	void StartHolyWatch()
	{
		RepeatAlways(250, []() {
			if (!g_loadingSave) {
				HolyTick();
			}
			return true;
		});
	}

	// a save is loading or a new game starts: nothing of the game being left carries over
	void HolyReset()
	{
		{
			std::scoped_lock lock(g_holyLock);
			g_holyQueue.clear();
			g_holyDropped = 0;
		}
		g_holyTotals.clear();
		g_strikesLanded.clear();
		{
			std::scoped_lock lock(g_weaponHitLock);
			g_weaponHits.clear();
		}
		g_hitGame.fetch_add(1);
		g_elementMealPacked.store(-1, std::memory_order_relaxed);
		for (auto& id : g_elementFighters) {
			id.store(0, std::memory_order_relaxed);
		}
	}
