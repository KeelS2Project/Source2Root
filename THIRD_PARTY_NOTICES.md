# Dependency notices

Original Source2Root sources are Apache-2.0 (see LICENSE). Dependencies are
acquired at revisions in `dependencies.lock.json` and retain their own notices.

* KeelS2 SDK/runtime: Apache-2.0, including its shipped third-party notices.
* SourcePawn 1.12: compiler files include the CompuPhase zlib-style permission
  notice and other per-file notices; AMTL uses BSD-3-Clause. VM and API files
  carry GPL-3.0 or GPL-3.0-or-later notices, some with the Valve linking exception.
  Preserve the exact per-file terms. The repository-level LICENSE.txt is an overview,
  not a replacement for individual source notices. Runtime distributions must
  include GPL text and reproducible corresponding sources for the combined
  program. Source2Root's original files remain available under Apache-2.0.
* AMTL, zlib and other upstream build inputs retain their source notices.
* Pinned CS2 HL2SDK and bundled protobuf retain their upstream notices.
* nlohmann/json 3.11.3 is MIT licensed. Its exact upstream header and source
  archive must accompany the developer/source packages with the MIT notice.
* SwiftlyS2 is inspected as a primary reference, not imported. Its center HTML
  event and keyboard-state approach informs the backend; no source is copied.
