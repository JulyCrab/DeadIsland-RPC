#include "Config.h"
#include "DiscordIpc.h"
#include "GameState.h"
#include "Log.h"

#include <Windows.h>
#include <string>
#include <ctime>

namespace {
HMODULE g_dllModule = nullptr;
HANDLE g_shutdownEvent = nullptr;

std::string GetDllDirectory() {
    char path[MAX_PATH]{};
    GetModuleFileNameA(g_dllModule, path, MAX_PATH);
    std::string result = path;
    size_t lastSlash = result.find_last_of("\\/");
    return lastSlash != std::string::npos ? result.substr(0, lastSlash) : ".";
}

std::string FormatNumber(int value) {
    std::string text = std::to_string(value);
    // Add commas for readability (e.g., 12345 -> 12,345)
    for (int i = static_cast<int>(text.size()) - 3; i > 0; i -= 3) {
        text.insert(i, ",");
    }
    return text;
}

bool IsAtMainMenu(const GameState& state) {
    return state.location.empty() && state.questInternal.empty();
}

std::string BuildDetails(const GameState& state) {
    if (!state.validBuild) {
        return "Unsupported Game Version";
    }
    
    if (IsAtMainMenu(state)) {
        return "In Main Menu";
    }
    
    std::string result;
    
    // Show location if we have it
    if (!state.location.empty()) {
        result = state.location;
    }
    
    // Add story progress percentage
    if (state.storyPercent >= 0) {
        if (!result.empty()) result += " | ";
        result += std::to_string(state.storyPercent) + "% Complete";
    }
    
    // Add player level
    if (state.playerLevel >= 1) {
        if (!result.empty()) result += " | ";
        result += "Level " + std::to_string(state.playerLevel);
    }
    
    return result.empty() ? "Surviving Banoi" : result;
}

std::string BuildState(const GameState& state) {
    if (!state.validBuild || IsAtMainMenu(state)) {
        return "";
    }
    
    std::string result;
    
    // Character name
    if (!state.character.empty()) {
        result = state.character;
    }
    
    // Experience points
    if (state.experience >= 0) {
        if (!result.empty()) result += " | ";
        result += FormatNumber(state.experience) + " XP";
    }
    
    // Current quest or free roam
    if (!result.empty()) result += " | ";
    result += state.questTitle.empty() ? "Free Roam" : "Quest: " + state.questTitle;
    
    return result;
}

DWORD WINAPI WorkerThread(void*) {
    std::string dllDir = GetDllDirectory();
    OpenLog(dllDir + "\\DeadIslandRPC.log");
    Log("DeadIslandRPC ASI injected!");
    
    // Make sure the Discord app ID is configured
    std::string appId = Config::kDiscordAppId;
    if (appId == "YOUR_DISCORD_APP_ID_HERE") {
        Log("Error: Discord Application ID not set in Config.h");
        Log("Create an app at https://discord.com/developers/applications");
        return 0;
    }
    
    // Wait for the game to load
    GameStateReader reader;
    for (int i = 0; i < 120; ++i) {
        if (reader.Initialize()) break;
        if (WaitForSingleObject(g_shutdownEvent, 500) != WAIT_TIMEOUT) return 0;
    }
    
    DiscordIpc discord;
    const auto sessionStart = static_cast<std::int64_t>(std::time(nullptr));
    std::string lastDetails;
    std::string lastState;
    
    constexpr DWORD kPollIntervalMs = Config::kUpdateInterval;
    constexpr DWORD kReconnectDelayMs = 5000;
    
    while (WaitForSingleObject(g_shutdownEvent, 0) == WAIT_TIMEOUT) {
        // Try to connect to Discord if we're not connected
        if (!discord.IsConnected()) {
            if (!discord.Connect(Config::kDiscordAppId)) {
                if (WaitForSingleObject(g_shutdownEvent, kReconnectDelayMs) != WAIT_TIMEOUT) break;
                continue;
            }
            // Reset cached strings so we update on reconnect
            lastDetails.clear();
            lastState.clear();
        }
        
        discord.Pump();
        
        GameState game = reader.Read();
        std::string details = BuildDetails(game);
        std::string state = BuildState(game);
        
        // Only update if something changed
        if (discord.IsConnected() && (details != lastDetails || state != lastState)) {
            DiscordActivity activity;
            activity.details = details;
            activity.state = state;
            activity.largeImage = Config::kLargeImageKey;
            activity.largeText = Config::kLargeImageText;
            activity.startTimestamp = sessionStart;
            
            if (discord.SetActivity(activity)) {
                Log("Presence updated: %s / %s", details.c_str(), state.c_str());
                lastDetails = details;
                lastState = state;
            }
        }
        
        if (WaitForSingleObject(g_shutdownEvent, kPollIntervalMs) != WAIT_TIMEOUT) break;
    }
    
    discord.Disconnect();
    Log("DeadIslandRPC stopped.");
    return 0;
}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_dllModule = module;
        DisableThreadLibraryCalls(module);
        g_shutdownEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        HANDLE thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_shutdownEvent) {
            SetEvent(g_shutdownEvent);
            // Give the worker thread a moment to clean up
            Sleep(100);
        }
    }
    return TRUE;
}
