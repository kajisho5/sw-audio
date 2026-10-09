/* A test hook of every SW AUDIO plug-in: the text messages a plug-in window's page posts (ui/host.js: "s <i> <v>", "c <name> <args>", "p" ...) without a window.
   The reply is the script the window would evaluate ("" when none). tools/host_smoke.cpp sends them to check the whole path from the page to the core on Linux, where there is no window.
   Main thread only; the reply stays valid until the next call. */
#pragma once
#include <clap/plugin.h>

#define SW_EXT_MESSAGE "com.seventh-well.sw-audio.message/1"

typedef struct sw_plugin_message {
    const char *(*send)(const clap_plugin_t *plugin, const char *message);
} sw_plugin_message_t;
