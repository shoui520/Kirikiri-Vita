#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <stddef.h>

static const char k_boot_trace_path[] = "ux0:data/krkrvita/boot-status.txt";
static const char k_boot_trace_fallback_path[] = "ux0:data/krkrvita-boot-status.txt";
static const char k_error_path[] = "ux0:data/krkrvita/error.txt";
static const char k_error_fallback_path[] = "ux0:data/krkrvita-error.txt";

static size_t text_length(const char *text)
{
	size_t length = 0;
	if(!text) return 0;
	while(text[length] != '\0') ++length;
	return length;
}

static void write_text(const char *path, const char *fallback_path,
	const char *text, int flags)
{
	SceUID file;
	const size_t length = text_length(text);

	sceIoMkdir("ux0:data/krkrvita", 0777);
	file = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | flags, 0666);
	if(file < 0)
		file = sceIoOpen(fallback_path, SCE_O_WRONLY | SCE_O_CREAT | flags, 0666);
	if(file < 0) return;
	if(length > 0) sceIoWrite(file, text, length);
	sceIoWrite(file, "\n", 1);
	sceIoSyncByFd(file, 0);
	sceIoClose(file);
}

void krkrvita_boot_trace(const char *stage)
{
	write_text(k_boot_trace_path, k_boot_trace_fallback_path, stage,
		SCE_O_APPEND);
}

void krkrvita_write_error(const char *message)
{
	write_text(k_error_path, k_error_fallback_path,
		message ? message : "Unknown startup error", SCE_O_TRUNC);
}

static void krkrvita_preinit_trace(void)
{
	write_text(k_boot_trace_path, k_boot_trace_fallback_path,
		"preinit-entered", SCE_O_TRUNC);
}

/*
 * Vita newlib runs .preinit_array before every C++ global constructor.  This
 * gives us a durable boundary between a loader/import failure and a crash in
 * static initialization, without relying on SDL, libc stdio, or the engine.
 */
__attribute__((section(".preinit_array"), used))
static void (*const krkrvita_preinit_entry)(void) = krkrvita_preinit_trace;
