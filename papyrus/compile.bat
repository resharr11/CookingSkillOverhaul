@echo off
rem Cooking Skill Overhaul: compiles the scripts in Scripts\ into compiled\
rem Stubs\ holds compile-only stand-ins for other mods' scripts (SunHelm, Last Seed, iNeed, Frostfall, Survival Mode Improved): read, never compiled or shipped.
rem Needs the Creation Kit's Papyrus compiler and the script sources of: Skyrim (Scripts.zip from the Creation Kit), SKSE, PapyrusUtil SE,
rem powerofthree's Papyrus Extender, Dylbills Papyrus Functions and Custom Skills Framework. Edit SKY and MODS below to match your setup.
set "SKY=C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition"
set "MODS=%LOCALAPPDATA%\ModOrganizer\Skyrim Special Edition\mods"
set "HERE=%~dp0"
set "COMPILER=%SKY%\Papyrus Compiler\PapyrusCompiler.exe"
if not exist "%COMPILER%" (
  echo Papyrus compiler not found at "%COMPILER%". Install the Creation Kit, or edit SKY in this file.
  pause
  exit /b 1
)
if not exist "%HERE%compiled" mkdir "%HERE%compiled"
"%COMPILER%" "%HERE%Scripts" -all -f="TESV_Papyrus_Flags.flg" -i="%HERE%Scripts;%HERE%Stubs;%MODS%\Dylbills Papyrus Functions\Source\Scripts;%MODS%\powerofthree's Papyrus Extender\Source\scripts;%MODS%\PapyrusUtil SE - Modders Scripting Utility Functions\Source\Scripts;%MODS%\Custom Skills Framework\Source\Scripts;%SKY%\Data\Scripts\Source;%SKY%\Data\Source\Scripts" -o="%HERE%compiled" > "%HERE%compile_log.txt" 2>&1
type "%HERE%compile_log.txt"
echo.
echo ============================================================
findstr /C:" 0 failed" "%HERE%compile_log.txt" >nul
if errorlevel 1 (
  echo  COMPILE FAILED. The scripts with errors:
  findstr /R /C:"\.psc([0-9]*,[0-9]*)" "%HERE%compile_log.txt"
  echo  Full log: compile_log.txt
) else (
  for /f "tokens=*" %%L in ('findstr /C:"Batch compile of" "%HERE%compile_log.txt"') do echo  ALL SCRIPTS COMPILED - %%L
)
echo ============================================================
pause
