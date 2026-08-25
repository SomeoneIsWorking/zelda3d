// MM's run-scoped state: one owner, one reset.
//
// WHY THIS EXISTS SEPARATELY FROM OoT'S. soh has had `zelda3d/core/zelda3d_core_lifecycle.c` since
// 2026-08-07, and MM had nothing -- no run begin, no run end, no latch mechanism. That was not a
// small gap: the two cores are dlopen'd RTLD_LOCAL, so MM cannot link soh's copy even if it wanted
// to, and every argument the soh file makes applies here unchanged. A core is loaded once and RUN
// more than once; the process outlives the game; so every global and file static has a lifetime --
// the run -- that belonged to nobody.
//
// It cost the same crash, in the same shape, for the same reason. `tools/zelda3d_sequence.sh mm,mm`
// SIGSEGV'd on the second run inside `RegisterDebugMode` (2s2h/DeveloperTools/DeveloperTools.cpp),
// reached from InitOTR, on this line:
//
//     if (gPlayState != NULL) { gPlayState->frameAdvCtx.enabled = false; }
//
// which is character-for-character the failure soh's file was written for: a guard entitled to
// assume that a non-NULL gPlayState is a LIVE one, and a previous run that left it pointing into a
// freed heap. Everything here is the fix for that, ported.
//
// AND ONE DEFECT THAT WAS MM'S ALONE. soh's Graph_ThreadEntry loops
// `while (WindowIsRunning() || gGameState != NULL)`; MM's looped on `WindowIsRunning()` only. Since
// RunFrame returns once per frame and resumes through a goto, requesting exit made MM's loop simply
// stop calling it -- leaving a live, initialised gGameState that nothing destroyed. So on MM,
// **Play_Destroy never ran on any quit**, on every quit since the loop was written.
//
// What that actually costs, read out of Play_Destroy rather than assumed: Actor_CleanupContext,
// Interface_Destroy, KaleidoManager_Destroy, ZeldaArena_Cleanup,
// GameInteractor_ExecuteOnPlayDestroy() and `gPlayState = NULL` all never happened. It is NOT a save
// bug -- there is no save in that function, and the first draft of this comment claimed one. The
// OnPlayDestroy hooks are the part that reaches beyond the run: several 2s2h subsystems free their
// per-run state from that hook and nothing else, so on MM they simply never freed it.
//
// The two mechanisms are soh's, and the reasoning for keeping them distinct is in its header
// comment: a central reset list for STATE (it is also where you say what must be freed first), and
// self-stamping Zelda3DOnce latches for "once per run" flags (a flag in one file with its reset in
// another is the pairing that gets missed).

#include "mm3d_core_lifecycle.h"
#include "mm3d_model_lifecycle.h"
#include "2s2h/BenPortLifecycle.h"
#include "2s2h/zelda3d/repl/mm3d_repl.h"
#include "src/code/cutscene_manager_lifecycle.h"
#include "src/code/graph_lifecycle.h"
#include "object/ObjectExtension.h"

#include "global.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------
// Once-per-run latches
// ---------------------------------------------------------------------------------------------

// Counts runs. Starts at 0, so a zero-initialised Zelda3DOnce has never fired: the first run is
// epoch 1 and every latch is stale against it by construction, which is what lets the idiom work
// with no registration anywhere.
static unsigned int sRunEpoch = 0;

int Zelda3D_Once(Zelda3DOnce* once) {
    if (once->epoch == sRunEpoch) {
        return 0;
    }
    once->epoch = sRunEpoch;
    return 1;
}

// ---------------------------------------------------------------------------------------------
// The audio engine's run-scoped state
// ---------------------------------------------------------------------------------------------

// gAudioCtx is the whole N64 audio engine's state for this run: the note array, the sequence
// players, the soundfont/sample tables and the pointers into the audio heap they all index. It is a
// plain global, so a second run inherits the first one's.
//
// That is what SIGSEGV'd the second MM run after the gPlayState crash above was fixed --
// AudioHeap_ResetLoadStatus, from AudioHeap_Init, from AudioLoad_Init, writing through run 1's
// seqLoadStatus into an audio heap run 1's DeinitOTR had already released. On run 1 the same code is
// harmless for exactly one reason: the struct is in BSS. So the fix is not to guard the writes, it
// is to give every run the run-1 condition. Zeroing here IS what BSS did for the first one.
//
// It does NOT free anything: OTRAudio_Exit owns those allocations and now nulls them at the free
// (see BenPort.cpp). Freeing here as well would be a double free the first time both paths ran.
void Zelda3D_ResetAudioContext(void) {
    const s32 inheritedNotes = gAudioCtx.numNotes;
    const void* inheritedNotePtr = (const void*)gAudioCtx.notes;

    memset(&gAudioCtx, 0, sizeof(gAudioCtx));

    // Printed with what it inherited, not just that it ran: on run 1 this must report 0/NULL, and a
    // report that only ever says "reset" could not tell that apart from a run that inherited a live
    // note array. The equivalent line on the soh side is what falsified a wrong theory about which
    // piece of audio state was actually being carried over.
    fprintf(stderr, "MM3D CORE: gAudioCtx reset (%zu bytes) -- inherited numNotes=%d notes=%p.\n", sizeof(gAudioCtx),
            (int)inheritedNotes, inheritedNotePtr);
    fflush(stderr);
}

// ---------------------------------------------------------------------------------------------
// The save context
// ---------------------------------------------------------------------------------------------

// gSaveContext is the whole game's save + session state. Same argument as the soh side's copy: a
// plain global, so run 2 starts inside run 1's save, and persisted data goes to disk via SaveManager
// rather than living here across runs. Not a dangling-pointer hazard but a WRONG-STATE one, which is
// harder to notice because nothing crashes -- on OoT the equivalent was carrying 16,634 non-zero
// bytes into run 2 with no symptom that named it.
void Zelda3D_ResetSaveContext(void) {
    const unsigned char* bytes = (const unsigned char*)&gSaveContext;
    size_t inheritedNonZero = 0;

    for (size_t i = 0; i < sizeof(gSaveContext); i++) {
        if (bytes[i] != 0) {
            inheritedNonZero++;
        }
    }

    memset(&gSaveContext, 0, sizeof(gSaveContext));

    // A COUNT with its denominator, not "reset ok": run 1 must report 0, and a line that cannot
    // report anything else could not tell run 1 from a run inheriting a full save.
    fprintf(stderr, "MM3D CORE: gSaveContext reset -- inherited %zu non-zero byte(s) of %zu.\n", inheritedNonZero,
            sizeof(gSaveContext));
    fflush(stderr);
}

// ---------------------------------------------------------------------------------------------
// The lifecycle
// ---------------------------------------------------------------------------------------------

// Called by the core's entry point BEFORE anything else in a run -- in particular before InitOTR,
// which is where the stale-pointer crash happened.
void Zelda3D_CoreRunBegin(void) {
    // First, so everything below runs in the new epoch: every Zelda3DOnce in this core is now stale
    // without any of them having to be listed here.
    sRunEpoch++;

    // The live PlayState (src/code/z_play.c) and the gamestate RunFrame is running
    // (src/code/game.c). Both are nulled by the teardown that owns them; this makes a run start
    // clean BY CONSTRUCTION rather than because some earlier teardown behaved.
    gPlayState = NULL;
    gGameState = NULL;
    // graph.c's runFrameContext -- the frame loop's RESUME POINT plus its gfx context. Left alone,
    // run 2's first RunFrame sees state==1, jumps to nextFrame, and evaluates GameState_IsRunning
    // on the previous run's freed gamestate.
    // FIRST of all the resets. This frees the PREVIOUS run's OTRGlobals, which owns the only strong
    // references to the save-state manager, the randomizer and the rando context -- so every reset
    // below it sees state that has actually been released rather than state still held by a leak.
    // Ordering it after them would leave each one reporting "still live" about an object whose owner
    // is about to be freed two lines later, which is a reading that cannot be acted on.
    Zelda3D_FreePreviousOTRGlobals();

    Graph_ResetRunState();
    // The N64 audio engine's state, including pointers into an audio heap the last run gave back.
    Zelda3D_ResetAudioContext();
    // The save + session state: wrong-state, not dangling-pointer, and silent either way.
    Zelda3D_ResetSaveContext();
    // The REPL FIFO: an open descriptor onto a path this run must create for itself.
    Zelda3D_MmReplResetRunState();
    // The zelda3d layer's per-run caches: anim state keyed by ZeldaArena addresses (which the next
    // run reuses) and a parked Actor*.
    Zelda3D_MM_ModelResetRunState();
    // Object extensions, keyed by actor pointer. zelda3d_shared is a static library linked into
    // BOTH cores, so this instance is this core's own.
    ObjectExtension_ResetRunState();

    // The cutscene manager keeps a pointer into the scene segment and one into the actor heap, both
    // of which the previous run freed.
    CutsceneManager_ResetRunState();
}

// Called after the frame loop has finished and before the heaps are freed.
//
// The reset above already makes the NEXT run safe, so this exists for a different reason: to say
// whether the teardown ACTUALLY RAN. Without it a regression that stops destroying the gamestate is
// completely silent -- CoreRunBegin would paper over it every time and the first symptom would be a
// save that never flushed. So it reports rather than repairs. On MM that is not hypothetical: the
// Graph_ThreadEntry condition above meant this check would have failed on every single quit.
//
// Returns the number of pointers found still set.
int Zelda3D_CoreRunEnd(void) {
    struct {
        const char* name;
        void* value;
        const char* whoShouldHaveCleared;
    } checks[] = {
        { "gPlayState", (void*)gPlayState, "Play_Destroy, via RunFrame's GameState_Destroy" },
        { "gGameState", (void*)gGameState, "GameState_Destroy, after RunFrame frees the gamestate" },
    };
    const int total = (int)(sizeof checks / sizeof checks[0]);
    int leaked = 0;

    for (int i = 0; i < total; i++) {
        if (checks[i].value == NULL) {
            continue;
        }
        leaked++;
        fprintf(stderr,
                "MM3D CORE: %s is STILL SET (%p) after run() finished.\n"
                "  Should have been cleared by: %s -- so that teardown did NOT run.\n"
                "  The next run is safe (Zelda3D_CoreRunBegin resets it), but whatever that teardown\n"
                "  also does -- saving, actor destroy callbacks -- did not happen either.\n",
                checks[i].name, checks[i].value, checks[i].whoShouldHaveCleared);
    }

    // Printed pass or fail, with the denominator: "no leaks" on its own is indistinguishable from a
    // check that looked at nothing, and this list is exactly the kind a later change outgrows.
    fprintf(stderr, "MM3D CORE: run ended; checked %d run-scoped pointer(s), %d still set.\n", total, leaked);
    fflush(stderr);
    return leaked;
}
