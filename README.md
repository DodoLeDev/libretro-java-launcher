# External-Program-Launcher

A [Libretro](http://www.libretro.com/) core that runs content with an external program
(java, wine, dolphin, or anything else).

This project aims to be a program- and os-agnostic version of the [Libretro Dolphin Launcher](github.com/RobLoach/libretro-dolphin-launcher/) (and its fork, [Libretro Script launcher](https://github.com/yzzyx-network/libretro-script-launcher))

## Installation

1. Grab the core from the [releases](https://github.com/DodoLeDev/libretro-external-launcher/releases)
2. Copy the files to the right location (find the path in **Settings > Directory**)
    ``` bash
    cp external_launcher_libretro.so  /path/to/cores/folder      # e.g. /usr/lib/libretro/
    cp java_launcher_libretro.info    /path/to/core_info/folder  # e.g. /usr/share/libretro/info/
    ```

## Build

0. Install the minimal required build tools
    ``` bash
    sudo apt install git make gcc libc-dev  # Ubuntu
    sudo pacman -Syu git make gcc           # Arch Linux
    sudo dnf install git make gcc           # Fedora
    sudo apk add git make gcc musl-dev      # Alpine
    ```
1. Clone the repo
    ``` bash
    git clone https://github.com/DodoLeDev/libretro-java-launcher.git
    ```
2. Build
    ``` bash
    cd libretro-java-launcher
    make
    ```
3. Copy the core files to the right location (find the path in **Settings > Directory**)
    ``` bash
    cp build/external_launcher_libretro.so  /path/to/cores/folder      # e.g. /usr/lib/libretro/
    cp build/java_launcher_libretro.info    /path/to/core_info/folder  # e.g. /usr/share/libretro/info/
    ```


## Usage
> [!NOTE]
> Make sure that the program you want to run are installed and in your `PATH`

### One-time run

1. Load the core from the main menu
2. Open the file

Alternatively, you can use the command line:
``` bash
retroarch -L external_launcher_libretro.so Game.jar
retroarch -L external_launcher_libretro.so Game.exe
```

### Using RetroArch libraries

1. **Manually** scan the folder containing your games in RetroArch (Import Content > Manual Scan)
2. A launch screen appears. Optionally open the Quick Menu to change the game's options
   (see below), then press **Start** to launch the game.

## Configuring how a game runs

RetroArch does not allow to freely edit text in its menu, so the core uses profiles and presets.

Everything in manually configured in a text file by the user, and then selected from the RetroArch menu.

- **Profiles** are designed to define global directives for a kind of games (e.g. _Java games_ or _Windows Games_).
It can be bound to a library definition.
- **Presets** allow to override profile definitions, and can be bound per-game (e.g. setting screen resolution for a specific game).

Per-game choices need "Load Content Specific Core Options Automatically" to be on (the default).

> [!TIP]
> While only one profile can be used when running a game, multiple presets can be applied.
> In this case, the **last definition** is taken into account for each setting.

### The config file

> [!CAUTION]
> The core does not explicitly require a configuration file, and will not create the file by itself.
> <details>
> <summary>When not found the following default config will be used (click to develop):</summary>
>
> ``` ini
> [java]
> extensions = jar
> command = java {args} -jar {file}
>
> [wine]
> extensions = exe
> command = wine {args} {file}
>
> [dolphin]
> extensions = elf|dol|gcm|iso|wbfs|ciso|gcz|wad
> command = dolphin-emu -b -e {file} {args}
>
> [preset:720p]
> width = 1280
> height = 720
>
> [preset:1080p]
> width = 1920
> height = 1080;
> ```
> </details>

The configuration file should be created at `SYSTEM_BIOS_DIRECTORY/external_launcher/profiles.cfg`, and follows the `ini` file structure:

``` ini
[profile_name]
extensions = ext1|ext2
command = program {args} {file}
env = ENV_VAR1=value1
env = ENV_VAR2=value2

[preset:preset_name]
args = --arg1 value1 --arg2 value2
width = 1280
height = 720
```

[A sample file is available to quickstart](./profiles.cfg)

The following options are supported:

| Key               | Meaning                                                                    | Notes |
|-------------------|----------------------------------------------------------------------------|------------------|
| `extensions`      | Extensions handled by a profile, separated by `\|`                         | Only supported in profile definitions |
| `command`         | Shell command. `{file}` (quoted), `{args}`, `{width}`, `{height}` are replaced | Not enforced, but only using it for profiles is recommended |
| `args`            | Value of `{args}`                                                          | Not enforced, but only using it for presets is recommended |
| `env`             | `NAME=value` environment variable; may be repeated                         | All the env options are merged, following the [preset behavior described above](#configuring-how-a-game-runs) |
| `width`, `height` | Values of `{width}` and `{height}`                                         | |

A preset replaces the profile's `command`, `args`, `width` and `height` when it sets them,
and adds its `env` entries.

The core reports the extensions of your profiles to RetroArch. To also have them offered by
the scanner and in core suggestions, add them to `supported_extensions` in the `.info` file.

> [!NOTE]
> The config file is only loaded at boot time. Any modification to this file will require a full restart to be taken into account.

### Usage in RetroArch

The profiles and presets defined in the config file are selectable from inside the RetroArch menu.

- For **libraries**, select the right profile from the library editor menu.
- For **games**, configure the presets (and eventually the profile) from the **Quick Menu > Core options**. You can select up to 3 presets for each game.

## Contributors

- [DodoLeDev](https://github.com/DodoLeDev)

### Acknowledgements

Many thanks to:
- [**Rob Loach**](http://github.com/robloach) for its work on the [original project](https://github.com/RobLoach/libretro-dolphin-launcher)
- [**Alcaro**](https://github.com/Alcaro) for its work on the [original project](https://github.com/RobLoach/libretro-dolphin-launcher)
- [**Yzzyx**](http://github.com/yzzyx-network) for the original work on a [similar project](https://github.com/yzzyx-network/libretro-script-launcher)
