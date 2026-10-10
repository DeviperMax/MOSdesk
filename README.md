# MOSdesk

MOSdesk is a user-space desktop environment for [Minimal-OS](https://github.com/fsminecrafter/Minimal-OS). It renders the desktop as layered ARGB8888 surfaces, tracks dirty regions for efficient redraws, and exposes reusable text-rendering libraries for other Minimal-OS programs.

The project is split into a desktop implementation, the Minimal-OS SDK toolchain, and the underlying OS sources used to build and package the system.

## Features

- Composites desktop layers and window surfaces with dirty-rectangle updates
- Uses the reusable `ttrend` bitmap text renderer for UI text output
- Produces SDK `.slib` bundles that can be consumed by other freestanding programs
- Packages the desktop as a Minimal-OS deliver package (`.mpkg`) for installation

## Repository layout

- `mosdesk/` — desktop source, build script, generated fonts, and packaging metadata
- `mosWM/` — the Minimal-OS window manager, compositor, and protected application window API
- `Minimal-OS-SDK/` — toolchain and build utilities for freestanding C/C++ programs and `.slib` bundles
- `Minimal-OS/` — Minimal-OS source tree used by the SDK and runtime
- `msdk` — convenience wrapper for invoking the SDK toolchain

## Prerequisites

Before building MOSdesk, initialize the SDK and OS submodules and install the required toolchain:

```sh
git submodule update --init --recursive
cd Minimal-OS-SDK
./install-toolchain.sh
./install.sh
```

Then reload your shell or source your profile so `MSDK`/`msdk` are available on `PATH`.

For more details about the SDK, see [Minimal-OS-SDK/README.md](Minimal-OS-SDK/README.md).

## Build MOSdesk

From the repository root:

```sh
./mosdesk/build.sh
```

The build script creates the desktop bundle and the exported text-rendering libraries, then packages them under `mosdesk/dist/`.

## Build mosWM

`mosWM` is an optional user-space window manager, built separately from MOSdesk
because both programs currently claim exclusive ownership of the framebuffer:

```sh
./mosWM/build.sh
```

This produces `mosWM/dist/mosWM.run` and the installable
`mosWM/dist/mosWM.mpkg`. Minimal-OS continues to boot into its normal terminal
without mosWM installed. Installing the package places its executable in
`0:/programs/mosWM` and its optional `mosWM.service` module definition in
`0:/services`; the latter opts into automatic startup after the boot disk is
mounted. Remove the service file and reboot to disable automatic startup.
Alternatively, install only the executable and start the manager manually
from the Minimal-OS terminal:

```text
run 0:/programs/mosWM/mosWM.run
```

The OS service manager starts the registered window-manager module as a
supervised user process. If the program or service definition is absent,
service discovery skips it and the built-in terminal remains available.
Applications can use the SDK's [`window.h`](Minimal-OS-SDK/include/window.h)
API to create and manage their own windows. Kernel-enforced ownership prevents
unrelated applications from accessing window state, input events, or frame
buffers. See [mosWM/README.md](mosWM/README.md) for the API and behavior.

## Build artifacts

After a successful build, the following files are produced in `mosdesk/dist/`:

- `mosdesk.run` — the desktop executable
- `ttrend_raster.slib` — raster text renderer library bundle
- `ttrend_outline.slib` — outline text renderer library bundle
- `mosdesk.mpkg` — deliver package for Minimal-OS installation
- `mosdesk/` — unpacked package directory used to assemble the final package

The `.slib` bundles are distribution artifacts for Minimal-OS. The runtime does not currently load shared libraries dynamically at runtime; instead, they are used as packaged SDK deliverables and metadata for other programs.

## Notes

MOSdesk is designed to run as a user-space component within Minimal-OS rather than as a traditional desktop environment on a host OS. The build process expects the Minimal-OS SDK and supporting OS infrastructure to be present in the repository checkout.
