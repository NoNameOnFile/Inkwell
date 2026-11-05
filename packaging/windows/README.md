# Windows installer

This builds a single installer for a GitHub Release:

`dist/Inkwell-0.1.0-windows-x64.exe`

The installer adds Inkwell to Program Files, a Start Menu shortcut, a desktop shortcut, and an uninstall entry. It includes the Qt runtime and the English Hunspell dictionary. Code signing is not part of this version. Sign the finished exe later with `signtool` if you want SmartScreen to trust it.

Run the commands from the repository root in the x64 Native Tools Command Prompt for Visual Studio 2022. If `cl` is not already on `PATH`, call the Build Tools environment first:

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
```

Community edition uses `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat` instead.

Put the Qt kit and Inno Setup on `PATH` for this prompt. `CMAKE_PREFIX_PATH` is the folder that contains `lib\cmake\Qt6`.

```bat
set PATH=C:\Qt\6.8.3\msvc2022_64\bin;C:\Program Files (x86)\Inno Setup 6;%PATH%

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build

if exist build\windows-deploy rmdir /s /q build\windows-deploy
mkdir build\windows-deploy
copy /y build\inkwell.exe build\windows-deploy\
windeployqt --release --compiler-runtime build\windows-deploy\inkwell.exe

mkdir build\windows-deploy\hunspell
copy /y packaging\hunspell\en_US.aff build\windows-deploy\hunspell\
copy /y packaging\hunspell\en_US.dic build\windows-deploy\hunspell\
copy /y packaging\hunspell\README_en_US.txt build\windows-deploy\hunspell\

ISCC packaging\windows\Inkwell.iss
```

`--compiler-runtime` copies the Visual C++ runtime DLLs next to `inkwell.exe`, so the installer does not ask for a separate redistributable. `windeployqt` must be the one from the same Qt kit that built the app.

The script reads `build\windows-deploy` and writes `dist\Inkwell-0.1.0-windows-x64.exe`. Those paths are relative to the repository. Nothing in the script points at a particular user profile. To stage somewhere else, or to stamp another version, pass the values to `ISCC`:

```bat
ISCC /DAppVersion=0.1.0 /DSourceDir=C:\path\to\windows-deploy packaging\windows\Inkwell.iss
```

`SourceDir` is an absolute path, or a path relative to `packaging\windows`. The default version matches the CMake project version, `0.1.0`.

With the Visual Studio generator the executable is `build\Release\inkwell.exe`. Copy that file into `build\windows-deploy` instead of `build\inkwell.exe`, then run the same `windeployqt`, dictionary copy, and `ISCC` commands.

Inno Setup 6.3 or newer is required (`x64compatible`). The installer is not code-signed.
