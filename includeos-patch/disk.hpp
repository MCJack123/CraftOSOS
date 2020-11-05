
#pragma once
#ifndef FS_DISK_PATCH_HPP
#define FS_DISK_PATCH_HPP

#include <fs/common.hpp>
#include <fs/dirent.hpp>
#include <fs/filesystem.hpp>
#include <fs/partition.hpp>
#include <common>
#include <hw/block_device.hpp>
#include <deque>
#include <vector>

namespace fs
{
  /**
   * Class to initialize file systems on block devices / partitions
   **/
  class Disk_ {
  public:
    class Err_not_mounted : public std::runtime_error {
      using runtime_error::runtime_error;
    };

    using on_init_func  = delegate<void(fs::error_t, File_system&)>;
    using on_parts_func = delegate<void(fs::error_t, std::vector<fs::Partition>&)>;

    enum partition_t {
      MBR = 0, //< Master Boot Record (0)
      VBR1,   //> extended partitions (1-4)
      VBR2,
      VBR3,
      VBR4,
    };

    // construct a disk with a given block-device
    explicit Disk_(hw::Block_device&);

    std::string name() const {
      return device.device_name();
    }

    auto device_id() const {
      return device.id();
    }

    // Returns true if the disk has no sectors
    bool empty() const noexcept
    { return device.size() == 0; }

    // Initializes file system on the first partition detected (MBR -> VBR1-4 -> ext)
    // NOTE: Always detects and instantiates a FAT filesystem
    void init_fs(on_init_func func);

    // Initialize file system on specified partition
    // NOTE: Always detects and instantiates a FAT filesystem
    void init_fs(partition_t part, on_init_func func);

    // mount custom filesystem on MBR or VBRn
    template <class T, class... Args>
    void init_fs(Args&&... args, partition_t part, on_init_func func)
    {
      // construct custom filesystem
      filesys.reset(new T(args...));
      internal_init(part, func);
    }

    // returns true if a filesystem is initialized
    bool fs_ready() const noexcept
    { return filesys != nullptr; }

    // Returns a reference to an initialized filesystem
    // If no filesystem is initialized it will throw an error
    File_system& fs()
    {
      if(UNLIKELY(not fs_ready()))
      {
        throw Err_not_mounted{"Filesystem not ready - make sure to init_fs before accessing"};
      }
      return *filesys;
    }

    // Creates a vector of the partitions on disk (see: on_parts_func)
    // Note: No file system needs to be initialized beforehand
    void partitions(on_parts_func func);

    // returns the device the disk is using
    hw::Block_device& dev() noexcept
    { return device; }

  private:
    void internal_init(partition_t part, on_init_func func);

    hw::Block_device& device;
    std::unique_ptr<File_system> filesys;
  }; //< class Disk

  using Disk__ptr = std::shared_ptr<Disk_>;

} //< namespace fs

#endif //< FS_DISK_HPP
