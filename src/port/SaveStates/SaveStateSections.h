// [port] Force-included into every game source file (CMakeLists.txt): puts the file's variables in
// the save-state sections, .gsd (initialised data) and .gsb (zero / uninitialised), so a save state
// can copy all the game's variables and nothing of the port's (SaveStates.cpp).
#pragma once
#ifdef _MSC_VER
#pragma data_seg(".gsd$m")
#pragma bss_seg(".gsb$m")
#endif
