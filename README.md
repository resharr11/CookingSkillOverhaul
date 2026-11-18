# Cooking Skill Overhaul

Source code of Cooking Skill Overhaul, an SKSE mod for Skyrim Special Edition and Anniversary Edition.

The mod itself (plugin, meshes, textures, compiled scripts) is on Nexus Mods:
https://www.nexusmods.com/skyrimspecialedition/mods/199590

This repository holds the code that builds `FreeCooking.dll` (the SKSE plugin), the Papyrus script sources, and the text and data files the plugin reads. `FreeCooking` is the mod's working name and still names its files.

## Game version

Built and tested on Skyrim 1.7.104. The plugin is built against the 1.7.99 and newer memory layouts, so it does not work on 1.6.1170 or 1.5.97. I don't make versions for those, but the code is here for anyone who wants to port it.

## What is where

```
CMakeLists.txt, CMakePresets.json   build setup
vcpkg.json, vcpkg-configuration.json   libraries (spdlog, simpleini, commonlib-shared)
build.bat                           builds the DLL with Visual Studio (see below)
check_paths.ps1                     checks the built DLL for folder paths and the Windows user name
cmake/                              version files, vcpkg overlay ports and triplet
extern/CommonLibSSE/                CommonLibSSE (powerof3), the copy this mod is built with
src/                                the SKSE plugin
  main.cpp                          plugin entry point
  Cooking/                          cooking rules, dishes, meal effects, Survival features, hooks
  Patcher/                          the patcher that turns other mods' foods into dishes
  CookingMenu.cpp                   the cooking screen
  Menu.cpp, Settings.*              settings pages (SKSE Menu Framework)
  SkillsMenu.cpp                    adds Cooking to the Skills menu
papyrus/Scripts/                    Papyrus script sources
papyrus/Stubs/                      compile-only stand-ins for other mods' scripts
papyrus/compile.bat                 compiles the scripts
data/                               files that go into Skyrim's Data folder, as shipped
```

## Building the DLL

You need:

1. Visual Studio 2026 with the "Desktop development with C++" workload (it includes CMake and vcpkg).
2. Git.
3. A folder for the repository with no spaces anywhere in its path, for example `C:\Mods\CookingSkillOverhaul`. The build removes folder paths from the DLL, and that option stops with an error when the path two folders up has a space.

Then run `build.bat`. The first build downloads and builds the libraries, which takes a while; later builds are fast. The DLL ends up in `build\Release\FreeCooking.dll`, and the last lines of the window say whether the build worked. If something fails, the details are in `build_log.txt`.

Without the batch file: open a "Developer Command Prompt for VS", set `VCPKG_ROOT` to your vcpkg folder, then run `cmake --preset release` and `cmake --build --preset release`.

## Compiling the scripts

`papyrus/compile.bat` uses the Creation Kit's Papyrus compiler. Open it in a text editor first and set `SKY` (your Skyrim folder) and `MODS` (the folder that holds the script sources of SKSE, PapyrusUtil SE, powerofthree's Papyrus Extender, Dylbills Papyrus Functions and Custom Skills Framework). The compiled scripts go to `papyrus/compiled`.

## Licence

My code is licensed under the GNU General Public License, version 3 or later (see `LICENSE`). If you release something built from it, its source must be released under the same terms.

Third-party code in this repository keeps its own licence:

- CommonLibSSE by powerof3 and contributors: GPL-3.0-or-later with the Modding Exception (`extern/CommonLibSSE/COPYING.txt`, `extern/CommonLibSSE/EXCEPTIONS.md`).
- SKSE Menu Framework header (`src/SKSEMenuFramework.h`): LGPL-2.1 (`src/SKSEMenuFramework.LICENSE.txt`).
- commonlib-shared, spdlog and simpleini are downloaded by vcpkg during the build and keep their own licences.

The plugin (`FreeCooking.esp`), meshes, textures and other art are not in this repository; their permissions are on the Nexus page.

## Bug reports

Please report bugs on the Nexus page's Bugs tab, with your `FreeCooking.log` (Documents\My Games\Skyrim Special Edition\SKSE).
