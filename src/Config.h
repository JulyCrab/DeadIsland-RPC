#pragma once

namespace Config {
    // Discord setup
    constexpr const char* kDiscordAppId = "1549493059274932234";
    constexpr const char* kLargeImageKey = "di_logo";
    constexpr const char* kLargeImageText = "Dead Island GOTY Edition";
    
    // Update every 5 seconds
    constexpr int kUpdateInterval = 5000;
    
    // PC timestamps
    constexpr unsigned long kGameDllTimestamp = 0x4F10070E;
    constexpr unsigned long kEngineDllTimestamp = 0x4F10052F;
    
    // Offsets found via reverse engineering
    constexpr unsigned long kQuestManagerRva = 0x00B95228;
    constexpr unsigned long kGameSingletonRva = 0x00B409B4;
    constexpr unsigned long kLogicalPlayerVtableRva = 0x0089AF34;
    constexpr unsigned long kEngineGamePointerRva = 0x01D9F608;
}
