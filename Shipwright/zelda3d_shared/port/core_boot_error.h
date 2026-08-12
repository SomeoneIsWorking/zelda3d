#pragma once

// A game core must RETURN to the launcher, never exit() the process.
//
// The launcher dlopens a core, calls run(), and expects control back -- that is what makes the ESC
// menu's "Return to Launcher" row and the chooser work at all. But the boot path inherited from
// upstream SoH/2S2H calls exit() when an asset is missing or an archive is invalid, from a time when
// the game WAS the process. Under the launcher that turns "Ocarina's archive is missing" into "the
// whole application vanished", with the chooser and Majora's Mask taken down alongside it and no way
// to tell the user which game failed.
//
// The fix is a failure RETURN, and the mechanism has to respect one hard constraint: the core entry
// is C (core_entry.c / main.c) and InitOTR is `extern "C"` but implemented in C++. Unwinding an
// exception through a C frame is undefined, so the throw must be caught before it can reach one --
// i.e. inside InitOTR itself, which is the outermost C++ frame on the boot path. That is why this is
// an exception and not a longjmp: every destructor between the failure and the catch still runs, and
// the boot path is full of RAII (shared_ptr resources, file handles, the archive readers).
//
// Interactive popups are deliberately NOT converted. Those exit() calls live in lambdas the GUI
// invokes, and this build auto-answers the extraction prompts anyway, so they are unreachable here;
// converting them would mean throwing out of an ImGui callback, which is a different problem with a
// different answer.

#include <stdexcept>
#include <string>

namespace Zelda3D {

class CoreBootError : public std::runtime_error {
  public:
    explicit CoreBootError(const std::string& why) : std::runtime_error(why) {
    }
};

} // namespace Zelda3D
