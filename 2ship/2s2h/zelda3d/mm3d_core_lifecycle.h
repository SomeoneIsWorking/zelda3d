#ifndef MM3D_CORE_LIFECYCLE_H
#define MM3D_CORE_LIFECYCLE_H

#ifdef __cplusplus
extern "C" {
#endif

// MM's run lifecycle. The counterpart of soh's zelda3d/core/zelda3d_core_lifecycle.c -- deliberately
// a SEPARATE copy rather than shared code: the two cores are dlopen'd RTLD_LOCAL, so neither can see
// the other's symbols, and the state each one has to reset is its own game's globals.
void Zelda3D_CoreRunBegin(void);
int Zelda3D_CoreRunEnd(void);

// A latch that belongs to a RUN rather than to the process. Same contract as soh's: declare
// `static Zelda3DOnce x;` beside the thing it guards and write `if (Zelda3D_Once(&x))`. It carries
// its own run stamp, so it needs no entry in any reset list -- which is the point, because a flag in
// one file whose reset lives in another is exactly the pairing that gets missed.
typedef struct {
    unsigned int epoch;
} Zelda3DOnce;

int Zelda3D_Once(Zelda3DOnce* once);

#ifdef __cplusplus
}
#endif

#endif
