#include "render_module/render_module.hpp"

#include <iostream>

int main() {
    RenderModule::Init(980, 720);
    RenderModule::EnableDebugConsole();

    RenderModule::Console().Log("Application started...");
    RenderModule::Console().Log("Debug Console initialized.");

    std::cout << "Default std::cout text output." << std::endl;

    RenderModule::Console().SetCoutRedirect(true);
    std::cout << "Redirecting std::cout to ImGui Debug Console..." << std::endl;
    std::cout << "Hello, world!" << std::endl;
    RenderModule::Console().SetCoutRedirect(false);
    
    RenderModule::Run();
    RenderModule::Shutdown();
    return 0;
}
