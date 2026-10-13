// platform.cpp - what melonDS's machine asks of the program around it
// (extern/melonDS/src/Platform.h), answered for a core: no window, no
// network, no host clock, no thread, and nothing written to a file. A save
// stays in the machine's memory, where a savestate holds it; Chimera writes
// it to a file when the user exports it.
// SPDX-License-Identifier: MIT
#include "Platform.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "frontend.h"

namespace melonDS
{
namespace Platform
{

void SignalStop(StopReason reason, void *)
{
	frontend_stopped(int(reason));
}

// ---- files -------------------------------------------------------------------
// Two kinds. A file on the project's list is opened by name and only read.
// A file the machine also writes to while it runs (the DSi's NAND) was read
// whole into the machine's memory by the core, and is read and written
// there: the sandbox keeps no file's contents or position in a savestate,
// and a core writes nothing to the user's files.
struct FileHandle
{
	FILE *f;          // a file of the project's, or null
	u8 *mem;          // a file kept in memory
	u64 size, pos;
	bool eof;
};

std::string GetLocalFilePath(const std::string &filename) { return filename; }

FileHandle *OpenFile(const std::string &path, FileMode mode)
{
	uint8_t *mem = nullptr;
	uint64_t size = 0;
	if (frontend_memory_file(path.c_str(), &mem, &size)) return new FileHandle{ nullptr, mem, size, 0, false };
	if (mode & FileMode::Write) return nullptr;   // nothing is written to a file
	FILE *f = fopen(path.c_str(), "rb");
	return f ? new FileHandle{ f, nullptr, 0, 0, false } : nullptr;
}
FileHandle *OpenLocalFile(const std::string &path, FileMode mode) { return OpenFile(path, mode); }
bool FileExists(const std::string &name)
{
	uint8_t *mem = nullptr;
	uint64_t size = 0;
	if (frontend_memory_file(name.c_str(), &mem, &size)) return true;
	FILE *f = fopen(name.c_str(), "rb");
	if (f) fclose(f);
	return f != nullptr;
}
bool LocalFileExists(const std::string &name) { return FileExists(name); }
bool CheckFileWritable(const std::string &name)
{
	uint8_t *mem = nullptr;
	uint64_t size = 0;
	return frontend_memory_file(name.c_str(), &mem, &size);
}
bool CheckLocalFileWritable(const std::string &name) { return CheckFileWritable(name); }
bool CloseFile(FileHandle *file)
{
	if (!file) return false;
	if (file->f) fclose(file->f);
	delete file;
	return true;
}
bool IsEndOfFile(FileHandle *file) { return file->f ? feof(file->f) != 0 : file->eof; }
bool FileReadLine(char *str, int count, FileHandle *file)
{
	if (file->f) return fgets(str, count, file->f) != nullptr;
	int n = 0;
	while (n + 1 < count && file->pos < file->size)
	{
		char const c = char(file->mem[file->pos++]);
		str[n++] = c;
		if (c == '\n') break;
	}
	if (count > 0) str[n] = '\0';
	if (n == 0) file->eof = true;
	return n > 0;
}
u64 FilePosition(FileHandle *file) { return file->f ? u64(ftell(file->f)) : file->pos; }
bool FileSeek(FileHandle *file, s64 offset, FileSeekOrigin origin)
{
	if (file->f)
	{
		int const whence = origin == FileSeekOrigin::Start ? SEEK_SET : origin == FileSeekOrigin::Current ? SEEK_CUR : SEEK_END;
		return fseek(file->f, long(offset), whence) == 0;
	}
	s64 const base = origin == FileSeekOrigin::Start ? 0 : origin == FileSeekOrigin::Current ? s64(file->pos) : s64(file->size);
	if (base + offset < 0 || u64(base + offset) > file->size) return false;
	file->pos = u64(base + offset);
	file->eof = false;
	return true;
}
void FileRewind(FileHandle *file)
{
	if (file->f) rewind(file->f);
	file->pos = 0;
	file->eof = false;
}
u64 FileRead(void *data, u64 size, u64 count, FileHandle *file)
{
	if (file->f) return fread(data, size, count, file->f);
	if (!size) return 0;
	u64 const whole = std::min(count, (file->size - file->pos) / size);
	memcpy(data, file->mem + file->pos, size_t(whole * size));
	file->pos += whole * size;
	if (whole < count) file->eof = true;
	return whole;
}
bool FileFlush(FileHandle *) { return true; }
u64 FileWrite(const void *data, u64 size, u64 count, FileHandle *file)
{
	if (file->f || !size) return 0;
	// a file in memory keeps its length: the NAND is a chip of a fixed size
	u64 const whole = std::min(count, (file->size - file->pos) / size);
	memcpy(file->mem + file->pos, data, size_t(whole * size));
	file->pos += whole * size;
	return whole;
}
u64 FileWriteFormatted(FileHandle *, const char *, ...) { return 0; }
u64 FileLength(FileHandle *file)
{
	if (!file->f) return file->size;
	long const at = ftell(file->f);
	fseek(file->f, 0, SEEK_END);
	long const len = ftell(file->f);
	fseek(file->f, at, SEEK_SET);
	return u64(len);
}

void Log(LogLevel level, const char *fmt, ...)
{
	// the machine's own remarks, for Chimera's core log
	if (level == LogLevel::Debug) return;
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
}

// ---- threads -----------------------------------------------------------------
// A core is one thread. The machine's only thread is the 3D renderer's, which
// this core never asks for (the renderer is made without it), so these are
// never reached with work to do.
struct Thread {};
struct Semaphore {};
struct Mutex {};
Thread *Thread_Create(std::function<void()>)
{
	fprintf(stderr, "melonDS asked for a thread; this core runs on one\n");
	abort();
}
void Thread_Free(Thread *) {}
void Thread_Wait(Thread *) {}
Semaphore *Semaphore_Create() { return new Semaphore; }
void Semaphore_Free(Semaphore *sema) { delete sema; }
void Semaphore_Reset(Semaphore *) {}
void Semaphore_Wait(Semaphore *) {}
bool Semaphore_TryWait(Semaphore *, int) { return true; }
void Semaphore_Post(Semaphore *, int) {}
Mutex *Mutex_Create() { return new Mutex; }
void Mutex_Free(Mutex *mutex) { delete mutex; }
void Mutex_Lock(Mutex *) {}
void Mutex_Unlock(Mutex *) {}
bool Mutex_TryLock(Mutex *) { return true; }

// ---- time --------------------------------------------------------------------
// The machine's time is the cycles it has run. Nothing waits on the host, and
// the host's clock is never read: only the network code asks for it, and
// that is not built.
void Sleep(u64) {}
u64 GetMSCount() { return 0; }
u64 GetUSCount() { return 0; }

// ---- what the machine would have a program write to disk -----------------
void WriteNDSSave(const u8 *, u32, u32, u32, void *) {}
void WriteGBASave(const u8 *, u32, u32, u32, void *) {}
void WriteFirmware(const Firmware &, u32, u32, void *) {}
void WriteDateTime(int, int, int, int, int, int, void *) {}

// ---- wireless: no other console is in reach ----------------------------------
void MP_Begin(void *) {}
void MP_End(void *) {}
int MP_SendPacket(u8 *, int, u64, void *) { return 0; }
int MP_RecvPacket(u8 *, u64 *, void *) { return 0; }
int MP_SendCmd(u8 *, int, u64, void *) { return 0; }
int MP_SendReply(u8 *, int, u64, u16, void *) { return 0; }
int MP_SendAck(u8 *, int, u64, void *) { return 0; }
int MP_RecvHostPacket(u8 *, u64 *, void *) { return 0; }
u16 MP_RecvReplies(u8 *, u64, u16, void *) { return 0; }
int Net_SendPacket(u8 *, int, void *) { return 0; }
int Net_RecvPacket(u8 *, void *) { return 0; }

// ---- the DSi's cameras: a black picture ----------------------------------------
void Camera_Start(int, void *) {}
void Camera_Stop(int, void *) {}
void Camera_CaptureFrame(int, u32 *frame, int width, int height, bool yuv, void *)
{
	// black: in YUV that is luma 16 and both chromas 128
	for (int i = 0; i < width * height / (yuv ? 2 : 1); i++) frame[i] = yuv ? 0x80108010 : 0;
}

// ---- the microphone: what the core's control says ------------------------------
void Mic_Start(void *) {}
void Mic_Stop(void *) {}
int Mic_ReadInput(s16 *data, int maxlength, void *) { return frontend_mic(data, maxlength); }

// ---- the DSi's AAC decoder: none --------------------------------------------
AACDecoder *AAC_Init() { return nullptr; }
void AAC_DeInit(AACDecoder *) {}
bool AAC_Configure(AACDecoder *, int, int) { return false; }
bool AAC_DecodeFrame(AACDecoder *, const void *, int, void *, int) { return false; }

// ---- cartridges of the GBA slot with switches and motors of their own ---------
bool Addon_KeyDown(KeyType, void *) { return false; }
void Addon_RumbleStart(u32, void *) {}
void Addon_RumbleStop(void *) {}
float Addon_MotionQuery(MotionQueryType, void *) { return 0.0f; }

DynamicLibrary *DynamicLibrary_Load(const char *) { return nullptr; }
void DynamicLibrary_Unload(DynamicLibrary *) {}
void *DynamicLibrary_LoadFunction(DynamicLibrary *, const char *) { return nullptr; }

} // namespace Platform
} // namespace melonDS
