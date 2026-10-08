#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace mcbe::memory {

inline std::vector<int> parsePattern(std::string_view pattern) {
    std::vector<int> bytes;
    const char* cur = pattern.data();
    const char* end = cur + pattern.size();

    while (cur < end) {
        while (cur < end && *cur == ' ') ++cur;
        if (cur >= end) break;

        if (*cur == '?') {
            ++cur;
            if (cur < end && *cur == '?') ++cur;
            bytes.push_back(-1);
            continue;
        }

        char* next = nullptr;
        const auto value = std::strtoul(cur, &next, 16);
        if (next == cur) break;
        bytes.push_back(static_cast<int>(value & 0xFF));
        cur = next;
    }
    return bytes;
}

inline std::uintptr_t scan(std::string_view pattern) {
    auto* module = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"Minecraft.Windows.exe"));
    if (!module) return 0;

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

    const auto wanted = parsePattern(pattern);
    if (wanted.empty()) return 0;

    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; ++s) {
        if ((section[s].Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;

        auto* begin = module + section[s].VirtualAddress;
        const std::size_t size = section[s].Misc.VirtualSize;
        if (size < wanted.size()) continue;

        for (std::size_t i = 0; i <= size - wanted.size(); ++i) {
            bool match = true;
            for (std::size_t j = 0; j < wanted.size(); ++j) {
                if (wanted[j] != -1 && begin[i + j] != static_cast<std::uint8_t>(wanted[j])) {
                    match = false;
                    break;
                }
            }
            if (match) return reinterpret_cast<std::uintptr_t>(begin + i);
        }
    }
    return 0;
}

inline std::uintptr_t resolveRip(std::uintptr_t instruction, std::size_t displacementOffset) {
    if (!instruction) return 0;
    const auto displacement = *reinterpret_cast<const std::int32_t*>(instruction + displacementOffset);
    return instruction + displacementOffset + sizeof(std::int32_t) + displacement;
}

inline std::uintptr_t resolveCall(std::uintptr_t instruction) {
    return resolveRip(instruction, 1);
}

} // namespace mcbe::memory
