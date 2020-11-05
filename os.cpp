#include "os.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
extern "C" {
#include "lua/lauxlib.h"
#include "queue.h"
}
#include <timers>
#include <os>
#include <kernel/events.hpp>

int running = 1;
const char * label;
bool label_defined = false;
queue_t eventQueue;
lua_State* paramQueue;
queue_t timerQueue;

// PS/2 special keyboard codes.
typedef enum ps2code_ext_t {
    PS2CODE_EXT_ESC = 0x01,
    PS2CODE_EXT_BACKSPACE = 0x0E,
    PS2CODE_EXT_TAB = 0x0F,
    PS2CODE_EXT_ENTER = 0x1C,
    PS2CODE_EXT_LEFT_CONTROL,
    PS2CODE_EXT_LEFT_SHIFT = 0x2A,
    PS2CODE_EXT_RIGHT_SHIFT = 0x36,
    PS2CODE_EXT_LEFT_ALT = 0x38,
    PS2CODE_EXT_CAPS_LOCK = 0x3A,
    PS2CODE_EXT_F1,
    PS2CODE_EXT_F2,
    PS2CODE_EXT_F3,
    PS2CODE_EXT_F4,
    PS2CODE_EXT_F5,
    PS2CODE_EXT_F6,
    PS2CODE_EXT_F7,
    PS2CODE_EXT_F8,
    PS2CODE_EXT_F9,
    PS2CODE_EXT_F10,
    PS2CODE_EXT_NUM_LOCK,
    PS2CODE_EXT_SCROLL_LOCK,
    PS2CODE_EXT_F11 = 0x57,
    PS2CODE_EXT_F12 = 0x58
} ps2code_ext_t;

enum modifiers {
    MODIFIER_CONTROL = 2,
    MODIFIER_SHIFT = 1,
    MODIFIER_ALT = 4
};

const char* ps2codes_sc1 = "\0\0321234567890-=\032\032qwertyuiop[]\032\032asdfghjkl;'`\032\\zxcvbnm,./\032\0\032 \032\032\032\032\032\032\032\032\032\032\032\032\032\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\032\032";
const char* ps2codes_sc1_upper = "\0\032!@#$%^&*()_+\032\032QWERTYUIOP{}\032\032ASDFGHJKL:\"~\032|ZXCVBNM<>?\032\0\032 \032\032\032\032\032\032\032\032\032\032\032\032\032\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\032\032";
const char* ps2codes_sc2 = "\0\032\0\032\032\032\032\032\0\032\032\032\032\032`\0\0\032\032\0\032q1\0\0zsaw2\0\0cxde43\0\0 vftr5\0\0nbhgy6\0\0\0mju78\0\0,kio09\0\0./l;p-\0\0\0'\0[=\0\0\032\032\032]\0\\\0\0\0\0\0\0\0\0\032\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\032\032\032\0\0\0\0\0\032\0\0\0\0\032";
char modifiers = 0;

ps2code_ext_t getcode(char code) {
    return (ps2code_ext_t)(code & 0x7f);
}

char convert_ps2_code(unsigned char code) {
    ps2code_ext_t c;
    if (code == 0xAA || code == 0xB6) {
        modifiers &= ~MODIFIER_SHIFT;
        return 0;
    }
    if (code == 0x9D) {
        modifiers &= ~MODIFIER_CONTROL;
        return 0;
    }
    if (code == 0xB8) {
        modifiers &= ~MODIFIER_ALT;
        return 0;
    }
    if (code > 0x58 || ps2codes_sc1[code] == 0) return 0;
    if (ps2codes_sc1[code] == '\032') {
        c = getcode(code);
        if (c == PS2CODE_EXT_LEFT_SHIFT || c == PS2CODE_EXT_RIGHT_SHIFT) modifiers |= MODIFIER_SHIFT;
        else if (c == PS2CODE_EXT_LEFT_CONTROL) modifiers |= MODIFIER_CONTROL;
        else if (c == PS2CODE_EXT_LEFT_ALT) modifiers |= MODIFIER_ALT;
        else if (c == PS2CODE_EXT_CAPS_LOCK) modifiers = (modifiers & ~MODIFIER_SHIFT) | !(modifiers & MODIFIER_SHIFT);
        return 0;
    }
    return modifiers & MODIFIER_SHIFT ? ps2codes_sc1_upper[code] : ps2codes_sc1[code];
}

bool iscode(char code) {
    return code & 0x80;
}

unsigned char inb(unsigned short port)
{
    unsigned char ret;
    asm volatile ( "inb %1, %0"
                   : "=a"(ret)
                   : "Nd"(port) );
    return ret;
}

unsigned char get_scancode() {
    while (!(inb(0x64) & 1) && queue_empty(&timerQueue)) Events::get().process_events();
    if (!queue_empty(&timerQueue)) return 0;
    return inb(0x60);
}

int getkey() {
    int retval = 0;
    unsigned char code = get_scancode();
    if (code == 0xE0) retval = 0xE000 | get_scancode();
    else if (code == 0xE1) retval = 0xE00000 | (get_scancode() << 8) | get_scancode();
    else retval = code;
    return retval;
}

int ps2tocc(int code) {
    if ((code & 0xFF00) == 0xE000) return (code & 0x7F) | 0x80;
    else if ((code & 0xFF7F7F) == 0xE11D45) return 197;
    else return code & 0x7F;
}

int getNextEvent(lua_State *L, const char * filter) {
    const char * ev;
    do {
        while (queue_size(&eventQueue) == 0) {
            int code;
            if ((code = getkey())) {
                //printf("Got key %d\n", code);
                lua_State *param = lua_newthread(paramQueue);
                lua_pushinteger(param, ps2tocc(code));
                lua_pushboolean(param, false);
                queue_push(&eventQueue, code & 0x80 ? "key_up" : "key");
                if (convert_ps2_code(code)) {
                    char str[2];
                    str[0] = convert_ps2_code(code);
                    str[1] = 0;
                    param = lua_newthread(paramQueue);
                    lua_pushstring(param, str);
                    queue_push(&eventQueue, "char");
                }
            }
            while (!queue_empty(&timerQueue)) {
                lua_State *param = lua_newthread(paramQueue);
                lua_pushinteger(param, (ptrdiff_t)queue_front(&timerQueue));
                queue_push(&eventQueue, "timer");
                queue_pop(&timerQueue);
            }
        }
        ev = queue_front(&eventQueue);
        queue_pop(&eventQueue);
        if ((strlen(filter) > 0 && strcmp(ev, filter) != 0)) lua_remove(paramQueue, 1);
    } while (strlen(filter) > 0 && strcmp(ev, filter) != 0);
    //printf("Sending event %s %d %d\n", ev, lua_gettop(paramQueue), queue_size(&eventQueue));
    if ((size_t)lua_gettop(paramQueue) != queue_size(&eventQueue) + 1) {
        printf("Warning: Queue sizes are incorrect! Expect misaligned event parameters.\n");
    }
    lua_State *param = lua_tothread(paramQueue, 1);
    if (param == NULL) return 0;
    int count = lua_gettop(param);
    if (!lua_checkstack(L, count + 1)) {
        printf("Could not allocate enough space in the stack for %d elements, skipping event \"%s\"\n", count, ev);
        return 0;
    }
    lua_pushstring(L, ev);
    lua_xmove(param, L, count);
    lua_remove(paramQueue, 1);
    return count + 1;
}

int os_getComputerID(lua_State *L) {lua_pushinteger(L, 0); return 1;}
int os_getComputerLabel(lua_State *L) {
    if (!label_defined) return 0;
    lua_pushstring(L, label);
    return 1;
}

int os_setComputerLabel(lua_State *L) {
    label = lua_tostring(L, 1);
    label_defined = true;
    return 0;
}

int os_queueEvent(lua_State *L) {
    int count = lua_gettop(L);
    const char * name = lua_tostring(L, 1);
    lua_State *param = lua_newthread(paramQueue);
    lua_xmove(L, param, count - 1);
    queue_push(&eventQueue, name);
    return 0;
}

int os_clock(lua_State *L) {
    lua_pushinteger(L, clock() / CLOCKS_PER_SEC);
    return 1;
}

void timerEvent(Timers::id_t id) {
    printf("Timer %d fired\n", id);
    queue_push(&timerQueue, (const char*)id);
}

int os_startTimer(lua_State *L) {
    Timers::id_t tm = Timers::oneshot(std::chrono::nanoseconds((long long)(lua_tonumber(L, 1) * 1000000)), timerEvent);
    lua_pushinteger(L, tm);
    return 1;
}

int os_cancelTimer(lua_State *L) {
    int id = lua_tointeger(L, 1);
    Timers::stop(id);
    return 0;
}

int os_time(lua_State *L) {
    const char * type = "ingame";
    if (lua_gettop(L) > 0) type = lua_tostring(L, 1);
    time_t t = time(NULL);
    struct tm rightNow;
    if (strcmp(type, "utc") == 0) rightNow = *gmtime(&t);
    else rightNow = *localtime(&t);
    int hour = rightNow.tm_hour;
    int minute = rightNow.tm_min;
    int second = rightNow.tm_sec;
    lua_pushnumber(L, (double)hour + ((double)minute/60.0) + ((double)second/3600.0));
    return 1;
}

int os_epoch(lua_State *L) {
    const char * type = "ingame";
    if (lua_gettop(L) > 0) type = lua_tostring(L, 1);
    if (strcmp(type, "utc") == 0) {
        lua_pushinteger(L, (long long)time(NULL) * 1000LL);
    } else if (strcmp(type, "local") == 0) {
        time_t t = time(NULL);
        lua_pushinteger(L, (long long)mktime(localtime(&t)) * 1000LL);
    } else {
        time_t t = time(NULL);
        struct tm rightNow = *localtime(&t);
        int hour = rightNow.tm_hour;
        int minute = rightNow.tm_min;
        int second = rightNow.tm_sec;
        double m_time = (double)hour + ((double)minute/60.0) + ((double)second/3600.0);
        double m_day = rightNow.tm_yday;
        lua_pushinteger(L, m_day * 86400000 + (int) (m_time * 3600000.0f));
    }
    return 1;
}

int os_day(lua_State *L) {
    const char * type = "ingame";
    if (lua_gettop(L) > 0) type = lua_tostring(L, 1);
    time_t t = time(NULL);
    if (strcmp(type, "ingame") == 0) {
        struct tm rightNow = *localtime(&t);
        lua_pushinteger(L, rightNow.tm_yday);
        return 1;
    } else if (strcmp(type, "local")) t = mktime(localtime(&t));
    lua_pushinteger(L, t/(60*60*24));
    return 1;
}

int os_setAlarm(lua_State *L) {
    //alarms.push_back(lua_tonumber(L, 1));
    //lua_pushinteger(L, alarms.size() - 1);
    return 0;
}

int os_cancelAlarm(lua_State *L) {
    //int id = lua_tointeger(L, 1);
    //if (id == alarms.size() - 1) alarms.pop_back();
    //else alarms[id] = -1;
    return 0;
}

int os_shutdown(lua_State *L) {
    running = 0;
    os::shutdown();
    return 0;
}

int os_reboot(lua_State *L) {
    running = 2;
    os::reboot();
    return 0;
}

const char * os_keys[14] = {
    "getComputerID",
    "getComputerLabel",
    "setComputerLabel",
    "queueEvent",
    "clock",
    "startTimer",
    "cancelTimer",
    "time",
    "epoch",
    "day",
    "setAlarm",
    "cancelAlarm",
    "shutdown",
    "reboot"
};

lua_CFunction os_values[14] = {
    os_getComputerID,
    os_getComputerLabel,
    os_setComputerLabel,
    os_queueEvent,
    os_clock,
    os_startTimer,
    os_cancelTimer,
    os_time,
    os_epoch,
    os_day,
    os_setAlarm,
    os_cancelAlarm,
    os_shutdown,
    os_reboot
};

library_t os_lib = {"os", 14, os_keys, os_values};