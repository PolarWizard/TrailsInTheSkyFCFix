/*
 * MIT License
 *
 * Copyright (c) 2024 Dominik Protasewicz
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// System includes
#include <windows.h>
#include <fstream>
#include <iostream>
#include <string>
#include <filesystem>
#include <numeric>
#include <numbers>
#include <cmath>
#include <cstdint>

// 3rd party includes
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "yaml-cpp/yaml.h"
#include "safetyhook.hpp"

// Local includes
#include "utils.hpp"

// Macros
#define VERSION "1.0.0"
#define LOG(STRING, ...) spdlog::info("{} : " STRING, __func__, ##__VA_ARGS__)

// .yml to struct
typedef struct textures_t {
    bool enable;
} textures_t;

typedef struct fix_t {
    textures_t textures;
} fix_t;

typedef struct yml_t {
    std::string name;
    bool masterEnable;
    fix_t fix;
} yml_t;

// Globals
HMODULE baseModule = GetModuleHandle(NULL);
YAML::Node config = YAML::LoadFile("TrailsInTheSkyFCFix.yml");
yml_t yml;

/**
 * @brief Initializes logging for the application.
 *
 * This function performs the following tasks:
 * 1. Initializes the spdlog logging library and sets up a file logger.
 * 2. Retrieves and logs the path and name of the executable module.
 * 3. Logs detailed information about the module to aid in debugging.
 *
 * @return void
 */
void logInit() {
    // spdlog initialisation
    auto logger = spdlog::basic_logger_mt("TrailsInTheSkyFCFix", "TrailsInTheSkyFCFix.log", true);
    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::debug);

    // Get game name and exe path
    WCHAR exePath[_MAX_PATH] = { 0 };
    GetModuleFileNameW(baseModule, exePath, MAX_PATH);
    std::filesystem::path exeFilePath = exePath;
    std::string exeName = exeFilePath.filename().string();

    // Log module details
    LOG("-------------------------------------");
    LOG("Compiler: {:s}", Utils::getCompilerInfo().c_str());
    LOG("Compiled: {:s} at {:s}", __DATE__, __TIME__);
    LOG("Version: {:s}", VERSION);
    LOG("Module Name: {:s}", exeName.c_str());
    LOG("Module Path: {:s}", exeFilePath.string().c_str());
    LOG("Module Addr: 0x{:x}", (uintptr_t)baseModule);
}

/**
 * @brief Reads and parses configuration settings from a YAML file.
 *
 * This function performs the following tasks:
 * 1. Reads general settings from the configuration file and assigns them to the `yml` structure.
 * 2. Initializes global settings if certain values are missing or default.
 * 3. Logs the parsed configuration values for debugging purposes.
 *
 * @return void
 */
void readYml() {
    yml.name = config["name"].as<std::string>();

    yml.masterEnable = config["masterEnable"].as<bool>();

    yml.fix.textures.enable = config["fixes"]["textures"]["enable"].as<bool>();

    LOG("Name: {}", yml.name);
    LOG("MasterEnable: {}", yml.masterEnable);
    LOG("Fix.Textures.Enable: {}", yml.fix.textures.enable);
}

/**
 * @brief Forces the current aspect ratio.
 *
 * This function performs the following tasks:
 * 1. Checks if the master enable is enabled based on the configuration.
 * 2. Searches for a specific memory pattern in the base module.
 * 3. Hooks the identified pattern to clear the zero flag in the eflags (status) register.
 *
 * @details
 * The function uses a pattern scan to find a specific byte sequence in the memory of the base module.
 * If the pattern is found, a hook is created at an offset from the found pattern address. The hook
 * clears the zero flag in the eflags (status) register.
 *
 * The zero flag being set or not is dependent on the keepAspect setting in the config.ini file which
 * the game reads to setup things around the engine. The keepAspect setting is one such setting which
 * if set will force the game to keep the aspect ratio, causing the UI to not be stretched, but the
 * textures will be not be rendered, setting to 0 this cause the opposite effect: stretched UI and
 * rendered textures. The keepAspect flag is forced to be set, as fixing the black textures is simpler
 * than fixing the stretched UI which consists of many many elements.
 *
 * How was this found?
 * Using ProcMon it was observed that the binary reads a config.ini file for various settings which
 * are used to setup the game engine, keepAspect was one settings which if played around it would
 * as stated above stretch/fit the UI and render/not render the textures. As stated before fixing
 * not rendered textures is simpler than unstretching every UI element.
 * The operation of keepAspect is as follows:
 * ed6_win_DX9.exe+B0490 : FF D6             call esi
 * ed6_win_DX9.exe+B0499 : 83 F8 01          cmp eax,01
 * ed6_win_DX9.exe+B04A6 : 0F94 05 18817A00  sete byte ptr [ed6_win_DX9.exe+3A8118]
 * The game calls function to read the keepAspect flag from the config.ini file, compares the return
 * value in the eax register with 1 and then if sets the byte at ed6_win_DX9.exe+3A8118 accordingly
 * based on zero flag from the previous compare.
 *
 * Later in the code there is a checks that compares the value at the address ed6_win_DX9.exe+3A8118
 * with 0. If zero flag is not set then the game will take jump else it will set another memory location
 * with data which affects code behavior later.
 * Relevent code:
 * 1 - ed6_win_DX9.exe+5D87E : 80 3D 18817A00 00  cmp byte ptr [ed6_win_DX9.exe+A8118],00
 * 2 - ed6_win_DX9.exe+5D885 : 75 0E              jne ed6_win_DX9.exe+5D895
 * 3 - ed6_win_DX9.exe+5D887 : 0F28 05 60835400   movaps xmm0,xmmword ptr [ed6_win_DX9.exe+1C360]
 * 4 - ed6_win_DX9.exe+5D88E : 0F29 05 20817A00   movaps xmmword ptr [ed6_win_DX9.exe+3A8120],xmm0
 * 5 - ed6_win_DX9.exe+5D895 : B0 01              mov al,01
 *
 * Where:
 * ed6_win_DX9.exe+1C360+0 -> 00 00 00 00
 * ed6_win_DX9.exe+1C360+1 -> 00 00 00 00
 * ed6_win_DX9.exe+1C360+2 -> 00 00 00 00
 * ed6_win_DX9.exe+1C360+3 -> 00 00 00 00
 *
 * ed6_win_DX9.exe+3A8120+0 -> 55 55 55 55
 * ed6_win_DX9.exe+3A8120+1 -> 55 55 05 45
 * ed6_win_DX9.exe+3A8120+2 -> 00 00 00 00
 * ed6_win_DX9.exe+3A8120+3 -> 00 00 F0 3F
 *
 * ed6_win_DX9.exe+3A8120 is overwritten with the value at ed6_win_DX9.exe+1C360 then later in the code
 * the game will perform a check to see if its zero and will write it with:
 * ed6_win_DX9.exe+3A8120+0 -> 00 00 00 00
 * ed6_win_DX9.exe+3A8120+1 -> 00 00 F0 3F
 * ed6_win_DX9.exe+3A8120+2 -> 00 00 00 00
 * ed6_win_DX9.exe+3A8120+3 -> 00 00 F0 3F
 *
 * A hook is injected between 1 and 2 to clear the zero flag in the eflags (status) register and now the
 * jump is always taken and the data at ed6_win_DX9.exe+3A8120 is preserved. This is important for the
 * textures fix to work.
 *
 * @return void
 */
void forceKeepAspect() {
    const char* patternFind = "75 ?? 0F 28 05 ?? ?? ?? ?? 0F 29 05 ?? ?? ?? ??";
    uintptr_t  hookOffset = 0;

    bool enable = yml.masterEnable;
    LOG("Fix {}", enable ? "Enabled" : "Disabled");
    if (enable) {
        std::vector<uint64_t> addr;
        Utils::patternScan(baseModule, patternFind, &addr);
        uint8_t* hit = (uint8_t*)addr[0];
        uintptr_t absAddr = (uintptr_t)hit;
        uintptr_t relAddr = (uintptr_t)hit - (uintptr_t)baseModule;
        if (hit) {
            LOG("Found '{}' @ 0x{:x}", patternFind, relAddr);
            uintptr_t hookAbsAddr = absAddr + hookOffset;
            uintptr_t hookRelAddr = relAddr + hookOffset;
            static SafetyHookMid aspectMidHook{};
            aspectMidHook = safetyhook::create_mid(reinterpret_cast<void*>(hookAbsAddr),
                [](SafetyHookContext& ctx) {
                    ctx.eflags &= ~0x40; // Clear zero flag
                }
            );
            LOG("Hooked @ 0x{:x} + 0x{:x} = 0x{:x}", relAddr, hookOffset, hookRelAddr);
        }
        else {
            LOG("Did not find '{}'", patternFind);
        }
    }
}

/**
 * @brief Fixes the black textures.
 *
 * This function performs the following tasks:
 * 1. Checks if the master enable and textures fix are enabled based on the configuration.
 * 2. Searches for a specific memory pattern in the base module.
 * 3. Hooks at the identified pattern to inject a new value into xmm0.

 * @details
 * The function uses a pattern scan to find a specific byte sequence in the memory of the base module.
 * If the pattern is found, a hook is created at an offset from the found pattern address. The hook
 * injects a new value into xmm0.
 *
 * I haven't delved into the inner working of how the game handles and uses the xmm0 value, but this
 * value plays a key role of whether or not the game will render textures. This fix works in conjunction
 * with the forceKeepAspect fix.
 *
 * How was this found?
 * Read over forcekeepAspect documentation first as it required to understand the context here.
 * As stated before the keepAspect flag present in the config.ini will either stretch/fit the UI and will
 * cause the textures to render/not render depending on its value. With the forceKeepAspect fix we know that
 * the value at ed6_win_DX9.exe+3A8120 is important and controls the UI and textures.
 * Although the game writes this address as a xmmword quantity it is mainly read as a qword.
 * Using x32dbg we can track what instructions in the code access this memory location.
 * As x32dbg picks up instructions that read from this location we can inspect that instruction and inject
 * either '00 00 00 00    00 00 0F 3F' or '55 55 55 55    55 55 05 45' depending on the value currently
 * there. After enough trial and error eventually there is a hit which will bring the textures back or not:
 * ed6_win_DX9.exe+3FFD8 : F2 0F10 05 20817A00  movsd xmm0, qword ptr [ed6_win_DX9.exe+3A8120]
 *
 * This instruction and the code following it control the rendering of textures. Injecting
 * 0x3FF0000000000000 into xmm0 will fix the unrendered textures, while maintaining an unstretched UI.
 *
 * @return void
 */
void texturesFix() {
    const char* patternFind  = "66 0F 2F C1 76 ?? A1 ?? ?? ?? ?? 66 0F 6E 05 ?? ?? ?? ??";
    uintptr_t hookOffset = 0;

    bool enable = yml.masterEnable & yml.fix.textures.enable;
    LOG("Fix {}", enable ? "Enabled" : "Disabled");
    if (enable) { // Master FOV controller
        std::vector<uint64_t> addr;
        Utils::patternScan(baseModule, patternFind, &addr);
        uint8_t* hit = (uint8_t*)addr[0];
        uintptr_t absAddr = (uintptr_t)hit;
        uintptr_t relAddr = (uintptr_t)hit - (uintptr_t)baseModule;
        if (hit) {
            LOG("Found '{}' @ 0x{:x}", patternFind, relAddr);
            uintptr_t hookAbsAddr = absAddr + hookOffset;
            uintptr_t hookRelAddr = relAddr + hookOffset;
            static SafetyHookMid texturesMidHook{};
            texturesMidHook = safetyhook::create_mid(reinterpret_cast<void*>(hookAbsAddr),
                [](SafetyHookContext& ctx) {
                    ctx.xmm0.u64[0] = 0x3FF0000000000000;
                }
            );
            LOG("Hooked @ 0x{:x} + 0x{:x} = 0x{:x}", relAddr, hookOffset, hookRelAddr);
        }
        else {
            LOG("Did not find '{}'", patternFind);
        }
    }
}

/**
 * @brief Main function that initializes and applies various fixes.
 *
 * This function serves as the entry point for the DLL. It performs the following tasks:
 * 1. Initializes the logging system.
 * 2. Reads the configuration from a YAML file.
 * 3. Applies a forced aspect ratio fix.
 * 3. Applies a textures fix.
 *
 * @param lpParameter Unused parameter.
 * @return Always returns TRUE to indicate successful execution.
 */
DWORD __stdcall Main(void* lpParameter) {
    logInit();
    readYml();
    forceKeepAspect();
    texturesFix();
    return true;
}

/**
 * @brief Entry point for a DLL, called by the system when the DLL is loaded or unloaded.
 *
 * This function handles various events related to the DLL's lifetime and performs actions
 * based on the reason for the call. Specifically, it creates a new thread when the DLL is
 * attached to a process.
 *
 * @details
 * The `DllMain` function is called by the system when the DLL is loaded or unloaded. It handles
 * different reasons for the call specified by `ul_reason_for_call`. In this implementation:
 *
 * - **DLL_PROCESS_ATTACH**: When the DLL is loaded into the address space of a process, it
 *   creates a new thread to run the `Main` function. The thread priority is set to the highest,
 *   and the thread handle is closed after creation.
 *
 * - **DLL_THREAD_ATTACH**: Called when a new thread is created in the process. No action is taken
 *   in this implementation.
 *
 * - **DLL_THREAD_DETACH**: Called when a thread exits cleanly. No action is taken in this implementation.
 *
 * - **DLL_PROCESS_DETACH**: Called when the DLL is unloaded from the address space of a process.
 *   No action is taken in this implementation.
 *
 * @param hModule Handle to the DLL module. This parameter is used to identify the DLL.
 * @param ul_reason_for_call Indicates the reason for the call (e.g., process attach, thread attach).
 * @param lpReserved Reserved for future use. This parameter is typically NULL.
 * @return BOOL Always returns TRUE to indicate successful execution.
 */
BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
) {
    HANDLE mainHandle;
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        LOG("DLL_PROCESS_ATTACH");
        mainHandle = CreateThread(NULL, 0, Main, 0, NULL, 0);
        if (mainHandle)
        {
            SetThreadPriority(mainHandle, THREAD_PRIORITY_HIGHEST);
            CloseHandle(mainHandle);
        }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
