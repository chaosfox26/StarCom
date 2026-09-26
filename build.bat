@echo off
setlocal
if not defined VCINSTALLDIR call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
if not exist build mkdir build
cl /nologo /c /O2 /W3 /Fo:build\ third_party\monocypher\monocypher.c third_party\monocypher\monocypher-ed25519.c || exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /W4 /permissive- /D_CRT_SECURE_NO_WARNINGS /Fo:build\ /Fe:build\starcom.exe src\*.cpp build\monocypher.obj build\monocypher-ed25519.obj ws2_32.lib bcrypt.lib mfplat.lib mfuuid.lib ole32.lib || exit /b 1
echo BUILD OK: build\starcom.exe
