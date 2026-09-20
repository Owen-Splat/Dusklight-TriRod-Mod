rm -rf build
if %errorlevel% neq 0 exit /b %errorlevel%
"C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -B build
if %errorlevel% neq 0 exit /b %errorlevel%
"C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build
if %errorlevel% neq 0 exit /b %errorlevel%
cp "D:\Documents\Dusklight Mods\Dusklight-TriRod-Mod\build\mods\tri-rod.dusk" "C:\Users\Owen\AppData\Roaming\TwilitRealm\Dusklight\mods"
if %errorlevel% neq 0 exit /b %errorlevel%