#include "2s2h/zelda3d/repl/mm3d_repl_command.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static void TestExactCommandMatching(void) {
    Zelda3DMmReplArgs args;
    assert(Zelda3D_MmReplMatch("  warp\t0x5400 ", "warp", &args));
    int32_t entrance = 0;
    assert(Zelda3D_MmReplParseI32(&args, 0, &entrance));
    assert(entrance == 0x5400);
    assert(Zelda3D_MmReplArgsEnd(&args));

    assert(!Zelda3D_MmReplMatch("warpfoo 0x5400", "warp", &args));
    assert(!Zelda3D_MmReplMatch("turnip 90", "turn", &args));
    assert(!Zelda3D_MmReplMatch("pingfoo", "ping", &args));
    assert(!Zelda3D_MmReplMatch("linkstateful idle", "linkstate", &args));
}

static void TestStrictArguments(void) {
    Zelda3DMmReplArgs args;
    int32_t integer = 0;
    float value = 0.0f;

    assert(Zelda3D_MmReplMatch("mscale 0x132 1.25", "mscale", &args));
    assert(Zelda3D_MmReplParseI32(&args, 0, &integer) && (integer == 0x132));
    assert(Zelda3D_MmReplParseFloat(&args, &value) && (fabsf(value - 1.25f) < 0.0001f));
    assert(Zelda3D_MmReplArgsEnd(&args));

    assert(Zelda3D_MmReplMatch("mscale nope 1.0", "mscale", &args));
    assert(!Zelda3D_MmReplParseI32(&args, 0, &integer));
    assert(Zelda3D_MmReplMatch("warp 0x5400junk", "warp", &args));
    assert(!Zelda3D_MmReplParseI32(&args, 0, &integer));
    assert(Zelda3D_MmReplMatch("turn nan", "turn", &args));
    assert(!Zelda3D_MmReplParseFloat(&args, &value));
    assert(Zelda3D_MmReplMatch("actors 999999999999999999999", "actors", &args));
    assert(!Zelda3D_MmReplParseI32(&args, 10, &integer));
}

static void TestTokens(void) {
    Zelda3DMmReplArgs args;
    char token[8];
    assert(Zelda3D_MmReplMatch("switchgame oot", "switchgame", &args));
    assert(Zelda3D_MmReplNextToken(&args, token, sizeof(token)));
    assert(strcmp(token, "oot") == 0);
    assert(Zelda3D_MmReplArgsEnd(&args));

    assert(Zelda3D_MmReplMatch("switchgame identifier-too-long", "switchgame", &args));
    assert(!Zelda3D_MmReplNextToken(&args, token, sizeof(token)));
}

int main(void) {
    TestExactCommandMatching();
    TestStrictArguments();
    TestTokens();
    return 0;
}
