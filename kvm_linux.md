# The script `kvm_linux`

`kvm_linux` is meant to simplify running QEMU and Lacuna.  Reading the
first sections of the script will show you the options that are
available and how they affect QEMU, and they are summarized below.
All options are boolean unless otherwise noted.  To activate a boolean
option with name <x>, add flag `--<x>`, and to turn it off, add flag
`--no-<x>`.  Note it is possible to select options that will cause
QEMU to malfunction, but we hope this can be corrected in the future
and steer you away from these combinations.

`kvm_linux` depends on the file `utils/environment`, which you can
customize to set certain properties for your setup.  You should copy
`utils/environment.tmpl` to `utils/environment` as a template.

## Basic options

`--snapshot`: Turn on snapshot mode for QEMU; diffs will not be
written to the image, default yes.

`--serial`: Have QEMU listen on port `4444` for a serial connection.
Your VM must be configured to make use of this, default yes.

`--graphics`: Produce graphical output from QEMU, default yes.

`--sound`: Enable sound, default no.

`--tap`: Use tap networking (as opposed to NAT-based networking),
default no.

`--debug`: Have QEMU listen on port `6666` for a connection to a GDB
stub that can be used to debug the guest, default no.

`--monitor`: Have QEMU listen on port `5555` for a connection to the
QEMU monitor, default no.

`<image file>`: The last parameter should be the image file you want
to use (not boolean), default "base.img".

## Lacuna options

`--priv_swap`: Enable private swap, default no.

`--priv_diff`: Enable private storage, default no.

`--priv_usb`: Use encrypted ephemeral channel mode for private input.
Incompatible with `--priv-pci`.  Default no.

`--priv_pci`: Use hardware ephemeral channel mode for private input.
Incompatible with `--priv-usb`.  Default no.

`--priv_mouse`: Enable private mouse input.  Requires one of
`--priv_usb` or `--priv_pci`.  Default no.

`--priv_kbd`: Enable private keyboard input.  Requires one of
`--priv_usb` or `--priv_pci`.  Default no.

`--priv_net`: Enable private networking.  Requires `--tap`.  Default
no.

`--priv_sound`: Enable private sound.  Requires `--sound`.  Default
no.

Helpful hint: It is sometimes useful to run `kvm_linux` in a way that
you know it will not start (e.g., don't specify an image file) because
the script will print the command line it is about to use before it
runs it.  Then you can see the QEMU options that are used and perhaps
modify the command line to add extra QEMU options (e.g., add `-cdrom`
to load an `.iso` image as a CD-ROM).
