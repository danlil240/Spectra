#pragma once

#ifdef SPECTRA_USE_SDL3

    #include <SDL3/SDL.h>

namespace spectra
{

// Map SDL3 keycode to GLFW-compatible key integer (InputHandler / ShortcutManager use GLFW_KEY_*).
inline int sdl3_to_spectra_key(SDL_Keycode sym)
{
    switch (sym)
    {
        case SDLK_SPACE:
            return 32;
        case SDLK_APOSTROPHE:
            return 39;
        case SDLK_COMMA:
            return 44;
        case SDLK_MINUS:
            return 45;
        case SDLK_PERIOD:
            return 46;
        case SDLK_SLASH:
            return 47;
        case SDLK_0:
            return 48;
        case SDLK_1:
            return 49;
        case SDLK_2:
            return 50;
        case SDLK_3:
            return 51;
        case SDLK_4:
            return 52;
        case SDLK_5:
            return 53;
        case SDLK_6:
            return 54;
        case SDLK_7:
            return 55;
        case SDLK_8:
            return 56;
        case SDLK_9:
            return 57;
        case SDLK_SEMICOLON:
            return 59;
        case SDLK_EQUALS:
            return 61;
        case SDLK_A:
            return 65;
        case SDLK_B:
            return 66;
        case SDLK_C:
            return 67;
        case SDLK_D:
            return 68;
        case SDLK_E:
            return 69;
        case SDLK_F:
            return 70;
        case SDLK_G:
            return 71;
        case SDLK_H:
            return 72;
        case SDLK_I:
            return 73;
        case SDLK_J:
            return 74;
        case SDLK_K:
            return 75;
        case SDLK_L:
            return 76;
        case SDLK_M:
            return 77;
        case SDLK_N:
            return 78;
        case SDLK_O:
            return 79;
        case SDLK_P:
            return 80;
        case SDLK_Q:
            return 81;
        case SDLK_R:
            return 82;
        case SDLK_S:
            return 83;
        case SDLK_T:
            return 84;
        case SDLK_U:
            return 85;
        case SDLK_V:
            return 86;
        case SDLK_W:
            return 87;
        case SDLK_X:
            return 88;
        case SDLK_Y:
            return 89;
        case SDLK_Z:
            return 90;
        case SDLK_LEFTBRACKET:
            return 91;
        case SDLK_BACKSLASH:
            return 92;
        case SDLK_RIGHTBRACKET:
            return 93;
        case SDLK_GRAVE:
            return 96;
        case SDLK_ESCAPE:
            return 256;
        case SDLK_RETURN:
            return 257;
        case SDLK_TAB:
            return 258;
        case SDLK_BACKSPACE:
            return 259;
        case SDLK_INSERT:
            return 260;
        case SDLK_DELETE:
            return 261;
        case SDLK_RIGHT:
            return 262;
        case SDLK_LEFT:
            return 263;
        case SDLK_DOWN:
            return 264;
        case SDLK_UP:
            return 265;
        case SDLK_PAGEUP:
            return 266;
        case SDLK_PAGEDOWN:
            return 267;
        case SDLK_HOME:
            return 268;
        case SDLK_END:
            return 269;
        case SDLK_CAPSLOCK:
            return 280;
        case SDLK_SCROLLLOCK:
            return 281;
        case SDLK_NUMLOCKCLEAR:
            return 282;
        case SDLK_PRINTSCREEN:
            return 283;
        case SDLK_PAUSE:
            return 284;
        case SDLK_F1:
            return 290;
        case SDLK_F2:
            return 291;
        case SDLK_F3:
            return 292;
        case SDLK_F4:
            return 293;
        case SDLK_F5:
            return 294;
        case SDLK_F6:
            return 295;
        case SDLK_F7:
            return 296;
        case SDLK_F8:
            return 297;
        case SDLK_F9:
            return 298;
        case SDLK_F10:
            return 299;
        case SDLK_F11:
            return 300;
        case SDLK_F12:
            return 301;
        case SDLK_LSHIFT:
            return 340;
        case SDLK_LCTRL:
            return 341;
        case SDLK_LALT:
            return 342;
        case SDLK_LGUI:
            return 343;
        case SDLK_RSHIFT:
            return 344;
        case SDLK_RCTRL:
            return 345;
        case SDLK_RALT:
            return 346;
        case SDLK_RGUI:
            return 347;
        default:
            return -1;
    }
}

// Map SDL3 mouse button index to GLFW-compatible 0-based button index.
// SDL3: SDL_BUTTON_LEFT=1, SDL_BUTTON_RIGHT=3, SDL_BUTTON_MIDDLE=2
// GLFW: 0=left, 1=right, 2=middle
inline int sdl3_to_spectra_mouse_button(Uint8 sdl_button)
{
    switch (sdl_button)
    {
        case SDL_BUTTON_LEFT:
            return 0;
        case SDL_BUTTON_RIGHT:
            return 1;
        case SDL_BUTTON_MIDDLE:
            return 2;
        default:
            return static_cast<int>(sdl_button) - 1;
    }
}

// Map SDL3 modifier mask to GLFW-compatible modifier bits.
// GLFW: SHIFT=0x01, CONTROL=0x02, ALT=0x04, SUPER=0x08
inline int sdl3_to_spectra_mods(SDL_Keymod sdl_mods)
{
    int mods = 0;
    if (sdl_mods & SDL_KMOD_SHIFT)
        mods |= 0x01;
    if (sdl_mods & SDL_KMOD_CTRL)
        mods |= 0x02;
    if (sdl_mods & SDL_KMOD_ALT)
        mods |= 0x04;
    if (sdl_mods & SDL_KMOD_GUI)
        mods |= 0x08;
    return mods;
}

}   // namespace spectra

#endif   // SPECTRA_USE_SDL3
