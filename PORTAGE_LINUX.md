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

- [x] 1. Couche plateforme : `platform.h`, `platform_win32.cpp`, `platform_posix.cpp`
- [x] 2. Cœur : `sched.cpp` (fibres), `boot.cpp` (mémoire), `rt_sys.cpp` (arrêt, plantages), `main.cpp` (pile, journal, chemins)
- [x] 3. Chemins et fichiers dans le reste du runtime (B6, B7, B8, B9, B16)
- [x] 4. Disque virtuel : séparateurs, mods, casse (B10)
- [x] 5. Premier démarrage sous Linux (B11)
- [x] 6. Menus : redémarrage, ouverture de dossiers, ressources du launcher (B12, B13)
- [x] 7. `gx_lle.cpp` (B14), `profiler.cpp` (B15)
- [x] 8. `CMakeLists.txt` multi-plateforme + `build.sh` (B1, B17)
- [x] 9. Compilation Linux complète (runtime + Aurora + Dawn + SDL3) et édition de liens avec un `main.dol` factice
- [x] 10. Essais de démarrage : sans disque, avec faux disque, fenêtre + rendu Vulkan logiciel
- [x] 11. Vérification que le code Windows compile toujours (analyse syntaxique avec les en-têtes mingw-w64)
- [x] 12. README : section Linux

Points apparus pendant l'exécution (hors plan initial) :

- [x] `runtime/sched.h` masquait l'en-tête système `<sched.h>` (inclus par `<pthread.h>`) : sous Linux les dossiers
  `runtime/` et `generated/` sont passés en `-iquote` au lieu de `-I` (aucun renommage, Windows inchangé).
- [x] Polices des menus : les menus nomment `C:/Windows/Fonts/...`. Sous Linux, repli sur la police sans-serif du
  système trouvée par `fc-match` (`plat::ui_font`).
- [x] `tools/game_import/gameimport.py` : cherche aussi `dolphin-tool` (nom du binaire sous Linux) pour les `.rvz`.
- [x] `.gitignore` (liste blanche) : `build.sh` et `PORTAGE_LINUX.md` autorisés.

Deuxième passe (outils autour du jeu) :

- [x] Éditeur de niveaux `tools/stage_editor/server.py` : nom du binaire (`shadow` au lieu de `shadow.exe`), plus de
  liste de DLL à copier sous Linux, lancement du jeu de test sans `STARTUPINFO`, « révéler le fichier » par `xdg-open`,
  ouverture dans le navigateur par défaut (au lieu d'Edge), dossier `saves` absent toléré.
- [x] `tools/audio/adx.py` : sous Linux, décodage des musiques par le `ffmpeg` du système (Windows : Media Foundation
  puis ffmpeg portable, inchangé).
- [x] `tools/linux/install-desktop-entry.sh` : entrée dans le menu d'applications du bureau.
- Rien à faire : `tools/gen.py`, `lift.py`, `dol.py`, `one_archive.py`, `texture_dump.py`, `tools/menu`, `tools/tilesets`
  (scripts Blender) sont du Python portable ; les noms de fichiers du disque qu'ils utilisent ont déjà la bonne casse
  (vérifié contre le manifeste du disque). Les launchers externes (`ShadowLauncher`, `launcher/*.pyw`) ne sont pas
  dans le dépôt : rien à porter.

## 4. Modifications du code du jeu recompilé

Aucune. `generated/`, `tools/gen.py` et `tools/lift.py` ne sont pas touchés ; seuls `runtime/` (couche plateforme),
`CMakeLists.txt`, `build.sh` et une ligne de `gameimport.py` changent.

Deux corrections de chemins au passage, dans des lignes qu'il fallait de toute façon réécrire : `boss_ai.cpp` et
`stage_workshop.cpp` contenaient `"\mods\boss_arena.txt"` et `find_last_of("\/")` écrits avec un seul antislash
(donc `\b` = retour arrière) ; ils passent maintenant par `plat::exe_dir()`.

## 5. Compiler et lancer (Nobara / Fedora)

Dépendances :

```bash
sudo dnf install clang gcc-c++ cmake ninja-build python3 git pkgconf-pkg-config fontconfig libpng-devel freetype-devel sqlite-devel zlib-devel libzstd-devel wayland-devel wayland-protocols-devel libxkbcommon-devel libdecor-devel mesa-libEGL-devel mesa-libGL-devel mesa-libgbm-devel libdrm-devel libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel libXfixes-devel libXScrnSaver-devel libXtst-devel pipewire-devel pulseaudio-libs-devel alsa-lib-devel dbus-devel systemd-devel libusb1-devel vulkan-loader
```

Première compilation, avec votre disque (USA, GUPE8P ; `.iso`, `.gcm`, ou dossier extrait par Dolphin ; `.rvz` si
`dolphin-tool` est installé) :

```bash
./build.sh /chemin/vers/votre/disque.iso
```

Le script vérifie et décompacte le disque dans `../Shadow` (à côté du dossier du projet), génère `generated/`,
compile, puis place le binaire dans `play/`. Lancer le jeu :

```bash
./play/shadow
```

Recompiler plus tard :

```bash
./build.sh
```

Menu d'applications du bureau (facultatif) :

```bash
tools/linux/install-desktop-entry.sh
```

Éditeur de niveaux (en cours de développement en amont) :

```bash
sudo dnf install python3-numpy python3-pillow ffmpeg
```

```bash
python3 tools/stage_editor/server.py --ui2
```

Utile :

- `./play/shadow --builtin-launcher` : ouvre le launcher intégré au lieu de démarrer directement le jeu.
- Lancé depuis un terminal, le journal va dans `play/logs/shadow-<date>.log` ; `./play/shadow 2>journal.txt` pour
  choisir le fichier.
- Si la fenêtre pose problème sous Wayland : `SDL_VIDEODRIVER=x11 ./play/shadow` (XWayland).
- Sauvegardes : `play/saves/`, réglages : `play/shadow_settings.ini`.

## 6. État des vérifications

Environnement de vérification : conteneur Ubuntu 24.04, clang 18, sans GPU, sans carte son, sans manette, **sans le
disque du jeu**.

Vérifié ici :

- Compilation Linux complète sans erreur : les 64 fichiers de `runtime/`, Aurora, SDL3 3.4.10 (Wayland + X11),
  Dawn précompilé, puis édition de liens du binaire `shadow` (ELF x86-64) — avec un `generated/` produit par
  `tools/gen.py` à partir d'un `main.dol` **factice** (34 026 fonctions aux bonnes adresses, corps réduits à `blr`,
  `callhooks.txt` et `dtscale.txt` vidés car ils visent des instructions précises du vrai jeu).
- Démarrage sans disque (`--quiet`) : sortie propre.
- Démarrage avec un faux disque sous Xvfb : lecture du disque, mémoire du jeu, fenêtre SDL, initialisation de Dawn,
  polices, entrée dans le code « du jeu » (qui rend aussitôt la main, étant factice).
- Launcher intégré affiché (capture d'écran) : rendu WebGPU, ImGui, images compilées dans le binaire, polices système.
- Fenêtre de premier démarrage affichée (capture d'écran) ; import d'un faux disque refusé avec le bon message ;
  SHA-1 intégré identique à `hashlib` sur trois vecteurs.
- Couche plateforme (test unitaire) : fibres `ucontext`, `setjmp/longjmp` dans une fibre, récursion de ~200 Mo sur la
  grande pile, création/destruction de 2 000 fibres, recherche insensible à la casse, horodatages, rapport de plantage
  sur `SIGSEGV` depuis une fibre.
- Le code Windows compile toujours : analyse `clang --target=x86_64-w64-mingw32 -fsyntax-only` sans erreur sur tous
  les fichiers de `runtime/` (dont `platform_win32.cpp` et les parties Win32 de `first_run.cpp`, `profiler.cpp`,
  `gx_lle.cpp`).

Fait mais **non vérifié** (impossible ici) :

- Le jeu lui-même : jamais exécuté sous Linux, faute de `main.dol`. La compilation du vrai `generated/` n'a donc pas
  été faite non plus (le générateur et le code émis sont du C++ portable, mais ce n'est pas une preuve).
- Rendu **Vulkan** sur GPU réel : dans le conteneur, Aurora écarte l'adaptateur Vulkan logiciel et se replie sur
  OpenGL ES logiciel. Vulkan + NVIDIA + Wayland n'ont pas été essayés.
- Son : pas de périphérique audio ici (pilote SDL `dummy`). Le code audio est celui de Windows, inchangé (SDL3).
- Manette et clavier en jeu : code SDL3 inchangé, non essayé.
- Sélecteur de fichiers natif (portail XDG / zenity), glisser-déposer, HDR, plein écran, redémarrage depuis le menu,
  ouverture de dossiers (`xdg-open`), conversion `.rvz` par `dolphin-tool`.
- Compilation avec le clang plus récent de Nobara (essayé ici avec clang 18 seulement).
- Version Windows : analyse syntaxique seulement, pas de compilation MSVC/clang-cl réelle ni d'exécution.

Éditeur de niveaux et outils (deuxième passe) : le serveur démarre sous Linux et sert l'interface (vérifié ici,
sans disque) ; ouverture d'un niveau, construction, test en jeu et conversion de musique **non vérifiés** (il faut le
disque).

Reste à faire :

- Compiler avec le vrai disque et jouer sur la machine cible ; corriger ce que cet essai révélera.
- Profileur (`PROFILE=`) : Windows seulement ; sous Linux, utiliser `perf`.
- Bug de musique signalé en jeu (elle se dérègle après quelques minutes) : cause non trouvée ; la sortie audio de la
  machine est saine d'après les journaux, le défaut est dans le son produit par le jeu.
- Données à côté du binaire (comme sous Windows) ; pas de rangement XDG (`~/.local/share`).
