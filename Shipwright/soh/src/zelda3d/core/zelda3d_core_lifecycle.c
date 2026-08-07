// The core's run-scoped state: one owner, one reset.
//
// THE DEFECT THIS FIXES is not that these are globals, it is that nothing owned their LIFETIME.
// Written for a program that exits, a global's validity ends when the process does and no code has
// to say so. Under the launcher the process outlives the game -- a core is run, ends, and another
// game starts on the same engine -- so every one of them has a lifetime, the RUN, that belonged to
// nobody. Clearing was left to luck: whichever teardown path happened to run on the way out.
//
// It cost a crash. `gPlayState` survived a run because the exit path abandoned a live gamestate
// instead of destroying it, so Play_Destroy (which nulls it) never ran; the NEXT run's InitOTR then
// walked the previous run's actor lists through a freed heap and SIGSEGV'd, inside a ShipInit
// function whose only guard is `if (gPlayState != nullptr)`. Note what that guard was entitled to
// assume and could not: that a non-NULL gPlayState means a live one.
//
// So the state moved HERE, out of the decomp files that merely happened to declare it, and the core
// resets it at the start of every run. That is the load-bearing change: a run now begins clean BY
// CONSTRUCTION rather than because some earlier teardown behaved. Adding new run-scoped state is a
// line in Zelda3D_CoreRunBegin, in the file whose whole subject is this question -- not a global
// dropped in whichever .c first needed it, with its cleanup left to be discovered by the next crash.
//
// WHY THE SYMBOL NAMES STAY. `gPlayState` and `gGameState` are externed by name from dozens of
// decomp and enhancement files. Renaming them into `gCore.play` would touch every one of those and
// buy nothing this does not already give: the ownership, not the spelling, was the defect. They are
// defined here, reset here, and audited here; that they remain C globals is a linkage detail.

#include "global.h"
#include "z64.h"
#include <stdio.h>

void Graph_ResetRunState(void); // graph.c

// ---------------------------------------------------------------------------------------------
// The state itself. These definitions used to live in z_play.c and game.c.
// ---------------------------------------------------------------------------------------------

// The live PlayState, or NULL when no gameplay gamestate exists. Set by Play_Init, cleared by
// Play_Destroy -- and, since 2026-08-07, guaranteed NULL at the start of a run whether or not
// Play_Destroy ever got the chance.
PlayState* gPlayState;

// The gamestate currently being run by RunFrame, or NULL when none is live. RunFrame clears it when
// it destroys and frees one, which is also how Graph_ThreadEntry knows the state machine has
// finished unwinding.
GameState* gGameState;

// ---------------------------------------------------------------------------------------------
// The lifecycle
// ---------------------------------------------------------------------------------------------

// Called by the core's entry point BEFORE anything else in a run -- before InitOTR, which is where
// the stale-pointer crash happened. Everything above is reset here, unconditionally, so the run
// cannot inherit the last one regardless of how it ended (a clean quit, a crash-triggered exit, a
// game switch mid-frame).
void Zelda3D_CoreRunBegin(void) {
    gPlayState = NULL;
    gGameState = NULL;
    // graph.c's runFrameContext -- the frame loop's resume point and gfx context. Reset through a
    // function because it is file-static there, which is the right scope for it; what it must not
    // be is longer-lived than a run.
    Graph_ResetRunState();
}

// Called after the frame loop has finished and before the heaps are freed.
//
// The reset above already makes the next run safe, so this exists for a different reason: to say
// whether the teardown ACTUALLY RAN. Without it, a regression that stops destroying the gamestate
// is completely silent -- Zelda3D_CoreRunBegin would paper over it every time, and the first
// symptom would be a save that never flushed or an actor that never got its destroy callback. So
// the check reports rather than repairs, and repairs are left to the code whose job they are.
//
// Returns the number of pointers found still set.
int Zelda3D_CoreRunEnd(void) {
    struct {
        const char* name;
        void* value;
        const char* whoShouldHaveCleared;
    } checks[] = {
        { "gPlayState", (void*)gPlayState, "Play_Destroy, via RunFrame's GameState_Destroy" },
        { "gGameState", (void*)gGameState, "RunFrame, after it frees the gamestate" },
    };
    const int total = (int)(sizeof checks / sizeof checks[0]);
    int leaked = 0;

    for (int i = 0; i < total; i++) {
        if (checks[i].value == NULL) {
            continue;
        }
        leaked++;
        fprintf(stderr,
                "ZELDA3D CORE: %s is STILL SET (%p) after run() finished.\n"
                "  Should have been cleared by: %s -- so that teardown did NOT run.\n"
                "  The next run is safe (Zelda3D_CoreRunBegin resets it), but whatever that teardown\n"
                "  also does -- saving, actor destroy callbacks -- did not happen either.\n",
                checks[i].name, checks[i].value, checks[i].whoShouldHaveCleared);
    }

    // Printed pass or fail, with the denominator. "no leaks" on its own is indistinguishable from a
    // check that looked at nothing, and this list is exactly the kind a later change outgrows.
    fprintf(stderr, "ZELDA3D CORE: run ended; checked %d run-scoped pointer(s), %d still set.\n", total, leaked);
    fflush(stderr);
    return leaked;
}
