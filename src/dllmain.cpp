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
#define VERSION "1.1.0"
#define LOG(STRING, ...) spdlog::info("{} : " STRING, __func__, ##__VA_ARGS__)

#define TRAILS_IN_THE_SKY_FC  1
#define TRAILS_IN_THE_SKY_SC  2
#define TRAILS_IN_THE_SKY_3RD 3

// .yml to struct
typedef struct textures_t {
    bool enable;
} textures_t;

typedef struct tileRenderDistance_t {
    bool enable;
} tileRenderDistance_t;

typedef struct fix_t {
    textures_t textures;
    tileRenderDistance_t tileRenderDistance;
} fix_t;

typedef struct camera_t {
    bool enable;
    f32 zoom;
} camera_t;

typedef struct feature_t {
    camera_t camera;
} feature_t;

typedef struct yml_t {
    std::string name;
    bool masterEnable;
    fix_t fix;
    feature_t feature;
} yml_t;

// Globals
Utils::ModuleInfo module(GetModuleHandle(NULL));
YAML::Node config = YAML::LoadFile("TrailsInTheSkyFix.yml");
yml_t yml;
std::map <std::string, u32> exeMap = {
    {"ed6_win_DX9.exe",  TRAILS_IN_THE_SKY_FC},
    {"ed6_win2_DX9.exe", TRAILS_IN_THE_SKY_SC},
    {"ed6_win3_DX9.exe", TRAILS_IN_THE_SKY_3RD}
};

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
    auto logger = spdlog::basic_logger_mt("TrailsInTheSkyFix", "TrailsInTheSkyFix.log", true);
    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::debug);

    // Get game name and exe path
    WCHAR exePath[_MAX_PATH] = { 0 };
    GetModuleFileNameW(module.address, exePath, MAX_PATH);
    std::filesystem::path exeFilePath = exePath;
    module.name = exeFilePath.filename().string();
    module.id = exeMap[module.name];

    // Log module details
    LOG("-------------------------------------");
    LOG("Compiler: {:s}", Utils::getCompilerInfo());
    LOG("Compiled: {:s} at {:s}", __DATE__, __TIME__);
    LOG("Version: {:s}", VERSION);
    LOG("Module Name: {:s}", module.name);
    LOG("Module Path: {:s}", exeFilePath.string());
    LOG("Module Addr: 0x{:x}", reinterpret_cast<u64>(module.address));
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
    yml.fix.tileRenderDistance.enable = config["fixes"]["tileRenderDistance"]["enable"].as<bool>();

    yml.feature.camera.enable = config["features"]["camera"]["enable"].as<bool>();
    yml.feature.camera.zoom = (yml.feature.camera.enable == true) ?
        config["features"]["camera"]["zoom"].as<f32>() : 1.0f;

    LOG("Name: {}", yml.name);
    LOG("MasterEnable: {}", yml.masterEnable);
    LOG("Fix.Textures.Enable: {}", yml.fix.textures.enable);
    LOG("Fix.TileRenderDistance.Enable: {}", yml.fix.tileRenderDistance.enable);
    LOG("Feature.Camera.Enable: {}", yml.feature.camera.enable);
    LOG("Feature.Camera.Zoom: {}", yml.feature.camera.zoom);
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
    Utils::SignatureHook hook(
        "76 ?? F2 0F 5E C8 F2 0F 11 0D ?? ?? ?? ?? 80 3D ?? ?? ?? ?? 00 75 ??",
        0x15
    );
    bool enable = yml.masterEnable;
    Utils::injectHook(enable, module, hook,
        [](SafetyHookContext& ctx) {
            ctx.eflags &= ~0x40; // Clear zero flag
        }
    );
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
    Utils::SignatureHook hook(
        "66 0F 2F C1 76 ?? A1 ?? ?? ?? ?? 66 0F 6E 05 ?? ?? ?? ??"
    );

    bool enable = yml.masterEnable & yml.fix.textures.enable;
    Utils::injectHook(enable, module, hook,
        [](SafetyHookContext& ctx) {
            ctx.xmm0.u64[0] = 0x3FF0000000000000;
        }
    );
}

/**
 * @brief Increases the game's tile render distance.
 *
 * @details
 * Once you start playing the game in resolutions larger than 21:9 you begin to see tiles render in and out
 * based on the distance from the camera origin, or center of the screen.
 *
 * How was this found?
 * This is a simple case of culling, where things off camera are not rendered on purpose in order to reduce
 * workload on both the CPU and GPU which in turn improves performance as its only rendering that which is
 * visible to the player on the screen. As of writing this the game itself is over 20 years old since its
 * original release on Windows in 2004. It is quite remarkable that the game is able to run on modern
 * monitor resolutions up to 21:9 issue free, that may have been an update, but impressive none the less.
 *
 * The game runs DX9 or DX8, but the fix is done for DX9 and is expected that you run the DX9 version of the
 * game. The DX9 API offers many functions that provide culling support and luckily the game did use these
 * functions as opposed to a custom tile rendering manager solution, that manages which tiles should and
 * should not be rendered at any given time.
 *
 * In order to cull objects off camera we need a projection matrix which defines a frustum, which is used for
 * frustum culling. A good candidate function for this is `D3DXMatrixPerspectiveFovLH` which constructs a
 * perspective projection matrix based on provided field of view, aspect ratio, and z-near and z-far values.
 *
 * The game has 4 locations where `D3DXMatrixPerspectiveFovLH` is called:
 * 1 - ed6_win2_DX9.exe+5B5CD  - FF D6                - call esi -> D3DX9_43.D3DXMatrixPerspectiveFovLH
 * 2 - ed6_win2_DX9.exe+76D92  - FF 15 5C135800       - call dword ptr [ed6_win2_DX9.exe+18135C] -> D3DX9_43.D3DXMatrixPerspectiveFovLH
 * 3 - ed6_win2_DX9.exe+9224A  - FF 15 5C135800       - call dword ptr [ed6_win2_DX9.exe+18135C] -> D3DX9_43.D3DXMatrixPerspectiveFovLH
 * 4 - ed6_win2_DX9.exe+110A16 - FF 15 5C135800       - call dword ptr [ed6_win2_DX9.exe+18135C] -> D3DX9_43.D3DXMatrixPerspectiveFovLH
 *
 * Out of all these calls the one that effects tile rendering is the 1st one, where the address is first
 * stored within esi then called.
 *
 * The function is as follows:
 * https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dxmatrixperspectivefovlh
 * D3DXMATRIX* D3DXMatrixPerspectiveFovLH(
 *   _Inout_ D3DXMATRIX *pOut,
 *   _In_    FLOAT      fovy,
 *   _In_    FLOAT      Aspect,
 *   _In_    FLOAT      zn,
 *   _In_    FLOAT      zf
 * );
 * Out of all the parameters that this function has, there are 2 of interest:
 * 1 - `fovy`: Field of view in the y direction, in radians.
 * 2 - `Aspect`: Aspect ratio, defined as view space width divided by height.
 *
 * `fovy` by default is 0x3E8F5C29 (0.28f), and this value is read in multiple locations, but no writes
 * take place when ingame, or I just didnt encounter that case, but none the less changing this value
 * does 2 things visually:
 * 1. Increases/Decreases the FOV angle, which results in a larger/smaller frustum, which means the camera
 * is visually further/closer than before, so you see more/less of the world.
 * 2. Increases the tile render distance.
 *
 * 1. and 2. are mutually exclusive, changing this parameter for the DX9 call mentioned above will perform
 * point 1. as expected. In order to achieve 2. we need to see where else this is value is read:
 * 1 - ed6_win2_DX9.exe+87054 - F3 0F10 59 24         - movss xmm3,[ecx+24] <- this does 2.
 * 2 - ed6_win2_DX9.exe+928E1 - F3 0F10 59 24         - movss xmm3,[ecx+24] <- no visual effect when changed
 * 3 - ed6_win2_DX9.exe+903FF - F3 0F10 59 24         - movss xmm3,[ecx+24] <- no visual effect when changed
 * 4 - ed6_win2_DX9.exe+5B5BC - F3 0F10 40 24         - movss xmm0,[eax+24] <- this does 1.
 *
 * As already stated above with some experimentation we know where the value is read and now know
 * which read is the one that effects tile rendering.
 *
 * A hook and injection later where we set the value to 2.0f fixes the issue and now visible tile rendering
 * is gone. Why 2.0f? I needed a value large enough to just render the entire map, this rendering and choice
 * goes well beyond 32:9, and it is quite frankly a waste of CPU andGPU resources to render the entire map
 * every frame, game loop, whatever, but ultimately this game is oldenough and simple enough graphically
 * that this is not a performance bottleneck and it will never be onmodern hardware.
 *
 * @return void
 */
void tileRenderFix() {
    Utils::SignatureHook hook(
        "F3 0F 11 4C 24 04    F3 0F 11 04 24    51    FF D6"
    );
    bool enable = yml.masterEnable & yml.fix.tileRenderDistance.enable;
    Utils::injectHook(enable, module, hook,
        [](SafetyHookContext& ctx) {
            static f32 originalFov; // used to save the original game calculated FOV

            // Unfortunate the the third game uses a different offset...
            u32 offset = module.id == TRAILS_IN_THE_SKY_3RD ? 0x30 : 0x24;
            f32* targetAddr = reinterpret_cast<f32*>(ctx.eax + offset);

            f32 newFov = 2.0f * std::numbers::pi_v<f32>;
            if (*targetAddr != newFov) {
                originalFov = *targetAddr;
                *targetAddr = newFov;
            }
            ctx.xmm0.f32[0] = originalFov * yml.feature.camera.zoom;
        }
    );
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
    tileRenderFix();
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
