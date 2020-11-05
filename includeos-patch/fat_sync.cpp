
#include "fat.hpp"

#include <fs/path.hpp>
#include <cassert>
#include <cstring>
#include <memory>
#include <common>
#include <os>

inline size_t roundup(size_t n, size_t multiple)
{
  return ((n + multiple - 1) / multiple) * multiple;
}

inline bool iequals(const std::string& a, const std::string& b)
{
    return std::equal(a.begin(), a.end(), b.begin(),
                      [](char a, char b) {
                          return tolower(a) == tolower(b);
                      });
}

//#define FS_PRINT(fmt, ...)  printf(fmt, ##__VA_ARGS__)
#define FS_PRINT(fmt, ...)  /** **/

namespace fs
{
  Buffer FAT_::read(const Dirent& ent, uint64_t pos, uint64_t n) const
  {
    // bounds check the read position and length
    auto stapos = std::min(ent.size(), pos);
    auto endpos = std::min(ent.size(), pos + n);
    // new length
    n = endpos - stapos;
    // cluster -> sector + position
    auto cluster = stapos / (this->sector_size * this->sectors_per_cluster);
    auto nclust = roundup(endpos, sector_size * sectors_per_cluster) / (sector_size * sectors_per_cluster) - cluster;
    FS_PRINT("reading %d clusters starting at %d (base %d, start %d, end %d)\n", nclust, cluster, ent.block(), stapos, endpos);

    std::pmr::vector<uint8_t> finaldata;
    finaldata.reserve(n);
    for (uint32_t cn = 0, cl = ent.block() + cluster; cn < nclust; cn++, cl = next_cluster(cl)) {
      FS_PRINT("reading cluster %d (%d)\n", cl, cl_to_sector(cl));
      buffer_t data = device.read_sync(this->cl_to_sector(cl), sectors_per_cluster);
      if (!data) os::panic(("Could not read sector " + std::to_string(this->cl_to_sector(cl)) + " at cluster " + std::to_string(cl) + "\n").c_str());
      if (nclust == 1 && endpos % (device.block_size() * sectors_per_cluster) != 0) finaldata.insert(finaldata.end(), data->begin() + (stapos % (device.block_size() * sectors_per_cluster)), data->begin() + (endpos % (device.block_size() * sectors_per_cluster)));
      else if (cn == 0) finaldata.insert(finaldata.end(), data->begin() + (stapos % (device.block_size() * sectors_per_cluster)), data->end());
      else if (cn == nclust - 1 && endpos % (device.block_size() * sectors_per_cluster) != 0) finaldata.insert(finaldata.end(), data->begin(), data->begin() + (endpos % (device.block_size() * sectors_per_cluster)));
      else finaldata.insert(finaldata.end(), data->begin(), data->end());
    }
    FS_PRINT("total size %d vs. %d\n", finaldata.size(), n);
    return Buffer(no_error, buffer_t::make_shared(finaldata));
  }

  error_t FAT_::int_ls(uint32_t cluster, dirvector& ents) const
  {
    if (ls_cache->find(cluster) != ls_cache->end()) {
      //printf("Reading cached ls at %d\n", sector);
      ents = ls_cache->at(cluster);
      return no_error;
    }
    bool done = false;
    uint32_t orig_sector = cluster;
    do {
      // read sector sync
      buffer_t data = device.read_sync(cl_to_sector(cluster), sectors_per_cluster);
      if (UNLIKELY(!data))
          return { error_t::E_IO, "Unable to read directory" };
      // parse directory into @ents
      done = int_dirent(cl_to_sector(cluster), data->data(), ents);
      // go to next sector until done
      cluster = next_cluster(cluster);
      if ((fat_type == T_FAT12 && cluster >= 0xFF8) || (fat_type == T_FAT16 && cluster >= 0xFFF8) || (fat_type == T_FAT32 && cluster >= 0xFFFFFFF8)) {
        if (!done) FS_PRINT("premature end of cluster chain for directory at cluster %d\n", orig_sector);
        break;
      }
    } while (!done);
    //printf("Inserting ls at %d\n", orig_sector);
    ls_cache->insert(std::make_pair(orig_sector, ents));
    return no_error;
  }

  error_t FAT_::traverse(Path path, dirvector& ents, const Dirent* const start) const
  {
    // start with given entry (defaults to root)
    uint32_t cluster = start ? start->block() : 0;
    Dirent found(this, INVALID_ENTITY);

    while (!path.empty()) {

      //auto S = this->cl_to_sector(cluster);
      ents.clear(); // mui importante
      // sync read entire directory
      auto err = int_ls(cluster, ents);
      if (UNLIKELY(err)) return err;
      // the name we are looking for
      const std::string name = path.front();
      path.pop_front();

      // check for matches in dirents
      for (auto& e : ents) {
        if (UNLIKELY(iequals(e.name(), name))) {
          // go to this directory, unless its the last name
          FS_PRINT("traverse_sync: Found match for %s", name.c_str());
          // enter the matching directory
          FS_PRINT("\t\t cluster: %lu\n", e.block());
          // only follow if the name is a directory
          if (e.type() == DIR) {
            found = e;
            break;
          }
          else {
            // not dir = error, for now
            return { error_t::E_NOTDIR, "Cannot list non-directory" };
          }
        }
      } // for (ents)

      // validate result
      if (found.type() == INVALID_ENTITY) {
        FS_PRINT("traverse_sync: NO MATCH for %s\n", name.c_str());
        return { error_t::E_NOENT, name };
      }
      // set next cluster
      cluster = found.block();
    }

    //auto S = this->cl_to_sector(cluster);
    // read result directory entries into ents
    ents.clear(); // mui importante!
    return int_ls(cluster, ents);
  }

  List FAT_::ls(const std::string& strpath) const
  {
    auto ents = std::make_shared<dirvector> ();
    auto err = traverse(strpath, *ents);
    return { err, ents };
  }

  List FAT_::ls(const Dirent& ent) const
  {
    auto ents = std::make_shared<dirvector> ();
    // verify ent is a directory
    if (!ent.is_valid() || !ent.is_dir())
      return { { error_t::E_NOTDIR, ent.name() }, ents };
    // convert cluster to sector
    //auto S = this->cl_to_sector(ent.block());
    // read result directory entries into ents
    auto err = int_ls(ent.block(), *ents);
    return { err, ents };
  }

  Dirent FAT_::stat(Path path, const Dirent* const start) const
  {
    if (UNLIKELY(path.empty())) {
      return Dirent(this, Enttype::DIR, "/", 0);
    }

    FS_PRINT("stat_sync: %s\n", path.back().c_str());
    // extract file we are looking for
    const std::string filename = path.back();
    path.pop_back();

    // result directory entries are put into @dirents
    dirvector dirents;

    auto err = traverse(path, dirents, start);
    if (UNLIKELY(err)) {
      FS_PRINT("error traversing: %s\n", err.to_string().c_str());
      return Dirent(this, INVALID_ENTITY); // for now
    }

    // find the matching filename in directory
    for (auto& e : dirents)
    if (UNLIKELY(iequals(e.name(), filename))) {
      // return this directory entry
      FS_PRINT("found: %s\n", filename.c_str());
      return e;
    }
    FS_PRINT("not found: %s\n", filename.c_str());
    // entry not found
    return Dirent(this, INVALID_ENTITY);
  }
}
