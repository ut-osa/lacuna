# Requirements

Lacuna requires a fair amount of setup, since it uses modifications to
a virtual machine monitor (VMM) and OS (Linux) in order to provide
full-system privacy.  Setup and use of Lacuna's various components is
fairly modular, so you may be able to try out some of them without
having all of the appropriate hardware.

## Skill requirements

You should be familiar with using Linux, e.g., loading kernel modules.
You should be familiar with compiling a Linux kernel and use of
QEMU-KVM.  You should at least skim the Lacuna paper mentioned in the
README (or have it available) to understand some of the terminology in
these instructions.

## Hardware requirements

Lacuna has been tested on:

Dell Optiplex 9010 (quad-core Core i7 3770 3.4 GHz w/AES-NI, 8 GB RAM,
1 Gbps onboard NIC) - here private graphical output not tested

Dell Studio XPS 8100 (dual-core Core i5 650 3.2 GHz w/AES-NI, 12 GB
RAM, 1 Gbps onboard NIC) with a NVIDIA GeForce GTX 470 (new power
supply required to support the GeForce)

Lenovo ThinkPad T510 (dual-core i7 M 620 2.7 GHz w/AES-NI, 8 GB RAM) -
private input only

- Core requirements

	Lacuna requires an Intel x86-based computer.  You will need
	hardware virtualization support (Intel VT-x).  The performance of
	Lacuna will be improved if your computer supports AES-NI, and some
	pieces currently require AES-NI that in theory could be done
	otherwise (but probably shouldn't).  Note that you may need to
	turn on some features like VT-x in your BIOS to use them if your
	computer supports them.

- Private input

	You will need a USB keyboard and mouse.  For the hardware ephemeral
	channel mode, you will need an IOMMU (Intel VT-d).

- Private sound

	You will need an Intel HDA-compatible sound card, in particular, one
	that is supported by the `snd_hda_intel` driver.  Many built-in sound
	cards meet this requirement.

- Private networking

	You will need a network card supported by the `e1000e` driver.  Many
	built-in network cards meet this requirement.  You will need to be
	able to bridge your VM onto your local network.

- Private graphical output

    You will need a CUDA-capable graphics card.  AES-NI is required here.

- Private storage

    You need AES-NI for now.  (This could theoretically be done
    without AES-NI, but would lead to significant CPU cost.)

## Software requirements

You need a Linux desktop distribution.

We will describe installation instructions for an Ubuntu 12.04.4
Desktop system.  It is likely possible to use Lacuna on other Linux
distributions and versions, but some setup instructions will differ
(e.g. which packages to install).

# Core setup

1. Install prerequisite packages

        apt-get install libgcrypt11-dev zlib1g-dev libglib2.0-dev libsdl1.2-dev libpci-dev

2. Build cryptographic support

    In `crypto/polarssl-1.1.1`, `make`.

    In `crypto/libprivcrypt`, read `config.mak` and edit if necessary.
    (If you're using AES-NI, it should be fine as is.)  Run `make` in
    `crypto/libprivcrypt`.

3. Build QEMU

    In `host/qemu-kvm-0.15.1`, run `./configure --help` to see the set
    of options for compiling QEMU.  Depending on the parts of Lacuna
    you intend to use, you can build in fewer options.  To build all
    the pieces of Lacuna, use `./configure --enable-priv-input
    --enable-priv-net --enable-priv-sound --audio-drv-list=alsa
    --enable-priv-gpu --enable-priv-diff`.  If you have AES-NI, you
    should probably add `--enable-qcow-aesni`.

    `--enable-priv-gpu` adds some extra work to the build process, see
    the section [Private graphical output](#s_priv_graphics).

3. Build the Linux kernel

	Both the host and guest will use the same kernel, a modified
	version of Linux 3.0.0 (included in the distribution).  We have
	included a kernel configuration that may work for you
	(`privos.config` in `host/linux-3.0`), but depending on the
	options your kernel needs you may have to change this
	configuration.  We provide a mechanism for building the kernel
	outside of the source tree:

    - In `host/kbuild`, do `make -f Makefile.setup`
	
    - Run `make` to build the kernel and its modules.  (If you have
      multiple cores on your system, you may want to use `make
      -j<number>` to speed the kernel build process up.)

4. Install the kernel on the host

    From whereever your kernel is built, `sudo make install
    modules_install`.

5. Create a VM image

    Use QEMU to install Ubuntu on a VM image.  Lacuna was tested with
    the same version (12.04.4) on the image as on the main desktop.

    You might want to use the `kvm_linux` script for running QEMU, see
    its [documentation](kvm_linux.md).

6. Install the Linux kernel to your image

    This step is not strictly necessary for most of the components of
    Lacuna.  (It is required for the hardware input ephemeral
    channel.)  However, it is possible that you will experience
    problems due to variability between kernels by not doing so (e.g.,
    see the private graphical output section).

    One way to do this is through `nbd`.  This is a kernel subsystem
    that lets you mount network accessible drives.  QEMU supplies
    `qemu-nbd` to export a drive that you can then be read by `nbd`.
    We provide a script `utils/connect_hd` that allows you to mount a
    VM image using `qemu-nbd`.  Make sure you have the `nbd` kernel
    module loaded with `max_part=8` as an option to ensure that the
    partition devices (e.g. `nbd0p1`) are created and that `nbd0` is
    free for use.  Also, `connect_hd` uses the mount path `/mnt/vm1`
    (defined in `utils/environment`, so it can be modified), so make
    sure this directory exists or modify `utils/environment`.
    `utils/disconnect_hd` disconnects the drive so that it can then be
    used with QEMU.

    Once `connect_hd` and `disconnect_hd` work, you can use our
    provided script `utils/install_kernel` (use `utils/install_kernel
    <image name>`, starting from having no image connected) to install
    the kernel into your image.  Alternatively, you can look at the
    variations of the `install` and `modules_install` make rules that
    `utils/install_kernel` performs and do the work yourself.  We
    noticed that some of the kernel post-installation scripts on
    Ubuntu do not respect the alternate kernel install location,
    e.g. the GRUB configuration of the image is not updated, rather,
    the host's is instead.  So you might have to modify GRUB settings
    on your image.

# Using Lacuna

Using Lacuna is much like using any other QEMU VM.  Lacuna (and QEMU)
has a lot of command-line options, so we provide a script
`utils/kvm_linux` to simplify the process of providing some of the
arguments.  That script is documented [here](kvm_linux.md).

Before you run Lacuna, you need to run the `priv_crypt_mkdev` script
(`crypto/kernel/script/priv_crypt_mkdev`) as root if you have not done
so since boot.  This creates a device that is necesssary for operation
of Lacuna's cryptographic mechanisms.

One difference with normal QEMU operation is that you must use QEMU's
"input grab" option.  Normally if you mouse over the window for a QEMU
VM and press keys or click, the input will go to the VM.  However, for
Lacuna, you have to specifically transfer into the private session.

To put input into your VM, mouse over it and press "Left Ctrl-Left
Alt".  There will then be a slight delay after which your keystrokes
will register in the VM.  The title bar of the QEMU window will also
change.  Now all your input is going to the VM (and not the rest of
the system).  To interact with the rest of your system again, press
"Left Ctrl-Left Alt".  You can enter and exit the private session as
many times as you need.

When you are finished with the private session, kill the QEMU process.
(There is likely no reason to properly shut down the QEMU process, as
no state that is going to be used later is held in memory that needs
to be persisted.  Lacuna's protections will still engage even if the
process does not exit normally.)

# Component setup

## Private input

If you are going to use the encrypted ephemeral channel method, you
have to tell Lacuna which USB devices to use.  If you are going to use
the `kvm_linux` script, edit `utils/environment` to fill in the lines
`USB_KBD` and `USB_MOUSE`.  The value to put for each of these is of
the form `<bus number>.<device number>`.  For example, if your USB
keyboard is device 3 on bus 1, then you would modify
`utils/environment` to read `USB_KBD=1.3`.  You can find the
appropriate values to fill in via `lsusb`.  Note these values may
change on reboot (though in practice they seem somewhat stable), and
will change if the devices are unplugged and reconnected.

If you are going to use the hardware ephemeral channel method, then
you need to compile and install the `iswitch` kernel module, which
ensures that you can regain host use of input devices.  Do this as
follows:

1. In `host/input_switcher`, `make`.

2. Install the module into your image.  You can do this with
   `connect_hd`, then `sudo make install`, then `disconnect_hd`.

You may also need to modify `utils/environment` to configure the PCI
device numbers for your USB controllers.  The values should be of the
form `<bus number>:<device number>.<function number>`.  You can find
the appropriate values to fill in via `lspci`.  These values should
likely be stable for all time.

## Private sound

To get private sound to work, you need to ensure the
`snd_hda_intel_priv` driver is loaded instead of `snd_hda_intel` (in
the host, not the guest).  Currently the best way to do this is to
remove the `snd_hda_intel` module from `/lib/modules/<kernel version,
probably 3.0.0-privos>` or to rename it to something where it won't be
loaded.

## Private networking

Install package `bridge-utils` for utility `brctl` that will be used
to bridge your VM onto the local network.  You'll need to put your
network card onto the bridge.  You can do this with our included
script `utils/bridge.sh`, or you can imitate what it does.  When you
load a Lacuna VM, it will be added to this bridge.  You may have to
perform extra network configuration in your VM.

Additionally, you need to ensure the `e1000e_priv` driver is loaded
instead of `e1000e` (in the host, not the guest).  Again, as with
sound, this can be done by renaming or removing the `e1000e` module.

<a name="s_priv_graphics">
## Private graphical output
</a>

Private graphical output requires the use of CUDA, so before you
compile QEMU, you need to install some CUDA software.  Lacuna
currently requires the use of CUDA toolkit version <= 4.2 (4.2
recommended).  However, you can use an updated NVIDIA driver even with
an old toolkit.  This is potentially a good idea as we have not been
able to use NVIDIA cards at all with some of the older driver
versions.

You'll need to install and build the NVIDIA GPU Computing SDK that
accompanies the toolkit.  There's a script `make_cuda_env` that you
need to use create some environment variables for other parts of the
build process.  Modify variables in `make_cuda_env` if necessary
(the names should make it relatively self explanatory what the values
should be and the default values are those that the NVIDIA software
installs to by default) and run the script.  This script generates
`cuda_env` and `cuda_env.mak`.  If you are going to run the Lacuna
copy of `QEMU` on your own without `kvm_linux`, you may need to modify
your environment with `. ./cuda_env` first.

You'll need to build a few pieces prior to building QEMU:

1. In `host/gpu/display`, `make`.  In `host/gpu/aes`, `make`.

2. Build our modified version of SDL.  In `host/SDL-1.2.15`, run
   `configure` then `make`.

Now build QEMU as before, with `--enable-priv-gpu` among your
configure options.

Private graphics mode currently requires that your guest display
graphics in 1024 x 768 (a mechanism within private graphics mode uses
this to detect when to start).  We have noticed that the SDL display
for QEMU (even a stock QEMU version 1.0) may not work properly for
some kernel versions and configurations.  The kernel that you install
may also affect the resolution that your guest produces by default,
e.g., the default kernel for Ubuntu 12.04.4 produced a higher
resolution and graphical glitches displaying Unity, but switching to
the Lacuna kernel and kernel configuration in the guest fixed both
these issues.

## Private swap

Run `make` in `utils` to ensure that the `pfork` binary is compiled.

## Private storage

No further work is necessary.

# Current limitations and known bugs

## Inconveniences

- The setup and run process for Lacuna could stand to be easier and
  more automated.
  - There is a fair amount of manual compilation work.
  - Encrypted input ephemeral channel setup could use device/vendor ID
    to determine bus and device number automatically.
  - Hardware input ephemeral channel setup could probably do something
    similar.
- Lacuna requires use of an old version of the CUDA toolkit due to the
  use of some deprecated `cutil` functions.  However, porting to a new
  version of the CUDA toolkit would likely be easy.

## Implementation defects

- Places that use random numbers, e.g. key exchange, sometimes do not
  use real sources of randomness.  In particular, for Lacuna, they
  would have to use sources of cryptographic randomness that don't
  leave traces of the output behind.  We believe this can be
  accomplished (e.g. the Linux kernel's random number generator may be
  fine for the kernel, Intel's `RDRAND` would likely suffice for user
  level and kernel level CPU programs, and randomness can likely be
  collected on a GPU directly).  Moreover, in key exchange where the
  generation of shared randomness is the goal, it may make sense to
  instead explicitly transmit some randomness over channels whose
  traces are carefully controlled, thus using channels whose privacy
  you know to bootstrap those you don't.
- Depending on the computer, we have seen failures in assigning (that
  is, using an input hardware ephemeral channel with) more than one
  USB controller to a guest.  However, often only one is necessary for
  both private keyboard and mouse input, though it may require moving
  the devices to particular USB ports.
- The GPU cleanup code is currently not setup to run after every
  Lacuna instance.
- The benchmarking code is provided as is to give an idea of what we
  did to benchmark, but is likely incomplete and has not be re-tested
  in its current form.
- Lacuna requires your guest run with a certain fixed graphics
  resolution.  A fix for this would also likely be easy aside from the
  fact that the Lacuna modifications might have to be ported to a new
  version of QEMU, though higher resolutions could affect performance,
  e.g., through requiring more data transfer and cryptographic
  operations.
