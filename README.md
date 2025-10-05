# libretro-java-launcher

Launch Java games directly from [RetroArch](http://www.libretro.com/).

## Installation

1. Compile the core
    ``` bash
    git clone https://github.com/DodoLeDev/libretro-java-launcher.git
    cd libretro-java-launcher
    make
    ```

2. Copy the core file to the RetroArch cores directory
    ``` bash
    cp java_launcher_libretro.so /usr/lib/libretro/
    cp java_launcher_libretro.info /usr/share/libretro/info/
    ```

3. Make sure [Java](https://openjdk.org/) [is installed](https://pkgs.org/search/?q=openjdk%20jre). You should be able to run at least this command:
    ``` bash
    java
    ```

## Usage

1. Scan JAR-packaged games in RetroArch

2. Launch the games directly from the RetroArch menu

3. Alternatively, you can run games through the command line
    ``` bash
    retroarch -L java_launcher_libretro.so Mindustry.jar
    ```

## Contributors

- [DodoLeDev](https://github.com/DodoLeDev)
- [Rob Loach](http://github.com/robloach) _(original project)_
- [Alcaro](https://github.com/Alcaro) _(original project)_