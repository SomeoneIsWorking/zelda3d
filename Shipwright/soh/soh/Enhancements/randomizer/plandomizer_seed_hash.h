#pragma once

#include <nlohmann/json.hpp>

/// The spoiler log's `file_hash` column: the seed-icon ids Plandomizer reads out of a
/// player-supplied spoiler JSON, lets the player step in the UI, and writes back.
///
/// The whole producer side of that column lives here rather than at its three call sites, because the
/// bound on those ids does: a `file_hash` entry is a raw JSON int32 with no upper limit, and the
/// consumer indexes gSeedTextures with it. See docs/issues/0023.

/// Discards the column, so a fresh load starts from nothing.
void PlandomizerClearSeedHash();

/// Replaces the column with the ids held in @p fileHash, the spoiler log's `file_hash` value.
///
/// Called from inside the caller's parse scope: PlandomizerLoadSpoilerLog calls this from within a
/// `try` whose `catch` takes nlohmann::json::parse_error only, which is why a non-numeric entry is
/// refused here rather than converted.
void PlandomizerLoadSeedHash(const nlohmann::json& fileHash);

/// Writes the column into @p spoilerSave under `file_hash`.
void PlandomizerWriteSeedHash(nlohmann::json& spoilerSave);

/// Draws the "Current Seed Hash" section and the editable HashIcons row it holds.
///
/// @param hasLoadedSpoilerLog Whether a spoiler log is loaded. When it is not, the section shows a
/// placeholder in place of the row; the caller owns that state, not this column.
void PlandomizerDrawSeedHash(bool hasLoadedSpoilerLog);
