/*****************************************************************************
 * Copyright (c) 2014-2018 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "KeyboardShortcuts.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <openrct2/PlatformEnvironment.h>
#include <openrct2/common.h>
#include <openrct2/core/Console.hpp>
#include <openrct2/core/File.h>
#include <openrct2/core/FileStream.hpp>
#include <openrct2/core/Path.hpp>
#include <openrct2/core/String.hpp>
#include <openrct2/localisation/Localisation.h>
#include "../../openrct2/config/IniReader.hpp"
#include "../../openrct2/config/IniWriter.hpp"

using namespace OpenRCT2;
using namespace OpenRCT2::Input;

// Remove when the C calls are removed
static KeyboardShortcuts* _instance;

KeyboardShortcuts::KeyboardShortcuts(const std::shared_ptr<IPlatformEnvironment>& env)
    : _env(env)
{
    _instance = this;
}

KeyboardShortcuts::~KeyboardShortcuts()
{
    _instance = nullptr;
}

void KeyboardShortcuts::Reset()
{
    for (size_t i = 0; i < SHORTCUT_COUNT; i++)
    {
        _keys[i] = DefaultKeys[i];
    }
}

static const char* shortcut_names[SHORTCUT_COUNT] =
{
    "SHORTCUT_CLOSE_TOP_MOST_WINDOW",
    "SHORTCUT_CLOSE_ALL_FLOATING_WINDOWS",
    "SHORTCUT_CANCEL_CONSTRUCTION_MODE",
    "SHORTCUT_PAUSE_GAME",
    "SHORTCUT_ZOOM_VIEW_OUT",
    "SHORTCUT_ZOOM_VIEW_IN",
    "SHORTCUT_ROTATE_VIEW_CLOCKWISE",
    "SHORTCUT_ROTATE_VIEW_ANTICLOCKWISE",
    "SHORTCUT_ROTATE_CONSTRUCTION_OBJECT",
    "SHORTCUT_UNDERGROUND_VIEW_TOGGLE",
    "SHORTCUT_REMOVE_BASE_LAND_TOGGLE",
    "SHORTCUT_REMOVE_VERTICAL_LAND_TOGGLE",
    "SHORTCUT_SEE_THROUGH_RIDES_TOGGLE",
    "SHORTCUT_SEE_THROUGH_SCENERY_TOGGLE",
    "SHORTCUT_INVISIBLE_SUPPORTS_TOGGLE",
    "SHORTCUT_INVISIBLE_PEOPLE_TOGGLE",
    "SHORTCUT_HEIGHT_MARKS_ON_LAND_TOGGLE",
    "SHORTCUT_HEIGHT_MARKS_ON_RIDE_TRACKS_TOGGLE",
    "SHORTCUT_HEIGHT_MARKS_ON_PATHS_TOGGLE",
    "SHORTCUT_ADJUST_LAND",
    "SHORTCUT_ADJUST_WATER",
    "SHORTCUT_BUILD_SCENERY",
    "SHORTCUT_BUILD_PATHS",
    "SHORTCUT_BUILD_NEW_RIDE",
    "SHORTCUT_SHOW_FINANCIAL_INFORMATION",
    "SHORTCUT_SHOW_RESEARCH_INFORMATION",
    "SHORTCUT_SHOW_RIDES_LIST",
    "SHORTCUT_SHOW_PARK_INFORMATION",
    "SHORTCUT_SHOW_GUEST_LIST",
    "SHORTCUT_SHOW_STAFF_LIST",
    "SHORTCUT_SHOW_RECENT_MESSAGES",
    "SHORTCUT_SHOW_MAP",
    "SHORTCUT_SCREENSHOT",

    // New
    "SHORTCUT_REDUCE_GAME_SPEED",
    "SHORTCUT_INCREASE_GAME_SPEED",
    "SHORTCUT_OPEN_CHEAT_WINDOW",
    "SHORTCUT_REMOVE_TOP_BOTTOM_TOOLBAR_TOGGLE",
    "SHORTCUT_SCROLL_MAP_UP",
    "SHORTCUT_SCROLL_MAP_LEFT",
    "SHORTCUT_SCROLL_MAP_DOWN",
    "SHORTCUT_SCROLL_MAP_RIGHT",
    "SHORTCUT_OPEN_CHAT_WINDOW",
    "SHORTCUT_QUICK_SAVE_GAME",
    "SHORTCUT_SHOW_OPTIONS",
    "SHORTCUT_MUTE_SOUND",
    "SHORTCUT_WINDOWED_MODE_TOGGLE",
    "SHORTCUT_SHOW_MULTIPLAYER",
    "SHORTCUT_PAINT_ORIGINAL_TOGGLE",
    "SHORTCUT_DEBUG_PAINT_TOGGLE",
    "SHORTCUT_SEE_THROUGH_PATHS_TOGGLE",
    "SHORTCUT_RIDE_CONSTRUCTION_TURN_LEFT",
    "SHORTCUT_RIDE_CONSTRUCTION_TURN_RIGHT",
    "SHORTCUT_RIDE_CONSTRUCTION_USE_TRACK_DEFAULT",
    "SHORTCUT_RIDE_CONSTRUCTION_SLOPE_DOWN",
    "SHORTCUT_RIDE_CONSTRUCTION_SLOPE_UP",
    "SHORTCUT_RIDE_CONSTRUCTION_CHAIN_LIFT_TOGGLE",
    "SHORTCUT_RIDE_CONSTRUCTION_BANK_LEFT",
    "SHORTCUT_RIDE_CONSTRUCTION_BANK_RIGHT",
    "SHORTCUT_RIDE_CONSTRUCTION_PREVIOUS_TRACK",
    "SHORTCUT_RIDE_CONSTRUCTION_NEXT_TRACK",
    "SHORTCUT_RIDE_CONSTRUCTION_BUILD_CURRENT",
    "SHORTCUT_RIDE_CONSTRUCTION_DEMOLISH_CURRENT",
    "SHORTCUT_LOAD_GAME",
    "SHORTCUT_CLEAR_SCENERY",
    "SHORTCUT_GRIDLINES_DISPLAY_TOGGLE",
    "SHORTCUT_VIEW_CLIPPING",
    "SHORTCUT_HIGHLIGHT_PATH_ISSUES_TOGGLE",
    "SHORTCUT_PAUSE_GAME_ALT",
    "SHORTCUT_ZOOM_VIEW_OUT_ALT",
    "SHORTCUT_ZOOM_VIEW_IN_ALT",
    "SHORTCUT_ROTATE_VIEW_CLOCKWISE_ALT",
    "SHORTCUT_ROTATE_VIEW_ANTICLOCKWISE_ALT",
    "SHORTCUT_ROTATE_CONSTRUCTION_OBJECT_CCW",
    "SHORTCUT_SCROLL_MAP_UP_ALT",
    "SHORTCUT_SCROLL_MAP_LEFT_ALT",
    "SHORTCUT_SCROLL_MAP_DOWN_ALT",
    "SHORTCUT_SCROLL_MAP_RIGHT_ALT",
};

uint16_t GetKeyFromName(const char * key_name)
{
    const char *look = key_name;
    uint16_t modifier = 0;

    if (_strnicmp(look, "SHIFT +", 7) == 0)
    {
        modifier |= SHIFT; look += 7;
    }
    if (_strnicmp(look, "CTRL +", 6) == 0)
    {
        modifier |= CTRL; look += 6;
    }
    if (_strnicmp(look, "ALT +", 5) == 0)
    {
        modifier |= ALT; look += 5;
    }

    SDL_Scancode scancode = SDL_GetScancodeFromName(look);
    if (scancode == SDL_SCANCODE_UNKNOWN)
    {
        Console::WriteLine("Invalid shortcut key: %s", key_name);
        return SHORTCUT_UNDEFINED;
    }
    return (((scancode & 0x1ff) | (modifier & 0xf000)) & 0xf1ff);
}

bool KeyboardShortcuts::Load()
{
    bool result = false;
    Reset();
    try
    {
        std::string path = _env->GetFilePath(PATHID::CONFIG_KEYBOARD);
        if (!File::Exists(path))
            return LoadOld();

        auto fs = FileStream(path, FILE_MODE_OPEN);
        auto reader = std::unique_ptr<IIniReader>(CreateIniReader(&fs));

        reader->ReadSection(HEAD);
        int32_t count = reader->GetInt32("count", SHORTCUT_COUNT);
        count = std::min<int32_t>(count, SHORTCUT_COUNT);

        std::string keystr;
        for (int32_t i = 0; i < count; i++)
        {
            keystr = reader->GetString(shortcut_names[i], "");
            _keys[i] = keystr.empty() ? SHORTCUT_UNDEFINED : GetKeyFromName(keystr.c_str());
        }
        result = true;
    }
    catch (const std::exception& ex)
    {
        Console::WriteLine("Error reading shortcut keys: %s", ex.what());
    }
    return result;
}

bool KeyboardShortcuts::Save()
{
    bool result = false;
    try
    {
        std::string path = _env->GetFilePath(PATHID::CONFIG_KEYBOARD);
        auto fs = FileStream(path, FILE_MODE_WRITE);
        if (!fs.CanWrite())
            throw IOException("SaveShortcut");
        auto writer = std::unique_ptr<IIniWriter>(CreateIniWriter(&fs));
        writer->WriteSection(HEAD);
        writer->WriteInt32("version", VERSION);
        writer->WriteInt32("count", SHORTCUT_COUNT);

        for (int32_t i = 0; i < SHORTCUT_COUNT; i++)
        {
            writer->WriteString(shortcut_names[i], GetShortcutString(i));
        }
        result = true;
    }
    catch (const std::exception& ex)
    {
        Console::WriteLine("Error writing shortcut keys: %s", ex.what());
    }
    return result;
}

#define FILE_VERSION_OLD        1
#define SHORTCUT_COUNT_OLD      67

bool KeyboardShortcuts::LoadOld()
{
    bool result = false;
    Reset();
    try
    {
        std::string path = _env->GetFilePath(PATHID::CONFIG_KEYBOARD_OLD);
        if (File::Exists(path))
        {
            auto fs = FileStream(path, FILE_MODE_OPEN);
            uint16_t version = fs.ReadValue<uint16_t>();
            if (version == FILE_VERSION_OLD)
            {
                uint16_t key;
                int32_t numShortcutsInFile = (fs.GetLength() - sizeof(uint16_t)) / sizeof(uint16_t);
                int32_t numShortcutsToRead = std::min<int32_t>(SHORTCUT_COUNT_OLD, numShortcutsInFile);
                for (int32_t i = 0; i < numShortcutsToRead; i++)
                {
                    key = fs.ReadValue<uint16_t>();
                    if (key & 0xff00)
                        key = key;
                    if (key != SHORTCUT_UNDEFINED)
                        key = ((key & 0x0f00) << 4) | (key & 0xff); // move modifier bits position
                    _keys[i] = key;
                }
                result = true;
            }
        }
    }
    catch (const std::exception& ex)
    {
        Console::WriteLine("Error reading old shortcut keys: %s", ex.what());
    }
    return result;
}

void KeyboardShortcuts::Set(int32_t key)
{
    // Unmap shortcut that already uses this key
    int32_t shortcut = GetFromKey(key);
    if (shortcut != SHORTCUT_UNDEFINED)
    {
        _keys[shortcut] = SHORTCUT_UNDEFINED;
    }

    // Map shortcut to this key
    _keys[gKeyboardShortcutChangeId] = key;
    Save();
}

int32_t KeyboardShortcuts::GetFromKey(int32_t key)
{
    for (int32_t i = 0; i < SHORTCUT_COUNT; i++)
    {
        if (key == _keys[i])
        {
            return i;
        }
    }
    return SHORTCUT_UNDEFINED;
}

std::string KeyboardShortcuts::GetShortcutString(int32_t shortcut) const
{
    utf8 buffer[256] = { 0 };
    utf8 formatBuffer[256] = { 0 };
    uint16_t shortcutKey = _keys[shortcut];
    if (shortcutKey == SHORTCUT_UNDEFINED)
        return std::string();
    if (shortcutKey & SHIFT)
    {
        format_string(formatBuffer, sizeof(formatBuffer), STR_SHIFT_PLUS, nullptr);
        String::Append(buffer, sizeof(buffer), formatBuffer);
    }
    if (shortcutKey & CTRL)
    {
        format_string(formatBuffer, sizeof(formatBuffer), STR_CTRL_PLUS, nullptr);
        String::Append(buffer, sizeof(buffer), formatBuffer);
    }
    if (shortcutKey & ALT)
    {
#ifdef __MACOSX__
        format_string(formatBuffer, sizeof(formatBuffer), STR_OPTION_PLUS, nullptr);
#else
        format_string(formatBuffer, sizeof(formatBuffer), STR_ALT_PLUS, nullptr);
#endif
        String::Append(buffer, sizeof(buffer), formatBuffer);
    }
    if (shortcutKey & CMD)
    {
        format_string(formatBuffer, sizeof(formatBuffer), STR_CMD_PLUS, nullptr);
        String::Append(buffer, sizeof(buffer), formatBuffer);
    }
    String::Append(buffer, sizeof(buffer), SDL_GetKeyName(SDL_GetKeyFromScancode((SDL_Scancode)(shortcutKey & 0xFF))));
    return std::string(buffer);
}

void KeyboardShortcuts::GetKeyboardMapScroll(const uint8_t* keysState, int32_t* x, int32_t* y) const
{
    static const uint8_t scroll_shortcuts[] = {
        SHORTCUT_SCROLL_MAP_UP, SHORTCUT_SCROLL_MAP_LEFT, SHORTCUT_SCROLL_MAP_DOWN, SHORTCUT_SCROLL_MAP_RIGHT,
        SHORTCUT_SCROLL_MAP_UP_ALT, SHORTCUT_SCROLL_MAP_LEFT_ALT, SHORTCUT_SCROLL_MAP_DOWN_ALT, SHORTCUT_SCROLL_MAP_RIGHT_ALT,
    };

    for ( auto& shortcutId : scroll_shortcuts )
    {
        uint16_t shortcutKey = _keys[shortcutId];
        if (shortcutKey == SHORTCUT_UNDEFINED || !keysState[shortcutKey & 0x01FF])
            continue;

        if ((bool)(shortcutKey & SHIFT) !=
            (keysState[SDL_SCANCODE_LSHIFT] || keysState[SDL_SCANCODE_RSHIFT]))
            continue;
        if ((bool)(shortcutKey & CTRL) !=
            (keysState[SDL_SCANCODE_LCTRL] || keysState[SDL_SCANCODE_RCTRL]))
            continue;
        if ((bool)(shortcutKey & ALT) !=
            (keysState[SDL_SCANCODE_LALT] || keysState[SDL_SCANCODE_RALT]))
            continue;
#ifdef __MACOSX__
        if ((bool)(shortcutKey & CMD) !=
            (keysState[SDL_SCANCODE_LGUI] || keysState[SDL_SCANCODE_RGUI]))
            continue;
#endif
        switch (shortcutId)
        {
            case SHORTCUT_SCROLL_MAP_UP:
            case SHORTCUT_SCROLL_MAP_UP_ALT:
                *y = -1;
                break;
            case SHORTCUT_SCROLL_MAP_LEFT:
            case SHORTCUT_SCROLL_MAP_LEFT_ALT:
                *x = -1;
                break;
            case SHORTCUT_SCROLL_MAP_DOWN:
            case SHORTCUT_SCROLL_MAP_DOWN_ALT:
                *y = 1;
                break;
            case SHORTCUT_SCROLL_MAP_RIGHT:
            case SHORTCUT_SCROLL_MAP_RIGHT_ALT:
                *x = 1;
                break;
            default:
                break;
        }
        return;
    }
}

void keyboard_shortcuts_reset()
{
    _instance->Reset();
}

bool keyboard_shortcuts_load()
{
    return _instance->Load();
}

bool keyboard_shortcuts_save()
{
    return _instance->Save();
}

void keyboard_shortcuts_set(int32_t key)
{
    return _instance->Set(key);
}

int32_t keyboard_shortcuts_get_from_key(int32_t key)
{
    return _instance->GetFromKey(key);
}

void keyboard_shortcuts_format_string(char* buffer, size_t bufferSize, int32_t shortcut)
{
    auto str = _instance->GetShortcutString(shortcut);
    String::Set(buffer, bufferSize, str.c_str());
}

void get_keyboard_map_scroll(const uint8_t* keysState, int32_t* x, int32_t* y)
{
    _instance->GetKeyboardMapScroll(keysState, x, y);
}

// Default keyboard shortcuts
const uint16_t KeyboardShortcuts::DefaultKeys[SHORTCUT_COUNT] = {
    SDL_SCANCODE_BACKSPACE,                   // SHORTCUT_CLOSE_TOP_MOST_WINDOW
    SHIFT | SDL_SCANCODE_BACKSPACE,           // SHORTCUT_CLOSE_ALL_FLOATING_WINDOWS
    SDL_SCANCODE_ESCAPE,                      // SHORTCUT_CANCEL_CONSTRUCTION_MODE
    SDL_SCANCODE_PAUSE,                       // SHORTCUT_PAUSE_GAME
    SDL_SCANCODE_PAGEUP,                      // SHORTCUT_ZOOM_VIEW_OUT
    SDL_SCANCODE_PAGEDOWN,                    // SHORTCUT_ZOOM_VIEW_IN
    SDL_SCANCODE_RETURN,                      // SHORTCUT_ROTATE_VIEW_CLOCKWISE
    SHIFT | SDL_SCANCODE_RETURN,              // SHORTCUT_ROTATE_VIEW_ANTICLOCKWISE
    SDL_SCANCODE_Z,                           // SHORTCUT_ROTATE_CONSTRUCTION_OBJECT
    SDL_SCANCODE_1,                           // SHORTCUT_UNDERGROUND_VIEW_TOGGLE
    SDL_SCANCODE_H,                           // SHORTCUT_REMOVE_BASE_LAND_TOGGLE
    SDL_SCANCODE_V,                           // SHORTCUT_REMOVE_VERTICAL_LAND_TOGGLE
    SDL_SCANCODE_3,                           // SHORTCUT_SEE_THROUGH_RIDES_TOGGLE
    SDL_SCANCODE_4,                           // SHORTCUT_SEE_THROUGH_SCENERY_TOGGLE
    SDL_SCANCODE_5,                           // SHORTCUT_INVISIBLE_SUPPORTS_TOGGLE
    SDL_SCANCODE_6,                           // SHORTCUT_INVISIBLE_PEOPLE_TOGGLE
    SDL_SCANCODE_8,                           // SHORTCUT_HEIGHT_MARKS_ON_LAND_TOGGLE
    SDL_SCANCODE_9,                           // SHORTCUT_HEIGHT_MARKS_ON_RIDE_TRACKS_TOGGLE
    SDL_SCANCODE_0,                           // SHORTCUT_HEIGHT_MARKS_ON_PATHS_TOGGLE
    SDL_SCANCODE_F1,                          // SHORTCUT_ADJUST_LAND
    SDL_SCANCODE_F2,                          // SHORTCUT_ADJUST_WATER
    SDL_SCANCODE_F3,                          // SHORTCUT_BUILD_SCENERY
    SDL_SCANCODE_F4,                          // SHORTCUT_BUILD_PATHS
    SDL_SCANCODE_F5,                          // SHORTCUT_BUILD_NEW_RIDE
    SDL_SCANCODE_F,                           // SHORTCUT_SHOW_FINANCIAL_INFORMATION
    SDL_SCANCODE_D,                           // SHORTCUT_SHOW_RESEARCH_INFORMATION
    SDL_SCANCODE_R,                           // SHORTCUT_SHOW_RIDES_LIST
    SDL_SCANCODE_P,                           // SHORTCUT_SHOW_PARK_INFORMATION
    SDL_SCANCODE_G,                           // SHORTCUT_SHOW_GUEST_LIST
    SDL_SCANCODE_S,                           // SHORTCUT_SHOW_STAFF_LIST
    SDL_SCANCODE_M,                           // SHORTCUT_SHOW_RECENT_MESSAGES
    SDL_SCANCODE_TAB,                         // SHORTCUT_SHOW_MAP
    PLATFORM_MODIFIER | SDL_SCANCODE_S,       // SHORTCUT_SCREENSHOT
    SDL_SCANCODE_MINUS,                       // SHORTCUT_REDUCE_GAME_SPEED,
    SDL_SCANCODE_EQUALS,                      // SHORTCUT_INCREASE_GAME_SPEED,
    PLATFORM_MODIFIER | ALT | SDL_SCANCODE_C, // SHORTCUT_OPEN_CHEAT_WINDOW,
    SDL_SCANCODE_T,                           // SHORTCUT_REMOVE_TOP_BOTTOM_TOOLBAR_TOGGLE,
    SDL_SCANCODE_UP,                          // SHORTCUT_SCROLL_MAP_UP
    SDL_SCANCODE_LEFT,                        // SHORTCUT_SCROLL_MAP_LEFT
    SDL_SCANCODE_DOWN,                        // SHORTCUT_SCROLL_MAP_DOWN
    SDL_SCANCODE_RIGHT,                       // SHORTCUT_SCROLL_MAP_RIGHT
    SDL_SCANCODE_C,                           // SHORTCUT_OPEN_CHAT_WINDOW
    PLATFORM_MODIFIER | SDL_SCANCODE_F10,     // SHORTCUT_QUICK_SAVE_GAME
    SHORTCUT_UNDEFINED,                       // SHORTCUT_SHOW_OPTIONS
    SHORTCUT_UNDEFINED,                       // SHORTCUT_MUTE_SOUND
    ALT | SDL_SCANCODE_RETURN,                // SHORTCUT_WINDOWED_MODE_TOGGLE
    SHORTCUT_UNDEFINED,                       // SHORTCUT_SHOW_MULTIPLAYER
    SHORTCUT_UNDEFINED,                       // SHORTCUT_PAINT_ORIGINAL_TOGGLE
    SHORTCUT_UNDEFINED,                       // SHORTCUT_DEBUG_PAINT_TOGGLE
    SHORTCUT_UNDEFINED,                       // SHORTCUT_SEE_THROUGH_PATHS_TOGGLE
    SDL_SCANCODE_KP_4,                        // SHORTCUT_RIDE_CONSTRUCTION_TURN_LEFT
    SDL_SCANCODE_KP_6,                        // SHORTCUT_RIDE_CONSTRUCTION_TURN_RIGHT
    SDL_SCANCODE_KP_5,                        // SHORTCUT_RIDE_CONSTRUCTION_USE_TRACK_DEFAULT
    SDL_SCANCODE_KP_2,                        // SHORTCUT_RIDE_CONSTRUCTION_SLOPE_DOWN
    SDL_SCANCODE_KP_8,                        // SHORTCUT_RIDE_CONSTRUCTION_SLOPE_UP
    SDL_SCANCODE_KP_PLUS,                     // SHORTCUT_RIDE_CONSTRUCTION_CHAIN_LIFT_TOGGLE
    SDL_SCANCODE_KP_1,                        // SHORTCUT_RIDE_CONSTRUCTION_BANK_LEFT
    SDL_SCANCODE_KP_3,                        // SHORTCUT_RIDE_CONSTRUCTION_BANK_RIGHT
    SDL_SCANCODE_KP_7,                        // SHORTCUT_RIDE_CONSTRUCTION_PREVIOUS_TRACK
    SDL_SCANCODE_KP_9,                        // SHORTCUT_RIDE_CONSTRUCTION_NEXT_TRACK
    SDL_SCANCODE_KP_0,                        // SHORTCUT_RIDE_CONSTRUCTION_BUILD_CURRENT
    SDL_SCANCODE_KP_MINUS,                    // SHORTCUT_RIDE_CONSTRUCTION_DEMOLISH_CURRENT
    PLATFORM_MODIFIER | SDL_SCANCODE_L,       // SHORTCUT_LOAD_GAME
    SDL_SCANCODE_B,                           // SHORTCUT_CLEAR_SCENERY
    SDL_SCANCODE_7,                           // SHORTCUT_GRIDLINES_DISPLAY_TOGGLE
    SHORTCUT_UNDEFINED,                       // SHORTCUT_VIEW_CLIPPING
    SDL_SCANCODE_I,                           // SHORTCUT_HIGHLIGHT_PATH_ISSUES_TOGGLE
    SHORTCUT_UNDEFINED,                       // SHORTCUT_PAUSE_GAME_ALT
    SHORTCUT_UNDEFINED,                       // SHORTCUT_ZOOM_VIEW_OUT_ALT
    SHORTCUT_UNDEFINED,                       // SHORTCUT_ZOOM_VIEW_IN_ALT
    SHORTCUT_UNDEFINED,                       // SHORTCUT_ROTATE_VIEW_CLOCKWISE_ALT
    SHORTCUT_UNDEFINED,                       // SHORTCUT_ROTATE_VIEW_ANTICLOCKWISE_ALT
    SDL_SCANCODE_X,                           // SHORTCUT_ROTATE_CONSTRUCTION_OBJECT_CCW
    SHORTCUT_UNDEFINED,                       // SHORTCUT_SCROLL_MAP_UP_ALT
    SHORTCUT_UNDEFINED,                       // SHORTCUT_SCROLL_MAP_LEFT_ALT
    SHORTCUT_UNDEFINED,                       // SHORTCUT_SCROLL_MAP_DOWN_ALT
    SHORTCUT_UNDEFINED,                       // SHORTCUT_SCROLL_MAP_RIGHT_ALT
};
