// Part of CookingMenu.cpp: included in order inside its anonymous namespace (not compiled on its own).
// Cooking Skill Overhaul and Camping (the Anniversary Edition Creation). Camping's camp is single use, so Cooking Skill Overhaul gives it
// nothing to carry: its campfire grills and roasts over the open fire (Cooking/Hooks.inl, by Camping's keyword), and
// stews, soups and drinks need a real cooking pot. Pot cooking at a camp comes from Solid Ground Camping (CC Camping
// Improved), whose camps set a pot beside the fire; Cooking Skill Overhaul only recognizes that pot.
// (Until 2026-11-11 Cooking Skill Overhaul had its own cookware upgrade for the Camping Supplies: a pot set by every camp's fire, a
// forge recipe and a tutorial. Retired: FreeCooking.esp's 0x985, 0xA02, 0xA03, 0xA04 and 0xA59 are gone and never reused.
// Older saves may hold its 'CHNT' record: the save loader skips records it doesn't know.)

	// Camping (Anniversary Edition): its campfire cooks over an open fire. The keyword lives in Update.esm, so it is
	// always there; the campsite's own records are only looked up when Camping is installed.
	constexpr RE::FormID KEYWORD_CAMPFIRE = 0x314B;   // Update.esm: Camping_CampfireCookingShared
	constexpr const char* CAMPING = "ccqdrsse002-firewood.esl";
	constexpr RE::FormID CAMP_SUPPLIES = 0x801;       // ccQDRSSE002_CampingSupplies (used like a potion)
	constexpr RE::FormID CAMP_FIRE = 0x804;           // ccQDRSSE002_CampfireFurn

	constexpr const char* SOLID_GROUND_ESP = "SolidGroundCamping.esp";
	constexpr RE::FormID SOLID_GROUND_POT = 0x985;    // its camp's cooking pot, set beside the camp's fire

	RE::TESForm* CampingForm(RE::FormID a_id)
	{
		return PluginForm(a_id, CAMPING);  // (looked for once: Core/Common.inl)
	}

	// (filtered to the one item: GetInventoryCounts with no filter copies the whole inventory into a new map, every
	// entry of it, on each call; CommonLibSSE TESObjectREFR.cpp)
	std::int32_t ItemCount(RE::TESObjectREFR* a_ref, RE::TESForm* a_item)
	{
		std::int32_t have = 0;
		if (a_ref && a_item) {
			for (const auto& [item, count] : a_ref->GetInventoryCounts([a_item](RE::TESBoundObject& a_object) { return &a_object == a_item; })) {
				if (item == a_item) {
					have = count;
				}
			}
		}
		return have;
	}

	// Solid Ground Camping's camp pot
	bool IsCampPot(const RE::TESForm* a_base)
	{
		auto pot = a_base ? PluginForm(SOLID_GROUND_POT, SOLID_GROUND_ESP) : nullptr;
		return pot && a_base == pot;
	}

	// a camp's cooking pot over a cold fire: nothing cooks there (Solid Ground Camping says why and closes the menu).
	// Solid Ground knows its own pot: it is asked (Camp_PotUnlit, from its published records). The old search for a burning
	// Camping campfire within 250 units refused pots Solid Ground's layout sliders put further out, and could count
	// another camp's fire (Codex whole-system review 2026-11-17). Only a Solid Ground too old to answer gets that search.
	// Asked by the cooking screen (Cooking/Hooks.inl), which then stays shut
	bool CampPotUnlit(RE::TESObjectREFR* a_pot)
	{
		auto tes = RE::TES::GetSingleton();
		auto fireBase = CampingForm(CAMP_FIRE);
		if (!a_pot || !tes || !IsCampPot(a_pot->GetBaseObject())) {
			return false;
		}
		using PotUnlitFn = int (*)(RE::FormID);
		static const auto ask = []() -> PotUnlitFn {
			auto sg = REX::W32::GetModuleHandleW(L"SolidGroundCamping.dll");
			return sg ? reinterpret_cast<PotUnlitFn>(REX::W32::GetProcAddress(sg, "Camp_PotUnlit")) : nullptr;
		}();
		if (ask) {
			// Solid Ground answers for every pot of its kind: -1 means no camp of its recorded this pot, so it is no camp
			// pot at all and the cooking screen stays shut (the game's own crafting menu stays). No search for a fire
			// then: that guess is what Solid Ground's records replace (Codex review round 14, 3)
			const int answer = ask(a_pot->GetFormID());
			logger::info("Camping: cooking pot used, {} (Solid Ground's answer)", answer < 0 ? "no camp's pot" : answer ? "fire not lit" : "fire burning");
			return answer != 0;
		}
		bool burning = false;
		if (fireBase) {
			tes->ForEachReferenceInRange(a_pot, 250.0f, [&](RE::TESObjectREFR* a_ref) {
				if (a_ref && a_ref->GetBaseObject() == fireBase && !a_ref->IsDisabled() && !a_ref->IsMarkedForDeletion()) {
					burning = true;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
		}
		logger::info("Camping: cooking pot used, fire {}", burning ? "burning" : "not lit");
		return !burning;
	}

	// at kDataLoaded
	void CampingSetup()
	{
		g_gameDataLoaded = true;  // the load order is final: plugin lookups are kept from now on (Core/Common.inl)
		logger::info("Camping: campfires grill and roast; {}", PluginForm(SOLID_GROUND_POT, SOLID_GROUND_ESP) ?
			"Solid Ground Camping's camp pot cooks everything else" : "no camp pot (Solid Ground Camping not installed)");
	}
