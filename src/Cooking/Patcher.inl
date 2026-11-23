	// ---------------------------------------------------------------------------------------------------------------
	// The modded-foods patcher (rules settled 2026-11-13, tested offline in tools\patcher). At kDataLoaded it reads every
	// food and alchemy ingredient other mods add, decides what each one is in Cooking Skill Overhaul's rules (Patcher/Classifier,
	// Patcher/Dishes: plain code, no game types) and adds it to the pot rules (g_int / g_str, which the scripts read
	// through FC_Menu.RuleInt / RuleText) and to the cooking screen's recipe list (g_dishes). Nothing is written to any
	// record and nothing to Data: a report goes to the SKSE log folder. Vanilla, the DLCs, Creations, the Anniversary
	// Edition's _ResourcePack.esl and FreeCooking.esp count as vanilla: only the hand-made table in data.json decides
	// them, and the patcher never touches them (an item the table lacks is added to the table by hand).
	// ---------------------------------------------------------------------------------------------------------------

	std::map<std::int32_t, std::string> g_dishSaveKeys;  // dish index -> "m:<plugin>|<local ID>", only for dishes the patcher added

	// ---- what the patcher page shows (filled once at kDataLoaded, read-only after) and the edit files
	// The player's edits live in My Games\Skyrim Special Edition\SKSE (beside our log), never in Data: one file for every
	// save, used at the next game start. Food mods can ship ready-made edits in Data\SKSE\Plugins\FreeCooking\Patches.
	std::vector<FC::PatchRow>                        g_patchRows;
	std::vector<std::pair<std::string, std::string>> g_patchChoices;  // (name, key) of every ingredient a slot can name
	std::vector<std::string>                         g_patchGroups;
	std::map<std::string, std::vector<std::string>>  g_groupFlavours;  // group -> the flavours its items can have, its own first ("" = none)
	FCPatch::EditSet                                 g_patchFileEdits;  // what food mods' patch files set (the page starts from it)
	std::atomic<bool>                                g_patcherRan{ false };
	// recipes as the pot reads them, so a recipe can't be given to two dishes: the pot would only ever make the first one
	std::unordered_map<std::string, std::vector<std::string>>                     g_itemSlotKeys;   // FCPatch::ItemKey -> the rule keys a slot naming it takes
	std::unordered_map<std::string, std::vector<std::string>>                     g_groupSlotKeys;  // group or category -> the rule keys "any ..." takes
	// where it is made + recipe -> (dish name, page row key; "" for ours). The place in front ("o" an oven dish, "p" the
	// rest): a mod's oven recipe with the same ingredients as a pot dish is its own dish, not a variant (Codex review
	// 2026-11-15: it was hidden and the oven could make nothing in its place). Each dish is listed under its own place
	// only, so a dish at the other station never names itself as the clash (before: the place was a separate set and the
	// list held both; Codex review 2026-11-16 afternoon, M9)
	std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> g_recipeSigs;
	std::string PlaceSig(bool a_oven, const std::string& a_sig) { return (a_oven ? "o" : "p") + a_sig; }

	// a recipe in a form that compares equal whatever the slot order: each slot's keys sorted, then the slots sorted
	std::string RecipeSig(std::vector<std::vector<std::string>> a_slots)
	{
		std::vector<std::string> parts;
		for (auto& keys : a_slots) {
			std::ranges::sort(keys);
			keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
			std::erase(keys, std::string());
			if (keys.empty()) continue;
			std::string part;
			for (auto& k : keys) part += k + ",";
			parts.push_back(std::move(part));
		}
		std::ranges::sort(parts);
		std::string out;
		for (auto& part : parts) out += part + "#";
		return out;
	}
	std::string RecipeSigOfSets(const std::vector<std::string>& a_sets)  // "|key|key|" strings, as the rules hold them
	{
		std::vector<std::vector<std::string>> slots;
		for (auto& set : a_sets) slots.push_back(Split(set, '|'));
		return RecipeSig(std::move(slots));
	}
	std::mutex                                       g_editsLock;  // the page saves from the UI thread
	constexpr const char*                            PATCH_FOLDER = "Data/SKSE/Plugins/FreeCooking/Patches";
	constexpr const char*                            EDITS_FILE = "FreeCooking_PatcherEdits.txt";
	constexpr const char*                            EXPORT_FILE = "FreeCooking_PatcherExport.txt";
	constexpr const char*                            IMPORT_FILE = "FreeCooking_PatcherImport.txt";
	constexpr const char* EDITS_HEADER =
		"# Cooking Skill Overhaul patcher edits: what you changed on the patcher pages (Mod Control Panel > Cooking Skill Overhaul).\n"
		"# ING and DISH lines are used the next time the game starts. Lines: ING <plugin>|<form ID> <group or OUT> <stars>;\n"
		"# DISH <plugin>|<form ID> <AUTO/NEW/NOT/MISC> <tier> <slots: item:<plugin>|<form ID> or group:<group>, split by ;>. AUTO = the patcher decides.\n"
		"# MISC <plugin>|<form ID of the item the recipe makes> <SHOW/HIDE>: whether the cooking screen's Misc section lists it (Misc recipes page).\n"
		"# NEEDS <plugin>|<form ID of the dish a recipe makes> <NOTFOOD/LIGHT/MEDIUM/HEAVY/DRINK>: what SunHelm, Last Seed, iNeed and Realistic Needs count our cooked copies as (Needs mods page).\n"
		"# POTION <plugin>|<form ID> <AUTO/NONE/Mighty/Brainy/Hearty/Sneaky/Spicy/Chilly/Zesty/Earthy>: what a potion does as a pot seasoning; NONE = not a seasoning (Potions page).\n";

	std::filesystem::path MyGamesFile(const char* a_name)
	{
		auto dir = SKSE::log::log_directory();
		return dir ? *dir / a_name : std::filesystem::path();
	}

	struct PatchCounts
	{
		std::size_t ingredients = 0, unknown = 0, newDishes = 0, vanillaLook = 0, sameLook = 0, variants = 0, misc = 0, slotsJoined = 0;
	};

	bool IsCreationFile(std::string_view a_file);

	// vanilla for the patcher: the game's masters, Creations, the Anniversary Edition's resource pack and our own plugin
	bool IsVanillaFile(std::string_view a_file)
	{
		if (IsCreationFile(a_file)) return true;
		static const char* masters[] = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm", "_ResourcePack.esl", PLUGIN };
		for (auto* m : masters) {
			if (a_file.size() == std::strlen(m) && std::ranges::equal(a_file, std::string_view(m), [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); })) {
				return true;
			}
		}
		return false;
	}

	bool IsCreationFile(std::string_view a_file)
	{
		return a_file.size() > 2 && std::tolower(static_cast<unsigned char>(a_file[0])) == 'c' && std::tolower(static_cast<unsigned char>(a_file[1])) == 'c';
	}

	// the flavour, filler and brew code of an ingredient group (our table, sheet v122; "Mixed" groups take their most
	// common flavour, with word exceptions in the word file)
	struct GroupRule
	{
		const char* group;
		const char* flavour;
	};
	constexpr GroupRule GROUP_RULES[] = {
		{ "Drinks", "" }, { "Claws, teeth & horns", "Mighty" }, { "Staples", "" }, { "Shellfish", "Hearty" }, { "Exotic fish", "Sneaky" },
		{ "Hides, scales & shells", "Hearty" }, { "Sweeteners", "Mighty" }, { "Human & mer remains", "Hearty" }, { "Roots, bark & wild plants", "Earthy" },
		{ "Insects & eggs", "Mighty" }, { "River fish", "Brainy" }, { "Poisonous plants", "Sneaky" }, { "Glowing fungi", "Brainy" },
		{ "Feathers & wings", "Sneaky" }, { "Flowers & herbs", "Brainy" }, { "Wetland produce", "Zesty" }, { "Game meat", "Sneaky" },
		{ "Vegetables", "" }, { "Magical remains", "Brainy" }, { "Hot herbs", "Spicy" }, { "Fruit & nuts", "Brainy" }, { "Big game", "Mighty" },
		{ "Cold herbs", "Chilly" }, { "Fat & flesh", "Hearty" }, { "Berries", "Mighty" }, { "Mushrooms", "Mighty" },
		{ "Eyes, ears & tongues", "Sneaky" }, { "Cold-water fish", "Chilly" }, { "Dragon parts", "" }, { "Cheese", "" }
	};

	std::string GroupFlavour(const FCPatch::Words& a_w, const std::string& a_group, const std::string& a_name)
	{
		if (auto f = a_w.FlavourWord(a_group, a_name); !f.empty()) {
			return f;
		}
		for (auto& r : GROUP_RULES) {
			if (a_group == r.group) return r.flavour;
		}
		return {};
	}

	// the recipe slot labels of the cooking screen (tools\recipes.py LABEL) -> the word file's CATEGORY ids
	std::string CategoryOfLabel(const std::string& a_label)
	{
		static const std::pair<const char*, const char*> labels[] = {
			{ "any vegetable", "vegetable" }, { "any vegetable except Ash Yam", "vegetable" }, { "any vegetable or mushroom", "vegmush" }, { "any fish", "fish" }, { "any dairy", "dairy" }, { "any cheese", "cheese" },
			{ "any drink", "drink" }, { "any sweetener", "sweetener" }, { "any Spicy ingredient", "spicy" }, { "any grain", "grain" },
			{ "any fruit or berry", "fruit" }, { "any flower or herb", "herb" }, { "any herb or salt", "herbsalt" }, { "any mushroom", "mushroom" },
			{ "any egg", "egg" }, { "any bread", "bread" }, { "any flour", "flour" }, { "a vegetable, herb or salt", "vegherbsalt" },
			{ "a fruit, flower or herb", "fruitherb" }
		};
		for (auto& [l, c] : labels) {
			if (a_label == l) return c;
		}
		return {};
	}

	std::string AnyLabel(const std::string& a_group) { return "any " + Lower(a_group); }

	std::string NameOf(const RE::TESForm* a_form)
	{
		const char* name = a_form ? a_form->GetName() : nullptr;
		return name ? std::string(name) : std::string();
	}

	// po3's Tweaks keeps food and ingredient editor IDs (the game drops them) and shares them through this export
	using TweaksEditorIDFn = const char* (*)(std::uint32_t);
	TweaksEditorIDFn TweaksEditorIDFunction()
	{
		static TweaksEditorIDFn fn = []() -> TweaksEditorIDFn {
			auto module = REX::W32::GetModuleHandleW(L"po3_Tweaks.dll");
			return module ? reinterpret_cast<TweaksEditorIDFn>(REX::W32::GetProcAddress(module, "GetFormEditorID")) : nullptr;
		}();
		return fn;
	}
	const char* TweaksEditorID(RE::FormID a_id)
	{
		auto fn = TweaksEditorIDFunction();
		return fn ? fn(a_id) : nullptr;
	}

	// a word file as UTF-8 text: Skyrim's own translation files are UTF-16, so a translator may save ours that way too
	std::string ReadWordFile(const std::filesystem::path& a_path)
	{
		std::ifstream file(a_path, std::ios::binary);
		std::stringstream buffer;
		buffer << file.rdbuf();
		std::string bytes = buffer.str();
		if (bytes.size() < 2 || static_cast<unsigned char>(bytes[0]) != 0xFF || static_cast<unsigned char>(bytes[1]) != 0xFE) {
			return bytes;  // UTF-8 (with or without a byte order mark)
		}
		std::string out;  // UTF-16 little endian -> UTF-8
		for (std::size_t i = 2; i + 1 < bytes.size(); i += 2) {
			std::uint32_t cp = static_cast<unsigned char>(bytes[i]) | (static_cast<unsigned char>(bytes[i + 1]) << 8);
			if (cp >= 0xD800 && cp < 0xDC00 && i + 3 < bytes.size()) {
				const std::uint32_t lo = static_cast<unsigned char>(bytes[i + 2]) | (static_cast<unsigned char>(bytes[i + 3]) << 8);
				cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
				i += 2;
			}
			if (cp < 0x80) {
				out += static_cast<char>(cp);
			} else if (cp < 0x800) {
				out += static_cast<char>(0xC0 | (cp >> 6));
				out += static_cast<char>(0x80 | (cp & 0x3F));
			} else if (cp < 0x10000) {
				out += static_cast<char>(0xE0 | (cp >> 12));
				out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (cp & 0x3F));
			} else {
				out += static_cast<char>(0xF0 | (cp >> 18));
				out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
				out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (cp & 0x3F));
			}
		}
		return out;
	}

	std::string FileOf(const RE::TESForm* a_form)
	{
		auto file = a_form ? a_form->GetFile(0) : nullptr;
		return file ? std::string(file->GetFilename()) : std::string();
	}

	bool QuestConditions(const RE::BGSConstructibleObject* a_cobj)
	{
		using F = RE::FUNCTION_DATA::FunctionID;
		for (auto item = a_cobj->conditions.head; item; item = item->next) {
			const auto fn = item->data.functionData.function.get();
			// (GetQuestVariable too: a quest script's variable can gate a recipe; Codex review 2026-11-16, H4. And its script
			// form, GetVMQuestVariable: Codex review 2026-11-16 afternoon, M8)
			if (fn == F::kGetStage || fn == F::kGetStageDone || fn == F::kGetQuestCompleted || fn == F::kGetQuestRunning || fn == F::kGetQuestVariable ||
				fn == F::kGetVMQuestVariable) {
				return true;
			}
		}
		return false;
	}

	// quest items (his rule 2026-11-16): items a quest holds in a quest object alias, created there or placed by it. A
	// recipe making one stays in Misc as its mod made it; a recipe a quest only unlocks makes a dish of ours that unlocks
	// with it. Read once at kDataLoaded (main thread); read-only after. (An alias filled by conditions names no item: not
	// seen here.)
	// (g_questItems and g_lockedDishes: Cooking/PlayerView.inl, as Rules.inl reads them)

	void BuildQuestItems()
	{
		auto data = RE::TESDataHandler::GetSingleton();
		if (!data) return;
		// (an item a quest script gives with ForceRefTo, or a condition-filled alias finds, isn't seen here: a known limit)
		std::size_t created = 0, forced = 0, forcedMissing = 0;
		for (auto* quest : data->GetFormArray<RE::TESQuest>()) {
			if (!quest) continue;
			for (auto* alias : quest->aliases) {
				if (!alias || !alias->IsQuestObject() || alias->GetVMTypeID() != RE::BGSRefAlias::VMTYPEID) continue;
				const auto* ref = static_cast<RE::BGSRefAlias*>(alias);
				const RE::TESForm* item = nullptr;
				if (ref->fillType == RE::BGSBaseAlias::FILL_TYPE::kCreated) {
					item = ref->fillData.created.object;
					created += item ? 1 : 0;
				} else if (ref->fillType == RE::BGSBaseAlias::FILL_TYPE::kForced) {
					if (const auto placed = ref->fillData.forced.forcedRef.get()) item = placed->GetBaseObject();
					(item ? forced : forcedMissing) += 1;
				}
				if (item) g_questItems.insert(item->GetFormID());
			}
		}
		logger::info("Patcher: {} quest items held by quests ({} created, {} forced, {} forced refs not found)", g_questItems.size(), created, forced, forcedMissing);
		// the foods among them: their recipes stay in Misc, as their mods made them (Claude review 2026-11-16g, L3)
		for (const auto id : g_questItems) {
			if (const auto* food = RE::TESForm::LookupByID<RE::AlchemyItem>(id); food && food->IsFood()) {
				logger::info("Patcher:   quest food {} [{:08X}]", food->GetName(), id);
			}
		}
	}

	// a recipe that makes a quest item: it makes an item a quest holds, or takes a hidden quest item (a nameless one)
	bool QuestItemRecipe(const RE::BGSConstructibleObject* a_cobj)
	{
		if (a_cobj->createdItem && g_questItems.contains(a_cobj->createdItem->GetFormID())) return true;
		for (std::uint32_t c = 0; a_cobj->requiredItems.containerObjects && c < a_cobj->requiredItems.numContainerObjects; ++c) {
			const auto* co = a_cobj->requiredItems.containerObjects[c];
			if (co && co->obj && NameOf(co->obj).empty()) return true;
		}
		return false;
	}

	void AddToSet(const std::string& a_ruleKey, const std::string& a_itemKey)
	{
		auto& set = g_str[Lower(a_ruleKey)];
		if (set.empty()) set = "|";
		if (!InKeySet(set, a_itemKey)) set += a_itemKey + "|";
	}

	void RunPatcher()
	{
		const auto started = std::chrono::steady_clock::now();
		auto       data = RE::TESDataHandler::GetSingleton();
		if (!data) {
			return;
		}

		// ---- the word files (one per language; translators add their own)
		FCPatch::Words words;
		std::size_t    wordFiles = 0;
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator("Data/SKSE/Plugins/FreeCooking", ec)) {
			const auto name = entry.path().filename().wstring();  // wide: a file name in any script can't throw here
			if (name.starts_with(L"FreeCooking_PatcherWords_") && name.ends_with(L".txt") && words.LoadText(ReadWordFile(entry.path()))) {
				++wordFiles;
			}
		}
		if (wordFiles == 0) {
			logger::warn("Patcher: no word file (Data/SKSE/Plugins/FreeCooking/FreeCooking_PatcherWords_*.txt): modded foods are left out");
			return;
		}
		const bool tweaks = TweaksEditorIDFunction() != nullptr;
		g_potionWords = words;  // the POTION words, for potions with several flavours (Cooking/Rules.inl JudgePotion)

		// ---- hand edits: food mods' patch files (A-Z), then the player's own edits on top
		FCPatch::EditSet patchFiles, playerEdits;
		std::size_t      patchFileCount = 0, badLines = 0;
		{
			std::vector<std::filesystem::path> files;
			for (const auto& entry : std::filesystem::directory_iterator(PATCH_FOLDER, ec)) {
				if (entry.path().extension() == L".txt") files.push_back(entry.path());
			}
			std::ranges::sort(files);
			for (const auto& f : files) {
				patchFiles.Read(ReadWordFile(f), &badLines);
				++patchFileCount;
			}
			if (const auto path = MyGamesFile(EDITS_FILE); !path.empty() && std::filesystem::exists(path, ec)) {
				playerEdits.Read(ReadWordFile(path), &badLines);
			}
		}
		g_patchFileEdits = patchFiles;
		FCPatch::EditSet edits = patchFiles;
		edits.Replace(playerEdits);  // the player's entry wins whole: "Automatic" means the patcher's own result
		SetPotionChoices(edits.potions);
		g_patchGroups.clear();
		for (auto& r : GROUP_RULES) {
			g_patchGroups.emplace_back(r.group);
			auto& opts = g_groupFlavours[r.group];
			opts = { r.flavour };
			if (std::string_view(r.group) == "Dragon parts") continue;  // a dragon part's flavour comes from the drink
			for (auto& f : words.FlavourWordsOf(r.group)) {
				if (std::ranges::find(opts, f) == opts.end()) opts.push_back(f);
			}
		}
		std::ranges::sort(g_patchGroups);
		// a group named in an edit, matched without regard to case; "" if it isn't one of ours
		const auto canonGroup = [&](const std::string& a_g) -> std::string {
			for (const auto& g : g_patchGroups) {
				if (Lower(g) == Lower(a_g)) return g;
			}
			const auto* cat = FCPatch::SlotCategoryOf(a_g);  // a recipe category slot ("any flour"), not an ingredient group
			return cat ? std::string(cat->name) : std::string();
		};
		std::size_t unknownGroups = 0;
		for (auto* set : { &edits }) {
			for (auto& [key, e] : set->ingredients) {
				if (!e.group.empty() && e.group != "OUT") {
					e.group = canonGroup(e.group);
					if (e.group.empty()) ++unknownGroups;
				}
			}
			for (auto& [key, e] : set->dishes) {
				std::erase_if(e.slots, [&](FCPatch::SlotEdit& a_s) {
					if (a_s.item) return false;
					a_s.text = canonGroup(a_s.text);
					if (a_s.text.empty()) ++unknownGroups;
					return a_s.text.empty();
				});
			}
		}
		const auto isGroup = [&](const std::string& a_g) { return !a_g.empty() && std::ranges::find(g_patchGroups, a_g) != g_patchGroups.end(); };

		// ---- what our own table already knows: vanilla items and their groups (menu.tsv "I" lines, 5th column)
		std::unordered_map<std::string, std::string>         groupOfKey;  // rule key -> group
		std::vector<std::pair<RE::TESBoundObject*, std::string>> tableItems;
		{
			std::ifstream file(MENU_FILE);
			std::string   line;
			while (std::getline(file, line)) {
				if (!line.empty() && line.back() == '\r') line.pop_back();
				const auto f = Split(line, '\t');
				if (f.size() >= 5 && f[0] == "I") {
					if (auto item = LookupItem(f[1])) {
						groupOfKey[PlainKeyOf(item)] = f[4];
						tableItems.emplace_back(item, f[4]);
					}
				}
			}
		}
		if (groupOfKey.empty()) {
			logger::warn("Patcher: {} has no ingredient groups (an older file: run build_data.bat); new dishes can't use \"any ...\" slots", MENU_FILE);
		}
		if (!g_str.contains("dish.0.n")) {
			logger::warn("Patcher: the pot rules (data.json) didn't load, so modded foods are left out");
			return;
		}
		auto known = [&](const RE::TESForm* a_form) {
			const auto key = PlainKeyOf(a_form);
			return groupOfKey.contains(key) || HasInt("is." + key) || !Str("ic." + key).empty() || !Str("if." + key).empty() || HasInt("ia." + key);
		};

		// ---- the stations Cooking Skill Overhaul covers, and which items recipes make
		auto pot = PluginForm<RE::BGSKeyword>(0xA5CB3, "Skyrim.esm");
		auto oven = PluginForm<RE::BGSKeyword>(0x117F7, "HearthFires.esm");
		auto campfire = PluginForm<RE::BGSKeyword>(KEYWORD_CAMPFIRE, "Update.esm");  // Camping_CampfireCookingShared
		std::unordered_set<const RE::TESForm*>                                         madeByRecipe;
		std::unordered_map<const RE::TESForm*, std::vector<const RE::BGSConstructibleObject*>> covered;
		for (auto cobj : data->GetFormArray<RE::BGSConstructibleObject>()) {
			if (!cobj || !cobj->createdItem) continue;
			madeByRecipe.insert(cobj->createdItem);
			const auto bench = cobj->benchKeyword;
			if (bench && (bench == pot || bench == oven || (campfire && bench == campfire))) {
				covered[cobj->createdItem].push_back(cobj);
			}
		}

		// Water (his call 2026-11-15: "water is special because it's a staple and a drink ingredient"): drinking water is an
		// ingredient even when a mod makes it at the pot (SunHelm boils salt water into a Bottle of Water and fills its
		// Waterskin there). Its pot recipe stays in Misc (Cooking/Misc.inl), and the item itself is a Staple and a drink
		// ingredient (brew code 5). Water is the keyword VendorItemDrinkWater, or a Staples name with a WATER word
		// ("Waterskin 2/3"); sea and salt water (a SALTWATER word: "Bottle of Salt Water", "Waterskin, Salt") are only
		// boiled into drinking water and are left out. 0 not water, 1 drinking water, 2 salt water.
		auto waterOf = [&](const RE::TESForm* a_item) -> int {
			const auto name = NameOf(a_item);
			if (name.empty()) return 0;
			bool tagged = false;
			if (auto kf = const_cast<RE::TESForm*>(a_item)->As<RE::BGSKeywordForm>()) {
				kf->ForEachKeyword([&](RE::BGSKeyword* a_kw) {
					const char* id = a_kw ? a_kw->GetFormEditorID() : nullptr;
					if (id && Lower(id) == "vendoritemdrinkwater") {
						tagged = true;
						return RE::BSContainer::ForEachResult::kStop;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			// a Staples name with a water word: "Waterskin, Salt" is salt water (below); "Water Hyacinth Nectar" isn't a
			// Staple at all (hyacinth is a flower word), so it never gets here
			if (!tagged && !(words.GroupOf(name) == "Staples" && words.InCategory("water", "", name))) return 0;
			return words.InCategory("saltwater", "", name) ? 2 : 1;
		};
		// water made at the pot is no dish: it goes through the ingredient rules below, and its recipe to Misc
		std::erase_if(covered, [&](const auto& a_entry) { return waterOf(a_entry.first) != 0; });

		// preparation steps and conversions (his rule 2026-11-12, "take meals in the pot only if they are Misc preparation
		// steps"; built 2026-11-15 after the full verdict pass found 93 of them unusable): a food a mod makes at the pot or
		// oven from one ingredient (salt aside) that isn't cooked (Lemon Half, Raw Beef Steak, Cured Meat, Beef Jerky, a cheese
		// wedge, a Potato peeled from CACO's sack) is a pot ingredient of its base word's group, not a dish. Its recipe stays
		// in Misc (Cooking/Misc.inl). The same test as the dish rules' Misc verdict (Patcher/Dishes.cpp): a cooking word
		// ("Grilled ...") or a cooked cut ("Elk Steak") makes it a dish, a Misc word or a quest recipe keeps it out.
		std::unordered_set<const RE::TESForm*> prepOutputs;
		for (const auto& [made, cobjs] : covered) {
			const auto name = NameOf(made);
			if (name.empty() || !made->Is(RE::FormType::AlchemyItem) || words.IsMisc(name) || !words.DishTier(name).empty() || words.IsCookedCut(name)) {
				continue;
			}
			for (auto cobj : cobjs) {
				if (QuestConditions(cobj)) {
					continue;
				}
				std::unordered_set<std::string> kinds;
				bool                            hidden = false;
				for (std::uint32_t c = 0; cobj->requiredItems.containerObjects && c < cobj->requiredItems.numContainerObjects; ++c) {
					const auto* co = cobj->requiredItems.containerObjects[c];
					if (!co || !co->obj) continue;
					const auto itemName = NameOf(co->obj);
					if (itemName.empty()) {
						hidden = true;  // a hidden quest item
					} else if (!words.IsSalt(itemName)) {
						kinds.insert(Lower(itemName));
					}
				}
				if (!hidden && kinds.size() <= 1) {
					prepOutputs.insert(made);
					break;
				}
			}
		}

		auto inputOf = [&](RE::TESBoundObject* a_item, bool a_ingredient) {
			FCPatch::Input in;
			in.name = NameOf(a_item);
			if (tweaks) {
				if (const char* id = TweaksEditorID(a_item->GetFormID())) in.editorId = id;
			}
			in.isIngredient = a_ingredient;
			if (auto alch = a_item->As<RE::AlchemyItem>()) in.isPoison = alch->IsPoison();
			if (auto kf = a_item->As<RE::BGSKeywordForm>()) {
				kf->ForEachKeyword([&](RE::BGSKeyword* a_kw) {
					if (a_kw && a_kw->GetFormEditorID()) in.keywords.emplace_back(a_kw->GetFormEditorID());
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			return in;
		};

		std::ofstream report;
		if (auto dir = SKSE::log::log_directory()) {
			report.open(*dir / "FreeCooking_Patcher.txt", std::ios::trunc);
		}
		auto say = [&](const std::string& a_line) {
			if (report) report << a_line << '\n';
		};
		say("Cooking Skill Overhaul patcher report: what other mods' foods and ingredients became in Cooking Skill Overhaul.");
		say("Results are made automatically from names and recipes; check them on the patcher page.");
		say(std::format("Word files: {}. Editor IDs: {}.", wordFiles, tweaks ? "from po3's Tweaks" : "not available (po3's Tweaks not installed)"));
		say(std::format("Hand edits: {} patch files from mods, {} items set on the patcher page{}.", patchFileCount,
			playerEdits.ingredients.size() + playerEdits.dishes.size(), badLines ? std::format(" ({} lines couldn't be read)", badLines) : ""));
		if (unknownGroups) say(std::format("Hand edits: {} group names aren't ours and were ignored (see the groups in the word file).", unknownGroups));
		say("");

		PatchCounts n;
		// ---- 1. ingredients: alchemy ingredients, and foods no recipe makes (fruit, cheese, bread found or sold)
		struct Patched
		{
			RE::TESBoundObject* item;
			std::string         key, group;
		};
		std::vector<Patched> patched;
		std::vector<RE::TESBoundObject*> unrecognised;  // not recognised: a dish named after one waits for it
		say("== Ingredients");
		// foods (not alchemy ingredients) the player made dishes with their own recipe: the dish rules take them below
		struct FoodAsDish
		{
			RE::AlchemyItem* item;
			FCPatch::Result  autoResult;  // what the patcher made of it on its own, for the page's "Automatic"
		};
		std::vector<FoodAsDish> foodsAsDishes;
		auto consider = [&](RE::TESBoundObject* a_item, bool a_ingredient) {
			if (!a_item || a_item->IsDynamicForm()) return;
			const auto file = FileOf(a_item);
			if (file.empty() || IsVanillaFile(file) || known(a_item)) return;
			if (!a_ingredient && covered.contains(a_item) && !prepOutputs.contains(a_item)) return;  // made at a pot, oven or campfire: the dish rules decide
			auto r = words.Classify(inputOf(a_item, a_ingredient));
			const int water = waterOf(a_item);
			if (water == 2 && r.kind != FCPatch::Kind::Skip) {
				r.kind = FCPatch::Kind::Skip;
				r.why = "salt or sea water: only boiled into drinking water";
			} else if (water == 1 && r.kind != FCPatch::Kind::Skip) {
				r.kind = FCPatch::Kind::Ingredient;
				r.group = "Staples";
				r.restriction = FCPatch::Restriction::None;
				r.why = "drinking water: a staple and a drink ingredient";
			}
			// a finished meal no recipe makes anywhere (found or sold, like Skyrim Food Expansion's pies): eaten as it is
			if (!a_ingredient && r.kind == FCPatch::Kind::Dish && !madeByRecipe.contains(a_item)) r.why = "found or sold; " + r.why;
			if (!a_ingredient && !NameOf(a_item).empty()) {
				if (const auto e = edits.dishes.find(FCPatch::ItemKey(file, a_item->GetLocalFormID())); e != edits.dishes.end() && e->second.choice == FCPatch::DishChoice::New) {
					foodsAsDishes.push_back({ a_item->As<RE::AlchemyItem>(), r });
					return;
				}
			}
			const auto label = std::format("{} [{}|{:06X}]", NameOf(a_item), file, a_item->GetLocalFormID());
			if (NameOf(a_item).empty() || r.why == "not playable") return;  // hidden items: nothing to show or set
			FC::PatchRow row;
			row.key = FCPatch::ItemKey(file, a_item->GetLocalFormID());
			row.name = NameOf(a_item);
			row.plugin = file;
			row.why = r.why;
			row.food = !a_ingredient;
			const auto kindOf = [](const FCPatch::Result& a_r) -> const char* {
				switch (a_r.kind) {
				case FCPatch::Kind::Ingredient: return "Ingredient";
				case FCPatch::Kind::Unknown: return "Not recognised";
				case FCPatch::Kind::Misc: return "Misc";
				case FCPatch::Kind::Dish: return "A dish from another station";
				default: return "Left out";
				}
			};
			const auto needsOf = [](const FCPatch::Result& a_r) -> std::string {
				switch (a_r.restriction) {
				case FCPatch::Restriction::Gross: return "alc";
				case FCPatch::Restriction::Poisonous: return "garlic";
				case FCPatch::Restriction::Namira: return "ring";
				case FCPatch::Restriction::Dragon: return "dragon";
				default: return {};
				}
			};
			const auto flavourOf = [&](const FCPatch::Result& a_r) {
				return a_r.kind != FCPatch::Kind::Ingredient || a_r.restriction == FCPatch::Restriction::Dragon ? std::string() : GroupFlavour(words, a_r.group, NameOf(a_item));
			};
			row.autoKind = kindOf(r);
			row.autoGroup = r.kind == FCPatch::Kind::Ingredient ? r.group : std::string();
			row.autoFlavour = flavourOf(r);
			row.autoNeeds = r.kind == FCPatch::Kind::Ingredient ? needsOf(r) : std::string();
			row.autoStars = r.stars;
			row.edited = playerEdits.ingredients.contains(row.key);
			row.patched = patchFiles.ingredients.contains(row.key);
			bool byHand = false;
			if (const auto e = edits.ingredients.find(row.key); e != edits.ingredients.end()) {
				if (e->second.group == "OUT") {
					r.kind = FCPatch::Kind::Skip;
					byHand = true;
				} else if (!e->second.group.empty() && isGroup(e->second.group)) {
					r.kind = FCPatch::Kind::Ingredient;
					r.group = e->second.group;
					r.restriction = FCPatch::GroupRestriction(r.group);
					if (!a_ingredient && r.restriction == FCPatch::Restriction::Gross) r.restriction = FCPatch::Restriction::None;  // food is never gross
					byHand = true;
				}
				if (e->second.stars) {
					r.stars = e->second.stars;
					byHand = true;
				}
			}
			// a group whose items can have more than one flavour (Game meat: Sneaky, or Hearty by its name): the player's choice
			std::optional<std::string> flavourSet;
			if (const auto e = edits.ingredients.find(row.key); e != edits.ingredients.end() && !e->second.flavour.empty() && r.kind == FCPatch::Kind::Ingredient &&
																	 r.restriction != FCPatch::Restriction::Dragon) {
				const std::string f = e->second.flavour == "None" ? std::string() : e->second.flavour;
				if (const auto& opts = g_groupFlavours[r.group]; opts.size() > 1 && std::ranges::find(opts, f) != opts.end()) {
					flavourSet = f;
					byHand = true;
				}
			}
			if (byHand) r.why = row.edited ? "set by hand" : "set by a patch file";
			row.kind = kindOf(r);
			row.group = r.kind == FCPatch::Kind::Ingredient ? r.group : std::string();
			row.flavour = flavourSet ? *flavourSet : flavourOf(r);
			row.needs = r.kind == FCPatch::Kind::Ingredient ? needsOf(r) : std::string();
			row.stars = r.stars;
			g_patchRows.push_back(std::move(row));
			if (byHand && r.kind == FCPatch::Kind::Skip) {
				say(std::format("  -  {}: left out by hand", label));
				return;
			}
			if (r.kind == FCPatch::Kind::Unknown) {
				++n.unknown;
				unrecognised.push_back(a_item);
				say(std::format("  ?  {}: not recognised ({}), left out until set by hand", label, r.why));
				return;
			}
			if (r.kind == FCPatch::Kind::Misc) {
				++n.misc;
				say(std::format("  M  {}: Misc ({}), not in the pot's rules", label, r.why));
				return;
			}
			if (r.kind != FCPatch::Kind::Ingredient) return;  // a dish (a mod's own, made at its own station) or left out
			const auto key = PlainKeyOf(a_item);
			const auto flavour = flavourSet ? *flavourSet : r.restriction == FCPatch::Restriction::Dragon ? std::string() : GroupFlavour(words, r.group, NameOf(a_item));
			if (!flavour.empty()) g_str["ic." + key] = flavour;
			switch (r.restriction) {
			case FCPatch::Restriction::Gross: g_str["iq." + key] = "alc"; break;
			case FCPatch::Restriction::Poisonous: g_str["iq." + key] = "garlic"; break;
			case FCPatch::Restriction::Namira: g_str["iq." + key] = "ring"; break;
			case FCPatch::Restriction::Dragon: g_int["id." + key] = words.InCategory("dragondrink", "", NameOf(a_item)) ? 2 : 1; break;
			default: break;
			}
			if (flavour.empty() && r.group == "Vegetables") g_str["if." + key] = "veg";
			if (flavour.empty() && (r.group == "Staples" || r.group == "Cheese")) g_str["if." + key] = "staple";
			g_int["is." + key] = flavour.empty() ? 0 : r.stars;
			if (r.group == "Drinks" && !words.InCategory("nonalcohol", r.group, NameOf(a_item))) g_int["ia." + key] = 1;
			if (words.InCategory("garlic", r.group, NameOf(a_item))) g_int["ig." + key] = 1;
			std::int32_t brew = 0;
			if (r.group == "Sweeteners") brew = 1;
			else if (words.InCategory("grain", r.group, NameOf(a_item))) brew = 2;
			else if (r.group == "Fruit & nuts" || r.group == "Berries") brew = 3;
			else if (r.group == "Flowers & herbs") brew = 4;
			if (water == 1 && r.group == "Staples") {
				brew = 5;  // drinking water: a drink ingredient too (Cooking/Rules.inl)
			}
			if (brew) g_int["ib." + key] = brew;
			g_stars[a_item] = flavour.empty() ? 0 : r.stars;
			groupOfKey[key] = r.group;
			patched.push_back({ a_item, key, r.group });
			++n.ingredients;
			const std::string flavourText = flavour.empty() ? std::string() : ", " + flavour;
			const std::string restrictText = r.restriction == FCPatch::Restriction::None ? std::string() : std::string(", ") + FCPatch::RestrictionName(r.restriction);
			say(std::format("  +  {}: {}{}{} ({})", label, r.group, flavourText, restrictText, r.why));
		};
		for (auto ingr : data->GetFormArray<RE::IngredientItem>()) consider(ingr, true);
		for (auto alch : data->GetFormArray<RE::AlchemyItem>()) {
			if (alch && alch->IsFood()) consider(alch, false);
		}

		// ---- 2. they join our recipes' "any ..." slots (any vegetable, any flour...)
		for (auto& dish : g_dishes) {
			for (std::size_t j = 0; j < dish.slots.size(); ++j) {
				auto&      slot = dish.slots[j];
				const auto cat = CategoryOfLabel(slot.label);
				if (cat.empty()) continue;
				for (auto& p : patched) {
					const bool in = cat == "spicy" ? Str("ic." + p.key) == "Spicy" : words.InCategory(cat, p.group, NameOf(p.item));
					if (!in || std::ranges::find(slot.options, p.item) != slot.options.end()) continue;
					slot.options.push_back(p.item);
					AddToSet(std::format("dish.{}.s{}", dish.index, j), p.key);
					++n.slotsJoined;
				}
			}
		}
		for (auto& p : patched) {
			for (const char* base : { "vegetable", "dairy", "cheese", "drink", "sweetener", "grain", "fruit", "bread", "flour" }) {
				if (words.InCategory(base, p.group, NameOf(p.item))) {
					g_recipeKeys.insert(p.key);  // part of what a dish is made of
					break;
				}
			}
		}

		// ---- 3. dishes: foods made at a pot, oven or campfire
		std::vector<std::string> existing;
		std::unordered_set<std::string> ourSlotKeys;  // every item one of our recipes takes (modded ingredients joined in)
		std::int32_t             next = 0;
		for (; g_str.contains(Lower(std::format("dish.{}.n", next))); ++next) {
			existing.push_back(Str(std::format("dish.{}.n", next)));
			{
				std::vector<std::string> sets;
				for (std::int32_t j = 0; j < Int(std::format("dish.{}.ns", next)); ++j) sets.push_back(Str(std::format("dish.{}.s{}", next, j)));
				for (auto& set : sets) {
					for (auto& k : Split(set, '|')) {
						if (!k.empty()) ourSlotKeys.insert(k);
					}
				}
				if (auto sig = RecipeSigOfSets(sets); !sig.empty()) {
					g_recipeSigs[PlaceSig(Int(std::format("dish.{}.ov", next)) == 1, sig)].emplace_back(existing.back(), std::string());
				}
			}
			// and the game's own name of that dish's form, where our table renamed it ("Roasted Tomato Crab Bisque")
			if (auto form = PluginForm(static_cast<RE::FormID>(Int(std::format("dish.{}.id", next))), Str(std::format("dish.{}.p", next)).c_str())) {
				if (auto game = NameOf(form); !game.empty() && game != existing.back()) existing.push_back(game);
			}
		}
		std::unordered_set<std::string> vanillaModels;
		auto noteModel = [&](RE::TESBoundObject* a_item) {
			if (!a_item) return;
			const auto file = FileOf(a_item);
			if (!IsVanillaFile(file)) return;
			if (auto model = a_item->As<RE::TESModel>(); model && model->GetModel() && *model->GetModel()) vanillaModels.insert(Lower(model->GetModel()));
		};
		for (auto i : data->GetFormArray<RE::IngredientItem>()) noteModel(i);
		for (auto a : data->GetFormArray<RE::AlchemyItem>()) noteModel(a);

		std::unordered_map<std::string, std::vector<std::string>> keysByName;  // lower-case item name -> rule keys
		std::unordered_map<std::string, std::vector<RE::TESBoundObject*>> itemsByGroup;
		std::vector<FCPatch::RawCandidate> raws;
		for (auto& [item, group] : tableItems) {
			keysByName[Lower(NameOf(item))].push_back(PlainKeyOf(item));
			if (group != "Excluded") itemsByGroup[group].push_back(item);
			if (group != "Excluded") raws.push_back({ NameOf(item), true, FileOf(item), item->GetLocalFormID(), item->Is(RE::FormType::Ingredient), true, false, group });
		}
		for (auto& p : patched) {
			keysByName[Lower(NameOf(p.item))].push_back(p.key);
			itemsByGroup[p.group].push_back(p.item);
			raws.push_back({ NameOf(p.item), false, FileOf(p.item), p.item->GetLocalFormID(), p.item->Is(RE::FormType::Ingredient), true, false, p.group });
		}
		for (auto* item : unrecognised) {
			raws.push_back({ NameOf(item), false, FileOf(item), item->GetLocalFormID(), item->Is(RE::FormType::Ingredient), true, true });
		}
		const auto rawByKey = FCPatch::BuildRawByKey(raws, words);
		// the recipe category slots of new dishes, filled like our own recipes' ("any herb or salt", "any flour", "any dairy"...)
		for (const auto& cat : FCPatch::SLOT_CATEGORIES) {
			auto& items = itemsByGroup[cat.name];
			for (auto& [item, group] : tableItems) {
				if (group != "Excluded" && words.InCategory(cat.category, group, NameOf(item)) && !(cat.exclude && words.InCategory(cat.exclude, group, NameOf(item)))) items.push_back(item);
			}
			for (auto& p : patched) {
				if (words.InCategory(cat.category, p.group, NameOf(p.item)) && !(cat.exclude && words.InCategory(cat.exclude, p.group, NameOf(p.item)))) items.push_back(p.item);
			}
		}


		// every ingredient a recipe slot can name, for the patcher page and for slots set by hand
		std::unordered_map<std::string, std::string> nameOfItemKey;  // FCPatch::ItemKey -> name
		std::unordered_map<std::string, RE::TESBoundObject*> itemOfKey;  // FCPatch::ItemKey -> the item
		{
			auto addChoice = [&](RE::TESBoundObject* a_item) {
				const auto key = FCPatch::ItemKey(FileOf(a_item), a_item->GetLocalFormID());
				if (NameOf(a_item).empty() || !nameOfItemKey.emplace(key, NameOf(a_item)).second) return;
				itemOfKey.emplace(key, a_item);
				g_patchChoices.emplace_back(NameOf(a_item), key);
			};
			for (auto& [item, group] : tableItems) addChoice(item);  // Excluded items too: recipes can still name them
			for (auto& p : patched) addChoice(p.item);
			std::ranges::sort(g_patchChoices, [](const auto& a, const auto& b) { return Lower(a.first) != Lower(b.first) ? Lower(a.first) < Lower(b.first) : a.second < b.second; });
		}
		std::unordered_map<std::string, std::string> keyOfItemName;  // lower-case name -> first key (auto slots name items)
		for (auto& [name, key] : g_patchChoices) keyOfItemName.emplace(Lower(name), key);
		// a slot set by hand names one exact item (Codex review 2026-11-15, M13: it took every item of that name)
		for (auto& [key, item] : itemOfKey) g_itemSlotKeys[key] = { PlainKeyOf(item) };
		for (auto& [group, items] : itemsByGroup) {
			auto& keys = g_groupSlotKeys[group];
			for (auto* item : items) keys.push_back(PlainKeyOf(item));
		}

		const auto hunger = [&](RE::AlchemyItem* a_item) -> std::string {
			static const std::pair<RE::FormID, const char*> sizes[] = { { 0x2EE1, "VerySmall" }, { 0x2EE2, "Small" }, { 0x2EE3, "Medium" }, { 0x2EE4, "Large" } };
			for (auto e : a_item->effects) {
				for (auto& [id, size] : sizes) {
					if (e && e->baseEffect && e->baseEffect == PluginForm(id, "Update.esm")) return size;
				}
			}
			return {};
		};

		std::vector<FCPatch::DishInput>  inputs;
		std::vector<RE::AlchemyItem*>    forms;
		std::vector<std::array<bool, 3>> stations;  // pot, oven, campfire
		for (auto& [made, cobjs] : covered) {
			if (prepOutputs.contains(made)) continue;  // a preparation step: a pot ingredient (above), its recipe in Misc
			auto alch = const_cast<RE::TESForm*>(made)->As<RE::AlchemyItem>();
			if (!alch || !alch->IsFood() || alch->IsDynamicForm() || NameOf(alch).empty()) continue;  // nameless: a quest token
			const auto file = FileOf(alch);
			if (file.empty() || IsVanillaFile(file) || g_dishByBase.contains(alch->GetFormID())) continue;
			FCPatch::DishInput d;
			d.plugin = file;
			d.localId = alch->GetLocalFormID();
			d.name = NameOf(alch);
			d.model = alch->GetModel() ? Lower(alch->GetModel()) : "";
			for (std::uint32_t t = 0; t < alch->numAlternateTextures && alch->alternateTextures; ++t) {
				const auto& alt = alch->alternateTextures[t];
				d.textureSwap += std::format("{}:{:08X};", alt.name3D.c_str() ? alt.name3D.c_str() : "", alt.textureSet ? alt.textureSet->GetFormID() : 0);
			}
			d.vanillaModel = vanillaModels.contains(d.model);
			d.hunger = hunger(alch);
			d.questItem = g_questItems.contains(alch->GetFormID());
			std::array<bool, 3> where{};
			for (auto cobj : cobjs) {
				where[0] = where[0] || cobj->benchKeyword == pot;
				where[1] = where[1] || cobj->benchKeyword == oven;
				where[2] = where[2] || (campfire && cobj->benchKeyword == campfire);
				FCPatch::DishRecipe rc;
				rc.questConditions = QuestConditions(cobj);
				rc.recipeId = cobj->GetFormID();
				for (std::uint32_t c = 0; c < cobj->requiredItems.numContainerObjects; ++c) {
					auto co = cobj->requiredItems.containerObjects[c];
					if (!co || !co->obj) continue;
					FCPatch::DishItem it;
					it.name = NameOf(co->obj);
					it.hidden = it.name.empty();
					// coins, tools: a playable item that is neither food nor an ingredient (a non-playable named item is a recipe
					// token standing for a category, CACO's "Salt" or "Wheat Flour": kept)
					it.notFood = !co->obj->Is(RE::FormType::AlchemyItem) && !co->obj->Is(RE::FormType::Ingredient) && (co->obj->formFlags & 0x4) == 0;
					it.count = co->count;
					it.isFood = co->obj->Is(RE::FormType::AlchemyItem) && madeByRecipe.contains(co->obj);
					if (auto g = groupOfKey.find(PlainKeyOf(co->obj)); g != groupOfKey.end() && g->second != "Excluded") it.group = g->second;
					rc.items.push_back(std::move(it));
				}
				d.recipes.push_back(std::move(rc));
			}
			inputs.push_back(std::move(d));
			forms.push_back(alch);
			stations.push_back(where);
		}
		// found or sold foods the player made dishes: no recipe of their own mod, so their recipe is the player's (a pot dish)
		std::unordered_map<std::string, FCPatch::Result> foodAutoResult;
		for (auto& f : foodsAsDishes) {
			auto alch = f.item;
			if (!alch) continue;
			FCPatch::DishInput d;
			d.plugin = FileOf(alch);
			d.localId = alch->GetLocalFormID();
			d.name = NameOf(alch);
			d.model = alch->GetModel() ? Lower(alch->GetModel()) : "";
			for (std::uint32_t t = 0; t < alch->numAlternateTextures && alch->alternateTextures; ++t) {
				const auto& alt = alch->alternateTextures[t];
				d.textureSwap += std::format("{}:{:08X};", alt.name3D.c_str() ? alt.name3D.c_str() : "", alt.textureSet ? alt.textureSet->GetFormID() : 0);
			}
			d.vanillaModel = vanillaModels.contains(d.model);
			d.hunger = hunger(alch);
			foodAutoResult[FCPatch::ItemKey(d.plugin, d.localId)] = f.autoResult;
			inputs.push_back(std::move(d));
			forms.push_back(alch);
			stations.push_back({ true, false, false });
		}
		// DecideDishes sorts the inputs into the fixed reading order: keep the forms beside them
		std::vector<std::size_t> order(inputs.size());
		for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
		std::ranges::sort(order, [&](std::size_t a, std::size_t b) {
			const auto la = Lower(inputs[a].plugin), lb = Lower(inputs[b].plugin);
			return la != lb ? la < lb : inputs[a].localId < inputs[b].localId;
		});
		{
			std::vector<FCPatch::DishInput> sortedInputs;
			std::vector<RE::AlchemyItem*>   sortedForms;
			std::vector<std::array<bool, 3>> sortedStations;
			for (auto i : order) {
				sortedInputs.push_back(std::move(inputs[i]));
				sortedForms.push_back(forms[i]);
				sortedStations.push_back(stations[i]);
			}
			inputs = std::move(sortedInputs);
			forms = std::move(sortedForms);
			stations = std::move(sortedStations);
		}

		// ---- general dishes (decided 2026-11-14): one recipe per kind of dish for any meat, fish or fruit, so a food mod's
		// stew, pie or dumpling on a vanilla look really is covered by our rules (the look rule assumed they existed). Each
		// mirrors a vanilla dish with its named slot made general; named dishes win when they fit (more named slots). The
		// cooked dish is named after its main ingredient ("Goat Stew", "Catfish Pie"; DishName, Cooking/Rules.inl)
		struct GeneralSlot
		{
			std::array<const char*, 4> groups;  // the slot takes any item of these groups or recipe categories
			const char*                label = nullptr;  // the cooking screen's label, as our own recipes write it
		};
		struct General
		{
			const char*                name;     // English: the save key and the translation phrase
			const char*                word;     // the general word the main ingredient replaces
			std::array<GeneralSlot, 3> slots;    // empty groups: no slot
			const char*                plugin;   // its look: a vanilla dish of that kind
			RE::FormID                 look;
			std::int32_t               tier;     // 0 simple, 2 prepared, 4 elaborate
			bool                       oven;
			bool                       spit;
		};
		static constexpr const char* HOS = FCPatch::HERB_OR_SALT;
		static constexpr General generals[] = {
			{ "Grilled Meat", "Meat", { { { { "Game meat", "Big game" }, "any meat" }, {}, {} } }, "Skyrim.esm", 0x0721E8, 0, false, true },          // Cooked Beef
			{ "Grilled Fish", "Fish", { { { { "Fish" }, "any fish" }, {}, {} } }, "Skyrim.esm", 0x064B3B, 0, false, true },                             // Salmon Steak
			{ "Meat Stew", "Meat", { { { { "Game meat", "Big game" }, "any meat" }, { { "Vegetables" }, "any vegetable" }, { { "Vegetables", "Mushrooms", "Dairy", HOS }, "a vegetable, mushroom, dairy, herb or salt" } } }, "Skyrim.esm", 0x0F431D, 4, false, false },  // Venison Stew
			{ "Fish Stew", "Fish", { { { { "Fish" }, "any fish" }, { { "Vegetables" }, "any vegetable" }, { { "Vegetables", "Mushrooms", "Dairy", HOS }, "a vegetable, mushroom, dairy, herb or salt" } } }, "Skyrim.esm", 0x0F4314, 4, false, false },             // Beef Stew
			{ "Meat Pie", "Meat", { { { { "Game meat", "Big game" }, "any meat" }, { { "Flour" }, "any flour" }, { { "Vegetables", "Mushrooms", "Egg", "Dairy" }, "a vegetable, mushroom, egg or dairy" } } }, "Skyrim.esm", 0x0CD614, 4, true, false },  // Homecooked Meal
			{ "Fish Pie", "Fish", { { { { "Fish" }, "any fish" }, { { "Flour" }, "any flour" }, { { "Vegetables", "Egg", "Dairy" }, "a vegetable, egg or dairy" } } }, "Skyrim.esm", 0x064B43, 4, true, false },                            // Apple Pie
			{ "Fruit Pie", "Fruit", { { { { "Fruit & nuts", "Berries" }, "any fruit or berry" }, { { "Flour" }, "any flour" }, { { "Sweeteners" }, "any sweetener" } } }, "Skyrim.esm", 0x064B43, 4, true, false },                       // Apple Pie
			{ "Fish Dumpling", "Fish", { { { { "Fish" }, "any fish" }, { { "Flour" }, "any flour" }, { { "Vegetables", "Mushrooms", "Dairy" }, "a vegetable, mushroom or dairy" } } }, "HearthFires.esm", 0x0117FF, 4, true, false },           // Chicken Dumpling
			{ "Fruit Dumpling", "Fruit", { { { { "Fruit & nuts", "Berries" }, "any fruit or berry" }, { { "Flour" }, "any flour" }, { { "Dairy" }, "any dairy" } } }, "HearthFires.esm", 0x003533, 4, true, false },                  // Apple Dumpling
			{ "Fish Cakes", "Fish", { { { { "Fish" }, "any fish" }, { { "Flour" }, "any flour" }, {} } }, "ccbgssse001-fish.esm", 0x000F78, 2, true, false },                                                // Crab Cakes
		};
		static constexpr std::array<const char*, 8> generalFlavours{ "Mighty", "Brainy", "Hearty", "Sneaky", "Earthy", "Spicy", "Chilly", "Zesty" };
		std::int32_t generalCount = 0;
		for (const auto& gd : generals) {
			auto form = PluginForm(gd.look, gd.plugin);
			if (!form) form = PluginForm(0x064B3B, "Skyrim.esm");  // Crab Cakes needs the Fishing Creation: Salmon Steak's look then
			if (!form) continue;
			CookingMenu::Dish screen;
			std::vector<std::string> sets;
			bool ok = true;
			for (const auto& gs : gd.slots) {
				if (!gs.groups[0]) break;
				CookingMenu::Slot slot;
				std::string set = "|";
				for (const char* g : gs.groups) {
					if (!g) continue;
					for (auto* item : itemsByGroup[g]) {
						// like vanilla, Horker Meat alone cooks nothing (it needs its loaf's or stew's partners)
						if (sets.empty() && gd.slots[1].groups[0] == nullptr && Lower(NameOf(item)).find("horker") != std::string::npos) continue;
						if (sets.empty() && words.IsPreserved(NameOf(item))) continue;  // jerky, cured or smoked meat: already a food
						const auto key = PlainKeyOf(item);
						if (set.find("|" + key + "|") != std::string::npos) continue;
						set += key + "|";
						slot.options.push_back(item);
					}
				}
				slot.label = FC::Tr(gs.label);
				if (slot.options.empty()) {
					ok = false;
					break;
				}
				sets.push_back(set);
				screen.slots.push_back(std::move(slot));
			}
			if (!ok || sets.empty()) continue;
			const auto idx = next++;
			const std::string name = FC::Tr(gd.name);
			existing.push_back(gd.name);
			// the rules keep English, like every other dish's (DishName and the screen translate when they show it; the
			// cook notes match English words against it: in a translated game "Meat Stew" lost its line; review 2026-11-15)
			g_str[Lower(std::format("dish.{}.n", idx))] = gd.name;
			g_str[Lower(std::format("dish.{}.gw", idx))] = gd.word;
			g_str[Lower(std::format("dish.{}.p", idx))] = std::string(PluginForm(gd.look, gd.plugin) ? gd.plugin : "Skyrim.esm");
			g_int[Lower(std::format("dish.{}.id", idx))] = static_cast<std::int32_t>(PluginForm(gd.look, gd.plugin) ? gd.look : 0x064B3B);
			g_int[Lower(std::format("dish.{}.t", idx))] = gd.tier;
			g_int[Lower(std::format("dish.{}.dr", idx))] = 0;
			g_int[Lower(std::format("dish.{}.fe", idx))] = 0;
			if (gd.oven) g_int[Lower(std::format("dish.{}.ov", idx))] = 1;
			if (gd.spit) g_int[Lower(std::format("dish.{}.spit", idx))] = 1;
			g_int[Lower(std::format("dish.{}.ns", idx))] = static_cast<std::int32_t>(sets.size());
			for (std::size_t j = 0; j < sets.size(); ++j) {
				g_str[Lower(std::format("dish.{}.s{}", idx, j))] = sets[j];
				g_int[Lower(std::format("dish.{}.nm{}", idx, j))] = 0;
				for (auto& k : Split(sets[j], '|')) {
					if (k.empty()) continue;
					ourSlotKeys.insert(k);  // our rules now take it: the name rule leaves it to the look
				}
			}
			for (auto* f : generalFlavours) {
				g_str[Lower(std::format("dishc.{}.{}", idx, f))] = std::string(f) + " " + name;  // (DishName names it after its main ingredient)
			}
			if (auto sig = RecipeSigOfSets(sets); !sig.empty()) {
				g_recipeSigs[PlaceSig(Int(std::format("dish.{}.ov", idx)) == 1, sig)].emplace_back(name, std::string());
			}
			g_dishSaveKeys[idx] = std::format("f:{}", gd.name);  // stable whatever mods load (English, not translated)
			screen.index = idx;
			screen.name = name;
			screen.tier = gd.tier;
			g_dishes.push_back(std::move(screen));
			++generalCount;
		}
		// the main ingredient's short name, for the general dishes' cooked names ("Raw Goat Meat" -> "Goat")
		for (const char* g : { "Game meat", "Big game", "Fish", "Fruit & nuts", "Berries" }) {
			for (auto* item : itemsByGroup[g]) {
				if (const auto s = words.ShortName(NameOf(item)); !s.empty()) g_str["ish." + PlainKeyOf(item)] = s;
			}
		}
		say(std::format("General dishes: {} (Grilled Meat, Grilled Fish, Meat and Fish Stew, Meat, Fish and Fruit Pie, Fish and Fruit Dumpling, Fish Cakes)", generalCount));

		// a new dish only if every slot has something that can fill it (checked before it takes its look and name)
		const auto fillable = [&](const FCPatch::DishResult& a_r) {
			for (auto& s : a_r.slots) {
				const bool any = s.named ? !keysByName[Lower(s.text)].empty() : !itemsByGroup[s.text].empty();
				if (!any) return false;
			}
			return !a_r.slots.empty();
		};
		// twice: once as the patcher decides on its own (the page's "Automatic"), once with the player's choices, which the
		// dish rules weigh in reading order (a dish left out frees its look for the next one; one made new takes it)
		auto       autoInputs = inputs;
		// the name rule before the look rule: does one of our recipes take this named item?
		const auto ourRecipeTakes = [&](const std::string& a_name) {
			const auto k = keysByName.find(Lower(a_name));
			return k == keysByName.end() || std::ranges::any_of(k->second, [&](const std::string& a_key) { return ourSlotKeys.contains(a_key); });
		};
		// a look we already have (vanilla, or a modded dish read before) covers a dish only if our own rules cook it: one sample
		// item per slot, at its station, through the same chooser as the cooking screen. The look says what kind of dish it
		// is (all vanilla stews share one bowl), so the chosen dish only has to use its main ingredient (the named one, or a
		// meat, fish or fruit) and be more than a one-ingredient grill when the mod's dish has more than one main slot.
		// "Catfish Pie" on the apple pie mesh: Fish Pie bakes catfish, so it's covered; without it, it would be a new dish
		const auto rulesCook = [&](std::size_t a_i, const FCPatch::DishResult& a_r) {
			static const std::set<std::string> mains{ "game meat", "big game", "fish", "river fish", "cold-water fish", "exotic fish", "shellfish", "fruit & nuts", "berries" };
			std::vector<std::string> keys;
			std::vector<std::string> mainKeys;  // every item with the main slot's name: two can share one ("Mammoth Snout": the
			                                    // quest-only copy we exclude, and the real one), so the check tries each
			std::int32_t mainEntry = -1;
			std::int32_t bodySlots = 0;  // slots that aren't the seasoning
			for (auto& s : a_r.slots) {
				std::string key;
				if (s.named) {
					if (auto k = keysByName.find(Lower(s.text)); k != keysByName.end() && !k->second.empty()) {
						key = k->second.front();
						if (mainEntry < 0) mainKeys = k->second;
					}
				} else if (auto g = itemsByGroup.find(s.text); g != itemsByGroup.end() && !g->second.empty()) {
					key = PlainKeyOf(g->second.front());
				}
				if (key.empty()) return true;  // can't test it: leave it to the look, as before
				if (s.named || s.text != FCPatch::HERB_OR_SALT) ++bodySlots;
				if (mainEntry < 0 && (s.named || mains.contains(Lower(s.text)))) mainEntry = static_cast<std::int32_t>(keys.size());
				keys.push_back(key);
			}
			if (keys.empty()) return true;
			if (mainEntry < 0) mainEntry = 0;
			// one common partner of each kind, so the check asks whether our rules cook the main ingredient into a dish of
			// several ingredients at all ("Mammoth Stew" with cold herbs: Meat Stew cooks mammoth with any vegetable)
			for (const char* partner : { "Vegetables", "Flour", FCPatch::HERB_OR_SALT, "Dairy", "Sweeteners", "Egg", "Mushrooms" }) {
				if (auto g = itemsByGroup.find(partner); g != itemsByGroup.end() && !g->second.empty()) keys.push_back(PlainKeyOf(g->second.front()));
			}
			const bool baked = (a_i < stations.size() && stations[a_i][1] && !stations[a_i][0] && !stations[a_i][2]) || a_r.baked;
			const int place = g_place.load();
			g_place.store(baked ? 3 : 1);  // the oven or the pot (runs at data load, before any cooking screen)
			bool cooks = CanCookWith(keys, mainEntry, bodySlots < 2 ? 1 : 2);
			for (std::size_t m = 1; !cooks && m < mainKeys.size() && m < 8; ++m) {
				keys[mainEntry] = mainKeys[m];
				cooks = CanCookWith(keys, mainEntry, bodySlots < 2 ? 1 : 2);
			}
			g_place.store(place);
			return cooks;
		};
		const auto autoResults = FCPatch::DecideDishes(autoInputs, words, existing, rawByKey, fillable, ourRecipeTakes, rulesCook);
		// a quest recipe stays as its mod made it (in Misc), whatever an edits file says: its quest needs it
		const auto isQuest = [&](std::size_t a_i) { return autoResults[a_i].why == "quest recipe"; };
		for (std::size_t i = 0; i < inputs.size(); ++i) {
			auto& d = inputs[i];
			if (isQuest(i)) continue;
			if (const auto e = edits.dishes.find(FCPatch::ItemKey(d.plugin, d.localId)); e != edits.dishes.end()) {
				d.forced = e->second.choice == FCPatch::DishChoice::New ? 1 : e->second.choice == FCPatch::DishChoice::Not ? 2 : e->second.choice == FCPatch::DishChoice::Misc ? 3 : 0;
			}
		}
		auto       results = FCPatch::DecideDishes(inputs, words, existing, rawByKey, fillable, ourRecipeTakes, rulesCook);  // (already in order: unchanged)

		std::unordered_set<std::string> unrecognisedNames;
		for (auto* item : unrecognised) unrecognisedNames.insert(Lower(NameOf(item)));
		say("");
		say("== Dishes");
		std::unordered_set<std::string> newDishKeys;  // every item a new modded dish's recipe takes
		static constexpr std::array<const char*, 8> flavours{ "Mighty", "Brainy", "Hearty", "Sneaky", "Earthy", "Spicy", "Chilly", "Zesty" };
		// where our own dishes are made, by the name the variant check reads (base key): away from the oven, at the oven
		std::map<std::string, std::pair<bool, bool>> ourNameStations;
		for (std::int32_t n = 0; n < next; ++n) {
			const bool oven = Int(std::format("dish.{}.ov", n)) == 1;
			std::vector<std::string> names{ Str(std::format("dish.{}.n", n)) };
			if (auto f = PluginForm(static_cast<RE::FormID>(Int(std::format("dish.{}.id", n))), Str(std::format("dish.{}.p", n)).c_str())) {
				names.push_back(NameOf(f));
			}
			for (const auto& name : names) {
				auto& at = ourNameStations[words.BaseKey(name)];
				(oven ? at.second : at.first) = true;
			}
		}
		for (std::size_t i = 0; i < results.size(); ++i) {
			auto&       r = results[i];
			const auto& d = inputs[i];
			auto        form = forms[i];
			const auto  label = std::format("{} [{}|{:06X}]", d.name, d.plugin, d.localId);
			// a variant by name only where one of our dishes of that name is made at its station: an oven-only "Cooked
			// Beef" is not our campfire Cooked Beef, and hiding its recipe left it unmakeable (Codex review 2026-11-16, M10)
			if (r.verdict == FCPatch::DishVerdict::Variant && r.why == "variant of an existing dish" && i < stations.size() && !r.slots.empty()) {
				const auto at = ourNameStations.find(words.BaseKey(d.name));
				const bool atOven = stations[i][1], away = stations[i][0] || stations[i][2];
				if (at != ourNameStations.end() && !((atOven && at->second.second) || (away && at->second.first))) {
					r.verdict = FCPatch::DishVerdict::NewDish;
					r.why = "same name as one of our dishes, made at another station";
				}
			}

			// the page's row, and the hand edits
			const auto kindOf = [](FCPatch::DishVerdict a_v) -> const char* {
				switch (a_v) {
				case FCPatch::DishVerdict::NewDish: return "New dish";
				case FCPatch::DishVerdict::VanillaLook: return "Vanilla look";
				case FCPatch::DishVerdict::SameLook: return "Same look";
				case FCPatch::DishVerdict::Variant: return "Variant";
				case FCPatch::DishVerdict::Misc: return "Misc";
				case FCPatch::DishVerdict::NoLook: return "No mesh";
				case FCPatch::DishVerdict::LeftOut: return "Left out";
				default: return "No ingredient fits a slot";
				}
			};
			const auto slotEdits = [&](const std::vector<FCPatch::DishSlot>& a_slots) {
				std::vector<FCPatch::SlotEdit> out;
				for (auto& s : a_slots) {
					if (!s.named) {
						out.push_back({ false, s.text });
					} else if (!s.key.empty()) {
						out.push_back({ true, s.key });  // the exact item chosen by hand
					} else if (auto k = keyOfItemName.find(Lower(s.text)); k != keyOfItemName.end()) {
						out.push_back({ true, k->second });
					}
				}
				return out;
			};
			// a dish whose name names an ingredient the patcher didn't recognise ("Pineapple Pie" before Pineapple has a
			// group) waits for the player to set that ingredient: "To check", not "left out"
			const auto waitingFor = [&](const FCPatch::DishResult& a_r) -> std::string {
				if (a_r.verdict != FCPatch::DishVerdict::Unusable) return {};
				for (auto& s : a_r.slots) {
					if (!s.named) continue;
					// only an ingredient the player can give a group (one the patcher didn't recognise); anything else can't be fixed
					// that way, so the dish stays "a slot nothing can fill"
					if (auto k = keysByName.find(Lower(s.text)); (k == keysByName.end() || k->second.empty()) && unrecognisedNames.contains(Lower(s.text))) return s.text;
				}
				return {};
			};
			FC::PatchRow row;
			row.dish = true;
			row.key = FCPatch::ItemKey(d.plugin, d.localId);
			row.name = d.name;
			row.plugin = d.plugin;
			row.why = autoResults[i].why;
			row.autoKind = kindOf(autoResults[i].verdict);
			row.food = true;
			if (const auto fa = foodAutoResult.find(row.key); fa != foodAutoResult.end()) {  // a found food the player made a dish
				const auto& a = fa->second;
				row.autoKind = a.kind == FCPatch::Kind::Ingredient ? "Ingredient" : a.kind == FCPatch::Kind::Unknown ? "Not recognised" : a.kind == FCPatch::Kind::Misc ? "Misc" : "Left out";
				row.autoGroup = a.kind == FCPatch::Kind::Ingredient ? a.group : std::string();
				row.autoFlavour = a.kind == FCPatch::Kind::Ingredient && a.restriction != FCPatch::Restriction::Dragon ? GroupFlavour(words, a.group, d.name) : std::string();
				row.autoStars = a.stars;
				row.autoSlots.clear();
				row.why = a.why;
			}
			if (!waitingFor(autoResults[i]).empty()) row.autoKind = "Not recognised";
			row.autoTier = autoResults[i].tier;
			row.autoSlots = slotEdits(autoResults[i].slots);
			row.quest = isQuest(i);
			row.oven = (stations[i][1] && !stations[i][0] && !stations[i][2]) || autoResults[i].baked;
			row.edited = !row.quest && playerEdits.dishes.contains(row.key);
			row.patched = !row.quest && patchFiles.dishes.contains(row.key);
			const bool leftOut = r.verdict == FCPatch::DishVerdict::LeftOut;
			if (const auto e = edits.dishes.find(row.key); e != edits.dishes.end() && !row.quest) {
				const auto& ed = e->second;
				if (!ed.tier.empty()) r.tier = ed.tier;
				if (ed.slotsSet) {
					r.slots.clear();
					for (auto& s : ed.slots) {
						if (s.item) {
							if (auto n = nameOfItemKey.find(s.text); n != nameOfItemKey.end()) r.slots.push_back({ true, n->second, s.text });
						} else if (isGroup(s.text) || FCPatch::SlotCategoryOf(s.text)) {
							r.slots.push_back({ false, s.text });
						}
					}
				}
			}
			if (row.edited || row.patched) r.why = row.edited ? "set by hand" : "set by a patch file";
			row.kind = kindOf(r.verdict);
			row.waits = waitingFor(r);
			if (!row.waits.empty()) row.kind = "Not recognised";
			row.tier = r.tier;
			row.slots = slotEdits(r.slots);
			const auto rowIndex = g_patchRows.size();
			g_patchRows.push_back(std::move(row));
			if (form && (leftOut || r.verdict == FCPatch::DishVerdict::NoLook || r.verdict == FCPatch::DishVerdict::LeftOut || r.verdict == FCPatch::DishVerdict::Unusable)) {
				g_patchHidden.insert(form->GetFormID());  // left out: not offered in Misc either
			}
			if (leftOut) {
				say("  -  " + label + ": left out by hand");
				continue;
			}
			if (form && (r.verdict == FCPatch::DishVerdict::VanillaLook || r.verdict == FCPatch::DishVerdict::SameLook || r.verdict == FCPatch::DishVerdict::Variant)) {
				g_patchCooked.insert(form->GetFormID());  // our rules cook it: its own recipe stays out of Misc
			}
			switch (r.verdict) {
			case FCPatch::DishVerdict::VanillaLook: ++n.vanillaLook; say("  =  " + label + ": looks like a vanilla dish, our rules already make it"); continue;
			case FCPatch::DishVerdict::SameLook: ++n.sameLook; say("  =  " + label + ": same look as " + (r.sameAs < inputs.size() ? inputs[r.sameAs].name : std::string("another dish"))); continue;
			case FCPatch::DishVerdict::Variant: ++n.variants; say("  =  " + label + ": a variant of an existing dish"); continue;
			case FCPatch::DishVerdict::Misc: ++n.misc; say("  M  " + label + ": Misc (" + r.why + "), not in the pot's rules"); continue;
			case FCPatch::DishVerdict::NoLook: say("  -  " + label + ": no mesh, left out"); continue;
			case FCPatch::DishVerdict::LeftOut: continue;
			case FCPatch::DishVerdict::Unusable:
				if (const auto w = waitingFor(r); !w.empty()) say("  ?  " + label + ": not added yet, its recipe needs " + w + ", which was not recognised: give " + w + " a group on the patcher page and restart");
				else say("  -  " + label + ": a recipe slot has nothing that can fill it, left out");
				continue;
			default: break;
			}
			// a new Cooking Skill Overhaul dish: its slots as rule keys and as the cooking screen's choices
			CookingMenu::Dish screen;
			screen.index = next;
			screen.name = r.displayName;
			screen.tier = r.tier == "Elaborate" ? 4 : r.tier == "Prepared" ? 2 : 0;
			screen.drink = words.GroupOf(d.name) == "Drinks";
			std::vector<std::string> sets;
			std::vector<int>         named;
			bool                     ok = !r.slots.empty();
			for (auto& s : r.slots) {
				CookingMenu::Slot slot;
				std::string       set = "|";
				if (s.named && !s.key.empty()) {
					// chosen by hand: only that item, even when another mod has an item of the same name
					if (const auto it = itemOfKey.find(s.key); it != itemOfKey.end()) {
						slot.options.push_back(it->second);
						set += PlainKeyOf(it->second) + "|";
					}
				} else if (s.named) {
					for (auto& k : keysByName[Lower(s.text)]) set += k + "|";
					for (auto& [item, group] : tableItems) {
						if (Lower(NameOf(item)) == Lower(s.text)) slot.options.push_back(item);
					}
					for (auto& p : patched) {
						if (Lower(NameOf(p.item)) == Lower(s.text)) slot.options.push_back(p.item);
					}
				} else {
					const auto* cat = FCPatch::SlotCategoryOf(s.text);
					slot.label = cat ? std::string(cat->label) : AnyLabel(s.text);  // the label our own recipes use
					for (auto item : itemsByGroup[s.text]) {
						slot.options.push_back(item);
						set += PlainKeyOf(item) + "|";
					}
				}
				if (slot.options.empty()) {
					ok = false;
					break;
				}
				sets.push_back(set);
				named.push_back(s.named ? 1 : 0);
				screen.slots.push_back(std::move(slot));
			}
			if (!ok) {
				if (form) g_patchHidden.insert(form->GetFormID());
				g_patchRows[rowIndex].kind = "No ingredient fits a slot";
				say("  -  " + label + ": a recipe slot has nothing that can fill it, left out");
				continue;
			}
			// the same recipe as a dish the pot already makes (ours, or one added just before): that dish covers it, like a
			// variant (his call 2026-11-14: "does it matter if your crab pie has a different name?"); the player can still give
			// it another recipe on the patcher page to make it a dish of its own
			const auto sig = RecipeSigOfSets(sets);
			const bool ovenDish = (stations[i][1] && !stations[i][0] && !stations[i][2]) || r.baked;
			if (auto clash = g_recipeSigs.find(PlaceSig(ovenDish, sig)); clash != g_recipeSigs.end() && !clash->second.empty()) {
				auto& pageRow = g_patchRows[rowIndex];
				const auto other = clash->second.front().first;
				pageRow.kind = "Variant";
				pageRow.why = "same recipe as " + other;
				if (!pageRow.edited && !pageRow.patched) pageRow.autoKind = "Variant";  // what the patcher does on its own
				if (form) g_patchCooked.insert(form->GetFormID());
				++n.variants;
				say("  =  " + label + ": same recipe as " + other + ", our rules make that dish");
				continue;
			}
			g_recipeSigs[PlaceSig(ovenDish, sig)].emplace_back(r.displayName, g_patchRows[rowIndex].key);
			const auto idx = next++;
			g_str[Lower(std::format("dish.{}.n", idx))] = r.displayName;
			g_str[Lower(std::format("dish.{}.p", idx))] = d.plugin;
			g_int[Lower(std::format("dish.{}.id", idx))] = static_cast<std::int32_t>(d.localId);
			g_int[Lower(std::format("dish.{}.t", idx))] = screen.tier;
			g_int[Lower(std::format("dish.{}.dr", idx))] = screen.drink ? 1 : 0;
			// a modded drink's alcohol, by its name like the drink ingredients above (thirst and the needs mods' lists ask)
			if (screen.drink && !words.InCategory("nonalcohol", "Drinks", d.name)) g_int[Lower(std::format("dish.{}.al", idx))] = 1;
			g_int[Lower(std::format("dish.{}.fe", idx))] = 0;
			// baked: only at an oven (made only at an oven in its mod, or named as baked: "Blackbird Pie", made at a pot in its
			// mod, is an oven dish here like our own pies)
			const bool baked = (stations[i][1] && !stations[i][0] && !stations[i][2]) || r.baked;
			if (baked) g_int[Lower(std::format("dish.{}.ov", idx))] = 1;
			// all its mod's recipes for it wait for a quest: ours unlocks with the same one (his rule 2026-11-16)
			if (!r.lockRecipes.empty()) {
				std::vector<RE::BGSConstructibleObject*> locks;
				for (const auto id : r.lockRecipes) {
					if (auto lock = RE::TESForm::LookupByID<RE::BGSConstructibleObject>(id)) {
						locks.push_back(lock);
					}
				}
				if (!locks.empty()) {
					g_lockedDishes[idx] = std::move(locks);
					say("  #  " + label + ": unlocks with its mod's quest, like its own recipe");
				}
			}
			if (!baked && (stations[i][2] || (r.tier == "Simple" && !screen.drink))) g_int[Lower(std::format("dish.{}.spit", idx))] = 1;  // grilled or roasted: a campfire too
			if (screen.drink) g_int[Lower(std::format("dish.{}.pr", idx))] = 2;  // a drink, like the brews: made only when no food dish matches
			g_int[Lower(std::format("dish.{}.ns", idx))] = static_cast<std::int32_t>(sets.size());
			for (std::size_t j = 0; j < sets.size(); ++j) {
				for (auto& k : Split(sets[j], '|')) {
					if (!k.empty()) newDishKeys.insert(k);
				}
				g_str[Lower(std::format("dish.{}.s{}", idx, j))] = sets[j];
				g_int[Lower(std::format("dish.{}.nm{}", idx, j))] = named[j];
				if (named[j]) {
					for (auto& k : Split(sets[j], '|')) {
						if (!k.empty()) g_recipeKeys.insert(k);
					}
				}
			}
			if (sets.size() == 1 && named[0]) {  // a named item alone (not "any mushroom": the whole group would turn into "a food")
				for (auto& k : Split(sets[0], '|')) {
					if (!k.empty()) g_int["main." + k] = 1;  // makes a dish on its own
				}
			}
			std::string shortName = r.displayName.starts_with("Cooked ") ? r.displayName.substr(7) : r.displayName;
			for (auto* f : flavours) {
				g_str[Lower(std::format("dishc.{}.{}", idx, f))] = std::string(f) + " " + shortName;
			}
			g_dishByBase[form->GetFormID()] = idx;
			g_patchCooked.insert(form->GetFormID());
			g_dishSaveKeys[idx] = std::format("m:{}|{:06X}", d.plugin, d.localId);
			screen.index = idx;
			g_dishes.push_back(std::move(screen));
			++n.newDishes;
			std::string slotText;
			for (auto& s : r.slots) slotText += (slotText.empty() ? "" : " + ") + (s.named ? s.text : AnyLabel(s.text));
			say(std::format("  +  {}: new dish \"{}\", {} ({}), {}", label, r.displayName, r.tier, r.why, slotText));
		}

		// (the fallback dishes Grilled Meat and Grilled Fish of 2026-11-13 are general dishes now: see "general dishes" above)

		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
		say("");
		say(std::format("Totals: {} new ingredients ({} joined recipe slots), {} not recognised; {} new dishes, {} with a vanilla look, {} with a modded look already used, {} variants, {} Misc. {} ms.",
			n.ingredients, n.slotsJoined, n.unknown, n.newDishes, n.vanillaLook, n.sameLook, n.variants, n.misc, ms));
		logger::info("Patcher: {} new ingredients, {} not recognised, {} new dishes ({} vanilla look, {} same look, {} variants, {} Misc) in {} ms; report in FreeCooking_Patcher.txt",
			n.ingredients, n.unknown, n.newDishes, n.vanillaLook, n.sameLook, n.variants, n.misc, ms);
		g_patcherRan = true;
	}

	// ---- the patcher page's file actions (UI thread)
	FCPatch::EditSet LoadPlayerEdits()
	{
		FCPatch::EditSet e;
		std::error_code  ec;
		if (const auto path = MyGamesFile(EDITS_FILE); !path.empty() && std::filesystem::exists(path, ec)) {
			e.Read(ReadWordFile(path));
		}
		return e;
	}

	bool WriteMyGamesFile(const char* a_name, const std::string& a_text)
	{
		const auto path = MyGamesFile(a_name);
		if (path.empty()) return false;
		auto temp = path;  // written whole, then swapped in, so a crash never leaves half a file
		temp += L".tmp";
		{
			std::ofstream out(temp, std::ios::binary | std::ios::trunc);
			if (!out) return false;
			out << a_text;
			out.flush();
			if (!out) return false;
		}
		std::error_code ec;
		std::filesystem::rename(temp, path, ec);  // replaces the old file (MoveFileEx with MOVEFILE_REPLACE_EXISTING)
		if (ec) {
			std::error_code ignored;
			std::filesystem::remove(temp, ignored);
			return false;
		}
		return true;
	}

	// ---- dishes in our part of the save (recipe book counts, known dishes): Cooking Skill Overhaul's own dishes by index as before;
	// a dish the patcher added by its save key, since its index changes when food mods are added or removed
	constexpr std::int32_t DISH_BY_KEY = -2;

	void WriteDishRef(SKSE::SerializationInterface* a_intfc, std::int32_t a_dish)
	{
		const auto it = g_dishSaveKeys.find(a_dish);
		if (it == g_dishSaveKeys.end()) {
			a_intfc->WriteRecordData(a_dish);
			return;
		}
		a_intfc->WriteRecordData(DISH_BY_KEY);
		const auto len = static_cast<std::uint16_t>((std::min)(it->second.size(), std::size_t{ 0xFFFF }));
		a_intfc->WriteRecordData(len);
		a_intfc->WriteRecordData(it->second.data(), len);
	}

	// false: the record ended early. a_found false: a modded dish that isn't in this load order any more
	bool ReadDishRef(SKSE::SerializationInterface* a_intfc, std::int32_t& a_dish, bool& a_found)
	{
		a_found = false;
		if (!ReadAll(a_intfc, a_dish)) return false;
		if (a_dish != DISH_BY_KEY) {
			a_found = true;
			return true;
		}
		std::uint16_t len = 0;
		if (!ReadAll(a_intfc, len)) return false;
		std::string key(len, '\0');
		if (len && !ReadAll(a_intfc, key.data(), len)) return false;
		for (const auto& [index, saved] : g_dishSaveKeys) {
			if (Lower(saved) == Lower(key)) {  // the scripts' StorageUtil keys ignore case too
				a_dish = index;
				a_found = true;
				break;
			}
		}
		return true;
	}

	// ---- the scripts read the pot rules through these (FC_Menu.psc); written only at kDataLoaded, so reading needs no lock
	std::int32_t PapyrusRuleInt(RE::StaticFunctionTag*, std::string a_key) { return Int(a_key); }
	std::string  PapyrusRuleText(RE::StaticFunctionTag*, std::string a_key) { return Str(a_key); }
	// player-facing text from the scripts, in the player's language (FC::Tr / FC::TrF: a broken translation shows English)
	std::string  PapyrusTr(RE::StaticFunctionTag*, std::string a_english) { return FC::Tr(a_english.c_str()); }
	std::string  PapyrusTrF(RE::StaticFunctionTag*, std::string a_english, std::string a_0, std::string a_1, std::string a_2)
	{
		return FC::TrF(a_english.c_str(), a_0, a_1, a_2);
	}
	bool         PapyrusHasRuleText(RE::StaticFunctionTag*, std::string a_key) { return g_str.contains(Lower(a_key)); }
	// (by form ID, resolved here: forms don't cross the boundary; Codex review 2026-11-16, L3)
	std::string  PapyrusItemKey(RE::StaticFunctionTag*, std::int32_t a_formId) { return PlainKeyOf(RE::TESForm::LookupByID(static_cast<RE::FormID>(a_formId))); }
	// a potion's flavour as a pot seasoning ("" = not one), by the one rule the cooking screen also uses (Rules.inl
	// JudgePotion): the scripts' own copy of the rule fell behind it (Claude review 2026-11-22, M1)
	std::string  PapyrusPotionFlavour(RE::StaticFunctionTag*, std::int32_t a_formId) { return PotionFlavour(RE::TESForm::LookupByID(static_cast<RE::FormID>(a_formId))); }
	std::string  PapyrusDishName(RE::StaticFunctionTag*, std::int32_t a_dish, std::string a_flavour) { return DishName(a_dish, a_flavour, true); }
	std::string  PapyrusDishSaveKey(RE::StaticFunctionTag*, std::int32_t a_dish)
	{
		const auto it = g_dishSaveKeys.find(a_dish);
		return it != g_dishSaveKeys.end() ? it->second : std::to_string(a_dish);
	}

	// ---- the Potions page (2026-11-22, his call): every good potion the game has (vanilla and mods), what it does in the
	// pot on its own, and the player's choice. Built once at game start, after the patcher, read-only after that.
	std::vector<FC::PotionRow> g_potionRows;

	void BuildPotionRows()
	{
		g_potionRows.clear();
		auto data = RE::TESDataHandler::GetSingleton();
		if (!data) {
			return;
		}
		CookLock lock(g_cookLock);
		std::size_t mixed = 0;
		for (auto alch : data->GetFormArray<RE::AlchemyItem>()) {
			if (!alch || alch->IsFood() || alch->IsPoison() || alch->IsDynamicForm()) {
				continue;
			}
			const std::string name = alch->GetFullName();
			if (name.empty()) {
				continue;  // a hidden item: nothing to show
			}
			const auto v = JudgePotion(alch, false);
			if (v.harmful) {
				continue;  // never a seasoning: not listed
			}
			FC::PotionRow row;
			row.key = PotionKey(alch);
			if (row.key.empty()) {
				continue;
			}
			row.name = name;
			row.plugin = FileOf(alch);
			if (auto last = alch->GetFile(-1); last && last != alch->GetFile(0)) {
				row.changedBy = std::string(last->GetFilename());  // the plugin whose version the game uses (Apothecary changes vanilla potions)
			}
			row.flavours = v.flavours;
			row.autoFlavour = v.flavour;
			row.autoWhy = v.why;
			// a patch file's real choice (an AUTO line chooses nothing); the page also hides the mark while the player's own
			// edits file has the potion (Claude review 2026-11-22b, L1)
			if (const auto it = g_patchFileEdits.potions.find(row.key); it != g_patchFileEdits.potions.end() && it->second != "AUTO") {
				row.byPatchFile = true;
			}
			for (auto effect : alch->effects) {
				auto base = effect ? effect->baseEffect : nullptr;
				if (!base) continue;
				std::string fx = base->GetFullName();
				if (fx.empty()) continue;
				if (const auto f = EffectFlavour(base); !f.empty()) fx += " (" + std::string(FC::Tr(f.c_str())) + ")";
				row.effects += (row.effects.empty() ? "" : ", ") + fx;
			}
			mixed += row.flavours.size() > 1 ? 1 : 0;
			g_potionRows.push_back(std::move(row));
		}
		std::ranges::sort(g_potionRows, [](const FC::PotionRow& a_l, const FC::PotionRow& a_r) {
			return a_l.name != a_r.name ? a_l.name < a_r.name : a_l.key < a_r.key;
		});
		logger::info("Potions page: {} good potions, {} with several flavours, {} set by hand or by a patch file", g_potionRows.size(), mixed, g_potionChoice.size());
	}
