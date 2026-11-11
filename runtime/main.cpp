#include "sys.h"
#include "sched.h"
#include "hw.h"
#include "gx_lle.h"
#include "input.h"
#include "settings_ui.h"
#include "first_run.h"
#include "platform.h"
#include <cstring>
#include <cstdlib>
#include <string>
#include <cstdio>

extern void f_80003154(Cpu&);

double g_base_scale = 1.0;
bool g_quiet = false;
int g_boot_stage = 0;
bool g_play_stage = false;
bool g_boot_story = false;

// The extracted disc (folder with sys\main.dol): --disc wins; otherwise look next to / above the exe, so a plain
// double-click works from any working directory (play\shadow.exe finds ..\..\Shadow).
static std::string find_disc() {
  const std::string dir = plat::exe_dir();
  const std::string cands[] = {dir + "/Shadow", dir + "/../Shadow", dir + "/../../Shadow", "../Shadow"};
  for (auto& c : cands)
    if (plat::exists(c + "/sys/main.dol")) return c;
  return "../Shadow";
}

extern std::string g_disc_root;   // disc.cpp

static int game_main(int argc, char** argv) {
  // #151 (Windows): no power / timer throttling when tabbed out. TEST_ALLOW_THROTTLE: tests, A/B. (platform_win32.cpp)
  plat::process_setup(getenv("TEST_ALLOW_THROTTLE") != nullptr);
  std::string root = find_disc();
  bool no_launcher = getenv("SHADOW_NO_LAUNCHER") != nullptr;
  bool builtin_launcher = false;   // --builtin-launcher: the old in-window launcher (launcher.cpp)
  bool with_launcher = false;      // --with-launcher: also open the launcher window (ShadowLauncher.exe / .pyw) as before
  settings::load();   // shadow_settings.ini; command-line flags below override it
  gxlle::g_render_height = settings::g.height;
  gxlle::g_widescreen = settings::g.widescreen;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--disc") && i + 1 < argc) root = argv[++i];
    else if (!strcmp(argv[i], "--trace-mmio")) g_trace_mmio = true;
    else if (!strcmp(argv[i], "--watch")) sys_set_watch(true);
    else if (!strcmp(argv[i], "--trace-sched")) g_trace_sched = true;
    else if (!strcmp(argv[i], "--trace-dsp")) hw::g_trace_dsp = true;
    else if (!strcmp(argv[i], "--trace-audio")) hw::g_audio_trace = true;
    else if (!strcmp(argv[i], "--trace-gx")) gxlle::g_trace = true;
    else if (!strcmp(argv[i], "--timescale") && i + 1 < argc) g_base_scale = atof(argv[++i]);
    else if (!strcmp(argv[i], "--trace-disc")) g_trace_disc = true;
    else if (!strcmp(argv[i], "--skip-movies")) g_skip_movies = true;
    else if (!strcmp(argv[i], "--mash")) { g_mash = true; g_skip_movies = true; }
    else if (!strcmp(argv[i], "--fast")) { g_mash = true; g_skip_movies = !getenv("TEST_PLAY_MOVIES"); }   // boot -> NEW GAME -> stage load, movies skipped
    else if (!strcmp(argv[i], "--watch-ea") && i + 1 < argc) g_watch_ea = (uint32_t)strtoul(argv[++i], nullptr, 16) & RT_RAM_MASK;
    else if (!strcmp(argv[i], "--press") && i + 1 < argc) input_add_script(argv[++i]);
    else if (!strcmp(argv[i], "--scale") && i + 1 < argc) settings::g.height = gxlle::g_render_height = 480 * atoi(argv[++i]);
    else if (!strcmp(argv[i], "--height") && i + 1 < argc) settings::g.height = gxlle::g_render_height = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--quiet")) g_quiet = true;
    else if (!strcmp(argv[i], "--no-launcher")) no_launcher = true;
    else if (!strcmp(argv[i], "--builtin-launcher")) builtin_launcher = true;
    else if (!strcmp(argv[i], "--with-launcher")) with_launcher = true;
    else if (!strcmp(argv[i], "--hdr")) settings::g.hdr = true;   // testing (not saved)
    else if (!strcmp(argv[i], "--stage") && i + 1 < argc) { g_boot_stage = atoi(argv[++i]); g_mash = true; g_skip_movies = !getenv("TEST_PLAY_MOVIES"); }   // TEST_PLAY_MOVIES: boot movies play (#149 skip test)
    // the launcher's Free Mode / Continue: stage N started the way Select Mode starts it, for players - boot screens
    // auto-confirmed only until the start (as --story), movies normal, memory card writes ON (--stage blocks them)
    else if (!strcmp(argv[i], "--play-stage") && i + 1 < argc) { g_boot_stage = atoi(argv[++i]); g_play_stage = true; g_mash = true; }
    else if (!strcmp(argv[i], "--widescreen")) settings::g.widescreen = gxlle::g_widescreen = true;
    else if (!strcmp(argv[i], "--story")) { g_boot_story = true; g_mash = true; }   // the launcher's Story button: continue the save's story
  }
  if (getenv("TEST_STORY")) { g_boot_story = true; g_boot_stage = 0; g_mash = true; }   // testing: wsassert's --stage copy, booted as --story
  g_time_scale = g_base_scale;
  // When run from a console/launcher without redirection, send the log to <exe dir>\logs\shadow-<time>.log so a
  // crash report survives the window closing.
  // Also when started by double-click: a Windows-subsystem program then has no stderr at all.
  if (plat::stderr_needs_log_file()) {
    const std::string dir = plat::exe_dir() + "/logs";
    plat::make_dir(dir);
    const plat::LocalTime t = plat::local_time();
    char path[1024];
    snprintf(path, sizeof path, "%s/shadow-%04d%02d%02d-%02d%02d%02d.log", dir.c_str(), t.year, t.month, t.day, t.hour, t.minute, t.second);
    fprintf(stderr, "logging to %s\n", path);
    if (!freopen(path, "w", stderr)) fprintf(stdout, "could not open log %s\n", path);
  }
  // #164 (user: "tabbing out still causes massive lag and audio stutters"): reproduced only with something else busy -
  // a background test game (focus_test.py: 125 fps focused -> 88 tabbed out, audio under-runs). Once the window is not
  // in front, Windows favours the foreground app and our game competes as an equal with every other busy process. The
  // player's game now runs above normal priority, and quiet test games below normal, so they never slow real play.
  // TEST_NORMAL_PRIORITY=1: both normal (A/B).
  if (!getenv("TEST_NORMAL_PRIORITY"))
    plat::set_priority(g_quiet);
  // The launcher (launcher.cpp): the window opens on it and the game boots when Play is pressed. Tests and tools
  // (--stage, --fast, --quiet, --no-launcher) go straight into the game.
  const bool plain_start = !no_launcher && !g_quiet && !g_mash && g_boot_stage == 0 && !g_boot_story;
  if (getenv("SHADOW_FORCE_LAUNCHER") || (plain_start && builtin_launcher)) {   // FORCE: tests (quiet window)
    g_disc_root = root;
    launcher::run();
  } else if (plain_start && with_launcher) {
    // #131 (user, 2026-10-20: "scrap launcher and move to the in-game one only"): started directly, the game boots and
    // opens no launcher; settings are in the in-game menu. --with-launcher opens the old launcher window next to it.
    // the launcher is its own window (launcher\shadow_launcher.pyw, Python + Tk): it edits shadow_settings.ini live while
    // the game runs. Opened next to the game when the game is started directly; a second copy just raises the first.
    const std::string dir = plat::exe_dir();
    // the launcher: ShadowLauncher.exe (tools/launcher_web, one exe, nothing to install) beside the game or in launcher/,
    // else the Python ones (forge_launcher.pyw, then shadow_launcher.pyw). --from-game: an open launcher stays the only one.
#ifdef _WIN32
    const char* const kLauncherExe = "ShadowLauncher.exe";
    const char* const kPython = "pythonw";
#else
    const char* const kLauncherExe = "ShadowLauncher";
    const char* const kPython = "python3";
#endif
    std::string launcher_exe = dir + "/" + kLauncherExe;
    if (!plat::exists(launcher_exe)) launcher_exe = dir + "/launcher/" + kLauncherExe;
    std::string script = dir + "/launcher/forge_launcher.pyw";
    if (!plat::exists(script)) script = dir + "/launcher/shadow_launcher.pyw";
    if (plat::exists(launcher_exe)) {
      plat::launch(launcher_exe, "--from-game", dir);
    } else if (plat::exists(script)) {
      plat::launch(kPython, "\"" + script + "\"", dir);
    }
  }
  // first start without a disc (release folder): ask for the player's own disc, check it, unpack to <exe>\Shadow
  if (!first_run::disc_present(root) && (root = first_run::run(g_quiet)).empty()) return 0;
  extern void profiler_start_if_requested();
  profiler_start_if_requested();
  if (!boot_init(root)) return 1;
  static Cpu cpu;
  memset(&cpu, 0, sizeof cpu);
  cpu.msr = 0x2030;
  cpu.budget = 4000;
  cpu.lr = 0;
  sched_init(cpu);
  sys_install_crash_handlers();
  logf("[boot] starting guest at 80003154\n");
  f_80003154(cpu);
  logf("[boot] guest returned\n");
  return 0;
}

// Windows: the linker gives the main thread its 256 MiB stack. Elsewhere the game runs on a stack of that size made
// here, still on the main thread (platform_posix.cpp).
int main(int argc, char** argv) { return plat::run_on_big_stack(game_main, argc, argv); }
