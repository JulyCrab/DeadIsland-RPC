#pragma once

#include <string>

struct GameState {
    bool validBuild = false;
    
    // Extracted Data
    std::string levelInternal;     // Raw level ID from game
    std::string location;          // Friendly location name
    std::string questInternal;     // Raw quest ID
    std::string questTitle;        // Friendly quest name
    std::string character;         // Character name
    int storyPercent = -1;         // Story completion (0-100)
    int playerLevel = -1;          // Player level
    int experience = -1;           // Total XP
};

class GameStateReader {
public:
    bool Initialize();
    GameState Read();

private:
    void* gameModule_ = nullptr;
    void* engineModule_ = nullptr;
    void* getActiveLevel_ = nullptr;
    void* getLevelName_ = nullptr;
    std::string lastLocation_;
    std::string lastCharacter_;
    bool supported_ = false;
};
