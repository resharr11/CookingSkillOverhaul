// Part of CookingMenu.cpp: included in order inside its anonymous namespace (not compiled on its own).
// ---------------------------------------------------------------------------------------------------------------
// Other survival mods (his call 2026-11-14, evening): every Survival switch starts on whatever survival mod runs, and a
// switch on means Cooking Skill Overhaul applies that feature to the mod's own values; a player who wants the mod's own behaviour
// for a feature turns that switch off. Each mod's values were read from its own scripts (memory: needs-mods-data):
// they all change their needs a little at a time from the current value, so a value we lower stays lowered.
// Doors used, the mod authors' own wherever they exist (his rule): Frostfall's public FrostUtil API, SunHelm's,
// Last Seed's and iNeed's own decrease functions (FC_SunHelmBridge, FC_LastSeedBridge, FC_INeedBridge scripts, called
// only while that mod runs), Survival Mode Improved's API (FC_Core). Realistic Needs and Diseases has no functions, so
// its own globals are lowered (its food does the same, rnd_eatfood.psc).
// Which mod a need belongs to (NeedOwner): a running needs mod first (SunHelm, Last Seed, iNeed, Realistic Needs),
// then Survival Mode Improved, then Survival Mode. Cold: Frostfall, then SunHelm, then the two Survival ones.
// Save/load: nothing of ours; every value read here is the other mod's own, saved with the game.
// ---------------------------------------------------------------------------------------------------------------

	// the numbers are shared with Papyrus (FC_Menu.NeedOwner): don't renumber
	enum class NeedMod : std::int32_t
	{
		kNone = 0,
		kSurvival = 1,
		kSMI = 2,  // Survival Mode Improved, and Starfrost on top of it
		kSunHelm = 3,
		kLastSeed = 4,
		kINeed = 5,
		kRND = 6,
		kFrostfall = 7,
	};
	enum class Need : std::int32_t
	{
		kHunger = 0,
		kThirst = 1,
		kTiredness = 2,
		kCold = 3,
	};

	struct OtherModForms
	{
		bool           looked = false;
		bool           smi = false;
		// SunHelm (SunHelmSurvival.esp)
		RE::TESGlobal* shOn = nullptr;           // _SHEnabled 0x02EB63 (1 while it runs)
		RE::TESGlobal* shOff[4] = {};            // _SH<Hunger/Thirst/Fatigue/Cold>ShouldBeDisabled 0x752707-0x75270A
		RE::TESGlobal* shColdForceOff = nullptr;  // _SHForceDisableCold 0x83132C
		RE::TESGlobal* shRestPerHour = nullptr;   // _SHFatigueSleepRestoreAmount 0x812D28 (50)
		RE::TESGlobal* shBedroll = nullptr;       // _SHBedrollSleep 0x989769 (1: this sleep is in a bedroll; SunHelm sets it
		                                          // when a bedroll is used and clears it when the sleep ends)
		RE::TESGlobal* shFatigueNow = nullptr;    // _SHCurrentFatigueLevel 0x021E3F (higher is more tired, 0 at best)
		RE::TESGlobal* shSlumber = nullptr;       // _SH_PerkRank_Slumber 0x863D30 (its Slumber perk's rank: 0, 1 or 2)
		// Last Seed (LastSeed.esp)
		RE::TESGlobal* lsRunning = nullptr;     // LastSeedRunning 0x00B162 (2 = running)
		RE::TESGlobal* lsSystem[3] = {};        // _Seed_Setting_SystemEnabled_Hunger/Thirst/Fatigue 0x007073-0x007075 (2 = on)
		RE::TESGlobal* lsFatigue = nullptr;     // _Seed_AttributeFatigue 0x0029CF (0..120, higher is worse)
		RE::TESGlobal* lsSleepByPlace = nullptr;  // _Seed_Setting_SleepAffectedByLocation 0x1BDD1F (2 = on)
		RE::TESGlobal* lsOutdoorsman = nullptr;   // Provisioning_PerkRank_Outdoorsman 0x47DAAC (0: no Outdoorsman perk)
		RE::TESGlobal* lsSleepByNeeds = nullptr;  // _Seed_Setting_SleepAffectedByNeeds 0x1BDD1E (2 = on)
		RE::TESGlobal* lsHungerLevel = nullptr;   // _Seed_HungerLevel 0x00F7D6 (0 Well Fed .. 5 Starving; 2 or more: Hungry)
		RE::TESGlobal* lsThirstLevel = nullptr;   // _Seed_ThirstLevel 0x00F7D7 (the same for thirst)
		// iNeed (iNeed.esp)
		RE::TESQuest*  ineedQuest = nullptr;    // _SNQuest 0x000D62
		RE::BGSListForm* ineedBedrolls = nullptr;  // _SNBedRollList 0x059D46 (its activate perk marks a bedroll sleep by it)
		RE::BGSListForm* ineedTents = nullptr;     // _SNTentList 0x080A40 (a tent within 384 units: no bedroll penalty)
		RE::TESGlobal* ineedHungerPenalty = nullptr;  // _SNHungerPenalty 0x0012DB (-2..3: its sleep multiplier, _SNNeeds.NeedsCheck)
		RE::TESGlobal* ineedThirstPenalty = nullptr;  // _SNThirstPenalty 0x004378
		// Realistic Needs and Diseases (or Nirnfeel, which carries it with the same local IDs)
		RE::TESGlobal* rndState = nullptr;      // RND_State 0x012C4C (1 = running)
		// Frostfall (Frostfall.esp)
		RE::TESGlobal* ffRunning = nullptr;     // FrostfallRunning 0x06DCFB (2 = running)
		RE::TESGlobal* ffExposureOn = nullptr;  // _Frost_Setting_ExposureOn 0x068113 (2 = on)
		RE::BGSKeyword* ffColdWaterPotion = nullptr;  // _Frost_ColdWaterPotionKW 0x01D433
		std::array<RE::BGSKeyword*, 4> ffFrostbite{};  // _Frost_Frostbite<Body/Hands/Feet/Head>KW 0x069153-0x069156
	};
	OtherModForms g_other;

	bool SurvivalOn();  // Cooking/Survival.inl

	void OtherModsLookup()
	{
		auto& f = g_other;
		if (f.looked) {
			return;
		}
		f.looked = true;
		auto data = RE::TESDataHandler::GetSingleton();
		if (!data) {
			return;
		}
		const auto loaded = [&](const char* a_name) {
			return data->LookupLoadedModByName(a_name) || data->LookupLoadedLightModByName(a_name);
		};
		f.smi = loaded("SurvivalModeImproved.esp");
		constexpr const char* SH = "SunHelmSurvival.esp";
		f.shOn = data->LookupForm<RE::TESGlobal>(0x02EB63, SH);
		for (int i = 0; i < 4; ++i) {
			f.shOff[i] = data->LookupForm<RE::TESGlobal>(0x752707 + i, SH);
		}
		f.shColdForceOff = data->LookupForm<RE::TESGlobal>(0x83132C, SH);
		f.shRestPerHour = data->LookupForm<RE::TESGlobal>(0x812D28, SH);
		f.shBedroll = data->LookupForm<RE::TESGlobal>(0x989769, SH);
		f.shFatigueNow = data->LookupForm<RE::TESGlobal>(0x021E3F, SH);
		f.shSlumber = data->LookupForm<RE::TESGlobal>(0x863D30, SH);
		constexpr const char* LS = "LastSeed.esp";
		f.lsRunning = data->LookupForm<RE::TESGlobal>(0x00B162, LS);
		for (int i = 0; i < 3; ++i) {
			f.lsSystem[i] = data->LookupForm<RE::TESGlobal>(0x007073 + i, LS);
		}
		f.lsFatigue = data->LookupForm<RE::TESGlobal>(0x0029CF, LS);
		f.lsSleepByPlace = data->LookupForm<RE::TESGlobal>(0x1BDD1F, LS);
		f.lsOutdoorsman = data->LookupForm<RE::TESGlobal>(0x47DAAC, LS);
		f.lsSleepByNeeds = data->LookupForm<RE::TESGlobal>(0x1BDD1E, LS);
		f.lsHungerLevel = data->LookupForm<RE::TESGlobal>(0x00F7D6, LS);
		f.lsThirstLevel = data->LookupForm<RE::TESGlobal>(0x00F7D7, LS);
		f.ineedQuest = data->LookupForm<RE::TESQuest>(0x000D62, "iNeed.esp");
		f.ineedBedrolls = data->LookupForm<RE::BGSListForm>(0x059D46, "iNeed.esp");
		f.ineedTents = data->LookupForm<RE::BGSListForm>(0x080A40, "iNeed.esp");
		f.ineedHungerPenalty = data->LookupForm<RE::TESGlobal>(0x0012DB, "iNeed.esp");
		f.ineedThirstPenalty = data->LookupForm<RE::TESGlobal>(0x004378, "iNeed.esp");
		f.rndState = data->LookupForm<RE::TESGlobal>(0x012C4C, "RealisticNeedsandDiseases.esp");
		if (!f.rndState) {
			f.rndState = data->LookupForm<RE::TESGlobal>(0x012C4C, "MVC Nirnfeel Immersion.esp");
		}
		constexpr const char* FF = "Frostfall.esp";
		f.ffRunning = data->LookupForm<RE::TESGlobal>(0x06DCFB, FF);
		f.ffExposureOn = data->LookupForm<RE::TESGlobal>(0x068113, FF);
		f.ffColdWaterPotion = data->LookupForm<RE::BGSKeyword>(0x01D433, FF);
		for (int i = 0; i < 4; ++i) {
			f.ffFrostbite[i] = data->LookupForm<RE::BGSKeyword>(0x069153 + i, FF);
		}
		logger::info("Other survival mods: SunHelm {}, Last Seed {}, iNeed {}, Realistic Needs {}, Frostfall {}, Survival Mode Improved {}",
			f.shOn ? "found" : "-", f.lsRunning ? "found" : "-", f.ineedQuest ? "found" : "-", f.rndState ? "found" : "-",
			f.ffRunning ? "found" : "-", f.smi ? "found" : "-");
	}

	bool AtLeast(const RE::TESGlobal* a_global, float a_value) { return a_global && a_global->value >= a_value; }

	// SunHelm runs this need (it is started and the need isn't switched off in its own menu)
	bool SunHelmRuns(int a_need)
	{
		const auto& f = g_other;
		if (!AtLeast(f.shOn, 1.0f) || AtLeast(f.shOff[a_need], 1.0f)) {
			return false;
		}
		return a_need != 3 || !AtLeast(f.shColdForceOff, 1.0f);
	}

	bool LastSeedRuns(int a_need)
	{
		const auto& f = g_other;
		return AtLeast(f.lsRunning, 2.0f) && (!f.lsSystem[a_need] || AtLeast(f.lsSystem[a_need], 2.0f));
	}

	bool INeedRuns() { return g_other.ineedQuest && g_other.ineedQuest->IsRunning(); }
	bool RNDRuns() { return AtLeast(g_other.rndState, 1.0f); }
	bool FrostfallRuns() { return AtLeast(g_other.ffRunning, 2.0f) && (!g_other.ffExposureOn || AtLeast(g_other.ffExposureOn, 2.0f)); }

	// which mod runs this need right now (kNone: nothing does, so Cooking Skill Overhaul's feature has nothing to work on)
	NeedMod NeedOwner(Need a_need)
	{
		OtherModsLookup();
		const auto n = static_cast<int>(a_need);
		if (a_need == Need::kCold) {
			if (FrostfallRuns()) {
				return NeedMod::kFrostfall;
			}
			if (SunHelmRuns(n)) {
				return NeedMod::kSunHelm;
			}
		} else {
			if (SunHelmRuns(n)) {
				return NeedMod::kSunHelm;
			}
			if (LastSeedRuns(n)) {
				return NeedMod::kLastSeed;
			}
			if (INeedRuns()) {
				return NeedMod::kINeed;
			}
			if (RNDRuns()) {
				return NeedMod::kRND;
			}
			if (a_need == Need::kThirst) {
				return NeedMod::kNone;  // Survival Mode has no thirst: Cooking Skill Overhaul's own (Cooking/Thirst.inl)
			}
		}
		if (SurvivalOn()) {
			return g_other.smi ? NeedMod::kSMI : NeedMod::kSurvival;
		}
		return NeedMod::kNone;
	}

	// Survival Mode or another survival mod is running (Hearty's regeneration, the cooking-time effects)
	bool AnySurvivalRuns()
	{
		for (int n = 0; n < 4; ++n) {
			if (NeedOwner(static_cast<Need>(n)) != NeedMod::kNone) {
				return true;
			}
		}
		return false;
	}

	// a survival system is installed at all (Survival Mode or another survival mod's plugin): cooking then puts the
	// hunger, camp rest and dragon effects on dishes, and they find their mod when eaten
	bool AnySurvivalInstalled()
	{
		OtherModsLookup();
		const auto& f = g_other;
		auto data = RE::TESDataHandler::GetSingleton();
		const bool survival = data && data->LookupLoadedLightModByName("ccQDRSSE001-SurvivalMode.esl");
		return survival || f.shOn || f.lsRunning || f.ineedQuest || f.rndState || f.ffRunning;
	}

// ---------------------------------------------------------------------------------------------------------------
// Spicy and Frostfall's cold water: every cold-water effect of Frostfall (_Frost_ColdWaterSpell, frigid water's
// blackout timer included) runs only while the player has no active effect carrying _Frost_ColdWaterPotionKW: its own
// cold-water potion's door (read from Frostfall.esp 2026-11-14). With the "Spicy protects from freezing water" switch on,
// our 12 Spicy effects carry that keyword, so Frostfall itself skips cold water while Spicy lasts. The keyword is added
// to the effects in memory only (nothing written to Data) and taken off again when the switch goes off.
// Frostfall's frostbite (his call 2026-11-14: its version of Survival's Frostbitten): with "Flavours clear afflictions"
// on, a running Spicy meal, baked dish or drink clears it, like Frostbitten. Frostfall keeps no other record of it (it
// asks HasEffectKeyword each time), so ending its effects is all it takes.
// ---------------------------------------------------------------------------------------------------------------

	constexpr RE::FormID SPICY_ALL[] = { 0x914, 0x915, 0x916, 0x917, 0x934, 0x935, 0x936, 0x937, 0x9C4, 0x9C5, 0x9C6, 0x9C7 };
	bool g_frostbiteHelpShown = false;

	const std::vector<RE::EffectSetting*>& SpicyEffects()
	{
		static std::vector<RE::EffectSetting*> list;
		static bool looked = false;
		if (!looked) {
			looked = true;
			for (auto id : SPICY_ALL) {
				if (auto effect = PluginForm<RE::EffectSetting>(id, PLUGIN)) {
					list.push_back(effect);
				}
			}
		}
		return list;
	}

	// Spicy and SunHelm's freezing water (his call 2026-11-15: the "Spicy protects from freezing water" switch applies to
	// SunHelm too). SunHelm's own door (_SHHeatDetection.CheckForFreezingWater, read from its source): no freezing water
	// while the player has an effect with the MagicFlameCloak keyword (Update.esm 0x2EDA), as with a Flame Cloak spell.
	// So while SunHelm runs the cold, the switch is on and a Spicy effect runs, the player carries FC_SpicySunHelmWard
	// (FreeCooking.esp 0xAAB), an ability whose hidden effect FC_Fx_SpicySunHelmWard (0xAAA) has that keyword and does
	// nothing else. The keyword is NOT put on the Spicy effects themselves: Flame Cloak, Ancestor's Wrath and Zargo's cloak
	// "dispel effects with these keywords" (Update.esm, checked 2026-11-15), so casting one would have dispelled the meal's
	// Spicy effect (his question). If a cloak ever dispels our ability's effect, the tick gives the ability back.
	// Side effect, SunHelm's own rule for a flame cloak: +15 warmth where it counts cloaks. No damage aura (the effect is
	// a hidden script-type effect with no script). Save/load: the ability is saved like any spell; the tick (every
	// second, also right after a load) adds or removes it to match.
	bool g_shSpicyWard = false;  // the ward's effect runs right now (Survival.inl reads it for the HUD)
	void SunHelmWaterTick(RE::PlayerCharacter* a_player)
	{
		static RE::SpellItem*      ward = nullptr;
		static RE::EffectSetting*  wardFx = nullptr;
		static bool looked = false;
		if (!looked) {
			looked = true;
			ward = PluginForm<RE::SpellItem>(0xAAB, PLUGIN);
			wardFx = PluginForm<RE::EffectSetting>(0xAAA, PLUGIN);
			logger::info("SunHelm freezing water ward: {}", ward && wardFx ? "ready" : "MISSING FC_SpicySunHelmWard");
		}
		g_shSpicyWard = false;
		if (!ward || !wardFx || !a_player) {
			return;
		}
		bool spicy = false;
		for (auto effect : SpicyEffects()) {
			if (a_player->HasMagicEffect(effect)) {
				spicy = true;
				break;
			}
		}
		const bool want = spicy && FC::FeatureSwitch(FC::Feature::kSpicyWater) && NeedOwner(Need::kCold) == NeedMod::kSunHelm;
		const bool has = a_player->HasSpell(ward);
		if (want) {
			if (!has) {
				a_player->AddSpell(ward);
				logger::info("SunHelm: Spicy keeps its freezing water away (ward on)");
			} else if (!a_player->HasMagicEffect(wardFx)) {
				a_player->RemoveSpell(ward);  // its effect was dispelled (a flame cloak): give it back
				a_player->AddSpell(ward);
				logger::info("SunHelm: Spicy ward given back after a dispel");
			}
			g_shSpicyWard = true;
		} else if (has) {
			a_player->RemoveSpell(ward);
			logger::info("SunHelm: Spicy ward off");
		}
	}

	void FrostfallTick(RE::PlayerCharacter* a_player)
	{
		OtherModsLookup();
		const auto& f = g_other;
		if (!f.ffRunning) {
			return;  // Frostfall isn't installed
		}
		// the water keyword follows the switch
		static int applied = -1;
		const int want = FC::FeatureSwitch(FC::Feature::kSpicyWater) ? 1 : 0;
		if (f.ffColdWaterPotion && want != applied) {
			applied = want;
			for (auto effect : SpicyEffects()) {
				if (want) {
					if (!effect->HasKeyword(f.ffColdWaterPotion)) {
						effect->AddKeyword(f.ffColdWaterPotion);
					}
				} else {
					effect->RemoveKeyword(f.ffColdWaterPotion);
				}
			}
			logger::info("Frostfall: Spicy {} its cold water", want ? "keeps away" : "no longer keeps away");
		}
		// frostbite
		if (!a_player || !FrostfallRuns() || !FC::FeatureSwitch(FC::Feature::kAfflictions)) {
			return;
		}
		bool spicy = false, drink = false;
		for (std::size_t i = 0; i < SpicyEffects().size(); ++i) {
			if (a_player->HasMagicEffect(SpicyEffects()[i])) {
				spicy = true;
				const auto id = SpicyEffects()[i]->GetLocalFormID();
				drink = id >= 0x934 && id <= 0x937;
				break;
			}
		}
		auto list = a_player->GetActiveEffectList();
		if (!list) {
			return;
		}
		std::vector<RE::ActiveEffect*> frostbite;
		for (auto effect : *list) {
			auto base = effect ? effect->GetBaseObject() : nullptr;
			if (!base) {
				continue;
			}
			for (auto keyword : f.ffFrostbite) {
				if (keyword && base->HasKeyword(keyword)) {
					frostbite.push_back(effect);
					break;
				}
			}
		}
		if (frostbite.empty()) {
			return;
		}
		if (!spicy) {
			if (!g_frostbiteHelpShown) {
				g_frostbiteHelpShown = true;
				SKSE::GetTaskInterface()->AddTask([]() {
					RE::DebugMessageBox(FC::Tr("Frostbite lasts a while, even after you have warmed up. A Spicy dish clears it sooner: a meal, a baked dish and a brewed drink all count."));
				});
			}
			return;
		}
		for (auto effect : frostbite) {
			effect->Dispel(true);
		}
		RE::SendHUDMessage::ShowHUDMessage(FC::Tr(drink ? "The Spicy drink warms your frozen limbs. Your frostbite is gone." :
		                                                  "The Spicy meal warms your frozen limbs. Your frostbite is gone."));
		logger::info("Frostfall: {} frostbite effect(s) cleared by Spicy", frostbite.size());
	}

// (SunHelmDishesTick, removed 2026-11-15: it set SunHelm's _SHFoodIgnoreKeyword on every dish from the hunger switch
// alone and fought NeedsApplySlot, which sets the same keyword per dish; Cooking/Needs.inl now does it all)

	// Papyrus: FC_Menu.NeedOwner(need) - 0 hunger, 1 thirst, 2 tiredness, 3 cold; returns NeedMod
	std::int32_t PapyrusNeedOwner(RE::StaticFunctionTag*, std::int32_t a_need)
	{
		if (a_need < 0 || a_need > 3) {
			return 0;
		}
		return static_cast<std::int32_t>(NeedOwner(static_cast<Need>(a_need)));
	}

	// Papyrus: FC_Menu.SurvivalInstalled() - Survival Mode or another survival mod is installed
	bool PapyrusSurvivalInstalled(RE::StaticFunctionTag*)
	{
		return AnySurvivalInstalled();
	}
