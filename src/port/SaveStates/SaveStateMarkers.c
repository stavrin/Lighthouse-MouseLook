// [port] Markers at the start and end of the save-state sections (SaveStateSections.h). The linker
// orders sections named .gsd$a, .gsd$m, .gsd$z alphabetically within .gsd, so these bracket every
// game variable.
#ifdef _MSC_VER
#pragma data_seg(".gsd$a")
char gSaveStateDataBegin = 1;
#pragma data_seg(".gsd$z")
char gSaveStateDataEnd = 1;
#pragma data_seg()

#pragma bss_seg(".gsb$a")
char gSaveStateBssBegin;
#pragma bss_seg(".gsb$z")
char gSaveStateBssEnd;
#pragma bss_seg()
#else
char gSaveStateDataBegin = 1, gSaveStateDataEnd = 1, gSaveStateBssBegin, gSaveStateBssEnd;
#endif
