// Linux (POSIX) side of platform.h.
#ifndef _WIN32
#include "platform.h"
#include <atomic>
#include <memory>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <thread>
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

extern char** environ;

namespace fs = std::filesystem;

namespace plat {

// ---- where the program is ---------------------------------------------------------------------------------------
std::string exe_path() {
  static const std::string cached = [] {
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return std::string("./shadow");
    buf[n] = 0;
    return std::string(buf);
  }();
  return cached;
}
std::string exe_dir() {
  const std::string p = exe_path();
  const size_t cut = p.find_last_of('/');
  return cut == std::string::npos ? "." : p.substr(0, cut);
}
fs::path exe_dir_path() { return fs::path(exe_dir()); }

// ---- files --------------------------------------------------------------------------------------------------------
bool exists(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}
bool make_dir(const std::string& path) {
  if (mkdir(path.c_str(), 0755) == 0) return true;
  struct stat st;
  return errno == EEXIST && stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
bool copy_file(const std::string& from, const std::string& to) {
  std::error_code ec;
  return fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
}
bool replace_file(const std::string& from, const std::string& to) { return rename(from.c_str(), to.c_str()) == 0; }
bool file_stamp(const std::string& path, uint64_t* mtime, uint64_t* size) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) return false;
  if (mtime) *mtime = (uint64_t)st.st_mtim.tv_sec * 1000000000ull + (uint64_t)st.st_mtim.tv_nsec;
  if (size) *size = (uint64_t)st.st_size;
  return true;
}
uint64_t now_stamp() {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
FILE* fopen_path(const fs::path& p, const char* mode) { return fopen(p.c_str(), mode); }
std::string utf8(const fs::path& p) { return p.native(); }
fs::path from_utf8(const std::string& s) { return fs::path(s); }
int fseek64(FILE* f, int64_t off, int whence) { return fseeko(f, (off_t)off, whence); }
int64_t ftell64(FILE* f) { return (int64_t)ftello(f); }

std::string resolve_nocase(const std::string& path) {
  struct stat st;
  if (path.empty() || stat(path.c_str(), &st) == 0) return path;
  // the longest leading part that exists, then one case-insensitive directory lookup per remaining component
  std::string done = path[0] == '/' ? "/" : "";
  size_t pos = path[0] == '/' ? 1 : 0;
  while (pos <= path.size()) {
    size_t next = path.find('/', pos);
    if (next == std::string::npos) next = path.size();
    const std::string comp = path.substr(pos, next - pos);
    pos = next + 1;
    if (comp.empty()) continue;
    std::string cand = done + comp;
    if (stat(cand.c_str(), &st) != 0) {
      bool found = false;
      std::error_code ec;
      for (fs::directory_iterator it(done.empty() ? "." : done, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().native();
        if (strcasecmp(name.c_str(), comp.c_str()) == 0) { cand = done + name; found = true; break; }
      }
      if (!found) return path;
    }
    done = cand + (pos <= path.size() ? "/" : "");
  }
  return done;
}
bool free_space(const fs::path& dir, uint64_t* bytes) {
  struct statvfs sv;
  if (statvfs(dir.c_str(), &sv) != 0) return false;
  *bytes = (uint64_t)sv.f_bavail * sv.f_frsize;
  return true;
}
int stricmp(const char* a, const char* b) { return strcasecmp(a, b); }

// ---- time -----------------------------------------------------------------------------------------------------------
LocalTime local_time() {
  const time_t t = time(nullptr);
  struct tm lt{};
  localtime_r(&t, &lt);
  return {lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec};
}
uint64_t tick_ms() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}
void sleep_ms(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// ---- process --------------------------------------------------------------------------------------------------------
void process_setup(bool) {}   // Linux timers are already fine-grained and there is no background power throttling
void set_priority(bool background) {
  // Raising priority needs privileges on Linux; a quiet test game still steps aside.
  if (background) setpriority(PRIO_PROCESS, 0, 5);
}
bool stderr_needs_log_file() {
  struct stat st;
  return isatty(STDERR_FILENO) || fstat(STDERR_FILENO, &st) != 0;
}
std::string stderr_file() {
  struct stat st;
  if (fstat(STDERR_FILENO, &st) != 0 || !S_ISREG(st.st_mode)) return {};
  char buf[PATH_MAX];
  ssize_t n = readlink("/proc/self/fd/2", buf, sizeof buf - 1);
  if (n <= 0) return {};
  buf[n] = 0;
  return buf;
}
void* alloc_ram(size_t bytes) {
  void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return p == MAP_FAILED ? nullptr : p;
}
void terminate_now(int code) { _exit(code); }
void hide_own_windows() {}   // the compositor removes the window the moment the process ends
bool run_with_timeout(void (*fn)(), unsigned ms) {
  auto done = std::make_shared<std::atomic<bool>>(false);
  std::thread([fn, done] { fn(); *done = true; }).detach();
  const uint64_t t0 = tick_ms();
  while (!*done && tick_ms() - t0 < ms) sleep_ms(2);
  return *done;
}

static std::vector<std::string> own_args() {
  std::vector<std::string> out;
  if (FILE* f = fopen("/proc/self/cmdline", "rb")) {
    std::string cur;
    for (int c; (c = fgetc(f)) != EOF;) {
      if (c) cur += (char)c;
      else { out.push_back(cur); cur.clear(); }
    }
    fclose(f);
  }
  return out;
}
static pid_t spawn(const std::string& program, const std::vector<std::string>& args, const char* workdir, bool search_path) {
  std::vector<char*> argv;
  argv.push_back(const_cast<char*>(program.c_str()));
  for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 29))
  if (workdir && *workdir) posix_spawn_file_actions_addchdir_np(&fa, workdir);
#endif
  pid_t pid = -1;
  const int rc = search_path ? posix_spawnp(&pid, program.c_str(), &fa, nullptr, argv.data(), environ)
                             : posix_spawn(&pid, program.c_str(), &fa, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&fa);
  return rc == 0 ? pid : -1;
}
bool restart_self() {
  std::vector<std::string> args = own_args();
  if (!args.empty()) args.erase(args.begin());
  signal(SIGCHLD, SIG_IGN);   // this process ends right after: never leave a zombie behind
  return spawn(exe_path(), args, nullptr, false) > 0;
}
void open_folder(const fs::path& p) {
  signal(SIGCHLD, SIG_IGN);
  spawn("xdg-open", {p.native()}, nullptr, true);
}
bool launch(const std::string& program, const std::string& args, const std::string& workdir) {
  // "open" semantics: args is one string as on Windows (quotes kept simple: one optional quoted argument or words)
  std::vector<std::string> list;
  std::string cur;
  bool quoted = false;
  for (char c : args) {
    if (c == '"') { quoted = !quoted; continue; }
    if (c == ' ' && !quoted) { if (!cur.empty()) list.push_back(cur); cur.clear(); continue; }
    cur += c;
  }
  if (!cur.empty()) list.push_back(cur);
  signal(SIGCHLD, SIG_IGN);
  return spawn(program, list, workdir.c_str(), true) > 0;
}
int run_and_wait(const fs::path& program, const std::vector<std::string>& args, const volatile bool* cancel) {
  signal(SIGCHLD, SIG_DFL);
  const pid_t pid = spawn(program.native(), args, nullptr, true);
  if (pid <= 0) return -1;
  int status = 0;
  for (;;) {
    const pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid) break;
    if (r < 0 && errno != EINTR) return -1;
    if (cancel && *cancel) kill(pid, SIGTERM);
    sleep_ms(200);
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void (*g_crash_report)() = nullptr;
static void crash_signal(int sig, siginfo_t* si, void* uctx) {
  // not async-signal-safe by the book (stdio), but the process is about to die and the report is what matters
  const ucontext_t* uc = (const ucontext_t*)uctx;
  void* pc = nullptr;
#if defined(__x86_64__)
  pc = (void*)uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__aarch64__)
  pc = (void*)uc->uc_mcontext.pc;
#endif
  fprintf(stderr, "\n[crash] host signal %d (%s) at %p\n", sig, strsignal(sig), pc);
  Dl_info di{};
  if (pc && dladdr(pc, &di) && di.dli_fname)
    fprintf(stderr, "        in %s (+%llx)%s%s\n", di.dli_fname, (unsigned long long)((char*)pc - (char*)di.dli_fbase), di.dli_sname ? " " : "", di.dli_sname ? di.dli_sname : "");
  if (sig == SIGSEGV || sig == SIGBUS) fprintf(stderr, "        invalid access to address %p\n", si->si_addr);
  if (g_crash_report) g_crash_report();
  fflush(stderr);
  signal(sig, SIG_DFL);   // the default action (core dump) when the faulting instruction runs again
}
void install_crash_handler(void (*report)()) {
  g_crash_report = report;
  // its own stack: a guest stack overflow lands on a fiber's guard page with no room left to run a handler
  static std::vector<char> alt(256 * 1024);
  stack_t ss{};
  ss.ss_sp = alt.data();
  ss.ss_size = alt.size();
  sigaltstack(&ss, nullptr);
  struct sigaction sa{};
  sa.sa_sigaction = crash_signal;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) sigaction(sig, &sa, nullptr);
}

const char* ui_font(bool bold) {
  static std::string found[2];
  static bool looked[2];
  std::string& out = found[bold ? 1 : 0];
  if (looked[bold ? 1 : 0]) return out.c_str();
  looked[bold ? 1 : 0] = true;
  // fontconfig knows where this distribution keeps its fonts; .ttf / .otf only (FreeType reads both)
  if (FILE* p = popen(bold ? "fc-match -f '%{file}' 'sans-serif:bold' 2>/dev/null" : "fc-match -f '%{file}' 'sans-serif' 2>/dev/null", "r")) {
    char buf[PATH_MAX] = {};
    const size_t n = fread(buf, 1, sizeof buf - 1, p);
    pclose(p);
    std::string f(buf, n);
    const std::string ext = f.size() > 4 ? f.substr(f.size() - 4) : "";
    if ((ext == ".ttf" || ext == ".otf") && exists(f)) out = f;
  }
  if (out.empty()) {
    static const char* const regular[] = {"/usr/share/fonts/google-noto/NotoSans-Regular.ttf", "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
                                          "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf", "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
                                          "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
                                          "/usr/share/fonts/TTF/DejaVuSans.ttf"};
    static const char* const heavy[] = {"/usr/share/fonts/google-noto/NotoSans-Bold.ttf", "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
                                        "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Bold.ttf", "/usr/share/fonts/liberation-sans/LiberationSans-Bold.ttf",
                                        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
                                        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"};
    for (const char* f : bold ? heavy : regular)
      if (exists(f)) { out = f; break; }
  }
  return out.c_str();
}

// ---- stacks -----------------------------------------------------------------------------------------------------------
namespace {
struct PosixFiber {
  ucontext_t ctx{};
  void* map = nullptr;
  size_t map_size = 0;
  void (*fn)(void*) = nullptr;
  void* arg = nullptr;
};
PosixFiber* g_running = nullptr;   // fibers all live on the game thread

// A stack with an inaccessible page below it. Pages are committed by the kernel as they are first touched, so a
// large size only costs address space.
bool map_stack(size_t bytes, void** map, size_t* map_size, void** base) {
  const size_t page = (size_t)sysconf(_SC_PAGESIZE);
  bytes = (bytes + page - 1) & ~(page - 1);
  void* m = mmap(nullptr, bytes + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_STACK, -1, 0);
  if (m == MAP_FAILED) return false;
  mprotect(m, page, PROT_NONE);
  *map = m;
  *map_size = bytes + page;
  *base = (char*)m + page;
  return true;
}
void fiber_trampoline(unsigned lo, unsigned hi) {
  PosixFiber* f = (PosixFiber*)(((uintptr_t)hi << 32) | (uintptr_t)lo);
  f->fn(f->arg);
  fprintf(stderr, "[plat] a fiber function returned\n");
  abort();
}

struct BigCall { int (*fn)(int, char**); int argc; char** argv; int result; };
void big_trampoline(unsigned lo, unsigned hi) {
  BigCall* c = (BigCall*)(((uintptr_t)hi << 32) | (uintptr_t)lo);
  c->result = c->fn(c->argc, c->argv);
}
}  // namespace

int run_on_big_stack(int (*fn)(int, char**), int argc, char** argv) {
  constexpr size_t kStack = 256u << 20;   // what the Windows build asks its linker for (/STACK:268435456)
  void *map, *base;
  size_t map_size;
  if (!map_stack(kStack, &map, &map_size, &base)) {
    fprintf(stderr, "[plat] could not reserve the %zu MiB game stack, running on the default one\n", kStack >> 20);
    return fn(argc, argv);
  }
  static ucontext_t back, big;
  BigCall call{fn, argc, argv, 0};
  getcontext(&big);
  big.uc_stack.ss_sp = base;
  big.uc_stack.ss_size = kStack;
  big.uc_link = &back;
  const uintptr_t p = (uintptr_t)&call;
  makecontext(&big, (void (*)())big_trampoline, 2, (unsigned)p, (unsigned)(p >> 32));
  swapcontext(&back, &big);
  munmap(map, map_size);
  return call.result;
}

Fiber fiber_from_thread() {
  PosixFiber* f = new PosixFiber();
  g_running = f;   // its context is filled in by the first switch away from it
  return f;
}
Fiber fiber_create(size_t stack_bytes, void (*fn)(void*), void* arg) {
  PosixFiber* f = new PosixFiber();
  void* base;
  if (!map_stack(stack_bytes, &f->map, &f->map_size, &base)) { delete f; return nullptr; }
  f->fn = fn;
  f->arg = arg;
  getcontext(&f->ctx);
  f->ctx.uc_stack.ss_sp = base;
  f->ctx.uc_stack.ss_size = f->map_size - ((char*)base - (char*)f->map);
  f->ctx.uc_link = nullptr;
  const uintptr_t p = (uintptr_t)f;
  makecontext(&f->ctx, (void (*)())fiber_trampoline, 2, (unsigned)p, (unsigned)(p >> 32));
  return f;
}
void fiber_switch(Fiber to) {
  PosixFiber* me = g_running;
  PosixFiber* t = (PosixFiber*)to;
  g_running = t;
  swapcontext(&me->ctx, &t->ctx);
}
void fiber_delete(Fiber h) {
  PosixFiber* f = (PosixFiber*)h;
  if (!f) return;
  if (f->map) munmap(f->map, f->map_size);
  delete f;
}

}  // namespace plat
#endif  // !_WIN32
