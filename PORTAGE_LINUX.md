# Portage Linux natif de ShadowRecomp

Objectif : un exécutable Linux natif (sans Wine ni Proton) qui affiche le jeu, joue le son et répond à la
manette et au clavier, en gardant la version Windows intacte.

Machine cible de test : Nobara (base Fedora), RTX 4060 (pilote NVIDIA propriétaire), session Wayland.

## 1. Analyse

### Ce que contient le projet

| Élément | Constat |
| --- | --- |
| Build | CMake + Ninja, clang obligatoire (drapeaux `-gcodeview`, `/STACK`, `/ENTRY` passés à clang, cible MSVC). Lancé par `build.cmd` / `build_env.cmd` (vcvars64). |
| Code du jeu | `generated/` : ~34 000 fonctions C++ produites par `tools/gen.py` à partir du `main.dol` **de votre disque**. Ce dossier n'est pas dans le dépôt (et ne doit pas y être). |
| Runtime | `runtime/` : ~24 000 lignes écrites à la main (matériel GameCube émulé, hooks, menus). |
| Rendu / fenêtre / entrées | Aurora (`third_party/aurora`) : GX → WebGPU (Dawn), fenêtre et manettes par SDL3, interface par Dear ImGui. |
| Son | `runtime/audio.cpp` : déjà en SDL3 (`SDL_AudioStream`). Pas de XAudio. |
| Entrées | `runtime/input.cpp`, `controls.cpp` : déjà en SDL3 (gamepad + clavier + souris). Pas de XInput. |
| Outils | Python (`tools/gen.py`, `tools/game_import/gameimport.py`) : portables tels quels. |

### Prise en charge Linux déjà présente

- **Aurora** est multi-plateforme : son CMake sait télécharger un Dawn précompilé `linux-x86_64`, active le
  backend **Vulkan** et le support **Wayland + X11**, et compile SDL3 depuis les sources. Rien à réécrire côté
  rendu, fenêtre, son, manette : c'est réutilisé tel quel.
- Le code généré (`tools/lift.py`) n'utilise que du C++ standard et `__builtin_*` (clang/gcc) : portable.
- **Le runtime, lui, n'a aucune prise en charge Linux** : 26 fichiers incluent `<windows.h>`, aucun `#ifdef`.

### Points bloquants trouvés

| # | Point bloquant | Où |
| --- | --- | --- |
| B1 | `CMakeLists.txt` 100 % Windows : langage `RC`, exécutable `WIN32`, `-gcodeview`, `/STACK:256Mo`, `/ENTRY`, bibliothèques `dwmapi dbghelp winmm`, chemin conda `%USERPROFILE%` | racine |
| B2 | Threads du jeu = **fibres Win32** (`CreateFiberEx`, `SwitchToFiber`, `ConvertThreadToFiber`) | `sched.cpp` |
| B3 | Pile principale de **256 Mo** demandée à l'éditeur de liens (`/STACK`) ; Linux donne 8 Mo | `CMakeLists.txt`, `main.cpp` |
| B4 | Mémoire du jeu par `VirtualAlloc` | `boot.cpp` |
| B5 | Arrêt, plantages, priorités : `TerminateProcess`, `SetUnhandledExceptionFilter`, `EnumWindows`, `CreateThread`, `SetPriorityClass`, `SetProcessInformation`, `timeBeginPeriod`, `GetTickCount64` | `rt_sys.cpp`, `main.cpp` |
| B6 | Dossier de l'exécutable par `GetModuleFileNameA/W` + `MAX_PATH` (≈ 30 endroits) | 20 fichiers |
| B7 | **Antislashs codés en dur** dans les chemins (`"\\saves"`, `"\\mods\\files"`, `"\\sys\\main.dol"`…). Sous Linux `\` est un caractère de nom de fichier, pas un séparateur | ≈ 20 fichiers |
| B8 | Fichiers : `GetFileAttributesA/ExA`, `CreateDirectoryA`, `CopyFileA`, `MoveFileExA`, `FindFirstFileA`, `FILETIME`, `GetLocalTime`, `_wfopen`, `_fseeki64`, `localtime_s`, `_stricmp`, `_byteswap_ulong`, `GetFinalPathNameByHandleA` | divers |
| B9 | `wchar_t` supposé = type natif des chemins (`path.native()` comparé à `L"..."`, tables indexées par `std::wstring`) | `texture_pack.cpp`, `launcher.cpp` |
| B10 | **Sensibilité à la casse** : le disque GameCube est lu depuis un dossier extrait ; les mods et packs de textures sont cherchés par nom | `disc.cpp` |
| B11 | Fenêtre de premier démarrage (choix du disque) en **Win32 pur** : `CreateWindowExW`, `IFileOpenDialog` (COM), GDI, comctl32 ; SHA-1 par **BCrypt** ; conversion RVZ par `DolphinTool.exe` + `CreateProcessW` | `first_run.cpp` |
| B12 | Redémarrage du jeu (`CreateProcessW` + `GetCommandLineW`), ouverture de dossier (`ShellExecuteW`), lancement du launcher externe (`ShellExecuteA`) | `game_menu.cpp`, `launcher.cpp`, `texture_extract.cpp`, `main.cpp` |
| B13 | Images du launcher intégré lues dans les **ressources Windows** (`FindResourceW`, `shadow.rc`), icône `.ico` | `launcher.cpp`, `shadow.rc` |
| B14 | Bordure de fenêtre DWM, `SetWindowPos(HWND_BOTTOM)` | `gx_lle.cpp` |
| B15 | Profileur par `SuspendThread` + DbgHelp | `profiler.cpp` |
| B16 | Déclaration `__declspec(dllimport) __stdcall GetModuleFileNameA` écrite à la main | `boss_ai.cpp` |
| B17 | Scripts de build `.cmd` uniquement | racine |

Non trouvés (donc rien à faire) : DirectX direct, XAudio, XInput, intrinsèques MSVC (`_mm_*`, `__cpuid`…),
DLL précompilées dans le dépôt, SEH (`__try`).

### Fichier indispensable absent

`generated/` se fabrique avec `tools/gen.py` à partir de `sys/main.dol` de **votre** disque (USA, GUPE8P rév. 0).
Ni le disque ni le `main.dol` ne sont dans le dépôt, et l'environnement où ce portage a été écrit n'y a pas
accès. Conséquences :

- le binaire final du jeu doit être compilé sur votre machine, avec votre disque (`./build.sh`, voir §5) ;
- ici, la compilation et l'édition de liens sont vérifiées avec un `main.dol` **factice** (mêmes adresses, chaque
  fonction réduite à un `blr`) : cela valide tout le runtime, Aurora, Dawn, SDL3 et l'édition de liens, mais pas
  l'exécution du jeu lui-même.

## 2. Solutions retenues

| # | Solution | Pourquoi |
| --- | --- | --- |
| B1, B17 | `CMakeLists.txt` avec une branche `WIN32` (inchangée) et une branche Linux ; `build.sh` qui enchaîne import du disque → `gen.py` → CMake → Ninja | Un seul système de build pour les deux plateformes. |
| Rendu | **Vulkan via Dawn**, par Aurora (déjà là). Pas d'OpenGL. | C'est le chemin déjà utilisé sous Windows (WebGPU) ; Vulkan est le backend Dawn le mieux testé sous Linux et celui que le pilote NVIDIA gère le mieux. |
| Fenêtre, entrées, son | **SDL3** (déjà là), Wayland natif avec repli X11 automatique ; son par PipeWire/PulseAudio/ALSA selon ce que SDL trouve | Rien à écrire ; SDL choisit au lancement. |
| B2 | Fibres → **`ucontext`** (`makecontext` / `swapcontext`), pile de 16 Mo par fibre en `mmap` avec page de garde | Sémantique identique à `SwitchToFiber`, dans la glibc, aucune dépendance. Alternative écartée : un thread système par thread du jeu + verrou (plus lourd, change l'ordonnancement). |
| B3 | Le `main` Linux relance le vrai `main` sur une pile `mmap` de 256 Mo, toujours sur le thread principal | Garde SDL/Wayland sur le thread principal. Alternative écartée : `setrlimit` + ré-exécution (dépend du shell). |
| B4–B8, B12 | Nouvelle couche **`runtime/platform.h`** + `platform_win32.cpp` + `platform_posix.cpp`. Le runtime n'appelle plus Win32 directement pour ces services ; l'implémentation Windows appelle exactement les mêmes API qu'avant. | Isole chaque plateforme dans un fichier, au lieu de semer des `#ifdef` partout. |
| B7 | Tous les chemins construits avec `/` | `/` est accepté par toutes les API fichier de Windows : un seul code pour les deux. |
| B9 | Passage par `std::filesystem` générique (`generic_u8string`) au lieu de `native()` large | Portable, même comportement sous Windows. |
| B10 | `plat::resolve_nocase()` : si un fichier du disque / d'un mod n'existe pas avec la casse demandée, recherche insensible à la casse, composant par composant | Coût nul quand la casse est bonne ; évite d'exiger un système de fichiers insensible à la casse. |
| B11 | La logique d'import (vérification SHA-1, décompactage) est partagée. Sous Linux : SHA-1 portable intégré, sélecteur de fichiers **natif via SDL3** (portail XDG → boîte GNOME/KDE, repli zenity), petite fenêtre SDL de progression, `dolphin-tool` cherché dans le `PATH`. Le code Win32 reste tel quel sous `#ifdef _WIN32`. | SDL3 est déjà lié : aucune dépendance GTK/Qt. |
| B13 | Sous Linux, CMake convertit `emblem.png` / `hero.png` en tableaux C compilés dans le binaire ; `plat::resource()` les rend. Icône : inutile (pas de ressource d'icône ELF). | Binaire autonome comme sous Windows. |
| B14 | `#ifdef _WIN32` autour du bloc DWM / HWND | Purement cosmétique, propre à Windows. |
| B15 | Profileur compilé seulement sous Windows ; sous Linux un message renvoie vers `perf` | Outil de développement, non nécessaire pour jouer. |
| B16 | Remplacé par `plat::exe_dir()` | — |
| Emplacement des données | Comme sous Windows : **à côté de l'exécutable** (`Shadow/`, `saves/`, `logs/`, `shadow_settings.ini`, `mods/`, `textures/`) | Le plus simple et identique à la version Windows (les outils du projet s'y attendent). Passer à `~/.local/share` (XDG) reste possible plus tard. |

## 3. Ordre des travaux

- [ ] 1. Couche plateforme : `platform.h`, `platform_win32.cpp`, `platform_posix.cpp`
- [ ] 2. Cœur : `sched.cpp` (fibres), `boot.cpp` (mémoire), `rt_sys.cpp` (arrêt, plantages), `main.cpp` (pile, journal, chemins)
- [ ] 3. Chemins et fichiers dans le reste du runtime (B6, B7, B8, B9, B16)
- [ ] 4. Disque virtuel : séparateurs, mods, casse (B10)
- [ ] 5. Premier démarrage sous Linux (B11)
- [ ] 6. Menus : redémarrage, ouverture de dossiers, ressources du launcher (B12, B13)
- [ ] 7. `gx_lle.cpp` (B14), `profiler.cpp` (B15)
- [ ] 8. `CMakeLists.txt` multi-plateforme + `build.sh` (B1, B17)
- [ ] 9. Compilation Linux complète (runtime + Aurora + Dawn + SDL3) et édition de liens avec un `main.dol` factice
- [ ] 10. Essais de démarrage : sans disque, avec faux disque, fenêtre + rendu Vulkan logiciel
- [ ] 11. Vérification que le code Windows compile toujours (analyse syntaxique avec les en-têtes mingw-w64)
- [ ] 12. README : section Linux

## 4. Modifications du code du jeu recompilé

Aucune prévue : `generated/` et `tools/gen.py` / `tools/lift.py` ne sont pas touchés.

## 5. Compiler et lancer

(rempli à la fin)

## 6. État des vérifications

(rempli à la fin)
