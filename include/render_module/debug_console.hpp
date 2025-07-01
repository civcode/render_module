#ifndef DEBUG_CONSOLE_HPP_
#define DEBUG_CONSOLE_HPP_

#include <iostream>
#include <string>
#include <vector>
#include <streambuf>

#include <imgui.h>

class DebugConsole
{
public:
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

private:
    std::vector<std::string> logs;
    bool scrollToBottom = false;
};

#endif // DEBUG_CONSOLE_HPP_
