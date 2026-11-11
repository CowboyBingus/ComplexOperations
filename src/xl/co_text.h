/* Text that changes while it is shown: the copy counts in a complex mission's grouped HUD rows. HellForge serves mod
 * text from the engine API's text slot and has no call for text that changes; its function asks the engine's for every
 * id that is not a mod's. So the mod hooks the engine's lookup behind it (helldivers2.exe 0x321C40, which the slot's
 * own function jumps to): HellForge's slot, its texts and their order stay as they are, and only the ids named at the
 * install, which the engine has no text for, are answered here. */
#ifndef CO_TEXT_H
#define CO_TEXT_H

#include "co.h"

/* NULL when the slot's function and the lookup are the engine's code this was made for. Reads only. */
const char* co_text_check(uintptr_t exe);
/* Hooks the lookup for the ids first .. first + count - 1. Refused, with nothing hooked, when the code is another's or
 * the engine has a text of its own at one of the ids. NULL, or why. */
const char* co_text_install(uintptr_t exe, uint32_t first, uint32_t count);

/* Who answers the ids (NULL from it: not one it shows now, the engine's answer stands). Set at any time. */
typedef const char* (*CoTextFn)(uint32_t id);
CoTextFn* co_text_dynamic(void);
/* The engine's own text for an id in the game's language, in the loader's form; NULL until the install. */
typedef const char* (*CoEngineTextFn)(uint32_t id, void* a, void* b, void* c);
CoEngineTextFn co_text_engine(void);

#endif
