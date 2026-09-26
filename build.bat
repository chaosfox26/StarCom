@echo off
setlocal
if not defined VCINSTALLDIR call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
if not exist build mkdir build
cl /nologo /c /O2 /W3 /Fo:build\ third_party\monocypher\monocypher.c third_party\monocypher\monocypher-ed25519.c || exit /b 1
if not exist build\opus mkdir build\opus
if exist build\opus.rsp del build\opus.rsp
for /f "usebackq delims=" %%f in ("third_party\opus-sources.txt") do echo third_party\opus\%%f>> build\opus.rsp
cl /nologo /c /O2 /W3 /wd4244 /DOPUS_BUILD /DUSE_ALLOCA /DHAVE_LRINTF /DHAVE_LRINT /I third_party\opus\include /I third_party\opus\celt /I third_party\opus\silk /I third_party\opus\silk\float /I third_party\opus\src /Fo:build\opus\ @build\opus.rsp || exit /b 1
lib /nologo /out:build\opus.lib build\opus\*.obj || exit /b 1
set RV=
set RVDEF=
if exist .private\rendezvous.txt set /p RV=<.private\rendezvous.txt
if defined RV set RVDEF=/DSTARCOM_DEFAULT_RENDEZVOUS=\"%RV%\"
cl /nologo /std:c++17 /EHsc /O2 /W4 %RVDEF% /permissive- /D_CRT_SECURE_NO_WARNINGS /Fo:build\ /DUNICODE /D_UNICODE /Fe:build\starcom.exe src\*.cpp src\gui\*.cpp build\monocypher.obj build\monocypher-ed25519.obj build\opus.lib ws2_32.lib bcrypt.lib mfplat.lib mfuuid.lib ole32.lib d2d1.lib dwrite.lib user32.lib shell32.lib || exit /b 1
echo BUILD OK: build\starcom.exe
