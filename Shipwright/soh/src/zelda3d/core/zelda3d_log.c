// Zelda3D diagnostic logger — see zelda3d_log.h. The channel-name table here and the enum in the
// header are the ONE registry for debug channels (no per-site getenv).
#include "zelda3d_log.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char* kZelda3dLogNames[Z3D_LOG_COUNT] = {
    "rider",     // Z3D_LOG_RIDER
    "titlecam",  // Z3D_LOG_TITLECAM
    "titleskip", // Z3D_LOG_TITLESKIP
    "fireglow",  // Z3D_LOG_FIREGLOW
    "wordmark",  // Z3D_LOG_WORDMARK
    "sheen",     // Z3D_LOG_SHEEN
    "input",     // Z3D_LOG_INPUT
    "room",      // Z3D_LOG_ROOM
    "link",      // Z3D_LOG_LINK
};

static unsigned char sEnabled[Z3D_LOG_COUNT];
static int sInit = 0;

static int nameToChannel(const char* name, int len) {
    int i;
    for (i = 0; i < Z3D_LOG_COUNT; i++) {
        if ((int)strlen(kZelda3dLogNames[i]) == len && strncasecmp(kZelda3dLogNames[i], name, len) == 0) {
            return i;
        }
    }
    return -1;
}

static void setAll(int on) {
    int i;
    for (i = 0; i < Z3D_LOG_COUNT; i++) {
        sEnabled[i] = (unsigned char)(on ? 1 : 0);
    }
}

// Parse ZELDA3D_LOG once: comma-separated channel names, or "all".
static void lazyInit(void) {
    const char* env;
    if (sInit) {
        return;
    }
    sInit = 1;
    env = getenv("ZELDA3D_LOG");
    if (env == NULL || env[0] == '\0') {
        return;
    }
    while (*env != '\0') {
        const char* start;
        int len;
        while (*env == ',' || isspace((unsigned char)*env)) {
            env++;
        }
        start = env;
        while (*env != '\0' && *env != ',' && !isspace((unsigned char)*env)) {
            env++;
        }
        len = (int)(env - start);
        if (len > 0) {
            if (len == 3 && strncasecmp(start, "all", 3) == 0) {
                setAll(1);
            } else {
                int ch = nameToChannel(start, len);
                if (ch >= 0) {
                    sEnabled[ch] = 1;
                } else {
                    fprintf(stderr, "[z3dlog] unknown channel in ZELDA3D_LOG: %.*s\n", len, start);
                }
            }
        }
    }
}

int Zelda3D_LogEnabled(int channel) {
    lazyInit();
    if (channel < 0 || channel >= Z3D_LOG_COUNT) {
        return 0;
    }
    return sEnabled[channel];
}

int Zelda3D_LogSet(const char* name, int on) {
    int ch;
    lazyInit();
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    if (strcasecmp(name, "all") == 0) {
        setAll(on);
        return 1;
    }
    ch = nameToChannel(name, (int)strlen(name));
    if (ch < 0) {
        return 0;
    }
    sEnabled[ch] = (unsigned char)(on ? 1 : 0);
    return 1;
}

void Zelda3D_LogList(char* out, int outCap) {
    int i, used = 0;
    lazyInit();
    if (out == NULL || outCap <= 0) {
        return;
    }
    out[0] = '\0';
    for (i = 0; i < Z3D_LOG_COUNT; i++) {
        int n = snprintf(out + used, (size_t)(outCap - used), "%s%s=%s", (i > 0) ? " " : "",
                         kZelda3dLogNames[i], sEnabled[i] ? "on" : "off");
        if (n < 0 || n >= outCap - used) {
            break;
        }
        used += n;
    }
}

const char* Zelda3D_LogName(int channel) {
    if (channel < 0 || channel >= Z3D_LOG_COUNT) {
        return "?";
    }
    return kZelda3dLogNames[channel];
}
