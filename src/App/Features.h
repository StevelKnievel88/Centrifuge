#pragma once

namespace orbit::Features
{
    /** The rekordbox copy: importing rekordbox collections into Centrifuge's library as a second version of each
        track (REKORDBOX / CENTRIFUGE views, R / C edit buttons, import conflicts). Switched off for now - set to
        true to bring it all back. While it's off there is one library: edits are saved once, in Centrifuge's
        version, and your rekordbox collection and USB sticks are still there to play from. */
    constexpr bool rekordboxCopy = false;

    /** Your rekordbox PC collection (master.db) as a browser source. Hidden for now: the browser shows USB sticks
        and the music added to Centrifuge. Set to true to bring it back. */
    constexpr bool rekordboxCollection = false;
}
