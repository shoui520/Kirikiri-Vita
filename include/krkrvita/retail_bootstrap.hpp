#pragma once

// Called by the wholesale engine immediately before startup.tjs.
void krkrvita_execute_patch_script();

// Raw SceIofilemgr diagnostics. These are safe before C++ global constructors
// and do not depend on SDL, VitaGL, or libc stdio being initialized.
extern "C" void krkrvita_boot_trace(const char *stage);
extern "C" void krkrvita_write_error(const char *message);

// Replaces the Vita process arguments from ux0:data/krkrvita/active.ini when
// the shell did not already pass an explicit project path.
void krkrvita_resolve_launch(int &argc, char **&argv);

// Writes ux0:data/krkrvita/error.txt using the raw diagnostic path.
void krkrvita_report_launch_error(const char *message);
