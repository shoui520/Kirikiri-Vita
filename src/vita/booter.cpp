#include <psp2/appmgr.h>
#include <psp2/kernel/processmgr.h>

#include <cstdio>
#include <cstring>

int main() {
    char game_id[80]{};
    if (FILE* file = std::fopen("app0:game.id", "rb")) {
        std::fgets(game_id, sizeof(game_id), file);
        std::fclose(file);
    }
    game_id[std::strcspn(game_id, "\r\n")] = 0;
    if (game_id[0]) {
        char parameter[128]{};
        std::snprintf(parameter, sizeof(parameter), "game=%s", game_id);
        sceAppMgrLaunchAppByName2("KRVITA001", parameter, nullptr);
    }
    sceKernelExitProcess(0);
    return 0;
}

