# Clock ELF app

The Clock example is the second runtime test for CYD App Platform. It is built as a loadable ELF module and embedded into the launcher firmware for now.

The first version prints a seconds counter in `HH:MM:SS` format once per second for ten seconds, then returns control to the launcher. This deliberately tests periodic app work and the app-return/unload lifecycle through the Serial Monitor.

This is an elapsed-time demo, not a wall clock: NTP/time synchronization and the CYD display UI have not been added yet.

From the repository root, with ESP-IDF activated, run:

```sh
./tools/build-elf-prototype.sh
```
