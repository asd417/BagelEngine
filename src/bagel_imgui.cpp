#include "bagel_imgui.h"

namespace bagel {
    //Default Console Callbacks
    char* ClearConsole(void* ptr) {
        ConsoleApp* console = static_cast<ConsoleApp*>(ptr);
        console->ClearLog();
        return "";
    }
    char* HelpCommand(void* ptr) {
        ConsoleApp* console = static_cast<ConsoleApp*>(ptr);
        console->AddLog("Commands:");
        for (int i = 0; i < console->Commands.Size; i++)
            console->AddLog("- %s", console->Commands[i]);
        return "";
    }
    char* HistoryCommand(void* ptr) {
        ConsoleApp* console = static_cast<ConsoleApp*>(ptr);
        int first = console->History.Size - 10;
        for (int i = first > 0 ? first : 0; i < console->History.Size; i++)
            console->AddLog("%3d: %s\n", i, console->History[i]);
        return "";
    }
}