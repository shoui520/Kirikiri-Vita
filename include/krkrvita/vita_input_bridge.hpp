#pragma once

#include <SDL.h>

// Loads the per-game input section selected by -krkrprofile. Defaults remain
// usable when no profile is supplied.
void krkrvita_input_initialize();

// Converts a Vita controller event into the keyboard or mouse event requested
// by the per-game profile. False means the event is intentionally consumed.
bool krkrvita_input_translate(SDL_Event &event);

// Drives the mouse cursor from the left analog stick once per engine frame.
void krkrvita_input_pump(SDL_Window *window, SDL_GameController *controller);
