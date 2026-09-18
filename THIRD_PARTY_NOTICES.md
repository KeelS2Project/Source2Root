# Dependency notices

Original Source2Root sources are Apache-2.0 (see LICENSE). Dependencies are
acquired at revisions in `dependencies.lock.json` and retain their own notices.

* KeelS2 SDK/runtime: Apache-2.0, including its shipped third-party notices.
* SourcePawn 1.13: compiler files include the CompuPhase zlib-style permission
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

* SQLite 3.53.4: public domain. The pinned amalgamation retains its original
  notices. See https://www.sqlite.org/copyright.html.
* MariaDB Connector/C 3.4.9: LGPL-2.1-or-later, with additional per-file notices
  for its bundled components. It is built as a separate shared library. Preserve
  COPYING.LIB, source notices and corresponding buildable source when distributing
  it. TLS uses the platform TLS library (OpenSSL on Linux, Schannel on Windows).
* libmaxminddb: Apache-2.0. Only the reader library is included; production
  GeoIP databases are separate inputs with their own terms.
* PCRE2 retains its BSD license and the notices for bundled components,
  including SLJIT. Preserve its complete LICENCE.md and source notices.
* curl retains its upstream copyright and permission notice in COPYING.
* PostgreSQL's libpq retains the PostgreSQL license in COPYRIGHT and its
  per-file notices. The pinned Meson build tool retains its Apache-2.0 license.
  Our private libpq copy adds the marked Apache-2.0 TLS BIO-method cleanup in
  `cmake/libpq_tls_cleanup.c` for extension unload after all connections close.

Optional extension distributions include the actual dependency source inputs,
their per-file hashes, a CMake source-selection file and copied upstream license
material. Platform libraries such as OpenSSL and zlib remain system dependencies
unless a platform package explicitly includes them and their notices.
