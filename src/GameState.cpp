#include "GameState.h"
#include "Config.h"
#include "Log.h"
#include "QuestData.h"

#include <Windows.h>
#include <algorithm>
#include <cstring>

namespace {
// Function pointers for engine exports
using GetActiveLevelFn = void* (__thiscall*)(void*);
using GetLevelNameFn = const char* (__thiscall*)(void*);
using QuestGetStringFn = void* (__thiscall*)(void*);
using QuestGetBoolFn = bool (__thiscall*)(void*);
using QuestGetStateFn = int (__thiscall*)(void*);

// Raw data we pull from memory
struct RawGameData {
    char levelName[128];
    char questId[160];
    int characterIndex;
    int level;
    int xp;
};

// Engine's string format: pointer, length, capacity
struct EngineString {
    const char* data;
    std::uint32_t length;
    std::uint32_t capacity;
};

DWORD GetModuleTimestamp(void* module) {
    if (!module) return 0;
    auto* dosHeader = static_cast<const IMAGE_DOS_HEADER*>(module);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        static_cast<const unsigned char*>(module) + dosHeader->e_lfanew);
    return ntHeaders->Signature == IMAGE_NT_SIGNATURE ? ntHeaders->FileHeader.TimeDateStamp : 0;
}

bool IsExecutable(const void* addr) {
    MEMORY_BASIC_INFORMATION info{};
    if (!addr || !VirtualQuery(addr, &info, sizeof(info)) || info.State != MEM_COMMIT) {
        return false;
    }
    DWORD prot = info.Protect & 0xff;
    return prot == PAGE_EXECUTE || prot == PAGE_EXECUTE_READ ||
           prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY;
}

// Safely read a pointer without crashing if the address is invalid
static void* SafeReadPtr(void* ptr) {
    void* result = nullptr;
    __try { 
        result = *static_cast<void**>(ptr); 
    } 
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

bool CopyCString(char* dest, size_t capacity, const char* src) {
    if (!dest || !capacity || !src) return false;
    size_t i = 0;
    for (; i + 1 < capacity && src[i]; ++i) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
    return i > 0;
}

bool CopyEngineString(char* dest, size_t capacity, const EngineString* src) {
    if (!dest || !capacity || !src || !src->data || !src->length) return false;
    if (src->length > src->capacity || src->length > 4096) return false;
    
    size_t count = std::min(capacity - 1, static_cast<size_t>(src->length));
    std::memcpy(dest, src->data, count);
    dest[count] = '\0';
    return count > 0;
}

// Find the active story quest from the quest manager
void* FindActiveQuest(unsigned char* questManager) {
    auto** quests = *reinterpret_cast<void***>(questManager + 0x24);
    std::uint32_t count = *reinterpret_cast<std::uint32_t*>(questManager + 0x28);
    
    if (!quests || count == 0 || count > 4096) return nullptr;
    
    void* finalQuest = nullptr;
    
    for (std::uint32_t i = 0; i < count; ++i) {
        void* quest = quests[i];
        if (!quest) continue;
        
        auto** vtable = *reinterpret_cast<void***>(quest);
        if (!vtable || !IsExecutable(vtable[3]) || !IsExecutable(vtable[4]) || 
            !IsExecutable(vtable[10])) continue;
        
        auto getState = reinterpret_cast<QuestGetStateFn>(vtable[10]);
        auto isFinal = reinterpret_cast<QuestGetBoolFn>(vtable[3]);
        auto isMainPlot = reinterpret_cast<QuestGetBoolFn>(vtable[4]);
        
        int state = getState(quest);
        
        // Active main plot quest? Return it immediately
        if (state == 1 && isMainPlot(quest)) return quest;
        
        // Track the final quest in case we need it
        if (isFinal(quest)) finalQuest = quest;
    }
    
    // If the final quest is completed, return it (game finished)
    if (finalQuest) {
        auto** vtable = *reinterpret_cast<void***>(finalQuest);
        if (reinterpret_cast<QuestGetStateFn>(vtable[10])(finalQuest) == 2) {
            return finalQuest;
        }
    }
    
    return nullptr;
}

// Read all the game state from memory (wrapped in exception handler)
bool ReadGameMemory(void* engineModule, void* getActiveLevelAddr, void* getLevelNameAddr,
                    void* gameModule, RawGameData* output) {
    if (!output) return false;
    
    std::memset(output, 0, sizeof(*output));
    output->characterIndex = -1;
    output->level = -1;
    output->xp = -1;
    
    __try {
        // Get current level name
        auto** gamePointer = reinterpret_cast<void**>(
            static_cast<unsigned char*>(engineModule) + Config::kEngineGamePointerRva);
        void* game = *gamePointer;
        
        if (game && getActiveLevelAddr && getLevelNameAddr) {
            void* level = reinterpret_cast<GetActiveLevelFn>(getActiveLevelAddr)(game);
            if (level) {
                const char* name = reinterpret_cast<GetLevelNameFn>(getLevelNameAddr)(level);
                CopyCString(output->levelName, sizeof(output->levelName), name);
            }
        }
        
        // Get active quest
        auto* questManager = static_cast<unsigned char*>(gameModule) + Config::kQuestManagerRva;
        void* quest = FindActiveQuest(questManager);
        
        if (quest) {
            auto** vtable = *reinterpret_cast<void***>(quest);
            if (vtable && IsExecutable(vtable[0])) {
                auto* questName = static_cast<const EngineString*>(
                    reinterpret_cast<QuestGetStringFn>(vtable[0])(quest));
                CopyEngineString(output->questId, sizeof(output->questId), questName);
            }
        }
        
        // Get player data (character, level, XP)
        auto** singletonPtr = reinterpret_cast<unsigned char**>(
            static_cast<unsigned char*>(gameModule) + Config::kGameSingletonRva);
        unsigned char* singleton = *singletonPtr;
        
        if (singleton) {
            auto* localPlayer = *reinterpret_cast<unsigned char**>(singleton + 0x168);
            if (localPlayer) {
                int character = *reinterpret_cast<int*>(localPlayer + 0x3B8);
                if (character >= 0 && character <= 4) {
                    output->characterIndex = character;
                }
                
                // Get the LogicalPlayer for XP and level
                auto* gameplayState = *reinterpret_cast<unsigned char**>(localPlayer + 0x24);
                if (gameplayState) {
                    auto* logicalPlayer = *reinterpret_cast<unsigned char**>(gameplayState + 0x58C);
                    void* expectedVtable = static_cast<unsigned char*>(gameModule) + 
                                          Config::kLogicalPlayerVtableRva;
                    
                    if (logicalPlayer && *reinterpret_cast<void**>(logicalPlayer) == expectedVtable) {
                        int xp = *reinterpret_cast<int*>(logicalPlayer + 0x20);
                        int level = *reinterpret_cast<int*>(logicalPlayer + 0x2C);
                        
                        if (xp >= 0 && level >= 1 && level <= 60) {
                            output->xp = xp;
                            output->level = level;
                        }
                    }
                }
            }
        }
        
        return output->levelName[0] || output->questId[0] || 
               output->characterIndex >= 0 || output->level >= 1;
               
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::memset(output, 0, sizeof(*output));
        output->characterIndex = -1;
        output->level = -1;
        output->xp = -1;
        return false;
    }
}

std::string MakeFriendly(const std::string& internal) {
    std::string result = internal;
    std::replace(result.begin(), result.end(), '_', ' ');
    return result;
}

const DeadIslandData::QuestEntry* LookupQuest(const std::string& id) {
    for (const auto& entry : DeadIslandData::kQuests) {
        if (_stricmp(entry.id, id.c_str()) == 0) return &entry;
    }
    return nullptr;
}

const DeadIslandData::LevelEntry* LookupLevel(const std::string& id) {
    for (const auto& entry : DeadIslandData::kLevels) {
        if (_stricmp(entry.id, id.c_str()) == 0) return &entry;
    }
    return nullptr;
}

const char* GetCharacterName(int index) {
    switch (index) {
        case 0: return "Xian Mei";
        case 1: return "Ryder";
        case 2: return "Purna";
        case 3: return "Sam B";
        case 4: return "Logan";
        default: return nullptr;
    }
}
}

bool GameStateReader::Initialize() {
    gameModule_ = GetModuleHandleA("game_x86_rwdi.dll");
    engineModule_ = GetModuleHandleA("engine_x86_rwdi.dll");
    
    if (!gameModule_ || !engineModule_) {
        Log("Game DLLs not loaded yet");
        return false;
    }
    
    DWORD gameTimestamp = GetModuleTimestamp(gameModule_);
    DWORD engineTimestamp = GetModuleTimestamp(engineModule_);
    
    Log("game_x86_rwdi.dll timestamp: 0x%08lX", gameTimestamp);
    Log("engine_x86_rwdi.dll timestamp: 0x%08lX", engineTimestamp);
    
    supported_ = (gameTimestamp == Config::kGameDllTimestamp && 
                  engineTimestamp == Config::kEngineDllTimestamp);
    
    if (!supported_) {
        Log("Unsupported game build (need original 2011 release)");
        return true;
    }
    
    // Resolve exported functions
    getActiveLevel_ = reinterpret_cast<void*>(GetProcAddress(
        reinterpret_cast<HMODULE>(engineModule_), "?GetActiveLevel@IGame@@QAEPAVILevel@@XZ"));
    getLevelName_ = reinterpret_cast<void*>(GetProcAddress(
        reinterpret_cast<HMODULE>(engineModule_), "?GetLevelName@ILevel@@QAEPBDXZ"));
    
    if (!getActiveLevel_ || !getLevelName_) {
        supported_ = false;
        Log("Engine exports not found");
        return true;
    }
    
    Log("Successfully hooked into Dead Island (2011 build)");
    return true;
}

GameState GameStateReader::Read() {
    GameState result;
    result.validBuild = supported_;
    
    if (!supported_) return result;
    
    RawGameData raw{};
    if (!ReadGameMemory(engineModule_, getActiveLevel_, getLevelName_, 
                        gameModule_, &raw)) {
        return result;
    }
    
    result.levelInternal = raw.levelName;
    result.questInternal = raw.questId;
    
    // Get friendly level name
    if (!result.levelInternal.empty()) {
        if (auto* level = LookupLevel(result.levelInternal)) {
            result.location = level->title;
        } else {
            result.location = MakeFriendly(result.levelInternal);
        }
        lastLocation_ = result.location;
    } else {
        // Keep last known level to avoid flickering when engine returns nullptr temporarily
        result.location = lastLocation_;
    }
    
    // Get quest info
    if (auto* quest = LookupQuest(result.questInternal)) {
        result.questTitle = quest->title;
        result.storyPercent = quest->storyPercent;
    } else {
        result.questTitle = MakeFriendly(result.questInternal);
    }
    
    // Character name
    if (const char* name = GetCharacterName(raw.characterIndex)) {
        result.character = name;
        lastCharacter_ = result.character;
    } else {
        result.character = lastCharacter_;
    }
    
    result.playerLevel = raw.level;
    result.experience = raw.xp;
    
    return result;
}
