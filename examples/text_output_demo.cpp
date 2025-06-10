#include "render_module/render_module.hpp"

#include <iostream>
#include <vector>
#include <string>

class DebugConsole
{
public:
    void AddLog(const char* fmt, ...) IM_FMTARGS(2)
    {
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, IM_ARRAYSIZE(buf), fmt, args);
        va_end(args);
        buf[IM_ARRAYSIZE(buf) - 1] = 0;
        logs.emplace_back(buf);
        scrollToBottom = true;
    }

    void Clear()
    {
        logs.clear();
    }

    void Draw(const char* title, bool* p_open = nullptr)
    {
        if (!ImGui::Begin(title, p_open))
        {
            ImGui::End();
            return;
        }

        if (ImGui::Button("Clear")) Clear();
        ImGui::Separator();

        ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar);

        for (const auto& line : logs)
            ImGui::TextUnformatted(line.c_str());

        if (scrollToBottom)
            ImGui::SetScrollHereY(1.0f);

        scrollToBottom = false;
        ImGui::EndChild();
        ImGui::End();
    }

    // void spin_once()
    // {
    //     static int cnt;
    //     AddLog("Debug Console entry %d.", cnt++);
    //     Draw("Debug Console");
    // }

private:
    std::vector<std::string> logs;
    bool scrollToBottom = false;
};




int main() {
    RenderModule::Init(980, 720);

    RenderModule::RegisterImGuiCallback([]() {

        // ImGui::Begin("Main Window", nullptr, ImGuiWindowFlags_MenuBar);
        ImGui::Begin("Main Text Window", nullptr);
        ImGui::TextWrapped(
            "CJK text will only appear if the font was loaded with the appropriate CJK character ranges. "
            "Call io.Fonts->AddFontFromFileTTF() manually to load extra character ranges. "
            "Read docs/FONTS.md for details.");
        ImGui::Text("Hiragana: \xe3\x81\x8b\xe3\x81\x8d\xe3\x81\x8f\xe3\x81\x91\xe3\x81\x93 (kakikukeko)");
        ImGui::Text("Kanjis: \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e (nihongo)");
        static char buf[32] = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
        ImGui::InputText("UTF-8 input", buf, IM_ARRAYSIZE(buf));
        ImGui::End();

    });

    RenderModule::RegisterImGuiCallback([]() {
        ImGui::Begin("Main Window 2", nullptr, ImGuiWindowFlags_MenuBar);
        ImGui::TextWrapped(
            "This is a second window with some text. "
            "It will also display CJK characters if the font supports them.");
        ImGui::Text("Hiragana: \xe3\x81\x8b\xe3\x81\x8d\xe3\x81\x8f\xe3\x81\x91\xe3\x81\x93 (kakikukeko)");
        ImGui::Text("Kanjis: \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e (nihongo)");
        ImGui::End();
    });

    // static DebugConsole console;
    DebugConsole console;
    console.AddLog("Application started...");
    console.AddLog("Debug Console initialized.");

    // RenderModule::RegisterImGuiCallback([&console]() {
    //     static int cnt;
    //     console.AddLog("Debug Console entry %d.", cnt++);
    //     console.Draw("Debug Console");
    // });





    
    RenderModule::Run();
    RenderModule::Shutdown();
    return 0;
}
