/* The session side of the complex part: who the host is and what it said. The host's answer is one row of the mod's
 * shared table, which HellForge gives every machine of the lobby; no machine assumes an answer it does not hold. */
#include "co.h"

#define HAS(call) (g_coApi != NULL && g_coApi->size >= offsetof(HfApi, call) + sizeof(void*))

int co_peers_is_host(void)
{
    HfSession s = {0};
    if (!HAS(session)) return 1;
    g_coApi->session(g_coMod, &s);
    return s.role != HF_SESSION_JOINER;
}

int co_peers_host_row(void)
{
    CoHostRow row = {0};
    if (co_peers_is_host()) return co_complex_on() ? CO_HOST_ROW_ON : CO_HOST_ROW_OFF;
    /* another protocol's row is no answer this build can read */
    if (!HAS(share_get) || g_coApi->share_get(g_coMod, CO_SHARE_KEY, &row, sizeof row) != sizeof row || row.protocol != CO_SHARE_PROTOCOL)
        return CO_HOST_ROW_NONE;
    return row.on ? CO_HOST_ROW_ON : CO_HOST_ROW_OFF;
}
