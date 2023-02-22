#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <sstream>
#include <3ds.h>

#include <curl/curl.h>
#include <unistd.h>
#include <inih/cpp/INIReader.h>
#include "webdav.h"

using namespace std;

bool componentsInit() {
    bool result = true;
    gfxInitDefault();

    consoleInit(GFX_BOTTOM, NULL);
    printf(CONSOLE_RED "\n 3DavSync by szclsya\n" CONSOLE_RESET);

    consoleInit(GFX_TOP, NULL);
    printf("Initializing components...\n\n");

    APT_SetAppCpuTimeLimit(30);
    cfguInit();
    romfsInit();
    pxiDevInit();
    amInit();
    acInit();

    u32* socketBuffer = (u32*)memalign(0x1000, 0x100000);
    if(socketBuffer == NULL){printf("Failed to create socket buffer.\n"); result = false;}
    if(socInit(socketBuffer, 0x100000)){printf("socInit failed.\n"); result = false;}

    httpcInit(0);
    sslcInit(0);
    return result;
}

void componentsExit() {
    sslcExit();
    httpcExit();
    socExit();
    acExit();
    pxiDevExit();
    romfsExit();
    cfguExit();
    gfxExit();
}

int main(int argc, char* argv[]) {
    if(!componentsInit()) {
        printf("Component init failed! Exiting in 5secs.");
        sleep(15);
        componentsExit();
        return 1;
    };

    INIReader reader("/3ds/3DavSync.ini");
    vector<string> bad_config;
    vector<pair<string, WebDavClient*>> clients;
    if (reader.ParseError() < 0) {
        printf("Error loading configuration file at /3ds/3DavSync.ini");
    } else {
        string enabled = reader.Get("General", "Enabled", "");
        string buf;
        stringstream ss(enabled);

        while (ss >> buf) {
            if (buf == "") {
                break;
            }
            // Load server config
            string url = reader.Get(buf, "Url", "");
            string local_path = reader.Get(buf, "LocalPath", "");
            string username = reader.Get(buf, "Username", "");
            string password = reader.Get(buf, "Password", "");
            if (url.size() == 0 || local_path.size() == 0) {
                bad_config.push_back(buf);
            } else {
                WebDavClient* c = new WebDavClient(url, local_path);
                if (username.size() != 0) {
                    c->set_basic_auth(username, password);
                }
                clients.push_back(make_pair(buf, c));
            }
        }
    }

    if (!bad_config.empty()) {
        for (auto name : bad_config) {
            printf("Malformed config for server %s", name.c_str());
        }
    } else {
        vector<pair<string, bool>> results;
        for (auto [name, client] : clients) {
            printf(CONSOLE_BLUE "\nSyncing %s\n", name.c_str());
            printf(CONSOLE_RESET);
            bool this_result = client->compareAndUpdate();
            results.push_back(make_pair(name, this_result));
        }
        // Print summary
        printf(CONSOLE_BLUE "\n\n===== Sync Summary =====\n\n" CONSOLE_RESET);
        for (auto [name, result] : results) {
            if (result) {
                printf(CONSOLE_GREEN "SUCCESS " CONSOLE_RESET);
            } else {
                printf(CONSOLE_RED   "FAILED  " CONSOLE_RESET);
            }
            printf("%s\n", name.c_str());
        }
    }

    printf(CONSOLE_GREEN "\n Press A to exit.\n" CONSOLE_RESET);
    while (aptMainLoop()) {
        gspWaitForVBlank();
        gfxSwapBuffers();
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_A) {
            break;
        }
    }

    componentsExit();
    return 0;
}
