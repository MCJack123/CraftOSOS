extern "C" {
#include "lua/lauxlib.h"
}
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <os>
#include <ide>
#include <memdisk>
#include "includeos-patch/vfs.hpp"
#include "includeos-patch/disk.hpp"
#include "includeos-patch/fat.hpp"
#include <fs/filesystem.hpp>
#include <fs/mbr.hpp>
#include <fs/partition.hpp>
#include <hal/machine.hpp>
#include <hw/pci_device.hpp>
#include <hw/pci_manager.hpp>
#include <kernel/timers.hpp>

#include "bit.h"
#include "fs.h"
#include "os.h"
#include "term.h"
#include "redstone.h"
#include "queue.h"

#define CRAFTOSOS_VERSION "0.1"

extern lua_State* paramQueue;
extern queue_t eventQueue;
extern queue_t timerQueue;
static fs::Disk__ptr disk;
fs::File_system * rootfs;

const char * startup_str = "S\007t\007a\007r\007t\007i\007n\007g\007 \007C\007r\007a\007f\007t\007O\007S\007O\007S\007.\007.\007.\007";
const char * osos = "   ..------~~~--.__\n"
"  /               c~\\\n"
"  /             \\__ `\\\n"
"  |  /~~--__/  /'\\ ~~'\n"
" /'/'\\ |    | |`\\ \\_\n"
"`-))  `-))  `-)) `-))";

int main2 ();

// Entrypoint to set up FS and stuff before starting main
void Service::start() {
    memset((void*)0xB8000, 0, 4000);
    // for some reason just a straight memcpy breaks QEMU
    for (int i = 0; i < 10; i++) memcpy((void*)(0xB8000 + i*4), startup_str + i*4, 4);
    *(uint16_t*)0xB8028 = 0x072E;
    Timers::init([](Timers::duration_t){}, [](){});
    hw::PCI_manager::init();
    hw::PCI_manager::register_blk(PCI::VENDOR_INTEL, 0x7010, [](hw::PCI_Device& dev)->std::unique_ptr<hw::Block_device> {IDE * d = new IDE(dev, IDE::MASTER); return std::unique_ptr<hw::Block_device>(d);});
    hw::PCI_manager::init_devices(PCI::STORAGE);
    auto devices = os::machine().get<hw::Block_device>();
    printf("Found %d drives\n", devices.size());
    auto drive = devices.begin();
    while (drive->get().size() < 10 && drive != devices.end()) drive++;
    if (drive == devices.end()) os::panic("Could not find valid drive!\n");
    printf("%s: Size of drive is %d\n", drive->get().to_string().c_str(), drive->get().size());
    printf("Loading filesystem...\n");
    disk = fs::VFS::insert_disk(*drive);
    disk->init_fs(fs::Disk_::VBR1, [](fs::error_t err, fs::File_system& fs) {
        if (err) os::panic((std::string("Could not load filesystem: ") + err.to_string() + "\n").c_str());
        printf("Filesystem loaded.\n");
        /*fs::VFS::mount(fs::Path("/root"), fs, "root");
        fs::VFS::get_entry("/root").print_tree();
        fs.ls("/", [](fs::error_t err, fs::Dirvec_ptr dir) {
            if (err) printf("Could not list partition: %s\n", err.to_string().c_str());
            else {
                printf("Partition contents:\n");
                for (auto e : *dir) {
                    printf("%s\n", e.name().c_str());
                }
            }
            main2();
        });*/
        /*fs::VFS::mount(fs::Path("/root"), disk->device_id(), fs::Path("/"), "root", [](fs::error_t err) {
            if (err) os::panic((std::string("Could not mount filesystem: ") + err.to_string() + "\n").c_str());
            for (auto d : fs::get<fs::Dirent>(fs::Path("/root")).ls()) {
                printf("'%s' %s\n", d.name().c_str(), d.type_string().c_str());
                for (char c : d.name()) printf("%02X", c);
                printf("\n");
            }
            printf("%s\n", fs::get<fs::Dirent>({"root", ".fseventsd"}).type_string().c_str());
            main2();
        });*/
        // screw this VFS/stdio stuff, everything's irreparably broken so we won't be using it anymore
        rootfs = &fs;
        main2();
    });
}

void luaHook(lua_State *L, lua_Debug *ar) {
    lua_getinfo(L, "nSl", ar);
    printf("calling %s (%s:%d) @ %d\n", ar->name, ar->short_src, ar->linedefined, ar->currentline);
}

int main2 () {
    int status;
    lua_State *L;
    lua_State *coro;
    eventQueue._back = NULL;
    eventQueue._front = NULL;
    timerQueue._back = NULL;
    timerQueue._front = NULL;
    fs::Dirent bios_dir = rootfs->stat("bios.lua");
    if (!bios_dir.is_valid()) os::panic("Couldn't load BIOS file\n");
    fs::Buffer bios_buf = rootfs->read(bios_dir, 0, bios_dir.size() + (bios_dir.size() / 512));
start:
    printf("\n%s\n\n", osos);
    printf("Starting CraftOSOS...\n");
    /*
     * All Lua contexts are held in this structure. We work with it almost
     * all the time.
     */
    L = luaL_newstate();
    
    coro = lua_newthread(L);
    paramQueue = lua_newthread(L);
    
    luaL_openlibs(coro); /* Load Lua libraries */
    load_library(coro, bit_lib);
    load_library(coro, fs_lib);
    load_library(coro, os_lib);
    load_library(coro, rs_lib);
    lua_getglobal(coro, "redstone");
    lua_setglobal(coro, "rs");
    load_library(coro, term_lib);
    termInit();
    
    lua_pushstring(L, "bios.use_multishell=false");
    lua_setglobal(L, "_CC_DEFAULT_SETTINGS");
    lua_pushstring(L, "ComputerCraft 1.8 (CraftOSOS " CRAFTOSOS_VERSION ")");
    lua_setglobal(L, "_HOST");

    //lua_sethook(coro, luaHook, LUA_MASKCALL, 0);
    
    /* Load the file containing the script we are going to run */
    printf("Loading BIOS...\n");
    status = luaL_loadbuffer(coro, (const char*)bios_buf.data(), bios_buf.size(), "@bios.lua");
    if (status) {
        /* If something went wrong, error message is at the top of */
        /* the stack */
        const char * fullstr = lua_tostring(coro, -1);
        termClose();
        printf("Couldn't load BIOS: %s (%d)\n", fullstr, status);
        return 2;
    }
    
    /* Ask Lua to run our little script */
    status = LUA_YIELD;
    int narg = 0;
    printf("Running main coroutine.\n");
    while (status == LUA_YIELD && running == 1) {
        status = lua_resume(coro, narg);
        if (status == LUA_YIELD) {
            if (lua_isstring(coro, -1)) narg = getNextEvent(coro, lua_tostring(coro, -1));
            else narg = getNextEvent(coro, "");
        } else if (status != 0) {
            termClose();
            const char * fullstr = lua_tostring(coro, -1);
            printf("Errored: %s\n", fullstr);
            lua_close(L);
            return 1;
        }
        
    }
    printf("Closing session.\n");
    termClose();
    lua_close(L);   /* Cya, Lua */
    
    if (running == 2) {
        goto start;
    }
    return 0;
}
