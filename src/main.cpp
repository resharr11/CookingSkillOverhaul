#include "CookingMenu.h"
#include "Settings.h"

SKSE_EXPORT constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v;
	v.PluginVersion({ static_cast<std::uint16_t>(Version::MAJOR), static_cast<std::uint16_t>(Version::MINOR), static_cast<std::uint16_t>(Version::PATCH), 0 });  // from CMakeLists' VERSION (was a fixed 0.1.0.0)
	v.PluginName("FreeCooking");
	v.AuthorName("resharr");
	v.UsesAddressLibrary();
	v.UsesUpdatedStructs();
	return v;
}();

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	// Also creates My Games/Skyrim Special Edition/SKSE/FreeCooking.log
	SKSE::InitInfo init{};
	init.trampoline = true;  // the weapon hit hook (Cooking/HolyDamage.inl): one 5-byte call
	init.trampolineSize = 64;
	SKSE::Init(a_skse, init);
	logger::info("FreeCooking v{}", Version::NAME);
	// 1.7.99 or newer only (his decision 2026-11-18: no 1.6 support). On 1.6 the plugin's game structures don't match
	// (the script VM moved), so it would crash: refusing to load makes SKSE show its "A DLL plugin has failed to load
	// correctly" box, naming FreeCooking.dll "reported as incompatible during load", instead.
	if (const auto game = a_skse->RuntimeVersion(); game < SKSE::RUNTIME_SSE_1_7_99) {
		logger::critical("Skyrim {} is too old: Cooking Skill Overhaul needs 1.7.99 or newer. Not loaded.", game.string());
		return false;
	}
	CookingMenu::RegisterPapyrusFunctions();
	CookingMenu::RegisterSerialization();

	SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* msg) {
		if (msg->type == SKSE::MessagingInterface::kPostLoad) {
			FC::InstallSkillsMenu();  // before Custom Skills Framework reads its files (kDataLoaded)
		}
		if (msg->type == SKSE::MessagingInterface::kPreLoadGame) {
			CookingMenu::GameLoading();
		}
		if (msg->type == SKSE::MessagingInterface::kPostLoadGame && !static_cast<bool>(msg->data)) {
			// the save failed to load (SKSE passes the result as data): nothing of it to set up; the watchers wait for the next
			CookingMenu::GameLoadFailed();
			return;
		}
		if (msg->type == SKSE::MessagingInterface::kPostLoadGame || msg->type == SKSE::MessagingInterface::kNewGame) {
			CookingMenu::GameLoaded(msg->type == SKSE::MessagingInterface::kNewGame);
			FC::SyncSkillsPower();  // the Cooking Skills power only while Cooking isn't in the Skills menu
		}
		if (msg->type == SKSE::MessagingInterface::kDataLoaded) {
			FC::LoadSettings();  // before Register: the Survival features read their switches from the start
			CookingMenu::Register();
			FC::RegisterMenu();  // SKSE Menu Framework pages (Menu.cpp)
		}
	});
	return true;
}
