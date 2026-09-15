#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>

struct DiscordActivity {
    std::string details;
    std::string state;
    std::string largeImage;
    std::string largeText;
    std::string buttonLabel;
    std::string buttonUrl;
    std::int64_t startTimestamp = 0;
};

class DiscordIpc {
public:
    ~DiscordIpc();
    bool Connect(const std::string& applicationId);
    bool SetActivity(const DiscordActivity& activity);
    void Pump();
    void Disconnect();
    bool IsConnected() const { return pipe_ != INVALID_HANDLE_VALUE; }

private:
    bool WriteFrame(std::uint32_t opcode, const std::string& json);
    bool ReadFrame(std::uint32_t& opcode, std::string& json);
    static std::string Escape(const std::string& value);

    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    std::uint64_t nonce_ = 1;
};

