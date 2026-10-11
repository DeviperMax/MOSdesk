# MOSdesk

MOSdesk is a user-space desktop environment for [Minimal-OS](https://github.com/fsminecrafter/Minimal-OS). It renders the desktop as layered ARGB8888 surfaces, tracks dirty regions for efficient redraws, and exposes reusable text-rendering libraries for other Minimal-OS programs.

The project is split into a desktop implementation, the Minimal-OS SDK toolchain, and the underlying OS sources used to build and package the system.

## Features

- Composites desktop layers and window surfaces with dirty-rectangle updates
- Uses the reusable `ttrend` bitmap text renderer for UI text output
- Produces SDK `.slib` bundles that can be consumed by other freestanding programs
- Packages the desktop as a Minimal-OS deliver package (`.mpkg`) for installation

## Repository layout

- `mosdesk/` — MOSdesk Settings client, reusable file-type icon library, preview, and packaging metadata
- `mosWM/` — the MOSdesk desktop, compositor, cursor, and protected application window API
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

The build creates the MOSdesk Settings window client, exported text-rendering
libraries, and the required mosWM desktop shell. It produces
`mosdesk/dist/mosdesk.mpkg` and `mosWM/dist/mosWM.mpkg`.

## Preview MOSdesk

To view the desktop layout in a browser without building or booting Minimal-OS:

```sh
./mosdesk/preview.sh
```

Open `/mosdesk/preview.html` on port 8000 in the Codespaces Ports panel. This
browser preview recreates the desktop appearance; it does not run the
Minimal-OS executable.

## Build mosWM

`mosWM` is the desktop shell and window manager. Build its desktop package with:

```sh
./mosWM/build.sh
```

This produces `mosWM/dist/mosWM.run` and the installable
`mosWM/dist/mosWM.mpkg`. Installing the package places its executable, default
cursor, and `mosWM.service` module definition in the OS. The service starts the
desktop after the boot disk is mounted. MOSdesk Settings is opened from the
Start menu and saves desktop preferences for future sessions. Its default
pointer resource is the user-supplied Posy cursor; attribution is included in
the package.

The standalone manager can also be started manually from the Minimal-OS
terminal:

```text
run 0:/programs/mosWM/mosWM.run
```

The OS service manager starts the registered desktop as a supervised user
process. If the program or service definition is absent, service discovery
skips it and the built-in terminal remains available. Applications can use the
SDK's [`window.h`](Minimal-OS-SDK/include/window.h)
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
