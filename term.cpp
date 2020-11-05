#include "term.h"
#include "font.h"
extern "C" {
#include "lua/lua.h"
}
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vga>
#include <kernel/timers.hpp>

// for some reason IntelliSense doesn't know what size_t is, even though it's defined in lua.h
#ifdef __INTELLISENSE__
typedef unsigned long size_t;
#endif

#define TERM_HEIGHT 26
#define TERM_WIDTH 53
unsigned char characters[TERM_HEIGHT*TERM_WIDTH];
unsigned char colors[TERM_HEIGHT*TERM_WIDTH];
unsigned char current_colors = 0xF0;
int cursorX = 0, cursorY = 0;
bool cursorBlink = true, cursorShow = true;

uint32_t palette[16] = {
    0xf0f0f0,
    0xf2b233,
    0xe57fd8,
    0x99b2f2,
    0xdede6c,
    0x7fcc19,
    0xf2b2cc,
    0x4c4c4c,
    0x999999,
    0x4c99b2,
    0xb266e5,
    0x3366cc,
    0x7f664c,
    0x57a64e,
    0xcc4c4c,
    0x111111
};

extern unsigned char inb(unsigned short port);
void outb(unsigned short port, unsigned char val)
{
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
    /* There's an outb %al, $imm8  encoding, for compile-time constant port numbers that fit in 8b.  (N constraint).
     * Wider immediate constants would be truncated at assemble-time (e.g. "i" constraint).
     * The  outb  %al, %dx  encoding is the only option for all other cases.
     * %1 expands to %dx because  port  is a uint16_t.  %w1 could be used if we had the port number a wider C type */
}

uint8_t pixel_buffer[4][38400];

void blit_line(int y, int start_x, int end_x) {
    int length = end_x - start_x + 1;
    // Generate bitmap for blitted region
    uint8_t * bitmap = (uint8_t*)malloc(length * 12 * 18);
    for (int x = 0; x < length; x++) {
        unsigned char ch = characters[y*TERM_WIDTH+x+start_x], col = colors[y*TERM_WIDTH+x+start_x];
        for (int py = 0; py < 9; py++) {
            for (int px = 0; px < 6; px++) {
                uint8_t val = header_data[(ch >> 4) * 864 + py * 96 + (ch & 0x0f) * 6 + px] ? col & 0x0f : col >> 4;
                bitmap[py * length * 24 + x * 12 + px * 2] = val;
                bitmap[py * length * 24 + x * 12 + px * 2 + 1] = val;
                bitmap[py * length * 24 + length * 12 + x * 12 + px * 2] = val;
                bitmap[py * length * 24 + length * 12 + x * 12 + px * 2 + 1] = val;
            }
        }
    }
    // Blit the bitmap to each plane
    for (int i = 0; i < 4; i++) {
        for (int py = 0; py < 18; py++) {
            int boff = 7 - ((start_x % 2) * 4);
            int xoff = y * 1440 + py * 80 + (int)floor(start_x * 1.5);
            uint8_t b = boff != 7 ? pixel_buffer[i][xoff] & 0xF0 : 0;
            for (int px = 0; px < length * 12; px++) {
                b |= ((bitmap[py * length * 12 + px] >> i) & 1) << boff--;
                if (boff < 0) {
                    pixel_buffer[i][xoff++] = b;
                    b = 0, boff = 7;
                }
            }
            if (boff != 7) pixel_buffer[i][xoff] = b | (pixel_buffer[i][xoff] & ((1 << (8 - boff)) - 1));
        }
        outb(0x3C4, 0x02);
        outb(0x3C5, 1 << i);
        memcpy((void*)(0xA0000 + (y * 1440)), pixel_buffer[i] + (y * 1440), 1440);
    }
    free(bitmap);
}

void blit_screen() {
    for (int i = 0; i < TERM_HEIGHT; i++) blit_line(i, 0, TERM_WIDTH - 1);
}

void redrawCursor() {
    if (cursorX < 0 || cursorX >= TERM_WIDTH || cursorY < 0 || cursorY >= TERM_HEIGHT) return;
    blit_line(cursorY, cursorX, cursorX);
    if (cursorShow) {
        for (int i = 0; i < 4; i++) {
            outb(0x3C4, 0x02);
            outb(0x3C5, 1 << i);
            if (cursorX % 2) {
                *(uint8_t*)(0xA0000 + cursorY * 1440 + (uintptr_t)(cursorX * 1.5) + 1280) &= 0xF0;
                *(uint8_t*)(0xA0000 + cursorY * 1440 + (uintptr_t)(cursorX * 1.5) + 1281) = 0;
                *(uint8_t*)(0xA0000 + cursorY * 1440 + (uintptr_t)(cursorX * 1.5) + 1360) &= 0xF0;
                *(uint8_t*)(0xA0000 + cursorY * 1440 + (uintptr_t)(cursorX * 1.5) + 1361) = 0;
            } else {
                *(uint8_t*)(0xA0000 + cursorY * 1440 + (uintptr_t)(cursorX * 1.5) + 1280) = 0;
                *(uint8_t*)(0xA0000 + cursorY * 1440 + (uintptr_t)(cursorX * 1.5) + 1281) &= 0x0F;
                *(uint8_t*)(0xA0000 + cursorY * 1440 + (uintptr_t)(cursorX * 1.5) + 1360) = 0;
                *(uint8_t*)(0xA0000 + cursorY * 1440 + (uintptr_t)(cursorX * 1.5) + 1361) &= 0x0F;
            }
        }
    }
}

void termInit() {
    printf("Initializing terminal...\n");
    VGA_gfx::set_mode(VGA_gfx::MODE_640_480_16);
    for (int i = 0; i < 16; i++) {
        /* reset Attribute Controller (AC) flip-flop so 0x3C0 is AC address */
        inb(0x3DA);
        /* set AC address = i. You must clear bit b5 here to modify the palettes.
        Clearing this bit will also blank the display. */
        outb(0x3C0, i);
        /* read AC register #i
        THIS is the index into the 256-color palette, not 'i' */
        int j = inb(0x3C1);
        /* set the 256-color palette (DAC) entry. Only the bottom 6 bits of each
        R, G, and B entry are significant, so we divide by 4. Change RED, GREEN,
        and BLUE to whatever is appropriate for your code. */
        outb(0x3C8, j);
        outb(0x3C9, (palette[i] >> 18) & 0x3F);
        outb(0x3C9, (palette[i] >> 10) & 0x3F);
        outb(0x3C9, (palette[i] >> 2) & 0x3F);
    }
    for (int i = 0; i < 4; i++) {
        memset(pixel_buffer[i], 0xFF, 38400);
        outb(0x3C4, 0x02);
        outb(0x3C5, 1 << i);
        outb(0x3CE, 0x04);
        outb(0x3CF, i);
        memset((void*)0xA0000, 0xFF, 38400);
    }
    outb(0x3CE, 0x00);
    outb(0x3CF, 0x00);
    outb(0x3CE, 0x01);
    outb(0x3CF, 0x00);
    outb(0x3CE, 0x03);
    outb(0x3CF, 0x00);
    outb(0x3CE, 0x05);
    outb(0x3CF, 0x00);
    outb(0x3CE, 0x08);
    outb(0x3CF, 0xFF);
    memset(characters, ' ', TERM_WIDTH * TERM_HEIGHT);
    memset(colors, 0xF0, TERM_WIDTH * TERM_HEIGHT);
    blit_screen();
    /*Timers::periodic(std::chrono::nanoseconds(500000), [](Timers::id_t id){
        printf("Timer\n");
        if (cursorBlink) {
            cursorShow = !cursorShow;
            redrawCursor();
        }
    });*/
    redrawCursor();
    /* set bit b5 in AC address register to lock palette and unblank display */
    inb(0x3DA);
    outb(0x3C0, 0x20);
    printf("Terminal initialized.\n");
}

void termClose() {

}

int term_write(lua_State *L) {
    if (cursorY < 0 || cursorY >= TERM_HEIGHT || cursorX >= TERM_WIDTH) return 0;
    size_t len;
    const char * str = lua_tolstring(L, 1, &len); 
    //printf("writing %d %s\n", len, str);
    for (int x = cursorX, i = 0; i < len && x < TERM_WIDTH; x++, i++) {
        if (x < 0) continue;
        characters[cursorY*TERM_WIDTH+x] = str[i];
        colors[cursorY*TERM_WIDTH+x] = current_colors;
    }
    if (cursorX + len > 0) blit_line(cursorY, cursorX > 0 ? cursorX : 0, cursorX + len >= TERM_WIDTH ? TERM_WIDTH - 1 : cursorX + len - 1);
    if (cursorX >= 0 && cursorX < TERM_WIDTH && cursorY >= 0 && cursorY < TERM_HEIGHT) blit_line(cursorY, cursorX, cursorX);
    cursorX += len;
    redrawCursor();
    return 0;
}

int term_scroll(lua_State *L) {
    int scroll = lua_tointeger(L, 1);
    if (scroll >= TERM_HEIGHT || -scroll >= TERM_HEIGHT) {
        // scrolling more than the height is equivalent to clearing the screen
        memset(characters, ' ', TERM_WIDTH * TERM_HEIGHT);
        memset(colors, current_colors, TERM_WIDTH * TERM_HEIGHT);
    } else if (scroll > 0) {
        memmove(characters, characters + scroll * TERM_WIDTH, (TERM_HEIGHT - scroll) * TERM_WIDTH);
        memset(characters + (TERM_HEIGHT - scroll) * TERM_WIDTH, ' ', scroll * TERM_WIDTH);
        memmove(colors, colors + scroll * TERM_WIDTH, (TERM_HEIGHT - scroll) * TERM_WIDTH);
        memset(colors + (TERM_HEIGHT - scroll) * TERM_WIDTH, current_colors, scroll * TERM_WIDTH);
    } else if (scroll < 0) {
        memmove(characters - scroll * TERM_WIDTH, characters, (TERM_HEIGHT + scroll) * TERM_WIDTH);
        memset(characters, ' ', -scroll * TERM_WIDTH);
        memmove(colors - scroll * TERM_WIDTH, colors, (TERM_HEIGHT + scroll) * TERM_WIDTH);
        memset(colors, current_colors, -scroll * TERM_WIDTH);
    }
    blit_screen();
    return 0;
}

int term_setCursorPos(lua_State *L) {
    if (cursorX >= 0 && cursorX < TERM_WIDTH && cursorY >= 0 && cursorY < TERM_HEIGHT) blit_line(cursorY, cursorX, cursorX);
    cursorX = lua_tointeger(L, 1)-1;
    cursorY = lua_tointeger(L, 2)-1;
    redrawCursor();
    return 0;
}

int term_setCursorBlink(lua_State *L) {
    cursorBlink = cursorShow = lua_toboolean(L, 1);
    redrawCursor();
    return 0;
}

int term_getCursorPos(lua_State *L) {
    lua_pushinteger(L, cursorX+1);
    lua_pushinteger(L, cursorY+1);
    return 2;
}

int term_getSize(lua_State *L) {
    lua_pushinteger(L, TERM_WIDTH);
    lua_pushinteger(L, TERM_HEIGHT);
    return 2;
}

int term_clear(lua_State *L) {
    memset(characters, ' ', TERM_WIDTH * TERM_HEIGHT);
    memset(colors, current_colors, TERM_WIDTH * TERM_HEIGHT);
    blit_screen();
    return 0;
}

int term_clearLine(lua_State *L) {
    if (cursorY < 0 || cursorY >= TERM_HEIGHT) return 0;
    memset(&characters[cursorY*TERM_WIDTH], ' ', TERM_WIDTH);
    memset(&colors[cursorY*TERM_WIDTH], current_colors, TERM_WIDTH);
    blit_line(cursorY, 0, TERM_WIDTH-1);
    return 0;
}

int term_setTextColor(lua_State *L) {
    current_colors = (current_colors & 0xF0) | ((int)log2(lua_tointeger(L, 1)) & 0x0F);
    return 0;
}

int term_setBackgroundColor(lua_State *L) {
    current_colors = (current_colors & 0x0F) | (((int)log2(lua_tointeger(L, 1)) << 4) & 0xF0);
    return 0;
}

int term_isColor(lua_State *L) {
    lua_pushboolean(L, true);
    return 1;
}

int term_getTextColor(lua_State *L) {
    lua_pushinteger(L, 1 << (current_colors & 0x0F));
    return 1;
}

int term_getBackgroundColor(lua_State *L) {
    lua_pushinteger(L, 1 << (current_colors >> 4));
    return 1;
}

int hexch(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    else if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    else return -1;
}

int term_blit(lua_State *L) {
    size_t len, blen, flen;
    const char * str = lua_tolstring(L, 1, &len);
    const char * fg = lua_tolstring(L, 2, &flen);
    const char * bg = lua_tolstring(L, 3, &blen);
    if (len != flen || flen != blen) {
        lua_pushstring(L, "arguments must be the same length");
        lua_error(L);
    }
    for (int x = cursorX, i = 0; i < len && x < TERM_WIDTH; x++, i++) {
        if (x < 0) continue;
        characters[cursorY*TERM_WIDTH+x] = str[i];
        colors[cursorY*TERM_WIDTH+x] = hexch(fg[i]) | (hexch(bg[i]) << 4);
    }
    if (cursorX + len > 0) blit_line(cursorY, cursorX > 0 ? cursorX : 0, cursorX + len >= TERM_WIDTH ? TERM_WIDTH - 1 : cursorX + len - 1);
    if (cursorX >= 0 && cursorX < TERM_WIDTH && cursorY >= 0 && cursorY < TERM_HEIGHT) blit_line(cursorY, cursorX, cursorX);
    cursorX += len;
    redrawCursor();
    return 0;
}

int term_getPaletteColor(lua_State *L) {
    int color = log2(lua_tointeger(L, 1));
    lua_pushnumber(L, (palette[color] & 0xFF) / 255.0);
    lua_pushnumber(L, ((palette[color] >> 8) & 0xFF) / 255.0);
    lua_pushnumber(L, ((palette[color] >> 24) & 0xFF) / 255.0);
    return 3;
}

int term_setPaletteColor(lua_State *L) {
    int color = log2(lua_tointeger(L, 1));
    int r = lua_tonumber(L, 2) * 63, g = lua_tonumber(L, 3) * 63, b = lua_tonumber(L, 4) * 63;
    /* reset Attribute Controller (AC) flip-flop so 0x3C0 is AC address */
    inb(0x3DA);
    /* set AC address = i. You must clear bit b5 here to modify the palettes.
    Clearing this bit will also blank the display. */
    outb(0x3C0, color);
    /* read AC register #i
    THIS is the index into the 256-color palette, not 'i' */
    int j = inb(0x3C1);
    /* set the 256-color palette (DAC) entry. Only the bottom 6 bits of each
    R, G, and B entry are significant, so we divide by 4. Change RED, GREEN,
    and BLUE to whatever is appropriate for your code. */
    outb(0x3C8, j);
    outb(0x3C9, r);
    outb(0x3C9, g);
    outb(0x3C9, b);
    /* set bit b5 in AC address register to lock palette and unblank display */
    inb(0x3DA);
    outb(0x3C0, 0x20);
    palette[color] = (b << 2) | (g << 10) | (r << 18); 
    return 0;
}

const char * term_keys[23] = {
    "write",
    "scroll",
    "setCursorPos",
    "setCursorBlink",
    "getCursorPos",
    "getSize",
    "clear",
    "clearLine",
    "setTextColour",
    "setTextColor",
    "setBackgroundColour",
    "setBackgroundColor",
    "isColour",
    "isColor",
    "getTextColour",
    "getTextColor",
    "getBackgroundColour",
    "getBackgroundColor",
    "blit",
    "getPaletteColor",
    "getPaletteColour",
    "setPaletteColor",
    "setPaletteColour"
};

lua_CFunction term_values[23] = {
    term_write,
    term_scroll,
    term_setCursorPos,
    term_setCursorBlink,
    term_getCursorPos,
    term_getSize,
    term_clear,
    term_clearLine,
    term_setTextColor,
    term_setTextColor,
    term_setBackgroundColor,
    term_setBackgroundColor,
    term_isColor,
    term_isColor,
    term_getTextColor,
    term_getTextColor,
    term_getBackgroundColor,
    term_getBackgroundColor,
    term_blit,
    term_getPaletteColor,
    term_getPaletteColor,
    term_setPaletteColor,
    term_setPaletteColor
};

library_t term_lib = {"term", 23, term_keys, term_values};