# SQLite amalgamation

This directory contains `sqlite3.c` and `sqlite3.h` from the official SQLite
3.53.4 amalgamation (`sqlite-amalgamation-3530400.zip`). SQLite is dedicated to
the public domain.

- Source: https://sqlite.org/2026/sqlite-amalgamation-3530400.zip
- SHA3-256: `628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e`

Only the engine and public header are vendored. The command-line shell and
extension header are not part of the Vita build.
