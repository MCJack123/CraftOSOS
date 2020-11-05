# CraftOSOS
ComputerCraft as an x86 PC operating system. Mostly emulates ComputerCraft 1.8. Based on [CraftOS-EFI](https://github.com/MCJack123/craftos-efi), which in turn is based on [craftos-native](https://github.com/MCJack123/craftos-native) - this is NOT compatible with [CraftOS-PC](https://github.com/MCJack123/craftos2)!

## Features
* Decent emulation of ComputerCraft 1.80pr1
* 53x26 terminal with proper font and text colors
* Compatibility with real x86 machines - bootable as an OS on bare metal

## To-do
* Fix timers (they currently either immediately fire or never fire)
* Fix cursor blinking (linked to timers)
* Add HTTP support
* Improve error checking
* Add support for writing files
* Add drive scanning to support non-IDE/non-primary drives
* Add support for drive peripherals to mount floppy disks or CD/DVDs

## Missing features
* CC: Tweaked/CraftOS-PC features
* Peripheral emulation (maybe something later?)
* Redstone emulation
* USB support

## Usage
The most recent version is available in the Releases tab as a disk image (.img) that can either be booted with a virtual machine like QEMU or VirtualBox (conversion is required for VBox), or written to a real hard drive. At the moment, CraftOSOS can only search IDE drives for ROM files. Because of this, it will not work on a USB flash drive.

### Requirements
* x86-64 PC or virtual machine
* 32 MB RAM
* Monitor and video card that support 16-color 640x480 VGA (this should be standard)
* IDE interface for storage, or SATA in ATA emulation mode
  * All ROM files must be on the first partition of the primary master drive, formatted as FAT
  * 20 MB should be enough space to fit everything (GRUB, chainloader, kernel, ROM)
* PS/2 keyboard, or a BIOS that supports PS/2 emulation for USB
* For debug messages, a serial port is required

### Booting with QEMU
To boot the built image with QEMU, run this command:
```
qemu-system-x86_64 -hda CraftOSOS.img -cpu kvm64,+rdrand,+rdseed
```

### Booting with VirtualBox
First, you'll need to convert the image to a VDI with `VBoxManage`:
```
VBoxManage convertfromraw CraftOSOS.img CraftOSOS.vdi --format VDI
```
Then create a 64-bit Other OS VM using the VDI created earlier.

### Installing on a real system
Connect an IDE or SATA hard drive to your main computer, and flash the IMG file to the entire disk (NOT a partition!). On Windows, Rufus should work fine for this. On Mac or Linux, you can run this command:
```
sudo dd if=CraftOSOS.img of=/dev/<disk> bs=512
```
where `<disk>` is the name of the disk's block device, found on Mac with `diskutil list` and on Linux with `lsblk`. Then move the drive to the target system on the primary bus (if using IDE, make sure the jumper is set to master, or cable select with the drive on the end connector). If using a SATA drive, open the BIOS and ensure ATA emulation mode is set (it may be called something different on your system - in general, search for ATA, IDE, AHCI, or sometimes Intel(R) Rapid Storage Technology). Finally, boot to the hard drive normally, and select the CraftOSOS option when prompted.

## Compiling
### Requirements
* [IncludeOS](https://github.com/includeos/includeos)
  * You'll need the [Conan](https://conan.io) package manager for this
* CMake 3.0 or later
* C++14-compatible G++/Clang compiler
  * Windows: MinGW
  * Mac: Xcode
  * Linux: GCC/G++ or LLVM/Clang

### Setting up
You'll need to create a ROM disk image before running CraftOSOS. To do this, create an IMG file with ~20 MB of space, with an MBR partition table with one FAT partition. Then mount it and copy `bios.lua` and `rom/` from this repo to the partition. Remember to unmount and eject the image before booting.

These commands should work on Linux:
```
$ dd if=/dev/zero of=rom.img bs=1k count=20480
$ fdisk rom.img
: n
: p
: 1
: <enter>
: <enter>
: t
: 04
: w
$ kpartx -a rom.img
$ kpartx -l rom.img # to see mounted partitions
$ sudo mkfs.msdos -L CRAFTOS /dev/mapper/loop0p1
$ sudo mount /dev/mapper/loop0p1 /mnt
$ sudo cp -r bios.lua rom /mnt/
$ sudo umount /mnt
$ kpartx -d rom.img
```

For macOS:
```
$ dd if=/dev/zero of=rom.img bs=1k count=20480
$ hdiutil attach -nomount rom.img
$ diskutil partitionDisk disk2 1 MBR MS-DOS CRAFTOS 0
$ diskutil mount disk2s1
$ cp -r bios.lua rom /Volumes/CRAFTOS/
$ diskutil eject disk2
```

### Building
From the repository root:
```
$ mkdir build
$ cd build
$ conan install .. -pr <profile>
$ source activate.sh
$ cmake ..
$ make
$ boot craftos
```
Replace `<profile>` with the Conan profile to use. On Mac, use `clang-6.0-macos-x86_64`, and on Linux, use `gcc-7.3.0-linux-x86_64` or `clang-6.0-linux-x86_64`. Also make sure `rom.img` is placed in `build` before booting.

On macOS I've experienced an issue where CMake detects the wrong linker and strip executables. If this happens to you, try adding these lines to `CMakeLists.txt`:
```cmake
set(CMAKE_LINKER /Users/<username>/.conan/data/binutils/2.31/includeos/toolchain/package/8dc70706baab1939ba428617495e811067e89812/x86_64-elf/bin/ld)
set(CMAKE_STRIP /Users/<username>/.conan/data/binutils/2.31/includeos/toolchain/package/8dc70706baab1939ba428617495e811067e89812/x86_64-elf/bin/strip)
```
Replace `<username>` with your short username. If the path doesn't exist, try tweaking the hash between `package/` and `/x86_64-elf`.