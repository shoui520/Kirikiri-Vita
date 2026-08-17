#pragma once

// Vita newlib does not expose ioctl(2). SQLite includes this header on every
// Unix build, but all ioctl call sites are excluded when batch-atomic writes
// and platform locking styles are disabled (the krkrvita configuration).
