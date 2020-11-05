
#pragma once
#ifndef FS_FAT_PATCH_HPP
#define FS_FAT_PATCH_HPP

#include <fs/filesystem.hpp>
#include <fs/dirent.hpp>
#include <hw/block_device.hpp>
#include <functional>
#include <cstdint>
#include <memory>
#include <map>

namespace fs
{
  class Path;

  struct FAT_ : public File_system
  {
    /// ----------------------------------------------------- ///
    void init(uint64_t lba, uint64_t size, on_init_func on_init) override;

    int device_id() const noexcept override {
      return device.id();
    }

    // path is a path in the initialized filesystem
    void  ls     (const std::string& path, on_ls_func) const override;
    void  ls     (const Dirent& entry,     on_ls_func) const override;
    List  ls(const std::string& path) const override;
    List  ls(const Dirent&) const override;

    /** Read @n bytes from file pointed by @entry starting at position @pos */
    void   read(const Dirent&, uint64_t pos, uint64_t n, on_read_func) const override;
    Buffer read(const Dirent&, uint64_t pos, uint64_t n) const override;

    // return information about a filesystem entity
    void   stat(Path_ptr, on_stat_func, const Dirent* const start) const override;
    Dirent stat(Path ent, const Dirent* const start) const override;
    // async cached stat
    void cstat(const std::string&, on_stat_func) override;

    // returns the name of the filesystem
    std::string name() const override
    {
      switch (this->fat_type)
        {
        case T_FAT12:
          return "FAT12";
        case T_FAT16:
          return "FAT16";
        case T_FAT32:
          return "FAT32";
        }
      return "Invalid fat type";
    }

    uint64_t block_size() const noexcept override
    { return device.block_size(); }
    /// ----------------------------------------------------- ///

    // constructor
    FAT_(hw::Block_device& dev);
    virtual ~FAT_() {
      for (int i = 0; i < fat_count; i++) free(file_allocation_tables[i]);
      free(file_allocation_tables);
    }

  private:
    // FAT types
    static const int T_FAT12 = 0;
    static const int T_FAT16 = 1;
    static const int T_FAT32 = 2;

    // helper functions
    uint32_t cl_to_sector(uint32_t const cl) const
    {
      if (cl <= 2)
        return lba_base + data_index + (this->root_cluster - 2) * sectors_per_cluster - this->root_dir_sectors;
      else
        return lba_base + data_index + (cl - 2) * sectors_per_cluster;
    }

    uint32_t next_cluster(uint32_t const cl) const {
      if (fat_type == T_FAT12) {
        if (cl % 2) return (((uint16_t)(((uint8_t**)file_allocation_tables)[0][((cl) / 2) * 3 + 1] & 0xF0) >> 4) | ((uint16_t)((uint8_t**)file_allocation_tables)[0][((cl) / 2) * 3 + 2] << 4));
        else return (((uint16_t)(((uint8_t**)file_allocation_tables)[0][((cl) / 2) * 3 + 1] & 0x0F) << 8) | (uint16_t)(((uint8_t**)file_allocation_tables)[0][((cl) / 2) * 3]));
      } else if (fat_type == T_FAT16)
        return (((uint16_t**)file_allocation_tables)[0][cl]);
      else
        return (((uint32_t**)file_allocation_tables)[0][cl]);
    }

    uint16_t cl_to_entry_offset(uint32_t cl) const
    {
      if (fat_type == T_FAT16)
        return (cl * 2) % sector_size;
      else // T_FAT32
        return (cl * 4) % sector_size;
    }
    uint16_t cl_to_entry_sector(uint32_t cl) const
    {
      if (fat_type == T_FAT16)
        return reserved + (cl * 2 / sector_size);
      else // T_FAT32
        return reserved + (cl * 4 / sector_size);
    }

    // initialize filesystem by providing base sector
    void init(const void* base_sector);
    // return a list of entries from directory entries at @sector
    typedef delegate<void(error_t, Dirvec_ptr)> on_internal_ls_func;
    void int_ls(uint32_t sector, Dirvec_ptr, on_internal_ls_func) const;
    bool int_dirent(uint32_t sector, const void* data, dirvector&) const;

    // tree traversal
    typedef delegate<void(error_t, Dirvec_ptr)> cluster_func;
    // async tree traversal
    void traverse(std::shared_ptr<Path> path, cluster_func callback, const Dirent* const = nullptr) const;
    // sync version
    error_t traverse(Path path, dirvector&, const Dirent* const = nullptr) const;
    error_t int_ls(uint32_t cluster, dirvector&) const;

    // device we can read and write sectors to
    hw::Block_device& device;

    /// private members ///
    // the location of this partition
    uint32_t lba_base;
    // the size of this partition
    uint32_t lba_size;

    uint16_t sector_size; // from bytes_per_sector
    uint32_t sectors;   // total sectors in partition
    uint32_t clusters;  // number of indexable FAT clusters

    uint8_t  fat_type;  // T_FAT12, T_FAT16 or T_FAT32
    uint16_t reserved;  // number of reserved sectors

    uint32_t sectors_per_fat;
    uint16_t sectors_per_cluster;
    uint16_t root_dir_sectors; // FAT16 root entries

    uint32_t root_cluster;  // index of root cluster
    uint32_t data_index;    // index of first data sector (relative to partition)
    uint32_t data_sectors;  // number of data sectors
    uint16_t fat_count;     // number of FATs

    // simplistic cache for stat results
    std::map<std::string, Dirent> stat_cache;
    std::unique_ptr<std::map<uint32_t, dirvector> > ls_cache;
    void ** file_allocation_tables;
  };

} // fs

#endif
