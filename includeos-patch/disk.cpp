
#include "disk.hpp"
#include <fs/mbr.hpp>
#include "fat.hpp"
#include <cassert>
#include <info>

namespace fs {

  Disk_::Disk_(hw::Block_device& dev)
    : device {dev} {}

  void Disk_::partitions(on_parts_func func) {

    /** Read Master Boot Record (sector 0) */
    device.read(
      0,
      hw::Block_device::on_read_func::make_packed(
      [func] (hw::Block_device::buffer_t data)
      {
        std::vector<fs::Partition> parts;

        if (!data) {
          func({ error_t::E_IO, "Unable to read MBR"}, parts);
          return;
        }

        // First sector is the Master Boot Record
        auto* mbr =(MBR::mbr*) data->data();

        for (int i {0}; i < 4; ++i) {
          // all the partitions are offsets to potential Volume Boot Records
          parts.emplace_back(
            mbr->part[i].flags,     //< flags
            mbr->part[i].type,      //< id
            mbr->part[i].get_LBA(), //< LBA
            mbr->part[i].get_sectors());
        }

        func(no_error, parts);
      })
    );
  }

  void Disk_::init_fs(on_init_func func)
  {
    device.read(
      0,
      hw::Block_device::on_read_func::make_packed(
      [this, func] (hw::Block_device::buffer_t data)
      {
        if (UNLIKELY(!data)) {
          // TODO: error-case for unable to read MBR
          func({ error_t::E_IO, "Unable to read MBR"}, *filesys);
          return;
        }

        // auto-detect FAT on MBR:
        auto* mbr = (MBR::mbr*) data->data();
        MBR::BPB* bpb = mbr->bpb();

        if (bpb->bytes_per_sector >= 512
         && bpb->fa_tables != 0
         && (bpb->signature != 0 // check MBR signature too
         || bpb->large_sectors != 0)) // but its not set for FAT32
        {
          // detected FAT on MBR
          filesys.reset(new FAT_(device));
          // initialize on MBR
          internal_init(MBR, func);
          return;
        }

        // go through partition list
        for (int i = 0; i < 4; i++)
        {
          if (mbr->part[i].type != 0       // 0 is unused partition
           && mbr->part[i].lba_begin != 0  // 0 is MBR anyways
           && mbr->part[i].sectors != 0)   // 0 means no size, so...
          {
            // FIXME: for now we can only assume FAT, anyways
            // To be replaced with lookup table for partition identifiers,
            // but we really only have FAT atm, so its just wasteful
            filesys.reset(new FAT_(device));
            // initialize on VBRn
            internal_init((partition_t) (VBR1 + i), func);
            return;
          }
        }

        // no partition was found (TODO: extended partitions)
        func({ error_t::E_MNT, "No FAT partition auto-detected"}, fs());
      })
    );
  }

  void Disk_::init_fs(partition_t part, on_init_func func)
  {
    filesys.reset(new FAT_(device));
    internal_init(part, func);
  }

  void Disk_::internal_init(partition_t part, on_init_func func) {

    if (part == MBR)
    {
      // For the MBR case, all we need to do is initialize on sector 0
      fs().init(0, device.size(), func);
    }
    else
    {
      /**
       *  Otherwise, we will have to read the LBA offset
       *  of the partition to be initialized
       */
      device.read(
        0,
        hw::Block_device::on_read_func::make_packed(
        [this, part, func] (hw::Block_device::buffer_t data)
        {
          if (UNLIKELY(!data)) {
            // TODO: error-case for unable to read MBR
            func({ error_t::E_IO, "Could not read MBR" }, *filesys);
            return;
          }

          auto* mbr = (MBR::mbr*) data->data();
          auto pint = (int) part - 1;

          auto lba_base = mbr->part[pint].lba_begin;
          auto lba_size = mbr->part[pint].sectors;
          assert(lba_size && "No such partition (length was zero)");

          // Call the filesystems init function
          // with lba_begin as base address
          fs().init(lba_base, lba_size, func);
        })
      );
    }
  }

  std::string Partition::name() const {
    return MBR::id_to_name(id);
  }

} //< namespace fs
