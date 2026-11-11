// Operating-system services the runtime needs, behind one interface.
// platform_win32.cpp implements it with the Win32 calls the runtime used directly before the Linux port;
// platform_posix.cpp implements it for Linux. Everything else in runtime/ stays free of OS headers where it can.
//
// Paths: UTF-8 std::string or std::filesystem::path, always built with '/' (Windows accepts it everywhere a file
// name goes; on Linux '\' is an ordinary file-name character, so it must never be used as a separator).
#pragma once
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace plat {

// ---- where the program is ---------------------------------------------------------------------------------------
std::string exe_path();                 // the running executable (UTF-8)
std::string exe_dir();                  // its folder, no trailing separator
std::filesystem::path exe_dir_path();   // the same as a path

// ---- files --------------------------------------------------------------------------------------------------------
bool exists(const std::string& path);                               // a file or a folder
bool make_dir(const std::string& path);                             // one level; true when it exists afterwards
bool copy_file(const std::string& from, const std::string& to);     // replaces 'to'
bool replace_file(const std::string& from, const std::string& to);  // rename over 'to' (atomic on the same volume)
// Last-write time in an opaque unit that only grows (compare with other stamps and with now_stamp()).
bool file_stamp(const std::string& path, uint64_t* mtime, uint64_t* size = nullptr);
uint64_t now_stamp();
FILE* fopen_path(const std::filesystem::path& p, const char* mode);  // fopen for any path (wide on Windows)
std::string utf8(const std::filesystem::path& p);                   // native separators
std::filesystem::path from_utf8(const std::string& s);
int fseek64(FILE* f, int64_t off, int whence);
int64_t ftell64(FILE* f);
// The path as it really is on disk when only the letter case differs (disc files, mods and texture packs made on
// Windows). Returns the input unchanged when it exists as given, or when nothing matches.
std::string resolve_nocase(const std::string& path);
bool free_space(const std::filesystem::path& dir, uint64_t* bytes);
int stricmp(const char* a, const char* b);

// ---- time -----------------------------------------------------------------------------------------------------------
struct LocalTime { int year, month, day, hour, minute, second; };
LocalTime local_time();
uint64_t tick_ms();          // monotonic milliseconds
void sleep_ms(unsigned ms);

// ---- process --------------------------------------------------------------------------------------------------------
void process_setup(bool allow_throttle);    // start of main: timer resolution, no background power throttling
void set_priority(bool background);         // the player's game above normal, quiet test games below
// true when stderr goes nowhere useful (a terminal, or nothing at all): main() then logs to a file instead
bool stderr_needs_log_file();
std::string stderr_file();                  // the file stderr is redirected to ("" when it is not a file)
void* alloc_ram(size_t bytes);              // zeroed, page aligned; nullptr on failure
[[noreturn]] void terminate_now(int code);  // end the process at once: no destructors, no atexit
void hide_own_windows();                    // before a fast exit: the window disappears immediately
bool run_with_timeout(void (*fn)(), unsigned ms);   // fn on a helper thread; false when it did not finish in time
bool restart_self();                        // start this program again with the same command line
void open_folder(const std::filesystem::path& p);   // in the desktop's file manager
// Start a program without waiting for it ("open" semantics: an executable, or a script by its interpreter).
bool launch(const std::string& program, const std::string& args, const std::string& workdir);
// Run a program and wait; *cancel is polled and the child killed when it turns true. Returns its exit code, -1 on error.
int run_and_wait(const std::filesystem::path& program, const std::vector<std::string>& args, const volatile bool* cancel);
// Fatal host faults (access violation / SIGSEGV...): a short report on stderr, then 'report' (guest backtrace), then
// the default action (crash dump / core).
void install_crash_handler(void (*report)());
// Embedded data: the launcher art (201 emblem, 202 hero). nullptr when the build has none.
const void* resource(int id, size_t* size);

// ---- stacks -----------------------------------------------------------------------------------------------------------
// The game needs a very deep host stack (recompiled guest calls nest on it). Windows gets it from the linker
// (/STACK); elsewhere this runs fn on a stack of that size, still on the calling thread.
int run_on_big_stack(int (*fn)(int, char**), int argc, char** argv);

// Cooperative fibers (one per guest thread). Same contract as the Win32 fiber calls.
typedef void* Fiber;
Fiber fiber_from_thread();                                             // the calling thread becomes a fiber
Fiber fiber_create(size_t stack_bytes, void (*fn)(void*), void* arg);  // fn must never return
void fiber_switch(Fiber to);
void fiber_delete(Fiber f);                                            // not the running one

}  // namespace plat
