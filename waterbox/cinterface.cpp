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
#include "NDSCart.h"
#include "GPU.h"
#include "GPU3D_Soft.h"
#include "SPU.h"
#include "RTC.h"
#include "Args.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
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

// ---- the picture: the two screens, one above the other -----------------------
constexpr int kScreenW = 256, kScreenH = 192;
std::vector<uint32_t> g_picture(size_t(kScreenW) * kScreenH * 2);
bool g_render = true;

// ---- the sound -----------------------------------------------------------------
constexpr int kSampleRate = 48000;
constexpr int kMaxSamples = 2048;
ECL_INVISIBLE int16_t g_audio[kMaxSamples * 2];
int g_audioFrames;

// ---- the controls --------------------------------------------------------------
// The twelve buttons in the order of the machine's key register (a bit is 0
// while its button is held), then the pen on the lower screen and the lid.
enum { kTouch = 12, kLid = 13, kButtons = 14 };
const char *const kButtonNames[kButtons] = {
	"A", "B", "Select", "Start", "Right", "Left", "Up", "Down", "R", "L", "X", "Y", "Touch", "Lid Closed",
};
bool g_held[kButtons];
enum { kTouchX, kTouchY, kAxes };
const char *const kAxisNames[kAxes] = { "Touch X", "Touch Y" };
int g_axis[kAxes] = { kScreenW / 2, kScreenH / 2 };
bool g_inputRead;

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

void take_picture()
{
	// the machine draws into one of two pairs of buffers and shows the other
	int const front = g_nds->GPU.FrontBuffer;
	for (int screen = 0; screen < 2; screen++)
	{
		const u32 *src = g_nds->GPU.Framebuffer[front][screen].get();
		if (src) memcpy(&g_picture[size_t(screen) * kScreenW * kScreenH], src, size_t(kScreenW) * kScreenH * 4);
	}
}

} // namespace

void frontend_stopped(int) { g_stopped = true; }
int frontend_mic(int16_t *, int) { return 0; }

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

	// the machine: the interpreter, the software renderer on this thread, the
	// free BIOS melonDS carries and the firmware it writes for itself
	NDSArgs args;
	args.JIT = std::nullopt;
	args.Renderer3D = std::make_unique<SoftRenderer>();
	args.OutputSampleRate = kSampleRate;
	g_nds = new NDS(std::move(args), nullptr);
	g_nds->SetNDSCart(std::move(cart));
	g_nds->Reset();

	// the machine's clock starts at a time the project sets, never the host's
	char val[64];
	time_t start = 946684800;   // 2000-01-01 00:00:00 UTC
	if (wbx_setting_str("initialTime", val, sizeof val) > 0) start = time_t(strtoll(val, nullptr, 10));
	struct tm when;
	gmtime_r(&start, &when);
	g_nds->RTC.SetDateTime(when.tm_year + 1900, when.tm_mon + 1, when.tm_mday, when.tm_hour, when.tm_min, when.tm_sec);

	if (g_nds->NeedsDirectBoot())
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
ECL_EXPORT const char *GetButtonName(int32_t i) { return i >= 0 && i < kButtons ? kButtonNames[i] : ""; }
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
	if (i == kTouchX) g_axis[i] = std::min(std::max(value, 0), kScreenW - 1);
	if (i == kTouchY) g_axis[i] = std::min(std::max(value, 0), kScreenH - 1);
}

ECL_EXPORT void FrameAdvance(uint64_t)
{
	if (!g_nds) return;
	u32 mask = 0xFFF;
	for (int i = 0; i < 12; i++)
		if (g_held[i]) mask &= ~(1u << i);
	g_nds->SetKeyMask(mask);
	if (g_held[kTouch]) g_nds->TouchScreen(u16(g_axis[kTouchX]), u16(g_axis[kTouchY]));
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
ECL_EXPORT int GetVideoWidth(void) { return kScreenW; }
ECL_EXPORT int GetVideoHeight(void) { return kScreenH * 2; }

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
