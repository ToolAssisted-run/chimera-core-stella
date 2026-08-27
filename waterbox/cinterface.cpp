/* cinterface.cpp - Stella behind the chimera guest ABI.
 *
 * Descended from the author's BizHawk Stella port (waterbox/stella's
 * BizhawkInterface.cxx and the Stella.* C# core), but built on a different
 * foundation: upstream Stella now carries its own libretro front end, and
 * StellaLIBRETRO is already the headless facade that port needed - it creates
 * the console from a rom in memory, runs a frame, hands back video, audio and
 * savestates, and takes input as Event types. So this file drives THAT rather
 * than reaching into OSystem, and the patch set upstream needs is one hook.
 *
 * What is the author's own work, carried across: the console-switch handling
 * (Reset/Select/difficulty/colour as inputs rather than menu state), the
 * memory domain choice, and the blip resampling of Stella's ~31.4kHz TIA audio
 * to the 44100 the frontend expects - the same rates and the same resampler
 * the C# core used.
 *
 * This file compiles IDENTICALLY for the guest (miniBox emulibc) and for the
 * native reference build (native-shim/emulibc.h), which is what makes the
 * equivalence gate a real proof.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "libretro.h"
#include "StellaLIBRETRO.hxx"
#include "SettingsLIBRETRO.hxx"
#include "Event.hxx"
#include "Console.hxx"
#include "M6532.hxx"
#include "Settings.hxx"
#include "OSystem.hxx"
#include "FSNode.hxx"
#include "Cart.hxx"
#include "System.hxx"

extern "C" {
#include "blip_buf.h"
}

static char g_loadError[512];
static int g_inited;
static int g_inputRead;

static StellaLIBRETRO g_stella;

/* ---- the wire: waterbox.config "input.buttons" order ----------------------
 * 0  Power (hard reset)
 * 1  Reset      (the console switch)
 * 2  Select     (the console switch)
 * 3  Toggle Left Difficulty   (A when held, B when not)
 * 4  Toggle Right Difficulty
 * 5  Toggle TV Type           (colour when held, B/W when not)
 * then P1 {Up,Down,Left,Right,Button}, then P2 {Up,Down,Left,Right,Button}.
 *
 * The difficulty and TV switches are LEVELS on a real 2600, not presses: the
 * frontend holds them, and the machine reads the level every frame. */
#define BTN_POWER 0
#define BTN_RESET 1
#define BTN_SELECT 2
#define BTN_LEFT_DIFF 3
#define BTN_RIGHT_DIFF 4
#define BTN_TV_TYPE 5
#define BTN_PORT1 6
#define BTN_PER_PORT 5
#define BTN_PORT2 (BTN_PORT1 + BTN_PER_PORT)
#define BTN_COUNT (BTN_PORT2 + BTN_PER_PORT)
/* Two channels reach a core, and this one is small enough to see both: a
 * controller of 64 buttons or fewer arrives from the frontend as a PACKED
 * mask in FrameAdvance, while the gate harness (and any wide controller)
 * drives SetButton. Each channel keeps its own state and a frame is their
 * union, so neither can leave a button stuck down for the other. */
static uint8_t g_buttons[BTN_COUNT];   /* what the machine sees this frame */
static uint8_t g_setButtons[BTN_COUNT]; /* the SetButton channel's state */

/* the driving controller is a dial: the two directions are its rotation */
#define PORT_UNPLUGGED 0
#define PORT_JOYSTICK 1
#define PORT_DRIVING 2
static int g_portType[2] = { PORT_JOYSTICK, PORT_JOYSTICK };

/* ---- video ---------------------------------------------------------------
 * StellaLIBRETRO hands back XRGB8888 rows at a fixed pitch; chimera wants a
 * packed BGRA frame of the LIVE size, which on a 2600 changes with the format
 * the cartridge drives (192 lines NTSC, 250-ish PAL). */
#define MAX_WIDTH 160
#define MAX_HEIGHT 312
static uint32_t g_videoOut[MAX_WIDTH * MAX_HEIGHT];
static int g_vwidth = MAX_WIDTH, g_vheight = 192;

/* ---- audio ---------------------------------------------------------------
 * The TIA runs at 262*76*60/38 = 31440Hz on an NTSC machine and 312*76*50/38
 * on a PAL one; the frontend wants 44100. blip_buf does that conversion the
 * way the author's C# core did it, with the same rates and the same library. */
#define OUT_RATE 44100
#define MAX_SAMPLES 4096
/* this blip_buf carries both channels in one buffer (deltas take a left and a
 * right, reads come out interleaved), so there is one of it rather than two */
static blip_t *g_blip;
static int g_latchL, g_latchR;
static int16_t g_soundOut[MAX_SAMPLES * 2];
static int g_nsamples;

static int g_vsyncNum = 60, g_vsyncDen = 1;

/* ---------------------------------------------------------------------------
 * Lag detection: the ONE thing upstream does not offer. patches/ adds a call
 * to this from M6532::peek for SWCHA and SWCHB - the two addresses a game
 * reads its controllers and console switches through - which is exactly the
 * "did this frame look at the input" question chimera's InputWasRead answers.
 */
extern "C" void chimera_input_was_read(void) { g_inputRead = 1; }

/* ---------------------------------------------------------------------------
 * THE MACHINE IS A FUNCTION OF THE PROJECT, not of the moment.
 *
 * Stella powers a console on the way a real one comes up: RAM full of whatever
 * was there, the RIOT timer at some point in its cycle, from a generator that
 * upstream seeds off the wall clock. That is faithful to hardware and the
 * opposite of what a movie needs - the same cartridge would start from a
 * different machine every time, and a run recorded on one would not replay on
 * another. patches/0002 lets a host pin that seed; this is the host doing so,
 * from a setting, so the number a project ran with is recorded in it.
 *
 * The sandbox hid the problem: its clock is frozen, so the guest was stable
 * and only the native reference wandered. The equivalence gate found it.
 */
static uint32_t g_randomSeed;

extern "C" int chimera_pinned_random_seed(uint32_t *seed)
{
	if (seed) *seed = g_randomSeed;
	return 1;
}

/* ...and not of the wall clock either. Upstream arms a 500 MILLISECOND
 * timeout, on a background thread, that clears every event once emulation
 * starts: interactively that drops the keys a user was still holding while the
 * rom booted. Frame-driven there is nothing to drop - each frame's input
 * arrives with the frame - and the timeout instead lands on whatever frame the
 * run happened to reach when half a real second had passed, wiping the console
 * switches between a poll and the program's read of them.
 *
 * That is exactly what the gate caught: the same movie, replayed natively,
 * read Select as pressed in one run and released in the next, at the identical
 * cycle. The sandbox could not see it - no threads, no clock - so it was the
 * harness's native reference that proved the machine was listening to the
 * moment. patches/0003 asks this before arming the timeout.
 */
extern "C" int chimera_frame_driven(void) { return 1; }

/* ---------------------------------------------------------------------------
 * What libretro.cxx would have provided. Upstream's headless backend expects a
 * few things of its host: somewhere to log, somewhere to show a message, a
 * file interface, and the rom's path. In a sandbox with no user watching,
 * messages are diagnostics on stderr; the VFS is unused, because the cartridge
 * arrives as bytes read here rather than as a path Stella opens for itself.
 */
void libretro_logger(int level, const char *message)
{
	if (level >= RETRO_LOG_WARN) fprintf(stderr, "[stella] %s", message ? message : "");
}

void libretro_show_message(const char *message)
{
	fprintf(stderr, "[stella] %s\n", message ? message : "");
}

void post_message(const char *message, retro_log_level level, unsigned)
{
	libretro_logger(static_cast<int>(level), message);
}

/* ---------------------------------------------------------------------------
 * A file system for Stella, over the sandbox's own.
 *
 * Upstream's file layer asks its host to stat and read files - that is how it
 * decides a rom node IS a rom before looking at the bytes. In the sandbox
 * there really are files (the frontend mounts the project's), so rather than
 * patching that check away this hands Stella the small part of the interface
 * it uses, backed by plain stdio. Everything it does not use is null, and
 * anything Stella writes goes nowhere: a core package is read-only, and what
 * a machine keeps is savestates and save data, not files on the side.
 */
struct VfsFile { FILE *f; std::string path; };

static const char *VfsGetPath(retro_vfs_file_handle *h)
{
	return h ? reinterpret_cast<VfsFile *>(h)->path.c_str() : nullptr;
}

static retro_vfs_file_handle *VfsOpen(const char *path, unsigned mode, unsigned)
{
	if (!path || (mode & RETRO_VFS_FILE_ACCESS_WRITE)) return nullptr;
	FILE *f = fopen(path, "rb");
	if (!f) return nullptr;
	VfsFile *file = new VfsFile{ f, path };
	return reinterpret_cast<retro_vfs_file_handle *>(file);
}

static int VfsClose(retro_vfs_file_handle *h)
{
	if (!h) return -1;
	VfsFile *file = reinterpret_cast<VfsFile *>(h);
	fclose(file->f);
	delete file;
	return 0;
}

static int64_t VfsSize(retro_vfs_file_handle *h)
{
	if (!h) return -1;
	FILE *f = reinterpret_cast<VfsFile *>(h)->f;
	const long here = ftell(f);
	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, here, SEEK_SET);
	return size;
}

static int64_t VfsTell(retro_vfs_file_handle *h)
{
	return h ? ftell(reinterpret_cast<VfsFile *>(h)->f) : -1;
}

static int64_t VfsSeek(retro_vfs_file_handle *h, int64_t offset, int whence)
{
	if (!h) return -1;
	const int w = whence == RETRO_VFS_SEEK_POSITION_END ? SEEK_END
		: whence == RETRO_VFS_SEEK_POSITION_CURRENT ? SEEK_CUR : SEEK_SET;
	if (fseek(reinterpret_cast<VfsFile *>(h)->f, static_cast<long>(offset), w) != 0) return -1;
	return ftell(reinterpret_cast<VfsFile *>(h)->f);
}

static int64_t VfsRead(retro_vfs_file_handle *h, void *buffer, uint64_t length)
{
	if (!h || !buffer) return -1;
	return static_cast<int64_t>(fread(buffer, 1, static_cast<size_t>(length),
		reinterpret_cast<VfsFile *>(h)->f));
}

static int64_t VfsWrite(retro_vfs_file_handle *, const void *, uint64_t) { return -1; }
static int VfsFlush(retro_vfs_file_handle *) { return 0; }
static int VfsRemove(const char *) { return -1; }
static int VfsRename(const char *, const char *) { return -1; }

static int VfsStat(const char *path, int32_t *size)
{
	if (!path) return 0;
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	fseek(f, 0, SEEK_END);
	const long bytes = ftell(f);
	fclose(f);
	if (size) *size = static_cast<int32_t>(bytes);
	return RETRO_VFS_STAT_IS_VALID;
}

static int VfsMkdir(const char *) { return -1; }
static retro_vfs_dir_handle *VfsOpendir(const char *, bool) { return nullptr; }
static bool VfsReaddir(retro_vfs_dir_handle *) { return false; }
static const char *VfsDirentGetName(retro_vfs_dir_handle *) { return nullptr; }
static bool VfsDirentIsDir(retro_vfs_dir_handle *) { return false; }
static int VfsClosedir(retro_vfs_dir_handle *) { return -1; }

static retro_vfs_interface g_vfs = {
	VfsGetPath, VfsOpen, VfsClose, VfsSize, VfsTell, VfsSeek, VfsRead, VfsWrite,
	VfsFlush, VfsRemove, VfsRename,
	/* v2 */ nullptr,
	/* v3 */ VfsStat, VfsMkdir, VfsOpendir, VfsReaddir, VfsDirentGetName,
	VfsDirentIsDir, VfsClosedir,
};

retro_vfs_interface *libretro_vfs = &g_vfs;
std::string libretro_rom_path;
std::string libretro_save_dir;

/* The cartridge, for the one path that asks for it by file rather than being
 * handed the bytes (FSNodeLIBRETRO's read of the rom node). It is the same
 * buffer Init loaded, so there is one copy of the cartridge in the sandbox. */
static uint8_t *g_rom;
static uint32_t g_romSize;

uInt32 libretro_get_rom_size() { return g_romSize; }

uInt32 libretro_read_rom(void *data)
{
	if (data && g_rom) memcpy(data, g_rom, g_romSize);
	return g_romSize;
}

/* ---------------------------------------------------------------------------
 * Input. Upstream's EventHandlerLIBRETRO::pollEvent() calls update_input()
 * once per frame, inside the window where the controllers replay it; the
 * libretro front end defines that function, and since we replace libretro.cxx
 * we define it ourselves. Everything reaches Stella as an Event, which is how
 * its controllers are written.
 */
static void SendPort(int port, const uint8_t *b)
{
	const bool left = port == 0;
	switch (g_portType[port])
	{
		case PORT_UNPLUGGED:
			break;
		case PORT_DRIVING:
			g_stella.setInputEvent(left ? Event::LeftDrivingCCW : Event::RightDrivingCCW, b[2]);
			g_stella.setInputEvent(left ? Event::LeftDrivingCW : Event::RightDrivingCW, b[3]);
			g_stella.setInputEvent(left ? Event::LeftDrivingFire : Event::RightDrivingFire, b[4]);
			break;
		default:
			g_stella.setInputEvent(left ? Event::LeftJoystickUp : Event::RightJoystickUp, b[0]);
			g_stella.setInputEvent(left ? Event::LeftJoystickDown : Event::RightJoystickDown, b[1]);
			g_stella.setInputEvent(left ? Event::LeftJoystickLeft : Event::RightJoystickLeft, b[2]);
			g_stella.setInputEvent(left ? Event::LeftJoystickRight : Event::RightJoystickRight, b[3]);
			g_stella.setInputEvent(left ? Event::LeftJoystickFire : Event::RightJoystickFire, b[4]);
			break;
	}
}

void update_input()
{
	if (!g_inited) return;

	/* the console switches. Difficulty and TV type are levels: held means A /
	 * colour, released means B / black-and-white, and Stella takes each as its
	 * own event rather than as a toggle. */
	g_stella.setInputEvent(Event::ConsoleReset, g_buttons[BTN_RESET]);
	g_stella.setInputEvent(Event::ConsoleSelect, g_buttons[BTN_SELECT]);
	g_stella.setInputEvent(Event::ConsoleLeftDiffA, g_buttons[BTN_LEFT_DIFF]);
	g_stella.setInputEvent(Event::ConsoleLeftDiffB, !g_buttons[BTN_LEFT_DIFF]);
	g_stella.setInputEvent(Event::ConsoleRightDiffA, g_buttons[BTN_RIGHT_DIFF]);
	g_stella.setInputEvent(Event::ConsoleRightDiffB, !g_buttons[BTN_RIGHT_DIFF]);
	g_stella.setInputEvent(Event::ConsoleColor, g_buttons[BTN_TV_TYPE]);
	g_stella.setInputEvent(Event::ConsoleBlackWhite, !g_buttons[BTN_TV_TYPE]);

	SendPort(0, g_buttons + BTN_PORT1);
	SendPort(1, g_buttons + BTN_PORT2);
}

/* ---------------------------------------------------------------------------
 * Settings, from the mounted JSON.
 */
static int PortTypeFromSetting(const char *name)
{
	char value[32];
	strncpy(value, "joystick", sizeof(value) - 1);
	value[sizeof(value) - 1] = '\0';
	wbx_setting_str(name, value, sizeof(value));
	if (!strcmp(value, "none")) return PORT_UNPLUGGED;
	if (!strcmp(value, "driving")) return PORT_DRIVING;
	return PORT_JOYSTICK;
}

/* the chimera guest ABI is a C ABI: the adapter looks these up by name */
extern "C" {

ECL_EXPORT const char *GetLoadError(void) { return g_loadError; }

ECL_EXPORT int Init(void)
{
	g_loadError[0] = '\0';

	/* the cartridge: the project slot's file, else the plain rom mount */
	char name[512];
	const char *file = "rom";
	if (wbx_slot_count("cart") > 0 && wbx_slot_name("cart", 0, name, sizeof(name)) != nullptr) file = name;

	FILE *f = fopen(file, "rb");
	if (!f)
	{
		snprintf(g_loadError, sizeof(g_loadError), "no cartridge is mounted");
		return 0;
	}
	fseek(f, 0, SEEK_END);
	const long romSize = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (romSize <= 0 || static_cast<size_t>(romSize) > Cartridge::maxSize())
	{
		fclose(f);
		snprintf(g_loadError, sizeof(g_loadError),
			"'%s' is %ld bytes, which is not a 2600 cartridge", file, romSize);
		return 0;
	}
	static uint8_t *romBuffer;
	romBuffer = new uint8_t[static_cast<size_t>(romSize)];
	g_rom = romBuffer;
	g_romSize = static_cast<uint32_t>(romSize);
	if (fread(romBuffer, 1, static_cast<size_t>(romSize), f) != static_cast<size_t>(romSize))
	{
		fclose(f);
		snprintf(g_loadError, sizeof(g_loadError), "could not read '%s'", file);
		return 0;
	}
	fclose(f);

	g_randomSeed = static_cast<uint32_t>(wbx_setting_long("randomSeed", 0));

	SettingsLIBRETRO cfg;
	char format[16];
	strncpy(format, "AUTO", sizeof(format) - 1);
	format[sizeof(format) - 1] = '\0';
	wbx_setting_str("format", format, sizeof(format));
	cfg.console_format = format;
	cfg.video_phosphor = "never"; /* a display choice, and a nondeterministic one */

	g_portType[0] = PortTypeFromSetting("port1");
	g_portType[1] = PortTypeFromSetting("port2");

	/* Stella asks its file layer whether the rom node IS a file before it will
	 * look at the bytes, and with no VFS the libretro backend answers that by
	 * comparing the path against libretro_rom_path - so the path we give it and
	 * the one we claim have to be the same one. */
	libretro_rom_path = "cartridge.a26";
	g_stella.setROM(libretro_rom_path.c_str(), romBuffer, static_cast<size_t>(romSize));
	if (!g_stella.create(cfg, false))
	{
		snprintf(g_loadError, sizeof(g_loadError),
			"Stella could not make a console from this cartridge");
		return 0;
	}

	/* the machine's own rate, which is the cartridge's doing rather than a
	 * choice: 60Hz for an NTSC program, 50 for a PAL one */
	g_vsyncNum = static_cast<int>(g_stella.getVideoRate());
	g_vsyncDen = 1;

	g_blip = blip_new(MAX_SAMPLES);
	blip_set_rates(g_blip, g_stella.getAudioRate(), OUT_RATE);
	g_latchL = g_latchR = 0;

	g_inited = 1;
	return 1;
}

ECL_EXPORT void SetButton(int32_t index, int32_t state)
{
	if (index >= 0 && index < BTN_COUNT) g_setButtons[index] = state ? 1 : 0;
}

/* A 2600 controller is digital: the joystick is four switches and a button,
 * and the driving controller reports rotation rather than a position. Nothing
 * here reads an axis, but the harness that proves guest and native agree
 * exercises the whole ABI, so the export exists and does nothing. */
ECL_EXPORT void SetAxis(int32_t, int32_t) { }

static void DrainVideo(void)
{
	const uint32_t width = g_stella.getVideoWidth();
	const uint32_t height = g_stella.getVideoHeight();
	g_vwidth = static_cast<int>(width > MAX_WIDTH ? MAX_WIDTH : width);
	g_vheight = static_cast<int>(height > MAX_HEIGHT ? MAX_HEIGHT : height);

	const uint8_t *src = static_cast<const uint8_t *>(g_stella.getVideoBuffer());
	if (!src) return;
	const uint32_t pitch = StellaLIBRETRO::getVideoPitch();
	for (int y = 0; y < g_vheight; y++)
	{
		memcpy(g_videoOut + y * g_vwidth, src + static_cast<size_t>(y) * pitch,
			static_cast<size_t>(g_vwidth) * 4);
	}
}

static void DrainAudio(void)
{
	g_nsamples = 0;
	const uint32_t count = g_stella.getAudioSize();
	if (!count) return;

	const int16_t *samples = g_stella.getAudioBuffer();
	for (uint32_t i = 0; i < count; i++)
	{
		const int l = samples[2 * i + 0];
		const int r = samples[2 * i + 1];
		if (l != g_latchL || r != g_latchR)
		{
			blip_add_delta(g_blip, i, g_latchL - l, g_latchR - r);
			g_latchL = l;
			g_latchR = r;
		}
	}
	blip_end_frame(g_blip, count);

	const int avail = blip_samples_avail(g_blip);
	g_nsamples = avail > MAX_SAMPLES ? MAX_SAMPLES : avail;
	blip_read_samples(g_blip, g_soundOut, g_nsamples);
}

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	for (int i = 0; i < BTN_COUNT; i++)
	{
		g_buttons[i] = g_setButtons[i] | (i < 64 ? static_cast<uint8_t>((packed >> i) & 1) : 0);
	}
	g_inputRead = 0;

	if (g_buttons[BTN_POWER]) g_stella.reset();

	/* the input window first: this is what makes upstream call update_input()
	 * (through its event handler), and it has to happen inside the window so
	 * the controllers replay the frame's input rather than the last one's */
	g_stella.pollInput();
	g_stella.runFrame();

	DrainVideo();
	DrainAudio();

}

ECL_EXPORT uint32_t *GetVideoBgra(void) { return g_videoOut; }
ECL_EXPORT int GetVideoWidth(void) { return g_vwidth; }
ECL_EXPORT int GetVideoHeight(void) { return g_vheight; }

ECL_EXPORT int16_t *GetAudio(void) { return g_soundOut; }
ECL_EXPORT int GetAudioSampleCount(void) { return g_nsamples; }

ECL_EXPORT int GetVsyncNumerator(void) { return g_vsyncNum; }
ECL_EXPORT int GetVsyncDenominator(void) { return g_vsyncDen; }

ECL_EXPORT int InputWasRead(void) { return g_inputRead; }

/* ---------------------------------------------------------------------------
 * Memory domains. A 2600 has 128 bytes of RAM and that is the machine's whole
 * working memory - it is what a TAS watches. Stella exposes the TIA, the PIA
 * and the system bus through peek/poke functions rather than as buffers, so
 * they cannot be handed over as pointers; a cartridge's own RAM likewise.
 */
ECL_EXPORT int GetMemoryDomainCount(void) { return g_inited ? 1 : 0; }

ECL_EXPORT const char *GetMemoryDomainName(int i)
{
	return i == 0 ? "Main RAM" : nullptr;
}

ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i)
{
	return i == 0 && g_inited ? g_stella.getRAM() : nullptr;
}

ECL_EXPORT int64_t GetMemoryDomainSize(int i)
{
	return i == 0 ? StellaLIBRETRO::getRAMSize() : 0;
}

ECL_EXPORT int GetMemoryDomainWritable(int i) { return i == 0 ? 1 : 0; }

/* ---------------------------------------------------------------------------
 * Save data: a 2600 cartridge has none. The handful that do (AtariVox and
 * SaveKey write to an EEPROM on the controller port) are not wired here yet,
 * which is also where BizHawk's port left it - it declares ISaveRam not
 * applicable. The exports exist because the group is all-or-nothing.
 */
ECL_EXPORT int32_t GetSaveDataFileCount(void) { return 0; }
ECL_EXPORT const char *GetSaveDataFileName(int32_t) { return nullptr; }
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t) { return 0; }
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t) { return nullptr; }

} /* extern "C" */
