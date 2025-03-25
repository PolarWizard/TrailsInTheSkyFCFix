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

#include <windows.h>
#include <vector>
#include <format>
#include <iostream>
#include <sstream>
#include <span>
#include <cstdint>
#include <algorithm>

#include "utils.hpp"

namespace Utils
{
    std::string getCompilerInfo()
    {
#if defined(__GNUC__)
        std::string compiler = "GCC - "
            + std::to_string(__GNUC__) + "."
            + std::to_string(__GNUC_MINOR__) + "."
            + std::to_string(__GNUC_PATCHLEVEL__);
#elif defined(__clang__)
        std::string compiler = "Clang - "
            + std::to_string(__clang_major__) + "."
            + std::to_string(__clang_minor__) + "."
            + std::to_string(__clang_patchlevel__);
#elif defined(_MSC_VER)
        std::string compiler = "MSVC - "
            + std::to_string(_MSC_VER);
#else
        std::string compiler = "UNKNOWN";
#endif
        return compiler;
    }

    std::string bytesToString(std::span<const u8> bytes)
    {
        std::string pattern;
        for (u8 byte : bytes) {
            pattern += std::format("{:02X} ", byte);
        }
        if (!pattern.empty()) {
            pattern.pop_back();
        }
        return pattern;
    }

    std::pair<u32, u32> getDesktopDimensions()
    {
        DEVMODE devMode{};
        devMode.dmSize = sizeof(DEVMODE);
        if (EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &devMode)) {
            return { devMode.dmPelsWidth, devMode.dmPelsHeight };
        }
        return {};
    }

    void patch(u64 address, std::string_view pattern) {
        static auto patternToByte = [](std::string_view pattern) {
            std::vector<u8> bytes;
            std::istringstream stream(pattern.data());
            std::string byteStr;

            while (stream >> byteStr) {  // Extract space-separated hex values
                bytes.push_back(static_cast<u8>(std::stoul(byteStr, nullptr, 16)));
            }
            return bytes;
        };

        DWORD oldProtect;
        auto patternBytes = patternToByte(pattern);
        VirtualProtect((LPVOID)address, patternBytes.size(), PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy((LPVOID)address, patternBytes.data(), patternBytes.size());
        VirtualProtect((LPVOID)address, patternBytes.size(), oldProtect, &oldProtect);
    }

    void patternScan(void* module, std::string_view signature, std::vector<u64>& addresses)
    {
        static auto patternToByte = [](std::string_view pattern) {
            std::vector<u8> bytes;
            std::stringstream ss(pattern.data()); // Create a stringstream from the input pattern
            std::string byteStr;

            while (ss >> byteStr) {  // Extract each token (hex byte or ??)
                if (byteStr == "??") {
                    bytes.push_back(0xFF); // Wildcard byte
                }
                else {
                    u32 byte;
                    std::stringstream(byteStr) >> std::hex >> byte; // Convert hex string to integer
                    bytes.push_back(static_cast<u8>(byte));
                }
            }
            return bytes;
        };

        auto* dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
        auto* ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(
            reinterpret_cast<u8*>(module) + dosHeader->e_lfanew);

        size_t sizeOfImage = ntHeaders->OptionalHeader.SizeOfImage;
        auto patternBytes = patternToByte(signature);
        std::span<const u8> scanBytes(reinterpret_cast<u8*>(module), sizeOfImage);

        u64 patternSize = patternBytes.size();

        for (size_t i = 0; i <= (sizeOfImage - patternSize); i++) {
            if (std::equal(patternBytes.begin(), patternBytes.end(), scanBytes.begin() + i,
                [](u8 patternByte, u8 scanByte) {
                    return patternByte == 0xFF || patternByte == scanByte;
                })) {
                addresses.push_back(reinterpret_cast<u64>(&scanBytes[i]));
            }
        }
    }
}
