#include "Settings.h"

#include <windows.h>

#include <atomic>
#include <fstream>

// ---------------------------------------------------------------------------------------------------------------
// Cooking in the vanilla Skills menu, with no file written into Data (his rules, 2026-11-12: "we never want to break
// saves. and we aim for convenience"; "keep it fixed"; other mods' menus: Cooking stays on its power).
//
// Custom Skills Framework (CSF) replaces the vanilla Skills menu when Data/SKSE/Plugins/CustomSkills/SKILLS.json exists
// (only one mod can own that path). Cooking Skill Overhaul used to write that file at runtime, which under Mod Organizer 2 lands
// in the Overwrite folder and stays after uninstalling. Instead, Cooking Skill Overhaul now shows CSF a SKILLS.json that only
// exists in memory: the 18 vanilla skills with Cooking right of Alchemy, and Cooking Skill Overhaul's own 19-slot sky.
//
// How: CSF 3.2.0 links the C runtime statically, so its folder listing (std::filesystem::directory_iterator) and file
// reading (std::ifstream) call FindFirstFileExW / FindNextFileW / FindClose / CreateFileW through CustomSkills.dll's
// own import table (checked with objdump on the shipped DLL). At kPostLoad, before CSF reads its files at kDataLoaded,
// those four entries of CustomSkills.dll's import table (and only that DLL's) are pointed at the functions below:
//   - listing Data/SKSE/Plugins/CustomSkills: after the real files, one extra entry "SKILLS.json" is reported;
//   - opening that SKILLS.json: a temporary file in %TEMP% opened with FILE_FLAG_DELETE_ON_CLOSE is returned, holding
//     the JSON; Windows deletes it as soon as CSF closes it (or the game ends, even on a crash).
// Every other call goes to the function that was in the import table before (so Mod Organizer's file system and any
// other plugin's hook keep working).
//
// Steps aside (Cooking keeps its own skill screen, the Cooking Skills power) when any of these is true at game start:
// another mod has a SKILLS.json (its menu is left exactly as it is); Cooking Skill Overhaul's 19-slot sky is missing;
// CustomSkills.dll isn't loaded or doesn't import these functions (another CSF build); any step of the hook setup fails.
// Otherwise the hooks stay in place and the switch decides, at once (his report 2026-11-12: switching it off didn't
// remove Cooking until the next start): switching sets FreeCooking.esp's FC_SkillsMenuReload global (0xAA2), which
// FreeCooking.json names as its "debugReload" global, so CSF reloads its menus as soon as the game runs again and
// no Skills menu is open (CustomSkillsManager::UpdateSkills), and the hooks answer by the switch's new value. The list and the sky always come together: both
// are Cooking Skill Overhaul's own, the sky shipped as a normal file, so the menu never has more skills than sky pictures (a
// mismatch crashes the game on reaching the extra skill: Nexus SE article 12426, and CSF's SetShader has no null check).
// Saves: CSF keeps the menu layout out of saves (it only saves event registrations), so the switch and uninstalling
// are safe mid-playthrough.
// ---------------------------------------------------------------------------------------------------------------

namespace
{
	constexpr auto SKILLS_FILE = "Data/SKSE/Plugins/CustomSkills/SKILLS.json";
	constexpr auto SKYDOME_FILE = "Data/Meshes/FreeCooking/Interface/INTPerkSkydome.nif";
	constexpr auto CSF_DLL = L"CustomSkills.dll";

	// the vanilla skills, left to right, with Cooking right of Alchemy (his call: fixed, between the thief and magic skills)
	constexpr auto SKILLS_JSON = R"({
  "version": 1,
  "skydome": { "model": "FreeCooking/Interface/INTPerkSkydome.nif", "cameraRightPoint": 1 },
  "skills": [ "Enchanting", "Smithing", "HeavyArmor", "Block", "TwoHanded", "OneHanded", "Marksman", "LightArmor", "Sneak",
    "Lockpicking", "Pickpocket", "Speechcraft", "Alchemy", { "$ref": "FreeCooking/Cooking.json" }, "Illusion", "Conjuration",
    "Destruction", "Restoration", "Alteration" ]
}
)";

	using FindFirstFileExW_t = HANDLE(WINAPI*)(LPCWSTR, FINDEX_INFO_LEVELS, LPVOID, FINDEX_SEARCH_OPS, LPVOID, DWORD);
	using FindNextFileW_t = BOOL(WINAPI*)(HANDLE, LPWIN32_FIND_DATAW);
	using FindClose_t = BOOL(WINAPI*)(HANDLE);
	using CreateFileW_t = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

	FindFirstFileExW_t g_findFirst = nullptr;  // what was in CustomSkills.dll's import table before us
	FindNextFileW_t    g_findNext = nullptr;
	FindClose_t        g_findClose = nullptr;
	CreateFileW_t      g_createFile = nullptr;

	bool              g_other = false;      // another mod's SKILLS.json at game start
	bool              g_installed = false;  // the hooks are in place
	std::atomic<bool> g_show = false;       // the switch: CSF is shown our SKILLS.json
	constexpr RE::FormID RELOAD_GLOBAL = 0xAA2;  // FC_SkillsMenuReload, FreeCooking.esp
	constexpr RE::FormID SKILLS_POWER = 0x860;   // FC_CookingSkillsPower, FreeCooking.esp: opens Cooking's own skill screen

	std::mutex          g_searchLock;
	std::vector<HANDLE> g_searches;  // open listings of the CustomSkills folder that still owe the extra entry

	// "...\CustomSkills\*" or ".../CustomSkills/*", any case
	bool IsCustomSkillsListing(LPCWSTR a_pattern)
	{
		if (!a_pattern) {
			return false;
		}
		std::wstring p(a_pattern);
		std::ranges::replace(p, L'/', L'\\');
		std::ranges::transform(p, p.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
		return p.ends_with(L"\\customskills\\*");
	}

	bool IsSkillsJson(LPCWSTR a_path)
	{
		if (!a_path) {
			return false;
		}
		std::wstring p(a_path);
		std::ranges::replace(p, L'/', L'\\');
		std::ranges::transform(p, p.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
		return p.ends_with(L"\\customskills\\skills.json");
	}

	void FillEntry(WIN32_FIND_DATAW& a_data)
	{
		a_data = {};
		a_data.dwFileAttributes = FILE_ATTRIBUTE_ARCHIVE;  // a plain file: std::filesystem counts it as a regular file
		a_data.nFileSizeLow = static_cast<DWORD>(std::char_traits<char>::length(SKILLS_JSON));
		::wcscpy_s(a_data.cFileName, L"SKILLS.json");
	}

	HANDLE WINAPI HookFindFirst(LPCWSTR a_name, FINDEX_INFO_LEVELS a_level, LPVOID a_data, FINDEX_SEARCH_OPS a_op, LPVOID a_filter, DWORD a_flags)
	{
		const HANDLE h = g_findFirst(a_name, a_level, a_data, a_op, a_filter, a_flags);
		if (h != INVALID_HANDLE_VALUE && g_show && IsCustomSkillsListing(a_name)) {
			std::scoped_lock lock(g_searchLock);
			g_searches.push_back(h);
			logger::info("Skills menu: Custom Skills Framework lists its folder; Cooking Skill Overhaul adds SKILLS.json to the list");
		}
		return h;
	}

	BOOL WINAPI HookFindNext(HANDLE a_search, LPWIN32_FIND_DATAW a_data)
	{
		const BOOL ok = g_findNext(a_search, a_data);
		if (ok || ::GetLastError() != ERROR_NO_MORE_FILES || !a_data) {
			return ok;
		}
		std::scoped_lock lock(g_searchLock);
		const auto it = std::ranges::find(g_searches, a_search);
		if (it == g_searches.end()) {
			return ok;
		}
		g_searches.erase(it);  // the extra entry comes once, after the real files; the next call ends the listing
		FillEntry(*a_data);
		::SetLastError(ERROR_SUCCESS);
		return TRUE;
	}

	BOOL WINAPI HookFindClose(HANDLE a_search)
	{
		{
			std::scoped_lock lock(g_searchLock);
			std::erase(g_searches, a_search);  // a listing given up early must not leave its handle behind (handles get reused)
		}
		return g_findClose(a_search);
	}

	HANDLE WINAPI HookCreateFile(LPCWSTR a_name, DWORD a_access, DWORD a_share, LPSECURITY_ATTRIBUTES a_security, DWORD a_disposition,
		DWORD a_flags, HANDLE a_template)
	{
		if (!g_show || !IsSkillsJson(a_name) || (a_access & GENERIC_WRITE) || a_disposition != OPEN_EXISTING) {
			return g_createFile(a_name, a_access, a_share, a_security, a_disposition, a_flags, a_template);
		}
		// a temporary file that deletes itself when closed; outside Data, so nothing reaches Mod Organizer's Overwrite
		wchar_t dir[MAX_PATH], path[MAX_PATH];
		if (!::GetTempPathW(MAX_PATH, dir) || !::GetTempFileNameW(dir, L"fcs", 0, path)) {
			logger::error("Skills menu: no temporary file ({}); Custom Skills Framework sees no SKILLS.json", ::GetLastError());
			::SetLastError(ERROR_FILE_NOT_FOUND);
			return INVALID_HANDLE_VALUE;
		}
		const HANDLE h = g_createFile(path, GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
			CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
		if (h == INVALID_HANDLE_VALUE) {
			::DeleteFileW(path);
			logger::error("Skills menu: no temporary file ({}); Custom Skills Framework sees no SKILLS.json", ::GetLastError());
			::SetLastError(ERROR_FILE_NOT_FOUND);
			return INVALID_HANDLE_VALUE;
		}
		const DWORD size = static_cast<DWORD>(std::char_traits<char>::length(SKILLS_JSON));
		DWORD written = 0;
		if (!::WriteFile(h, SKILLS_JSON, size, &written, nullptr) || written != size ||
			::SetFilePointer(h, 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
			::CloseHandle(h);  // deletes it
			logger::error("Skills menu: the temporary file could not be written; Custom Skills Framework sees no SKILLS.json");
			::SetLastError(ERROR_FILE_NOT_FOUND);
			return INVALID_HANDLE_VALUE;
		}
		logger::info("Skills menu: SKILLS.json served from memory (Cooking right of Alchemy)");
		return h;
	}

	// points one kernel32 import of a module at a_hook; returns what was there before, or nullptr if not imported
	void* PatchImport(HMODULE a_module, const char* a_function, void* a_hook)
	{
		const auto base = reinterpret_cast<std::uint8_t*>(a_module);
		const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
			return nullptr;
		}
		const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
		const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
		if (!dir.VirtualAddress) {
			return nullptr;
		}
		for (auto imp = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
			if (::_stricmp(reinterpret_cast<const char*>(base + imp->Name), "KERNEL32.dll") != 0 || !imp->OriginalFirstThunk) {
				continue;
			}
			auto names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
			auto slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
			for (; names->u1.AddressOfData; ++names, ++slots) {
				if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
					continue;
				}
				const auto byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
				if (std::strcmp(reinterpret_cast<const char*>(byName->Name), a_function) != 0) {
					continue;
				}
				DWORD old = 0;
				if (!::VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &old)) {
					return nullptr;
				}
				void* previous = reinterpret_cast<void*>(slots->u1.Function);
				slots->u1.Function = reinterpret_cast<ULONGLONG>(a_hook);
				::VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), old, &old);
				return previous;
			}
		}
		return nullptr;
	}

	bool SwitchOn()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(FC::IniToRead().wstring().c_str());
		return ini.GetBoolValue("Cooking", "bShowInSkillsMenu", FC::Options{}.skillsMenu);
	}
}

namespace FC
{
	void InstallSkillsMenu()
	{
		std::error_code ec;
		// an earlier Cooking Skill Overhaul build wrote its own SKILLS.json into Data (marked "writtenBy": "Cooking Skill Overhaul"): remove
		// that leftover so it isn't mistaken for another mod's menu
		if (fs::exists(SKILLS_FILE, ec)) {
			std::ifstream f(SKILLS_FILE, std::ios::binary);
			const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			f.close();
			// never deleted from here: nothing is written to or removed from Data while the game runs (his rule; under
			// Mod Organizer 2 that would change a mod folder or Overwrite). Cooking Skill Overhaul steps aside until it is deleted
			if (text.find("\"writtenBy\": \"Cooking Skill Overhaul\"") != std::string::npos) {
				logger::warn("Skills menu: an earlier Cooking Skill Overhaul build left Data/SKSE/Plugins/CustomSkills/SKILLS.json; delete it (in Mod Organizer 2, look in Overwrite) so Cooking can join the Skills menu");
			}
		}
		if (fs::exists(SKILLS_FILE, ec)) {  // ours from an earlier build counts too: Custom Skills Framework would read it
			g_other = true;
			logger::info("Skills menu: another mod replaces the Skills menu (SKILLS.json); Cooking Skill Overhaul leaves it as it is and Cooking keeps its own skill screen");
			return;
		}
		if (!fs::exists(SKYDOME_FILE, ec)) {
			logger::info("Skills menu: Cooking Skill Overhaul's 19-slot sky is missing; Cooking uses its own skill screen");
			return;
		}
		const HMODULE csf = ::GetModuleHandleW(CSF_DLL);
		if (!csf) {
			logger::info("Skills menu: Custom Skills Framework isn't loaded");
			return;
		}
		// all four or none
		g_findFirst = reinterpret_cast<FindFirstFileExW_t>(PatchImport(csf, "FindFirstFileExW", reinterpret_cast<void*>(&HookFindFirst)));
		g_findNext = reinterpret_cast<FindNextFileW_t>(PatchImport(csf, "FindNextFileW", reinterpret_cast<void*>(&HookFindNext)));
		g_findClose = reinterpret_cast<FindClose_t>(PatchImport(csf, "FindClose", reinterpret_cast<void*>(&HookFindClose)));
		g_createFile = reinterpret_cast<CreateFileW_t>(PatchImport(csf, "CreateFileW", reinterpret_cast<void*>(&HookCreateFile)));
		if (!g_findFirst || !g_findNext || !g_findClose || !g_createFile) {
			// put back what was patched
			if (g_findFirst) PatchImport(csf, "FindFirstFileExW", reinterpret_cast<void*>(g_findFirst));
			if (g_findNext) PatchImport(csf, "FindNextFileW", reinterpret_cast<void*>(g_findNext));
			if (g_findClose) PatchImport(csf, "FindClose", reinterpret_cast<void*>(g_findClose));
			if (g_createFile) PatchImport(csf, "CreateFileW", reinterpret_cast<void*>(g_createFile));
			logger::warn("Skills menu: this Custom Skills Framework build reads its files differently; Cooking uses its own skill screen");
			return;
		}
		g_installed = true;
		g_show = SwitchOn();
		logger::info("Skills menu: {}", g_show ? "Cooking joins the vanilla Skills menu, right of Alchemy" : "off; Cooking uses its own skill screen");
	}

	void SetSkillsMenuShown(bool a_show)
	{
		if (!g_installed || g_show == a_show) {
			return;
		}
		g_show = a_show;
		// the settings page runs on the render thread: the game's own data changes on the main thread
		SKSE::GetTaskInterface()->AddTask([a_show]() {
			SyncSkillsPower();
			auto global = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESGlobal>(RELOAD_GLOBAL, "FreeCooking.esp");
			if (!global) {
				logger::warn("Skills menu: FC_SkillsMenuReload not found; the change shows at the next game start");
				return;
			}
			global->value = 1.0f;  // Custom Skills Framework reloads its menus and sets it back to 0
			logger::info("Skills menu: switched {}; Custom Skills Framework reloads its menus", a_show ? "on" : "off");
		});
	}

	// The Cooking Skills power (his call, 2026-11-12: "the cooking skills in menu enable should hide the cooking power"):
	// the player has it only while Cooking isn't in the Skills menu. Cooking Skill Overhaul's scripts no longer hand it out (they
	// used to at every load); this runs at every game load and when the switch changes. Main thread only.
	void SyncSkillsPower()
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		auto power = RE::TESDataHandler::GetSingleton()->LookupForm<RE::SpellItem>(SKILLS_POWER, "FreeCooking.esp");
		if (!player || !power) {
			return;
		}
		const bool want = OtherSkillsMenu() != OtherMenu::kOurs;
		const bool has = player->HasSpell(power);
		if (want && !has) {
			player->AddSpell(power);
			logger::info("Skills menu: the Cooking Skills power is given (Cooking isn't in the Skills menu)");
		} else if (!want && has) {
			player->RemoveSpell(power);
			logger::info("Skills menu: the Cooking Skills power is taken away (Cooking is in the Skills menu)");
		}
	}

	OtherMenu OtherSkillsMenu()
	{
		return g_other ? OtherMenu::kOther : (g_installed && g_show ? OtherMenu::kOurs : OtherMenu::kNone);
	}

	bool SkydomeAvailable()
	{
		std::error_code ec;
		return fs::exists(SKYDOME_FILE, ec);
	}
}

// ---- where the player's settings live (2026-11-12, the 1.0.3 NeedsHUD code review's finding applies here too):
// Documents\My Games\Skyrim Special Edition\SKSE\FreeCooking.ini, next to the SKSE logs, never in Data. Documents is
// found through Windows, so a Documents folder moved to OneDrive or another drive is followed.
#include <ShlObj.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")  // FOLDERID_Documents

namespace FC
{
	std::filesystem::path UserIniPath()
	{
		static const std::filesystem::path path = []() -> std::filesystem::path {
			// the folder SKSE and our own log use: Documents\My Games\Skyrim Special Edition (or ... GOG)\SKSE, picked by
			// the game install, not by which folder happens to exist (2026-11-13 review: with both present, Steam won)
			if (const auto dir = SKSE::log::log_directory()) {
				return *dir / L"FreeCooking.ini";
			}
			// no Documents folder: nowhere. Never Data (his rule: nothing is written to Data at runtime); the settings
			// then stay at their defaults
			return {};
		}();
		return path;
	}

	std::filesystem::path IniToRead()
	{
		const std::filesystem::path legacy = "Data/SKSE/Plugins/FreeCooking/FreeCooking.ini";
		std::error_code ec;
		const auto user = UserIniPath();
		if (std::filesystem::exists(user, ec) || !std::filesystem::exists(legacy, ec)) {
			return user;
		}
		return legacy;
	}
}
