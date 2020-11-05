#include "fs.h"
extern "C" {
#include "fs_handle.h"
#include "lua/lauxlib.h"
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef NULL
#define NULL (void*)0
#endif
#include <unistd.h>
//#include <sys/stat.h>
//#include <libgen.h>
#include <errno.h>
#include <dirent.h>
#include <ide>
#include <fs/filesystem.hpp>
#include <posix/file_fd.hpp>
#include <posix/fd_map.hpp>

extern fs::File_system * rootfs;
extern FILE *__fdopen(int fd, const char *mode);
extern FILE *fdopen(int fd, const char *mode);

char* basename(char* path) {
    char* filename = strrchr(path, '/');
    if (filename == NULL)
        filename = path;
    else
        filename++;
    return filename;
}

char* dirname(char* path) {
    if (path[0] == '/') strcpy(path, &path[1]);
    char tch;
    if (strrchr(path, '/') != NULL) tch = '/';
    else if (strrchr(path, '\\') != NULL) tch = '\\';
    else return path;
    path[strrchr(path, tch) - path] = '\0';
    return path;
}

void err(lua_State *L, const char * path, const char * err) {
    luaL_error(L, "%s: %s (%d)", path, err, errno);
}

char * unconst(const char * str) {
    char * retval = (char*)malloc(strlen(str) + 1);
    strcpy(retval, str);
    return retval;
}

int fs_list(lua_State *L) {
    struct dirent *dir;
    const char * path = luaL_checkstring(L, 1);
    fs::Dirent d = rootfs->stat(path);
    if (d.is_dir()) {
        lua_newtable(L);
        fs::List list = d.ls();
        std::sort(list.begin(), list.end(), [](fs::Dirent a, fs::Dirent b)->bool {return a.name() < b.name();});
        for (int i = 0, io = 0; i < list.entries->size(); i++) {
            fs::Dirent dir = (*list.entries)[i];
            if ((!dir.is_dir() && !dir.is_file()) || ((dir.attrib() & 0x0F) != 0x0F && (dir.attrib() & 0x02))) {io++; continue;}
            lua_pushinteger(L, i - io);
            lua_pushlstring(L, dir.name().c_str(), dir.name().size());
            lua_settable(L, -3);
        }
    } else err(L, path, "Not a directory");
    return 1;
}

int fs_exists(lua_State *L) {
    const char * path = luaL_checkstring(L, 1);
    fs::Dirent d = rootfs->stat(path);
    lua_pushboolean(L, d.is_valid());
    return 1;
}

int fs_isDir(lua_State *L) {
    const char * path = luaL_checkstring(L, 1);
    fs::Dirent d = rootfs->stat(path);
    lua_pushboolean(L, d.is_dir());
    return 1;
}

int fs_isReadOnly(lua_State *L) {
    const char * path = luaL_checkstring(L, 1);
    lua_pushboolean(L, true);
    //lua_pushboolean(L, access(path, W_OK) != 0);
    return 1;
}

int fs_getName(lua_State *L) {
    char * path = unconst(luaL_checkstring(L, 1));
    lua_pushstring(L, basename(path));
    free(path);
    return 1;
}

int fs_getDrive(lua_State *L) {
    lua_pushstring(L, "hdd");
    return 1;
}

int fs_getSize(lua_State *L) {
    const char * path = luaL_checkstring(L, 1);
    fs::Dirent d = rootfs->stat(path);
    if (d.is_file()) lua_pushinteger(L, d.size());    
    else if (d.is_dir()) lua_pushinteger(L, 0);
    else err(L, path, "No such file");
    return 1;
}

int fs_getFreeSpace(lua_State *L) {
    lua_pushinteger(L, 100000000);
    return 1;
}

// tmp
int mkdir(const char * path, int mode) {return 0;}

int recurse_mkdir(const char * path) {
    if (mkdir(path, 0777) != 0) {
        if (errno == ENOENT && strcmp(path, "/") != 0) {
            if (recurse_mkdir(dirname(unconst(path)))) return 1;
            mkdir(path, 0777);
            return 0;
        } else return 1;
    } else return 0;
}

int fs_makeDir(lua_State *L) {
    const char * path = luaL_checkstring(L, 1);
    if (recurse_mkdir(path) != 0) err(L, path, strerror(errno));
    return 0;
}

int fs_move(lua_State *L) {
    const char * fromPath = luaL_checkstring(L, 1);
    const char * toPath = luaL_checkstring(L, 2);
    if (rename(fromPath, toPath) != 0) {
        err(L, fromPath, strerror(errno));
    }
    return 0;
}

int fs_copy(lua_State *L) {
    const char * fromPath = luaL_checkstring(L, 1);
    const char * toPath = luaL_checkstring(L, 2);

    FILE * fromfp = fopen(fromPath, "r");
    if (fromfp == NULL) {
        err(L, fromPath, "Cannot read file");
    }
    FILE * tofp = fopen(toPath, "w");
    if (tofp == NULL) {
        fclose(fromfp);
        err(L, toPath, "Cannot write file");
    }

    char tmp[1024];
    while (!feof(fromfp)) {
        int read = fread(tmp, 1, 1024, fromfp);
        fwrite(tmp, read, 1, tofp);
        if (read < 1024) break;
    }

    fclose(fromfp);
    fclose(tofp);
    return 0;
}

int fs_delete(lua_State *L) {
    const char * path = luaL_checkstring(L, 1);
    if (unlink(path) != 0) err(L, path, strerror(errno));
    return 0;
}

int fs_combine(lua_State *L) {
    size_t blen = 0, llen = 0;
    const char * basePath = luaL_checklstring(L, 1, &blen);
    const char * localPath = luaL_checklstring(L, 2, &llen);
    char * retval = (char*)malloc(blen + llen + 2);
    retval[blen+llen+1] = 0;
    if (basePath[0] == '/') strcpy(retval, basePath + 1);
    else strcpy(retval, basePath);
    if (basePath[blen-1] != '/') strcat(retval, "/");
    if (localPath[0] == '/') strcat(retval, localPath + 1);
    else strcat(retval, localPath);
    if (localPath[llen-1] == '/') retval[strlen(retval)-1] = 0;
    lua_pushstring(L, retval);
    free(retval);
    return 1;
}

int fs_open(lua_State *L) {
    const char * path = luaL_checkstring(L, 1);
    const char * mode = luaL_checkstring(L, 2);
    fs::Dirent d = rootfs->stat(path);
    if (!d.is_file()) err(L, path, "No such file");
    FILE * fp = fdopen(FD_map::_open<File_FD>(d).get_id(), mode);
    if (fp == NULL) err(L, path, strerror(errno));
    lua_newtable(L);
    lua_pushstring(L, "close");
    lua_pushlightuserdata(L, fp);
    lua_pushcclosure(L, handle_close, 1);
    lua_settable(L, -3);
    if (strcmp(mode, "r") == 0) {
        lua_pushstring(L, "readAll");
        lua_pushlightuserdata(L, fp);
        lua_pushcclosure(L, handle_readAll, 1);
        lua_settable(L, -3);

        lua_pushstring(L, "readLine");
        lua_pushlightuserdata(L, fp);
        lua_pushcclosure(L, handle_readLine, 1);
        lua_settable(L, -3);

        lua_pushstring(L, "read");
        lua_pushlightuserdata(L, fp);
        lua_pushcclosure(L, handle_readChar, 1);
        lua_settable(L, -3);
    } else if (strcmp(mode, "w") == 0 || strcmp(mode, "a") == 0) {
        lua_pushstring(L, "write");
        lua_pushlightuserdata(L, fp);
        lua_pushcclosure(L, handle_writeString, 1);
        lua_settable(L, -3);

        lua_pushstring(L, "writeLine");
        lua_pushlightuserdata(L, fp);
        lua_pushcclosure(L, handle_writeLine, 1);
        lua_settable(L, -3);

        lua_pushstring(L, "flush");
        lua_pushlightuserdata(L, fp);
        lua_pushcclosure(L, handle_flush, 1);
        lua_settable(L, -3);
    } else if (strcmp(mode, "rb") == 0) {
        lua_pushstring(L, "read");
        lua_pushlightuserdata(L, fp);
        lua_pushcclosure(L, handle_readByte, 1);
        lua_settable(L, -3);
    } else if (strcmp(mode, "wb") == 0) {
        lua_pushstring(L, "write");
        lua_pushlightuserdata(L, fp);
        lua_pushcclosure(L, handle_writeByte, 1);
        lua_settable(L, -3);

        lua_pushstring(L, "flush");
        lua_pushlightuserdata(L, fp);
        lua_pushcclosure(L, handle_flush, 1);
        lua_settable(L, -3);
    } else {
        lua_remove(L, -1);
        fclose(fp);
        err(L, unconst(mode), "Invalid mode");
    }
    return 1;
}

int fs_find(lua_State *L) {
    lua_newtable(L);
    return 1;
}

int fs_getDir(lua_State *L) {
    char * path = unconst(luaL_checkstring(L, 1));
    lua_pushstring(L, dirname(path));
    free(path);
    return 1;
}

const char * fs_keys[16] = {
    "list",
    "exists",
    "isDir",
    "isReadOnly",
    "getName",
    "getDrive",
    "getSize",
    "getFreeSpace",
    "makeDir",
    "move",
    "copy",
    "delete",
    "combine",
    "open",
    "find",
    "getDir"
};

lua_CFunction fs_values[16] = {
    fs_list,
    fs_exists,
    fs_isDir,
    fs_isReadOnly,
    fs_getName,
    fs_getDrive,
    fs_getSize,
    fs_getFreeSpace,
    fs_makeDir,
    fs_move,
    fs_copy,
    fs_delete,
    fs_combine,
    fs_open,
    fs_find,
    fs_getDir
};

library_t fs_lib = {"fs", 16, fs_keys, fs_values};