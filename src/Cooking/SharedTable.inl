	// ---------------------------------------------------------------------------------------------------------------
	// Shared Table (perk): followers who are following you (not told to wait) near you share each meal or drink you
	// eat, at the same level and duration. Each gets the effect best for them, by their best combat skill: weapons and
	// archery Mighty, magic Brainy, armor, Block and Alteration Hearty (never Sneaky); elemental dishes go to them as they are
	// (FC_Core.ShareMeal does the sharing). The list is taken here on the main thread when the player eats (EatWatcher),
	// and the script reads it with FC_Menu.SharedTableFollowers. The perk parts of the effects reach followers through
	// FC_SharedTableNPC (0xA5B), which SPID gives every NPC (FreeCooking_DISTR.ini).
	// ---------------------------------------------------------------------------------------------------------------

	constexpr float       UNITS_PER_METRE = 70.0f;  // Skyrim's units: about 70 to a metre

	std::mutex                g_sharedLock;
	std::vector<std::int32_t> g_sharedTable;  // form ID, then 0 Mighty / 1 Brainy / 2 Hearty, for each follower
	// the list taken for each meal still on its way to the eat script, by our eat effect it carries (oldest first): a
	// second food eaten before the first meal's script ran no longer changes who shares the first (Codex review
	// 2026-11-15, M4)
	struct SharedMeal
	{
		std::chrono::steady_clock::time_point when;
		std::vector<std::int32_t>             table;
	};
	std::unordered_map<RE::FormID, std::deque<SharedMeal>> g_sharedMeals;

	// how far followers may be and still share (game units): the Cooking settings page's Sharing distance
	// (FreeCooking.ini [SharedTable] fRangeMetres, default 50 metres)
	float SharedTableRange()
	{
		return FC::GetOptions().shareRangeMetres * UNITS_PER_METRE;
	}

	// 0 Mighty (weapons, archery), 1 Brainy (magic), 2 Hearty (Heavy and Light Armor, Block, Alteration); ties go to the
	// follower's attack skill (the higher of weapons and magic; weapons on a tie), then Hearty, then the other (his rule
	// 2026-11-22: "attack skill, hearty then the other skill"; "the attack skill depends on the follower")
	// (his choices 2026-11-22: Light Armor and Alteration, the armor school, Hearty, as in the potion rule)
	std::int32_t SharedTableCategory(RE::Actor* a_actor)
	{
		const auto av = [a_actor](RE::ActorValue a_value) { return a_actor->GetActorValue(a_value); };
		const float weapons = std::max({ av(RE::ActorValue::kOneHanded), av(RE::ActorValue::kTwoHanded), av(RE::ActorValue::kArchery) });
		const float magic = std::max({ av(RE::ActorValue::kDestruction), av(RE::ActorValue::kConjuration),
			av(RE::ActorValue::kIllusion), av(RE::ActorValue::kRestoration) });
		const float armor = std::max({ av(RE::ActorValue::kHeavyArmor), av(RE::ActorValue::kLightArmor), av(RE::ActorValue::kBlock), av(RE::ActorValue::kAlteration) });
		const bool  weaponsAttack = weapons >= magic;
		const float attack = weaponsAttack ? weapons : magic;  // never below the other attack skill
		if (attack >= armor) {
			return weaponsAttack ? 0 : 1;
		}
		return 2;
	}

	// main thread: who would share a meal eaten now (a_food: the food being eaten)
	void SnapshotSharedTable(const RE::AlchemyItem* a_food)
	{
		std::vector<std::int32_t> found;
		auto player = Player();
		auto lists = RE::ProcessLists::GetSingleton();
		auto npcKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13794);  // ActorTypeNPC: people (the perk goes to them)
		if (player && lists) {
			const auto range = SharedTableRange();
			const auto me = player->GetPosition();
			const auto myCell = player->GetParentCell();
			const bool inside = myCell && myCell->IsInteriorCell();
			for (const auto& h : lists->highActorHandles) {
				auto a = h.get();
				if (!a || a.get() == player || !a->IsPlayerTeammate()) {
					continue;
				}
				const char* skip = nullptr;
				if (a->IsDead() || !a->Is3DLoaded()) {
					skip = "dead or not loaded";
				} else if (a->GetActorValue(RE::ActorValue::kWaitingForPlayer) != 0.0f) {
					// following: told to wait sets WaitingForPlayer (1 wait, 2 sandbox with some follower mods)
					skip = "told to wait";
				} else if (npcKeyword && !a->HasKeyword(npcKeyword)) {
					skip = "not a person";
				} else {
					const auto cell = a->GetParentCell();
					if (!cell || (inside ? cell != myCell : (cell->IsInteriorCell() || a->GetWorldspace() != player->GetWorldspace()))) {
						skip = "elsewhere";
					} else if (a->GetPosition().GetDistance(me) > range) {
						skip = "too far";
					}
				}
				if (skip) {
					logger::info("Shared Table: teammate {:08X} left out ({})", a->GetFormID(), skip);
					continue;
				}
				found.push_back(static_cast<std::int32_t>(a->GetFormID()));
				found.push_back(SharedTableCategory(a.get()));
			}
		}
		logger::info("Shared Table: {} follower(s) at the table", found.size() / 2);
		std::scoped_lock lock(g_sharedLock);
		if (a_food) {
			const auto now = std::chrono::steady_clock::now();
			for (const auto effect : a_food->effects) {
				const auto base = effect ? effect->baseEffect : nullptr;
				const auto file = base ? base->GetFile(0) : nullptr;
				if (!file || file->GetFilename() != PLUGIN) {
					continue;  // only our eat effects run the eat script
				}
				auto& queue = g_sharedMeals[base->GetFormID()];
				std::erase_if(queue, [now](const SharedMeal& a_meal) { return now - a_meal.when > std::chrono::seconds(30); });
				if (queue.size() < 8) {
					queue.push_back({ now, found });
				}
			}
		}
		g_sharedTable = std::move(found);
	}

	// Papyrus: int[] FC_Menu.SharedTableFollowers(int effect, bool last) - the followers taken when the player ate the meal
	// whose eat effect this is (the oldest such meal waiting): form ID, then category; { -1 } when that meal was taken with
	// nobody at the table. None waiting yet: empty, so the script asks again; on its last try, the last list taken
	// (before, an empty table looked like "not taken yet", and the script then took the next meal's followers; Codex review
	// 2026-11-16, M9)
	std::vector<std::int32_t> PapyrusSharedTableFollowers(RE::StaticFunctionTag*, std::int32_t a_effect, bool a_last)
	{
		std::scoped_lock lock(g_sharedLock);
		if (const auto it = g_sharedMeals.find(static_cast<RE::FormID>(a_effect)); it != g_sharedMeals.end() && !it->second.empty()) {
			auto table = std::move(it->second.front().table);
			it->second.pop_front();
			return table.empty() ? std::vector<std::int32_t>{ -1 } : table;
		}
		return a_last ? g_sharedTable : std::vector<std::int32_t>{};
	}

	// ---- followers eat or drink with you: a vanilla idle (bread or tankard in hand) for a few seconds ----
	// idleEatingStandingStart / idleDrinkingStandingStart loop until stopped, so each one is stopped with IdleStop after
	// SHARE_IDLE_MS. Started only when the follower is free for it (not fighting, weapon out, sneaking, swimming, sitting,
	// riding, talking, in a scene, down or ragdolled); a follower still walking gets a few more tries while it stops.
	// A newer share restarts the clock (the older stop does nothing). Save/load: the followers mid-idle are saved
	// ('SIDL') and stopped soon after that save loads; the timers of the game being left are dropped (DropGameTimers),
	// and a new game or another save starts with none.
	constexpr std::uint32_t SHARE_IDLE_RECORD = 'SIDL';
	constexpr int           SHARE_IDLE_MS = 6000;
	constexpr int           SHARE_IDLE_CLEAR_MS = 500;
	constexpr int           SHARE_IDLE_RETRY_MS = 1500;
	constexpr int           SHARE_IDLE_TRIES = 4;
	constexpr RE::FormID    PERK_SHARED_TABLE = 0x857;

	std::unordered_map<RE::FormID, std::uint32_t> g_shareIdles;  // follower -> its newest idle (main thread only)
	std::uint32_t                                 g_shareIdleNext = 1;

	// why a follower can't take the idle right now ("" when it can); a_moving: only because it is still walking
	const char* ShareIdleBusy(RE::Actor* a_actor, bool& a_moving)
	{
		a_moving = false;
		if (!a_actor || a_actor->IsDead() || !a_actor->Is3DLoaded()) {
			return "not loaded";
		}
		if (a_actor->IsPlayerRef()) {
			// the player's own idle (option): third person only, the camera is never changed for it
			if (auto camera = RE::PlayerCamera::GetSingleton(); camera && camera->IsInFirstPerson()) {
				return "first person";
			}
		}
		if (a_actor->IsInCombat()) {
			return "in combat";
		}
		if (a_actor->IsWeaponDrawn()) {
			return "weapon out";
		}
		if (a_actor->IsSneaking()) {
			return "sneaking";
		}
		if (a_actor->IsSwimming()) {
			return "swimming";
		}
		if (a_actor->IsSitting() || a_actor->IsOnMount()) {
			return "sitting or riding";
		}
		if (a_actor->IsBleedingOut() || a_actor->IsUnconscious() || a_actor->IsStaggered() || a_actor->IsInKillMove() ||
			a_actor->IsInRagdollState() || a_actor->IsInMidair()) {
			return "down, staggered or in the air";
		}
		if (a_actor->GetCurrentScene()) {
			return "in a scene";
		}
		if (auto topics = RE::MenuTopicManager::GetSingleton()) {
			const auto handle = a_actor->GetHandle();
			if (topics->speaker == handle || topics->lastSpeaker == handle) {
				return "talking";
			}
		}
		// (the game's own test: ActorState's walking/running flags are the movement mode, set even when standing still)
		if (a_actor->IsMoving()) {
			a_moving = true;
			return "moving";
		}
		return "";
	}

	void StopShareIdle(RE::FormID a_follower, std::uint32_t a_token)
	{
		auto found = g_shareIdles.find(a_follower);
		if (found == g_shareIdles.end() || found->second != a_token) {
			return;  // a newer share has it now, or a load reset it
		}
		g_shareIdles.erase(found);
		auto actor = RE::TESForm::LookupByID<RE::Actor>(a_follower);
		// a fight, a sit-down or a ride already left the idle on its own: leave the new action alone
		if (actor && actor->Is3DLoaded() && !actor->IsDead() && !actor->IsInCombat() && !actor->IsWeaponDrawn() && !actor->IsSitting() &&
			!actor->IsOnMount()) {
			actor->NotifyAnimationGraph("IdleStop");
		}
	}

	// the start: the follower's current idle cleared first (IdleStop), then the eating or drinking idle half a second later.
	// A follower's AI keeps an idle of its own going while it follows you, and a new idle start is ignored over it
	// (Dine With Followers, Nexus SE 4310, does the same for its followers: idleStop, a short wait, then the start event)
	void StartShareIdle(RE::FormID a_follower, bool a_drink)
	{
		auto actor = RE::TESForm::LookupByID<RE::Actor>(a_follower);
		bool moving = false;
		if (const char* busy = ShareIdleBusy(actor, moving); *busy) {
			logger::info("Shared Table: {:08X} skips the {} idle ({})", a_follower, a_drink ? "drinking" : "eating", busy);
			return;
		}
		bool played = actor->NotifyAnimationGraph(a_drink ? "idleDrinkingStandingStart" : "idleEatingStandingStart");
		const char* how = "graph event";
		if (!played) {
			// the vanilla idle record through the AI's idle system, as Papyrus PlayIdle plays it (Skyrim.esm
			// IdleDrinkingStandingStart 0x640FC / IdleEatingStandingStart 0x64100)
			auto idle = RE::TESForm::LookupByID<RE::TESIdleForm>(a_drink ? 0x640FC : 0x64100);
			auto process = actor->currentProcess;
			played = idle && process && process->PlayIdle(actor, idle, nullptr);
			how = "idle record";
		}
		if (!played) {
			logger::info("Shared Table: {:08X} turned the {} idle down", a_follower, a_drink ? "drinking" : "eating");
			return;
		}
		logger::info("Shared Table: {:08X} plays the {} idle ({})", a_follower, a_drink ? "drinking" : "eating", how);
		const auto token = g_shareIdleNext++;
		g_shareIdles[a_follower] = token;
		After(SHARE_IDLE_MS, [a_follower, token]() { StopShareIdle(a_follower, token); });
	}

	void TryShareIdle(RE::FormID a_follower, bool a_drink, int a_triesLeft)
	{
		auto actor = RE::TESForm::LookupByID<RE::Actor>(a_follower);
		bool moving = false;
		if (const char* busy = ShareIdleBusy(actor, moving); *busy) {
			if (moving && a_triesLeft > 1) {
				After(SHARE_IDLE_RETRY_MS, [a_follower, a_drink, a_triesLeft]() { TryShareIdle(a_follower, a_drink, a_triesLeft - 1); });
			} else {
				logger::info("Shared Table: {:08X} skips the {} idle ({})", a_follower, a_drink ? "drinking" : "eating", busy);
			}
			return;
		}
		actor->NotifyAnimationGraph("IdleStop");
		After(SHARE_IDLE_CLEAR_MS, [a_follower, a_drink]() { StartShareIdle(a_follower, a_drink); });
	}

	// a drink (tankard) or food (bread): our pool dishes by the dish they look like, then the recipe sheet's drink flag
	// (mead, ale, wine, teas, juice); anything else counts as food
	bool SharedTableIsDrink(RE::TESForm* a_item)
	{
		RE::FormID id = a_item ? a_item->GetFormID() : 0;
		if (const auto file = a_item ? a_item->GetFile(0) : nullptr; file && file->GetFilename() == PLUGIN) {
			std::scoped_lock lock(g_dishLock);
			if (const auto it = g_dishDefs.find(a_item->GetLocalFormID()); it != g_dishDefs.end()) {
				id = it->second.base;
			}
		}
		const auto it = g_dishByBase.find(id);
		return it != g_dishByBase.end() && Int(std::format("dish.{}.dr", it->second)) == 1;
	}

	// main thread, when the player eats or drinks anything (EatWatcher, after SnapshotSharedTable): with Shared Table,
	// the followers at the table eat or drink along, whether or not the food has an effect to share
	void SharedTableIdles(RE::TESForm* a_item)
	{
		if (!HasTreePerk(PERK_SHARED_TABLE)) {
			return;
		}
		const bool drink = SharedTableIsDrink(a_item);
		std::vector<std::int32_t> table;
		{
			std::scoped_lock lock(g_sharedLock);
			table = g_sharedTable;
		}
		for (std::size_t i = 0; i + 1 < table.size(); i += 2) {
			const auto follower = static_cast<RE::FormID>(table[i]);
			logger::info("Shared Table: {:08X} {} along", follower, drink ? "drinks" : "eats");
			// once the game runs again (eating from the inventory: after the menu closes)
			After(0, [follower, drink]() { TryShareIdle(follower, drink, SHARE_IDLE_TRIES); });
		}
	}

	// the player's own eating or drinking idle (Settings, off by default, his call 2026-11-14: the idle's bread or tankard
	// doesn't match the dish): the same vanilla idles and the same rules as the followers, for Cooking Skill Overhaul's dishes and
	// drinks (our plugin's, and the modded foods our rules cook). Eating Animations and Sounds animates food itself (our
	// pool dishes too: they take its keywords from the dish they look like, DishPool.inl): with it installed ours steps
	// aside, two eating animations at once would clash
	void PlayerEatIdle(RE::TESForm* a_item)
	{
		if (!a_item || !FC::GetOptions().playerEatIdle || FC::EatingAnimationsInstalled()) {
			return;
		}
		const auto file = a_item->GetFile(0);
		if (!(file && file->GetFilename() == PLUGIN) && !g_patchCooked.contains(a_item->GetFormID())) {
			return;
		}
		const bool drink = SharedTableIsDrink(a_item);
		const RE::FormID player = 0x14;
		logger::info("You {} (eating idle option)", drink ? "drink" : "eat");
		// once the game runs again (eating from the inventory: after the menu closes)
		After(0, [player, drink]() { TryShareIdle(player, drink, SHARE_IDLE_TRIES); });
	}

	// Papyrus: FC_Menu.ShareIdle(int follower, int drink) - the follower eats (0) or drinks (1) with you
	void PapyrusShareIdle(RE::StaticFunctionTag*, std::int32_t a_follower, std::int32_t a_drink)
	{
		const auto follower = static_cast<RE::FormID>(a_follower);
		const bool drink = a_drink != 0;
		logger::info("Shared Table: {:08X} shares the {}", follower, drink ? "drink" : "meal");
		// on the main thread, once the game runs again (eating from the inventory: after the menu closes)
		After(0, [follower, drink]() { TryShareIdle(follower, drink, SHARE_IDLE_TRIES); });
	}

	void ShareIdleSave(SKSE::SerializationInterface* a_intfc)
	{
		if (!a_intfc->OpenRecord(SHARE_IDLE_RECORD, 1)) {
			return;
		}
		a_intfc->WriteRecordData(static_cast<std::uint32_t>(g_shareIdles.size()));
		for (const auto& [follower, token] : g_shareIdles) {
			a_intfc->WriteRecordData(follower);
		}
	}

	// a save made mid-idle: those followers are stopped soon after it loads (their idle would loop on)
	bool ShareIdleLoadRecord(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, std::uint32_t a_version)
	{
		if (a_type != SHARE_IDLE_RECORD) {
			return false;
		}
		if (a_version != 1) {
			return true;
		}
		std::uint32_t count = 0;
		if (!ReadAll(a_intfc, count)) {
			return true;
		}
		for (std::uint32_t k = 0; k < count; ++k) {
			RE::FormID saved = 0, follower = 0;
			if (!ReadAll(a_intfc, saved)) {
				break;
			}
			if (a_intfc->ResolveFormID(saved, follower)) {
				const auto token = g_shareIdleNext++;
				g_shareIdles[follower] = token;
				After(1000, [follower, token]() { StopShareIdle(follower, token); });
			}
		}
		return true;
	}

	// a new game or another save: no meal of the old game is shared there
	void SharedTableReset()
	{
		{
			std::scoped_lock lock(g_sharedLock);
			g_sharedTable.clear();
			g_sharedMeals.clear();
		}
		g_shareIdles.clear();
	}
