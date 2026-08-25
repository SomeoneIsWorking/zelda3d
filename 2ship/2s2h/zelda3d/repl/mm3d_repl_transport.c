#include "2s2h/zelda3d/repl/mm3d_repl_transport.h"

#include <fcntl.h>
#include <libultraship/bridge/fifo_rpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct Zelda3DMmReplRequest {
    char id[ZELDA3D_FIFO_RPC_ID_LENGTH + 1];
    size_t replyCount;
    int tagged;
    int writeFailed;
} Zelda3DMmReplRequest;

static int sFd = -2; // -2 = unopened, -1 = disabled or failed
static char sOutputPath[512];
static char sInputBuffer[1024];
static size_t sInputLength = 0;

static int Zelda3D_MmReplWriteLine(const char* line) {
    if (sOutputPath[0] == '\0') {
        return 0;
    }
    FILE* output = fopen(sOutputPath, "a");
    if (output == NULL) {
        return 0;
    }
    int wrote = (fputs(line, output) != EOF) && (fputc('\n', output) != EOF);
    return (fclose(output) == 0) && wrote;
}

static void Zelda3D_MmReplTransportReply(const char* line, void* user) {
    Zelda3DMmReplRequest* request = (Zelda3DMmReplRequest*)user;
    if (!request->tagged) {
        Zelda3D_MmReplWriteLine(line);
        return;
    }

    char frame[4096];
    int length = Zelda3DFifoRpc_FormatData(frame, sizeof(frame), request->id, line);
    if ((length >= 0) && ((size_t)length < sizeof(frame)) && Zelda3D_MmReplWriteLine(frame)) {
        request->replyCount++;
    } else {
        request->writeFailed = 1;
    }
}

static void Zelda3D_MmReplExecuteLine(char* line, Zelda3DMmReplCommandHandler handler, void* user) {
    Zelda3DMmReplRequest request = { 0 };
    const char* command = line;
    int requestKind = Zelda3DFifoRpc_ParseRequest(line, request.id, &command);
    if (requestKind < 0) {
        Zelda3D_MmReplWriteLine("err malformed-request");
        return;
    }
    request.tagged = requestKind > 0;
    handler(command, Zelda3D_MmReplTransportReply, &request, user);
    if (request.tagged && !request.writeFailed) {
        char frame[64];
        int length = Zelda3DFifoRpc_FormatEnd(frame, sizeof(frame), request.id, request.replyCount);
        if ((length >= 0) && ((size_t)length < sizeof(frame))) {
            Zelda3D_MmReplWriteLine(frame);
        }
    }
}

static void Zelda3D_MmReplOpen(void) {
    const char* path = getenv("ZELDA3D_MM_REPL");
    if ((path == NULL) || (path[0] == '\0')) {
        sFd = -1;
        return;
    }
    mkfifo(path, 0666);                    // EEXIST is expected across controlled restarts.
    sFd = open(path, O_RDWR | O_NONBLOCK); // O_RDWR keeps a writer so reads never return EOF.
    snprintf(sOutputPath, sizeof(sOutputPath), "%s.out", path);
    if (sFd >= 0) {
        FILE* output = fopen(sOutputPath, "w");
        if (output != NULL) {
            fprintf(output, "Z3D MM REPL ready (fifo=%s)\n", path);
            fclose(output);
        }
    }
}

void Zelda3D_MmReplTransportReset(void) {
    if (sFd >= 0) {
        close(sFd);
    }
    sFd = -2;
    sOutputPath[0] = '\0';
    sInputLength = 0;
}

void Zelda3D_MmReplTransportPoll(Zelda3DMmReplCommandHandler handler, void* user) {
    if (handler == NULL) {
        return;
    }
    if (sFd == -2) {
        Zelda3D_MmReplOpen();
    }
    if (sFd < 0) {
        return;
    }

    for (;;) {
        if (sInputLength >= sizeof(sInputBuffer) - 1) {
            sInputLength = 0;
        }
        ssize_t count = read(sFd, sInputBuffer + sInputLength, sizeof(sInputBuffer) - 1 - sInputLength);
        if (count <= 0) {
            break;
        }
        sInputLength += (size_t)count;
    }
    sInputBuffer[sInputLength] = '\0';

    char* start = sInputBuffer;
    char* newline = NULL;
    while ((newline = strchr(start, '\n')) != NULL) {
        *newline = '\0';
        Zelda3D_MmReplExecuteLine(start, handler, user);
        start = newline + 1;
    }
    sInputLength = strlen(start);
    memmove(sInputBuffer, start, sInputLength + 1);
}
