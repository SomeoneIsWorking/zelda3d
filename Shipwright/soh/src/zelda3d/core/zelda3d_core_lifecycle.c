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
// TWO MECHANISMS, AND WHICH ONE YOU WANT. A central reset list is right for STATE, because the reset
// is also where you say what has to be freed first: gPlayState, gAudioContext and the message tables
// are all here for that reason. It is the wrong shape for a LATCH -- a `static bool initialized`
// guarding setup that belongs to a run -- because the flag would sit in one file and its reset in
// another, and someone editing the first has no reason to look at the second. That failure already
// happened: the skybox latch sat three lines below runFrameContext when runFrameContext was hoisted
// into Graph_ResetRunState, and was missed. So latches carry their own run stamp instead --
// `static Zelda3DOnce x; if (Zelda3D_Once(&x))` -- and appear on no list at all.
//
// WHY THE SYMBOL NAMES STAY. `gPlayState` and `gGameState` are externed by name from dozens of
// decomp and enhancement files. Renaming them into `gCore.play` would touch every one of those and
// buy nothing this does not already give: the ownership, not the spelling, was the defect. They are
// defined here, reset here, and audited here; that they remain C globals is a linkage detail.

#include "global.h"
#include "z64.h"
#include "variables.h"
#include "zelda3d/zelda3d.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void Graph_ResetRunState(void);        // graph.c
void Zelda3D_AudioResetRunState(void);   // OTRGlobals.cpp -- the audio THREAD's control block
void Zelda3D_MessageResetRunState(void); // z_message_OTR.cpp -- the message tables
void Zelda3D_ReplResetRunState(void);    // zelda3d_repl.cpp -- the REPL FIFO descriptor
void Zelda3D_AnimResetRunState(void);    // zelda3d_anim.cpp -- last frame's pose, NOT the asset caches
void Zelda3D_DisableFixedCameraResetRunState(void); // DisableFixedCamera.cpp -- scene camera backups
void Zelda3D_ActorRefsResetRunState(void);   // zelda3d.c        -- REPL/debug actor selections
void Zelda3D_RenderRefsResetRunState(void); // zelda3d_render.cpp
void Zelda3D_HorseRefsResetRunState(void);  // behaviors/actor/en_horse.cpp

// The name tables AudioLoad_Init builds for this run, so they can be released before it builds the
// next run's. Declared here rather than in a header because this file is where their lifetime is.
extern char** sequenceMap;
extern size_t sequenceMapSize;
extern char** fontMap;
extern size_t fontMapSize;

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
// Once-per-run latches
// ---------------------------------------------------------------------------------------------

// Counts runs. Starts at 0, so a zero-initialised Zelda3DOnce has never fired: the first run is
// epoch 1 and every latch is stale against it by construction, which is what makes the idiom need no
// registration. See the comment on Zelda3D_Once in zelda3d.h for why latches are not on the reset
// list below.
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

// gAudioContext is the whole N64 audio engine's state: the note array, the sequence players, the
// soundfont/sample tables and the pointers into the audio heap they all index. Every one of those
// belongs to a run, and the struct is a plain global, so a second run inherits the first one's.
//
// That is what crashed the third core of an oot -> mm -> oot sequence, and the shape of it is worth
// stating because it is not the obvious one. The audio thread is started by OTRAudio_Init inside
// InitOTR, LONG before Main reaches AudioMgr_Init -- so between those two points the thread is live
// against an audio engine that has not been initialised for this run. It is held off by a priming
// gate (it waits for the gfx thread's first frame), but boot renders frames, so the gate opens
// there too. On run 1 that is harmless for one reason only: the struct is in BSS, so `numNotes` is
// 0 and Audio_ProcessNotes walks nothing. On run 3 it held run 1's note count and note pointers,
// aimed at an audio heap the MM session had since taken back -- SIGSEGV in Audio_ProcessNotes, on
// the audio thread, nowhere near the code responsible.
//
// So the fix is not to widen the gate; it is to give the run-1 condition to every run. Zeroing here
// IS what BSS did for the first one.
//
// (An earlier theory -- that the gate itself was inherited open via the thread-control flags -- was
// wrong, and the report below is what falsified it: run 3 inherited running=0 processing=0. Kept
// because a check that can only print the answer you expect is not a check.)
void Zelda3D_ResetAudioContext(void) {
    const s32 inheritedNotes = gAudioContext.numNotes;
    const void* inheritedNotePtr = (const void*)gAudioContext.notes;
    size_t freedNames = 0;

    // AudioLoad_Init strdup()s a name per sequence and per soundfont into these tables and allocates
    // the two load-status arrays. Nothing ever released them, which did not show while a process ran
    // one game; a second run simply calloc'd new tables over the top. Released here because this is
    // the last moment their pointers still exist -- the memset below is what forgets them.
    if (sequenceMap != NULL) {
        for (size_t i = 0; i < sequenceMapSize + 0xF; i++) {
            if (sequenceMap[i] != NULL) {
                free(sequenceMap[i]);
                freedNames++;
            }
        }
        free(sequenceMap);
        sequenceMap = NULL;
        sequenceMapSize = 0;
    }
    if (fontMap != NULL) {
        for (size_t i = 0; i < fontMapSize; i++) {
            if (fontMap[i] != NULL) {
                free(fontMap[i]);
                freedNames++;
            }
        }
        free(fontMap);
        fontMap = NULL;
        fontMapSize = 0;
    }
    free(gAudioContext.seqLoadStatus);
    free(gAudioContext.fontLoadStatus);

    memset(&gAudioContext, 0, sizeof(gAudioContext));

    fprintf(stderr,
            "ZELDA3D CORE: gAudioContext reset (%zu bytes) -- inherited numNotes=%d notes=%p, freed %zu"
            " sequence/soundfont name(s).\n",
            sizeof(gAudioContext), (int)inheritedNotes, inheritedNotePtr, freedNames);
    fflush(stderr);
}

// ---------------------------------------------------------------------------------------------
// The save context
// ---------------------------------------------------------------------------------------------

// gSaveContext is the whole game's save + session state: the save file itself, the current entrance,
// cutscene indices, link age, scene setup, time of day, event flags. It is a plain global, so run 2
// inherits every field run 1 left set -- and unlike most of the state on this list, that is not a
// dangling-pointer hazard but a WRONG-STATE one, which is worse to diagnose because nothing crashes.
//
// How it showed: a second OoT run in one process reached the same scene at a completely different
// position (link=(-56,1,2117) on run 1, link=(-5586,144,5273) on run 2), and ran ~30x as many player
// animation loads -- 26 resolutions against 747. Nothing reported an error; the run was simply
// somewhere else. Persisted save data is written to disk by SaveManager, so nothing legitimately
// needs to survive a run in this struct; run 1's safety comes only from BSS, exactly as with
// gAudioContext.
void Zelda3D_ResetSaveContext(void) {
    const unsigned char* bytes = (const unsigned char*)&gSaveContext;
    size_t inheritedNonZero = 0;

    for (size_t i = 0; i < sizeof(gSaveContext); i++) {
        if (bytes[i] != 0) {
            inheritedNonZero++;
        }
    }

    memset(&gSaveContext, 0, sizeof(gSaveContext));

    // Reported as a COUNT with its denominator rather than as "reset ok": on run 1 this must say 0,
    // and a line that cannot say anything else could not tell run 1 from a run that inherited a full
    // save. Naming individual fields would have been worse -- whichever field the next regression
    // uses would be the one not printed.
    fprintf(stderr, "ZELDA3D CORE: gSaveContext reset -- inherited %zu non-zero byte(s) of %zu.\n",
            inheritedNonZero, sizeof(gSaveContext));
    fflush(stderr);
}

// ---------------------------------------------------------------------------------------------
// The lifecycle
// ---------------------------------------------------------------------------------------------

// Called by the core's entry point BEFORE anything else in a run -- before InitOTR, which is where
// the stale-pointer crash happened. Everything above is reset here, unconditionally, so the run
// cannot inherit the last one regardless of how it ended (a clean quit, a crash-triggered exit, a
// game switch mid-frame).
void Zelda3D_CoreRunBegin(void) {
    // First, so that everything below runs in the new epoch: every Zelda3DOnce in the process is now
    // stale, without any of them having to be listed here.
    sRunEpoch++;

    gPlayState = NULL;
    gGameState = NULL;
    // graph.c's runFrameContext -- the frame loop's resume point and gfx context. Reset through a
    // function because it is file-static there, which is the right scope for it; what it must not
    // be is longer-lived than a run.
    Graph_ResetRunState();
    Zelda3D_AudioResetRunState();
    Zelda3D_ResetAudioContext();
    // The save + session state. Not a lifetime hazard -- a WRONG-STATE one: run 2 otherwise starts
    // inside run 1's save, silently.
    Zelda3D_ResetSaveContext();
    // The message tables: malloc'd arrays whose entries point into ResourceManager-owned text.
    Zelda3D_MessageResetRunState();
    // The REPL FIFO: an open descriptor onto a path this run has to create for itself.
    Zelda3D_ReplResetRunState();
    // Last frame's skin matrices. The asset-derived caches beside them deliberately do NOT reset --
    // see the comment on Zelda3D_AnimResetRunState for which zelda3d caches are run-scoped and why.
    Zelda3D_AnimResetRunState();
    // Camera-data backups keyed by a previous run's CollisionHeader addresses.
    Zelda3D_DisableFixedCameraResetRunState();
    // Actor pointers this layer parks in globals. They point into the play heap, so they are exactly
    // the shape this file's header argues against; most are only compared, one is dereferenced.
    Zelda3D_ActorRefsResetRunState();
    Zelda3D_RenderRefsResetRunState();
    Zelda3D_HorseRefsResetRunState();
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
