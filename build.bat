@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "bin" mkdir "bin"
cl.exe /nologo /std:c++20 /EHsc /O2 /I include /Fe:"bin\project_x.exe" src\memory_bridge.cpp src\lua_vm_bridge.cpp src\loading_engine.cpp src\context_sync.cpp src\main.cpp /link user32.lib advapi32.lib
if %ERRORLEVEL% equ 0 (
    echo [BUILD SUCCESS] bin\project_x.exe created successfully.
) else (
    echo [BUILD ERROR] Compilation failed.
)
