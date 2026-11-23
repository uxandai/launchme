#define LAUNCHME_PLUGIN_EXPORTS
#include "launchme_plugin.h"
#include <string.h>

/* A launch provider skeleton. The host calls `launch` with the game snapshot
 * (installDirectory, executable) when the user has chosen this plugin as the game's
 * launch route. A real provider parses params.game, prepares whatever it needs next to
 * the installed files, starts the game's process and returns promptly: the host then
 * follows the real process through its own activity watch. Returning quickly matters -
 * do not block until the game exits.
 *
 * Responses:
 *   {"launched":true}                      started; host begins the session
 *   {"launched":false,"message":"..."}     not started; message shown to the user
 *   {"launched":false,"steamRequired":true,"message":"..."}  needs the store client
 *
 * Lifecycle calls (activate/deactivate) and other methods need no work here.
 * Parse the UTF-8 request JSON in a real plugin; see plugins/howlongtobeat. */
static int32_t LAUNCHME_CALL invoke(const char* json, uint32_t length, LaunchmeEmitV1 emit, void* context) {
    static const char launched[] = "{\"launched\":true}";
    static const char ok[] = "{}";
    if (!json || !emit || length > LAUNCHME_PLUGIN_MAX_JSON) return 1;
    /* A real plugin dispatches on params.method / params.command; this stub starts the
     * game for every launch request and acknowledges everything else. */
    if (strstr(json, "\"launched\"") == 0 && strstr(json, "installDirectory") != 0) {
        /* TODO: start the game's process here, then report it started. */
        emit(context, launched, (uint32_t)strlen(launched));
        return 0;
    }
    emit(context, ok, (uint32_t)strlen(ok));
    return 0;
}
static const LaunchmePluginV1 api = {sizeof(LaunchmePluginV1), LAUNCHME_PLUGIN_ABI, invoke};
const LaunchmePluginV1* LAUNCHME_CALL launchme_plugin_v1(uint32_t host_abi) {
    return host_abi == LAUNCHME_PLUGIN_ABI ? &api : 0;
}
