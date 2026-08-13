#pragma once

// Called by the wholesale engine immediately before startup.tjs.
void krkrvita_execute_patch_script();

// Replaces the Vita process arguments from ux0:data/krkrvita/active.ini when
// the shell did not already pass an explicit project path.
void krkrvita_resolve_launch(int &argc, char **&argv);

// Writes ux0:data/krkrvita/error.txt and posts a Vita notification.
void krkrvita_report_launch_error(const char *message);
