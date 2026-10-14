// cinterface.cpp - the functions Chimera's engine calls, over melonDS's
// machine (extern/melonDS/src/NDS.h). The same file is compiled for the
// sandbox (core.wbx) and for the host (run-native), which is the reference
// the tests compare the sandbox with.
//
// The machine is built in Init from the project's files and settings, and
// nothing about it is read from the host afterwards: its clock starts at a
// time the project sets, its saves stay in its memory, and it runs on one
// thread.
// SPDX-License-Identifier: MIT
#include "NDS.h"
#include "DSi.h"
#include "DSi_NAND.h"
#include "Platform.h"
#include "NDSCart.h"
#include "GPU.h"
#include "GPU3D_Soft.h"
#include "SPU.h"
#include "RTC.h"
#include "Args.h"
#include "SPI_Firmware.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "frontend.h"

using namespace melonDS;

namespace
{

char g_loadError[1024];
NDS *g_nds;
bool g_stopped;
bool g_dsi;                       // the machine is a DSi
// The DSi's NAND, the chip its system and its settings are on. The machine
// reads and writes it while it runs, so it is kept whole in the machine's
// memory (platform.cpp reads and writes it there) and a savestate holds it.
std::vector<uint8_t> g_nand;
const char *const kNandName = "dsi_nand.bin";

// ---- the picture -----------------------------------------------------------------
// The machine has two screens of 256x192. They are put into one picture the
// way the project's settings say: one above the other as the console has
// them (the default), side by side, or one alone; with a gap between them
// like the console's hinge; exchanged; and turned, for the games that are
// held like a book. None of it reaches the machine.
constexpr int kScreenW = 256, kScreenH = 192;
enum { kStacked, kSideBySide, kTopOnly, kBottomOnly };
int g_layout = kStacked;
int g_gap;                        // pixels between the two screens
bool g_swap;                      // the lower screen first
int g_turn;                       // quarter turns clockwise
int g_canvasW, g_canvasH;         // the picture before it is turned
int g_placeX[2], g_placeY[2];     // where each screen goes in it (-1: not shown)
int g_width, g_height;            // the picture handed out
std::vector<uint32_t> g_canvas, g_picture;
bool g_render = true;

// ---- the sound -----------------------------------------------------------------
// The DS mixes one stereo pair every 512 cycles of its second processor:
// 16,756,991 / 512 = 32,728.5 pairs a second. They are handed out at that
// rate, about 547 a frame, so nothing is converted to another rate.
constexpr double kSampleRate = 16756991.0 / 512.0;
constexpr int kMaxSamples = 2048;
ECL_INVISIBLE int16_t g_audio[kMaxSamples * 2];
int g_audioFrames;

// ---- the controls --------------------------------------------------------------
// The twelve buttons, each with its bit in the machine's key register (a bit
// is 0 while its button is held), then the pen on the lower screen and the lid.
enum { kTouch = 12, kLid = 13, kButtons = 14 };
const struct { const char *name; int bit; } kButtonTable[kButtons] = {
	{ "Up", 6 }, { "Down", 7 }, { "Left", 5 }, { "Right", 4 }, { "Start", 3 }, { "Select", 2 },
	{ "B", 1 }, { "A", 0 }, { "Y", 11 }, { "X", 10 }, { "L", 9 }, { "R", 8 },
	{ "Touch", -1 }, { "Lid Closed", -1 },
};
bool g_held[kButtons];
// The pen's place is a fraction of the whole picture, 0 to 65535 each way, as
// every pointer in Chimera is: Chimera turns the mouse's place on the picture
// into it. The microphone is how loud a noise it hears.
enum { kTouchX, kTouchY, kMic, kAxes };
const char *const kAxisNames[kAxes] = { "Touch X", "Touch Y", "Mic Volume" };
int g_axis[kAxes] = { 32768, 32768, 0 };
bool g_inputRead;
// the noise the microphone hears: a generator of the machine's own, so that a
// savestate holds where it is
uint32_t g_noise = 0x2545F491;

// ---- the memory ----------------------------------------------------------------
struct memory_area
{
	const char *name;
	uint8_t *bytes;
	int64_t size;
};
std::vector<memory_area> g_memory;

// ---- the files -----------------------------------------------------------------
std::vector<std::string> slot_files(const char *slot)
{
	std::vector<std::string> out;
	char entry[512];
	const int32_t n = wbx_slot_count(slot);
	for (int32_t i = 0; i < n; i++)
		if (wbx_slot_name(slot, i, entry, sizeof entry) != nullptr)
			out.emplace_back(entry);
	return out;
}

// a game opened with no project is mounted under its own name
std::string lone_rom()
{
	char entry[512];
	FILE *f = fopen("rom.name", "rb");
	if (!f) return "";
	size_t const got = fread(entry, 1, sizeof entry - 1, f);
	fclose(f);
	entry[got] = '\0';
	return entry;
}

bool read_file(std::string const &name, std::vector<uint8_t> &bytes)
{
	FILE *f = fopen(name.c_str(), "rb");
	if (!f) return false;
	fseek(f, 0, SEEK_END);
	long const size = ftell(f);
	fseek(f, 0, SEEK_SET);
	bytes.resize(size > 0 ? size_t(size) : 0);
	bool const ok = bytes.empty() || fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
	fclose(f);
	return ok;
}

// the save file of the project's save data, when it has one
std::string save_file_named(const char *ext)
{
	std::string const tail = std::string(".") + ext;
	for (std::string const &file : slot_files("savedata"))
		if (file.size() > tail.size() && !strcasecmp(file.c_str() + file.size() - tail.size(), tail.c_str()))
			return file;
	return "";
}

struct save_file
{
	std::string name;
	std::vector<uint8_t> bytes;
};
std::vector<save_file> g_saveFiles;   // what the last export listed

bool setting_text(const char *name, std::string &value)
{
	char val[128];
	if (wbx_setting_str(name, val, sizeof val) <= 0) return false;
	value = val;
	return true;
}
bool setting_on(const char *name)
{
	std::string v;
	return setting_text(name, v) && (v[0] == 't' || v[0] == 'T' || (v[0] >= '1' && v[0] <= '9'));
}
int setting_number(const char *name, int otherwise, int low, int high)
{
	std::string v;
	if (!setting_text(name, v)) return otherwise;
	return std::min(std::max(int(strtol(v.c_str(), nullptr, 10)), low), high);
}

// where the two screens go, from the project's settings
void lay_out_screens()
{
	std::string layout = "stacked";
	setting_text("layout", layout);
	g_layout = layout == "side-by-side" ? kSideBySide : layout == "top" ? kTopOnly : layout == "bottom" ? kBottomOnly : kStacked;
	g_gap = setting_number("screen_gap", 0, 0, 192);
	g_swap = setting_on("swap_screens");
	g_turn = setting_number("rotation", 0, 0, 270) / 90;

	int const first = g_swap ? 1 : 0, second = 1 - first;
	g_placeX[0] = g_placeY[0] = g_placeX[1] = g_placeY[1] = -1;
	switch (g_layout)
	{
	case kTopOnly: g_canvasW = kScreenW; g_canvasH = kScreenH; g_placeX[0] = g_placeY[0] = 0; break;
	case kBottomOnly: g_canvasW = kScreenW; g_canvasH = kScreenH; g_placeX[1] = g_placeY[1] = 0; break;
	case kSideBySide:
		g_canvasW = kScreenW * 2 + g_gap; g_canvasH = kScreenH;
		g_placeX[first] = 0; g_placeY[first] = 0;
		g_placeX[second] = kScreenW + g_gap; g_placeY[second] = 0;
		break;
	default:
		g_canvasW = kScreenW; g_canvasH = kScreenH * 2 + g_gap;
		g_placeX[first] = 0; g_placeY[first] = 0;
		g_placeX[second] = 0; g_placeY[second] = kScreenH + g_gap;
		break;
	}
	g_canvas.assign(size_t(g_canvasW) * g_canvasH, 0xFF000000);
	g_width = (g_turn & 1) ? g_canvasH : g_canvasW;
	g_height = (g_turn & 1) ? g_canvasW : g_canvasH;
	g_picture.assign(size_t(g_width) * g_height, 0xFF000000);
}

void take_picture()
{
	// the machine draws into one of two pairs of buffers and shows the other
	int const front = g_nds->GPU.FrontBuffer;
	for (int screen = 0; screen < 2; screen++)
	{
		const u32 *src = g_nds->GPU.Framebuffer[front][screen].get();
		if (!src || g_placeX[screen] < 0) continue;
		for (int y = 0; y < kScreenH; y++)
			memcpy(&g_canvas[size_t(g_placeY[screen] + y) * g_canvasW + g_placeX[screen]], src + size_t(y) * kScreenW, size_t(kScreenW) * 4);
	}
	if (g_turn == 0)
	{
		memcpy(g_picture.data(), g_canvas.data(), g_canvas.size() * 4);
		return;
	}
	for (int y = 0; y < g_canvasH; y++)
		for (int x = 0; x < g_canvasW; x++)
		{
			uint32_t const pixel = g_canvas[size_t(y) * g_canvasW + x];
			if (g_turn == 1) g_picture[size_t(x) * g_width + (g_canvasH - 1 - y)] = pixel;            // a quarter turn clockwise
			else if (g_turn == 2) g_picture[size_t(g_canvasH - 1 - y) * g_width + (g_canvasW - 1 - x)] = pixel;
			else g_picture[size_t(g_canvasW - 1 - x) * g_width + y] = pixel;                          // three quarters
		}
}

// Where the pen is on the lower screen, from its place on the picture: the
// picture is turned back, and the lower screen's corner taken away. False when
// the place is not on the lower screen (on the upper one, in the gap, or the
// lower screen is not shown): there the pen touches nothing.
bool pen_on_lower_screen(int &x, int &y)
{
	if (g_placeX[1] < 0) return false;
	int const px = int(int64_t(g_axis[kTouchX]) * g_width / 65536);
	int const py = int(int64_t(g_axis[kTouchY]) * g_height / 65536);
	int cx = px, cy = py;
	if (g_turn == 1) { cx = py; cy = g_canvasH - 1 - px; }
	else if (g_turn == 2) { cx = g_canvasW - 1 - px; cy = g_canvasH - 1 - py; }
	else if (g_turn == 3) { cx = g_canvasW - 1 - py; cy = px; }
	x = cx - g_placeX[1];
	y = cy - g_placeY[1];
	return x >= 0 && x < kScreenW && y >= 0 && y < kScreenH;
}

// the Language setting, as the number the console keeps
int chosen_language()
{
	static const char *const kLanguages[] = { "japanese", "english", "french", "german", "italian", "spanish" };
	std::string language = "english";
	setting_text("language", language);
	for (int i = 0; i < 6; i++)
		if (language == kLanguages[i]) return i;
	return 1;
}

// The firmware's user settings a game can read: the language. The rest stays
// as melonDS writes it (the name "melonDS", a birthday, a favourite colour).
void set_firmware_language(Firmware &firmware)
{
	auto &user = firmware.GetEffectiveUserData();
	user.Settings = u16((user.Settings & ~Firmware::Language::Reserved) | chosen_language());
	firmware.UpdateChecksums();
}

// The DSi's four files, into the machine's description. The NAND is read
// whole into memory and its user settings are given the Language setting and
// the touch screen's calibration melonDS works with, as melonDS's own
// program does to the file it is pointed at; here the user's file is never
// written to.
bool load_dsi_files(NDSArgs &args, std::unique_ptr<DSiBIOSImage> &arm9i, std::unique_ptr<DSiBIOSImage> &arm7i, std::optional<DSi_NAND::NANDImage> &nandOut)
{
	std::vector<uint8_t> bios7i, bios9i, firmware;
	if (!read_file("bios7i.bin", bios7i) || bios7i.size() != DSiBIOSSize
		|| !read_file("bios9i.bin", bios9i) || bios9i.size() != DSiBIOSSize)
	{
		snprintf(g_loadError, sizeof g_loadError, "the DSi's BIOS files could not be read: bios7i.bin and bios9i.bin have to be %u bytes each", unsigned(DSiBIOSSize));
		return false;
	}
	if (!read_file("dsi_firmware.bin", firmware) || firmware.empty())
	{
		snprintf(g_loadError, sizeof g_loadError, "the DSi's firmware file (dsi_firmware.bin) could not be read");
		return false;
	}
	if (!read_file(kNandName, g_nand) || g_nand.empty())
	{
		snprintf(g_loadError, sizeof g_loadError, "the DSi's NAND image (dsi_nand.bin) could not be read");
		return false;
	}
	arm7i = std::make_unique<DSiBIOSImage>();
	arm9i = std::make_unique<DSiBIOSImage>();
	memcpy(arm7i->data(), bios7i.data(), DSiBIOSSize);
	memcpy(arm9i->data(), bios9i.data(), DSiBIOSSize);
	args.Firmware = Firmware(firmware.data(), u32(firmware.size()));
	if (!args.Firmware.Buffer())
	{
		snprintf(g_loadError, sizeof g_loadError, "dsi_firmware.bin is not a DSi firmware");
		return false;
	}
	// the key the NAND is encrypted with is in the second processor's BIOS
	DSi_NAND::NANDImage nand(Platform::OpenFile(kNandName, Platform::FileMode::ReadWriteExisting), &bios7i[0x8308]);
	if (!nand)
	{
		snprintf(g_loadError, sizeof g_loadError, "dsi_nand.bin is not a DSi NAND image, or not one with its console's footer (\"DSi eMMC CID/CPU\") at the end");
		return false;
	}
	{
		DSi_NAND::NANDMount mount(nand);
		DSi_NAND::DSiFirmwareSystemSettings user {};
		if (!mount || !mount.ReadUserData(user))
		{
			snprintf(g_loadError, sizeof g_loadError, "the DSi's NAND image could not be read with this BIOS: the two have to come from the same console");
			return false;
		}
		user.Language = Firmware::Language(chosen_language());
		user.TouchCalibrationADC1 = { 0, 0 };
		user.TouchCalibrationPixel1 = { 0, 0 };
		user.TouchCalibrationADC2 = { 255 << 4, 191 << 4 };
		user.TouchCalibrationPixel2 = { 255, 191 };
		user.UpdateHash();
		mount.ApplyUserData(user);
	}
	{
		// read back from the copy: a write that went nowhere must not pass for one
		DSi_NAND::NANDMount mount(nand);
		DSi_NAND::DSiFirmwareSystemSettings user {};
		if (!mount || !mount.ReadUserData(user) || int(user.Language) != chosen_language() || user.TouchCalibrationADC2[0] != (255 << 4))
		{
			snprintf(g_loadError, sizeof g_loadError, "the DSi's settings could not be written into the copy of its NAND image");
			return false;
		}
	}
	nandOut.emplace(std::move(nand));
	return true;
}

} // namespace

void frontend_stopped(int) { g_stopped = true; }
bool frontend_memory_file(const char *name, uint8_t **data, uint64_t *size)
{
	if (g_nand.empty() || strcmp(name, kNandName)) return false;
	*data = g_nand.data();
	*size = g_nand.size();
	return true;
}

// The microphone hears noise as loud as the Mic Volume control says, and
// silence at 0: enough for the games that are blown or shouted at.
int frontend_mic(int16_t *data, int max)
{
	int const volume = g_axis[kMic];
	for (int i = 0; i < max; i++)
	{
		g_noise = g_noise * 1664525u + 1013904223u;
		data[i] = int16_t((int32_t(int16_t(g_noise >> 16)) * volume) / 100);
	}
	return max;
}

extern "C" {

ECL_EXPORT const char *GetLoadError(void) { return g_loadError; }
ECL_EXPORT const char *SuggestSettings(void) { return "{\"values\":{},\"note\":\"\",\"firmware\":[]}"; }
ECL_EXPORT const char *DescribeDips(void) { return ""; }

ECL_EXPORT int Init(void)
{
	g_loadError[0] = '\0';
	std::vector<std::string> files = slot_files("cart");
	if (files.empty() && !lone_rom().empty()) files.push_back(lone_rom());
	if (files.empty())
	{
		snprintf(g_loadError, sizeof g_loadError, "no game was given");
		return 0;
	}
	std::vector<uint8_t> rom;
	if (!read_file(files[0], rom) || rom.empty())
	{
		snprintf(g_loadError, sizeof g_loadError, "the game's file could not be read: %s", files[0].c_str());
		return 0;
	}

	// the cartridge, with the project's save in it when there is one
	NDSCart::NDSCartArgs cartArgs;
	std::vector<uint8_t> save;
	std::string const saveName = save_file_named("sav");
	if (!saveName.empty() && read_file(saveName, save) && !save.empty())
	{
		cartArgs.SRAM = std::make_unique<u8[]>(save.size());
		memcpy(cartArgs.SRAM.get(), save.data(), save.size());
		cartArgs.SRAMLength = u32(save.size());
	}
	std::unique_ptr<NDSCart::CartCommon> cart = NDSCart::ParseROM(rom.data(), u32(rom.size()), nullptr, std::move(cartArgs));
	if (!cart)
	{
		snprintf(g_loadError, sizeof g_loadError, "%s is not a Nintendo DS cartridge", files[0].c_str());
		return 0;
	}

	// the machine: the interpreter, the software renderer on this thread, and
	// by default the free BIOS melonDS carries and the firmware it writes for
	// itself. With the BIOS setting on "original" the console's own three
	// files are the project's firmware.
	std::string machine = "nds";
	setting_text("machine", machine);
	if (machine != "nds" && machine != "dsi")
	{
		snprintf(g_loadError, sizeof g_loadError, "unknown machine \"%s\"", machine.c_str());
		return 0;
	}
	g_dsi = machine == "dsi";
	NDSArgs args;
	args.JIT = std::nullopt;
	args.Renderer3D = std::make_unique<SoftRenderer>();
	args.OutputSampleRate = kSampleRate;
	std::string bios = "free", boot = "game";
	setting_text("bios", bios);
	setting_text("boot", boot);
	bool const original = bios == "original";
	if (original)
	{
		std::vector<uint8_t> bios7, bios9, firmware;
		if (!read_file("bios7.bin", bios7) || bios7.size() != ARM7BIOSSize
			|| !read_file("bios9.bin", bios9) || bios9.size() != ARM9BIOSSize)
		{
			snprintf(g_loadError, sizeof g_loadError, "the console's BIOS files could not be read: bios7.bin has to be %u bytes and bios9.bin %u", unsigned(ARM7BIOSSize), unsigned(ARM9BIOSSize));
			return 0;
		}
		memcpy(args.ARM7BIOS->data(), bios7.data(), ARM7BIOSSize);
		memcpy(args.ARM9BIOS->data(), bios9.data(), ARM9BIOSSize);
		if (!g_dsi)
		{
			// (a DSi has its own firmware: load_dsi_files)
			if (!read_file("firmware.bin", firmware) || firmware.empty())
			{
				snprintf(g_loadError, sizeof g_loadError, "the console's firmware file (firmware.bin) could not be read");
				return 0;
			}
			args.Firmware = Firmware(firmware.data(), u32(firmware.size()));
			if (!args.Firmware.Buffer())
			{
				snprintf(g_loadError, sizeof g_loadError, "firmware.bin is not a Nintendo DS firmware: it has to be 128, 256 or 512 KB");
				return 0;
			}
		}
	}
	else if (boot == "firmware" && !g_dsi)
	{
		snprintf(g_loadError, sizeof g_loadError, "starting from the console's menu needs the console's own BIOS: set BIOS to 'original', or Start With to 'game'");
		return 0;
	}
	std::unique_ptr<DSiBIOSImage> arm9i, arm7i;
	std::optional<DSi_NAND::NANDImage> nand;
	if (g_dsi && !load_dsi_files(args, arm9i, arm7i, nand)) return 0;
	set_firmware_language(args.Firmware);
	lay_out_screens();
	if (g_dsi)
	{
		// the DSi's own start, from its BIOS to its menu, or the game at once;
		// its sound processor's programs are run by melonDS's own code for them
		DSiArgs dsi { std::move(args), std::move(arm9i), std::move(arm7i), std::move(*nand), std::nullopt, boot == "firmware", true };
		g_nds = new DSi(std::move(dsi), nullptr);
	}
	else g_nds = new NDS(std::move(args), nullptr);
	g_nds->SetNDSCart(std::move(cart));
	g_nds->Reset();

	// the machine's clock starts at a time the project sets, never the host's
	char val[64];
	time_t start = 946684800;   // 2000-01-01 00:00:00 UTC
	if (wbx_setting_str("initialTime", val, sizeof val) > 0) start = time_t(strtoll(val, nullptr, 10));
	struct tm when;
	gmtime_r(&start, &when);
	g_nds->RTC.SetDateTime(when.tm_year + 1900, when.tm_mon + 1, when.tm_mday, when.tm_hour, when.tm_min, when.tm_sec);

	if (boot != "firmware" || (!g_dsi && g_nds->NeedsDirectBoot()))
	{
		size_t const slash = files[0].find_last_of('/');
		g_nds->SetupDirectBoot(slash == std::string::npos ? files[0] : files[0].substr(slash + 1));
	}
	g_nds->Start();

	g_memory.push_back({ "Main RAM", g_nds->MainRAM, int64_t(g_nds->MainRAMMask) + 1 });
	g_memory.push_back({ "Shared WRAM", g_nds->SharedWRAM, SharedWRAMSize });
	g_memory.push_back({ "ARM7 WRAM", g_nds->ARM7WRAM, ARM7WRAMSize });
	return 1;
}

ECL_EXPORT int GetButtonCount(void) { return kButtons; }
ECL_EXPORT const char *GetButtonName(int32_t i) { return i >= 0 && i < kButtons ? kButtonTable[i].name : ""; }
ECL_EXPORT int IsButtonActive(int32_t i) { return i >= 0 && i < kButtons; }
ECL_EXPORT void SetButton(int32_t i, int32_t state)
{
	if (i >= 0 && i < kButtons) g_held[i] = state != 0;
}

ECL_EXPORT int GetAxisCount(void) { return kAxes; }
ECL_EXPORT const char *GetAxisName(int32_t i) { return i >= 0 && i < kAxes ? kAxisNames[i] : ""; }
ECL_EXPORT int IsAxisActive(int32_t i) { return i >= 0 && i < kAxes; }
ECL_EXPORT void SetAxis(int32_t i, int32_t value)
{
	if (i == kTouchX || i == kTouchY) g_axis[i] = std::min(std::max(value, 0), 65535);
	if (i == kMic) g_axis[i] = std::min(std::max(value, 0), 100);
}

ECL_EXPORT void FrameAdvance(uint64_t)
{
	if (!g_nds) return;
	u32 mask = 0xFFF;
	for (int i = 0; i < 12; i++)
		if (g_held[i]) mask &= ~(1u << kButtonTable[i].bit);
	g_nds->SetKeyMask(mask);
	// the pen: a place on the picture, turned into a pixel of the lower screen
	int penX, penY;
	if (g_held[kTouch] && pen_on_lower_screen(penX, penY)) g_nds->TouchScreen(u16(penX), u16(penY));
	else g_nds->ReleaseScreen();
	g_nds->SetLidClosed(g_held[kLid]);

	g_nds->RunFrame();
	// a frame in which the game did not read the buttons is a lag frame
	g_inputRead = !g_nds->LagFrameFlag;

	int const ready = std::min(g_nds->SPU.GetOutputSize(), kMaxSamples);
	g_audioFrames = g_nds->SPU.ReadOutput(g_audio, ready);
	if (g_render) take_picture();
}

ECL_EXPORT int InputWasRead(void) { return g_inputRead ? 1 : 0; }
ECL_EXPORT void SetRenderingEnabled(int on) { g_render = on != 0; }

ECL_EXPORT uint32_t *GetVideoBgra(void) { return g_picture.data(); }
ECL_EXPORT int GetVideoWidth(void) { return g_width; }
ECL_EXPORT int GetVideoHeight(void) { return g_height; }
// the DS's pixels are square: the picture's shape is its size
ECL_EXPORT int GetDisplayAspectX(void) { return g_width ? g_width : kScreenW; }
ECL_EXPORT int GetDisplayAspectY(void) { return g_height ? g_height : kScreenH * 2; }
// 33,513,982 cycles a second and 560,190 a frame: 59.8261 frames a second
ECL_EXPORT int GetVsyncNumerator(void) { return 59826; }
ECL_EXPORT int GetVsyncDenominator(void) { return 1000; }

ECL_EXPORT int16_t *GetAudio(void) { return g_audio; }
ECL_EXPORT int GetAudioSampleCount(void) { return g_audioFrames; }

ECL_EXPORT int GetMemoryDomainCount(void) { return int(g_memory.size()); }
ECL_EXPORT const char *GetMemoryDomainName(int i) { return i >= 0 && size_t(i) < g_memory.size() ? g_memory[i].name : ""; }
ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i) { return i >= 0 && size_t(i) < g_memory.size() ? g_memory[i].bytes : nullptr; }
ECL_EXPORT int64_t GetMemoryDomainSize(int i) { return i >= 0 && size_t(i) < g_memory.size() ? g_memory[i].size : 0; }
ECL_EXPORT int GetMemoryDomainWritable(int i) { return i >= 0 && size_t(i) < g_memory.size(); }

// no bus: the machine's memories are memory domains
ECL_EXPORT const int64_t *GetBusRanges(int32_t) { return nullptr; }
ECL_EXPORT const uint8_t *ReadBus(int32_t, int64_t, int32_t) { return nullptr; }
ECL_EXPORT void PokeBus(int32_t, int32_t, int32_t) {}

// Export Save Data: the cartridge's save memory as the file melonDS writes
ECL_EXPORT int32_t GetSaveDataFileCount(void)
{
	g_saveFiles.clear();
	if (g_nds && g_nds->GetNDSSave() && g_nds->GetNDSSaveLength() > 0)
	{
		save_file f;
		f.name = "nds.sav";
		f.bytes.assign(g_nds->GetNDSSave(), g_nds->GetNDSSave() + g_nds->GetNDSSaveLength());
		g_saveFiles.push_back(std::move(f));
	}
	return int32_t(g_saveFiles.size());
}
ECL_EXPORT const char *GetSaveDataFileName(int32_t i) { return i >= 0 && size_t(i) < g_saveFiles.size() ? g_saveFiles[i].name.c_str() : ""; }
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i) { return i >= 0 && size_t(i) < g_saveFiles.size() ? int64_t(g_saveFiles[i].bytes.size()) : 0; }
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t i) { return i >= 0 && size_t(i) < g_saveFiles.size() ? g_saveFiles[i].bytes.data() : nullptr; }

} // extern "C"
