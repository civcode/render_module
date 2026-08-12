#ifndef DEBUG_CONSOLE_HPP_
#define DEBUG_CONSOLE_HPP_

#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <streambuf>
#include <vector>

#include <imgui.h>

class DebugConsole
{
public:
    DebugConsole() = default;
    ~DebugConsole();
    DebugConsole(const DebugConsole&) = delete;
    DebugConsole& operator=(const DebugConsole&) = delete;

    // Nested ImGuiStreamBuf class
    class ImGuiStreamBuf : public std::streambuf {
    public:
        ImGuiStreamBuf(DebugConsole& console);

    protected:
        int overflow(int c) override;
        int sync() override;

    private:
        void flushBuffer();

        std::string buffer;
        DebugConsole& console;
    };

    static DebugConsole& Console(); // Singleton accessor

    void Log(const char* fmt, ...) IM_FMTARGS(2);
    void Clear();
    void Render(const char* title = "Debug Console", bool* p_open = nullptr);
    void SetCoutRedirect(bool enable);
    void SetMaxEntries(std::size_t maxEntries);

private:
    std::vector<std::string> logs;
    mutable std::mutex mutex_;
    bool scrollToBottom = false;
    std::size_t maxEntries_ = 10000;
    std::streambuf* oldCoutBuf_ = nullptr;
    std::unique_ptr<ImGuiStreamBuf> imguiBuf_;
};

#endif // DEBUG_CONSOLE_HPP_
