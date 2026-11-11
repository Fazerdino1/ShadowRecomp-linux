// Windows side of platform.h: the Win32 calls the runtime made directly before the Linux port, unchanged in behaviour.
#ifdef _WIN32
#define NOMINMAX
#include "platform.h"
#include <windows.h>
#include <shellapi.h>
#include <io.h>
#include <cstdlib>
#include <cstring>

namespace plat {

static std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}

// The narrow (ANSI) path, as GetModuleFileNameA gave it to every caller before: the runtime's fopen calls are narrow.
std::string exe_path() {
  char exe[MAX_PATH];
  GetModuleFileNameA(nullptr, exe, MAX_PATH);
  return exe;
}
std::string exe_dir() {
  std::string d = exe_path();
  return d.substr(0, d.find_last_of("\\/"));
}
std::filesystem::path exe_dir_path() {
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  return std::filesystem::path(exe).parent_path();
}

bool exists(const std::string& path) { return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES; }
bool make_dir(const std::string& path) {
  if (CreateDirectoryA(path.c_str(), nullptr)) return true;
  return GetLastError() == ERROR_ALREADY_EXISTS;
}
bool copy_file(const std::string& from, const std::string& to) { return CopyFileA(from.c_str(), to.c_str(), FALSE) != 0; }
bool replace_file(const std::string& from, const std::string& to) {
  return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
bool file_stamp(const std::string& path, uint64_t* mtime, uint64_t* size) {
  WIN32_FILE_ATTRIBUTE_DATA d;
  if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &d)) return false;
  if (mtime) *mtime = ((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
  if (size) *size = ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
  return true;
}
uint64_t now_stamp() {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}
FILE* fopen_path(const std::filesystem::path& p, const char* mode) {
  wchar_t wmode[8] = {};
  for (int i = 0; i < 7 && mode[i]; ++i) wmode[i] = (wchar_t)mode[i];
  return _wfopen(p.c_str(), wmode);
}
std::string utf8(const std::filesystem::path& p) {
  const std::wstring& w = p.native();
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}
std::filesystem::path from_utf8(const std::string& s) { return std::filesystem::path(widen(s)); }
int fseek64(FILE* f, int64_t off, int whence) { return _fseeki64(f, off, whence); }
int64_t ftell64(FILE* f) { return _ftelli64(f); }
std::string resolve_nocase(const std::string& path) { return path; }   // the file system already ignores case
bool free_space(const std::filesystem::path& dir, uint64_t* bytes) {
  ULARGE_INTEGER freeb{};
  if (!GetDiskFreeSpaceExW(dir.c_str(), &freeb, nullptr, nullptr)) return false;
  *bytes = freeb.QuadPart;
  return true;
}
int stricmp(const char* a, const char* b) { return _stricmp(a, b); }

LocalTime local_time() {
  SYSTEMTIME t;
  GetLocalTime(&t);
  return {t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond};
}
uint64_t tick_ms() { return GetTickCount64(); }
void sleep_ms(unsigned ms) { Sleep(ms); }

void process_setup(bool allow_throttle) {
  // #151: tabbed out / minimised, Windows 11 power throttling (EcoQoS: efficiency cores, low clocks) and timer
  // throttling slowed the game thread enough to starve the audio queue (stutter). This process opts out of both.
  PROCESS_POWER_THROTTLING_STATE pt{};
  pt.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  pt.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
  pt.StateMask = 0;   // controlled + off = never throttled
  if (!allow_throttle) SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &pt, sizeof pt);
  timeBeginPeriod(1);
}
void set_priority(bool background) {
  SetPriorityClass(GetCurrentProcess(), background ? BELOW_NORMAL_PRIORITY_CLASS : ABOVE_NORMAL_PRIORITY_CLASS);
}
bool stderr_needs_log_file() {
  // also when started by double-click: a Windows-subsystem program then has no stderr at all
  HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
  return _isatty(_fileno(stderr)) || err == nullptr || err == INVALID_HANDLE_VALUE;
}
std::string stderr_file() {
  HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
  char path[MAX_PATH * 2] = {};
  if (!h || h == INVALID_HANDLE_VALUE || !GetFinalPathNameByHandleA(h, path, sizeof path, 0)) return {};
  std::string p = path;
  if (p.rfind("\\\\?\\", 0) == 0) p = p.substr(4);
  return p;
}
void* alloc_ram(size_t bytes) { return VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE); }
void terminate_now(int code) {
  TerminateProcess(GetCurrentProcess(), (UINT)code);   // #153: no teardown (it can block on the GPU driver)
  ExitProcess((UINT)code);
}
void hide_own_windows() {
  EnumWindows([](HWND w, LPARAM) -> BOOL {
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid == GetCurrentProcessId()) ShowWindow(w, SW_HIDE);
    return TRUE;
  }, 0);
}
bool run_with_timeout(void (*fn)(), unsigned ms) {
  HANDLE h = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD { ((void (*)())p)(); return 0; }, (LPVOID)fn, 0, nullptr);
  if (!h) return false;
  const bool done = WaitForSingleObject(h, ms) == WAIT_OBJECT_0;
  CloseHandle(h);
  return done;
}
bool restart_self() {
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  std::wstring cmd = GetCommandLineW();
  STARTUPINFOW si{};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}
void open_folder(const std::filesystem::path& p) { ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
bool launch(const std::string& program, const std::string& args, const std::string& workdir) {
  return (INT_PTR)ShellExecuteA(nullptr, "open", program.c_str(), args.c_str(), workdir.c_str(), SW_SHOWNORMAL) > 32;
}
int run_and_wait(const std::filesystem::path& program, const std::vector<std::string>& args, const volatile bool* cancel) {
  std::wstring cmd = L"\"" + program.wstring() + L"\"";
  for (const auto& a : args) {
    const std::wstring w = widen(a);
    cmd += w.find(L' ') != std::wstring::npos || w.empty() ? L" \"" + w + L"\"" : L" " + w;
  }
  STARTUPINFOW si{sizeof si};
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return -1;
  while (WaitForSingleObject(pi.hProcess, 200) == WAIT_TIMEOUT)
    if (cancel && *cancel) TerminateProcess(pi.hProcess, 1);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return (int)code;
}

static void (*g_crash_report)() = nullptr;
static LONG WINAPI crash_filter(EXCEPTION_POINTERS* ep) {
  fprintf(stderr, "\n[crash] host exception %08lX at %p\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
  {   // which module / thread (host faults inside Dawn or the runtime look alike in the guest backtrace)
    HMODULE mod = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)ep->ExceptionRecord->ExceptionAddress, &mod))
      GetModuleFileNameA(mod, name, sizeof name);
    fprintf(stderr, "        in %s (+%llx), thread %lu\n", name, (unsigned long long)((char*)ep->ExceptionRecord->ExceptionAddress - (char*)mod), GetCurrentThreadId());
  }
  if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
    fprintf(stderr, "        access violation %s address %p\n", ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading", (void*)ep->ExceptionRecord->ExceptionInformation[1]);
  if (g_crash_report) g_crash_report();
  fflush(stderr);
  return EXCEPTION_CONTINUE_SEARCH;
}
void install_crash_handler(void (*report)()) {
  g_crash_report = report;
  SetUnhandledExceptionFilter(crash_filter);
}

const void* resource(int id, size_t* size) {
  HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
  HGLOBAL h = r ? LoadResource(nullptr, r) : nullptr;
  const void* data = h ? LockResource(h) : nullptr;
  if (data && size) *size = SizeofResource(nullptr, r);
  return data;
}

int run_on_big_stack(int (*fn)(int, char**), int argc, char** argv) { return fn(argc, argv); }   // the linker's /STACK

namespace {
struct FiberStart { void (*fn)(void*); void* arg; };
void CALLBACK fiber_entry(void* p) {
  FiberStart s = *(FiberStart*)p;
  delete (FiberStart*)p;
  s.fn(s.arg);
}
}  // namespace
Fiber fiber_from_thread() { return ConvertThreadToFiber(nullptr); }
Fiber fiber_create(size_t stack_bytes, void (*fn)(void*), void* arg) {
  return CreateFiberEx(1 << 16, stack_bytes, 0, fiber_entry, new FiberStart{fn, arg});
}
void fiber_switch(Fiber to) { SwitchToFiber(to); }
void fiber_delete(Fiber f) { DeleteFiber(f); }

}  // namespace plat
#endif  // _WIN32
