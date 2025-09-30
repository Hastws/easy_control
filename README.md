# easy_control

Cross‑platform C/C++ input & screen-capture libraries:
- `easy_control::system_input` (header‑only; platform backends selectable)
- `easy_control::system_output` (+ `easy_control::mac_bridge` on macOS)

This repo builds and installs CMake packages so downstream projects can simply:
```cmake
find_package(easy_control CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE easy_control::system_output easy_control::system_input)
```

It also auto‑derives the version from `git describe` and generates a public
header `easy_control/version.h` containing version, commit and dirty flags.

---

## Contents
- [Prerequisites](#prerequisites)
- [Quick Start (all platforms)](#quick-start-all-platforms)
- [Linux / Ubuntu](#linux--ubuntu)
- [macOS](#macos)
- [Windows (MSVC)](#windows-msvc)
- [Options & Backends](#options--backends)
- [Installing / Uninstalling](#installing--uninstalling)
- [Packaged Artifacts (CPack)](#packaged-artifacts-cpack)
- [Using in Downstream Projects](#using-in-downstream-projects)
- [Version Header](#version-header)
- [Troubleshooting](#troubleshooting)

---

## Prerequisites

- **CMake ≥ 3.10**
- A C/C++ toolchain for your OS
- (Recommended) **Ninja** generator for faster builds

To let the build embed the correct version, the repository should be a git
checkout with tags available (see [Troubleshooting](#troubleshooting)).

---

## Quick Start (all platforms)

```bash
# Configure + build (Release)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Optional: run demos (if built)
#   build/system_output_test, build/system_input_test, build/joint_test

# Install to /usr/local (Linux/macOS) or chosen prefix (Windows: see below)
sudo cmake --install build --prefix /usr/local
```

To skip demos during the build:
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DEASY_CONTROL_BUILD_DEMOS=OFF
```

---

## Linux / Ubuntu

### Install dependencies
```bash
sudo apt-get update
# Build tools
sudo apt-get install -y cmake ninja-build pkg-config
# X11 capture/input backends (default on Linux)
sudo apt-get install -y libx11-dev libxtst-dev libxrandr-dev libxfixes-dev
# (Optional) Wayland portal backend (if you enable -DAUTOALG_USE_WAYLAND_PORTAL=ON)
# sudo apt-get install -y libwayland-dev libglib2.0-dev
```

### Build
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Install (system‑wide)
```bash
sudo cmake --install build --prefix /usr
# or
# sudo cmake --install build --prefix /usr/local
```

---

## macOS

### Install tools
```bash
brew update
brew install cmake ninja
```

### Build
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Install
```bash
sudo cmake --install build --prefix /usr/local
# On Apple Silicon if you prefer Homebrew prefix:
# sudo cmake --install build --prefix /opt/homebrew
```

> macOS links against system frameworks (`ApplicationServices`, `Foundation`, and
> `ScreenCaptureKit` for mac capture). No extra packages are required.

---

## Windows (MSVC)

Open a **x64 Native Tools Command Prompt for VS** or use PowerShell with the
VS Build Tools installed.

### Install tools (PowerShell; optional)
```powershell
choco install -y ninja
```

### Configure + Build
```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Install
Choose an install prefix (e.g. `C:\easy_control`):
```powershell
cmake --install build --config Release --prefix C:\easy_control
```

Use it in downstream projects by pointing `CMAKE_PREFIX_PATH` to that prefix.

---

## Options & Backends

CMake options (all default to **OFF** unless noted):

- `EASY_CONTROL_BUILD_DEMOS` (**ON**): build example executables (not installed).
- `INPUT_STRICT_WARNINGS` (**ON**): enable strict warnings for `system_input`.
- Linux input backends (choose one if desired):
  - `INPUT_BACKEND_WAYLAND_WLR` (Wayland wlroots virtual input)  
  - `INPUT_BACKEND_UINPUT` (Linux uinput)  
  - default: X11 + XTest
- Screen capture on Linux:
  - `AUTOALG_USE_WAYLAND_PORTAL` (Wayland portal via gio/glib + stb)

Examples:
```bash
# Linux with Wayland portal capture
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DAUTOALG_USE_WAYLAND_PORTAL=ON

# Linux with uinput backend for system_input
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DINPUT_BACKEND_UINPUT=ON
```

---

## Installing / Uninstalling

Install to a prefix:
```bash
sudo cmake --install build --prefix /usr/local
```

Downstream projects may then locate it automatically, or you can hint:
```bash
cmake -S app -B build -DCMAKE_PREFIX_PATH=/usr/local
```

Uninstall (from the same build dir) using the install manifest:
```bash
sudo xargs rm -f < build/install_manifest.txt
```

---

## Packaged Artifacts (CPack)

You can create distributable packages without installing:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target package
```

On Linux you’ll typically get `.deb`, `.zip`, `.txz`; on macOS `.zip`/`.txz`;
on Windows an NSIS `.exe` installer and/or `.zip`.

Install a `.deb` on Ubuntu:
```bash
sudo dpkg -i build/*.deb || sudo apt-get -f install
```

On macOS, the `.zip/.txz` contains an install tree; extract to a prefix:
```bash
sudo unzip build/*.zip -d /usr/local
# or
sudo tar -C /usr/local -xvf build/*.txz
```

Windows: run the `*.exe` installer or extract the `*.zip` to a chosen prefix.

---

## Using in Downstream Projects

### CMake
```cmake
cmake_minimum_required(VERSION 3.10)
project(myapp CXX)

find_package(easy_control CONFIG REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE
  easy_control::system_output
  easy_control::system_input
)
# On macOS if you directly need the bridge:
# target_link_libraries(myapp PRIVATE easy_control::mac_bridge)
```

If the package is not in a default search path:
```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/usr/local
```

---

## Version Header

After configuration, the build generates:
```
<build>/generated/easy_control/version.h
```
It is **installed** to:
```
<prefix>/include/easy_control/version.h
```

Fields available:
```c
#include <easy_control/version.h>

// Macros
EASY_CONTROL_VERSION_MAJOR
EASY_CONTROL_VERSION_MINOR
EASY_CONTROL_VERSION_PATCH
EASY_CONTROL_VERSION_SEMVER   // "x.y.z"
EASY_CONTROL_VERSION_SUFFIX   // e.g. "+5.gabc1234", ".dirty", "+dirty-<sha>", or empty
EASY_CONTROL_VERSION_FULL     // "x.y.z" + suffix

EASY_CONTROL_GIT_DESCRIBE     // output of `git describe` (may be empty)
EASY_CONTROL_GIT_COMMIT       // short SHA or timestamp-rand
EASY_CONTROL_GIT_DIRTY        // 1 if dirty/untagged, else 0
```

Example:
```c++
#include <easy_control/version.h>
#include <iostream>

int main() {
  std::cout << "easy_control " << EASY_CONTROL_VERSION_FULL
            << " (commit " << EASY_CONTROL_GIT_COMMIT
            << (EASY_CONTROL_GIT_DIRTY ? ", dirty" : "")
            << ")\n";
}
```

---

## Troubleshooting

- **`git describe` returns empty / version shows dirty**  
  Ensure you cloned the repo with tags. If using GitHub Actions or shallow clones,
  fetch full history and tags:
  ```bash
  git fetch --tags --unshallow || git fetch --tags
  ```

- **Linux configure error: `Package 'x11' not found`**  
  Install dev packages:
  ```bash
  sudo apt-get install -y libx11-dev libxtst-dev libxrandr-dev libxfixes-dev
  ```

- **Wayland portal enabled but missing headers**  
  Install:
  ```bash
  sudo apt-get install -y libwayland-dev libglib2.0-dev
  ```

- **macOS include paths**  
  If you installed to `/opt/homebrew`, pass `-DCMAKE_PREFIX_PATH=/opt/homebrew`
  to your downstream CMake configure.

- **Windows cannot find package**  
  Use `--prefix C:\easy_control` on install and set
  `-DCMAKE_PREFIX_PATH=C:\easy_control` when configuring your downstream project.

---

## License

MIT © 2025 AutoAlg (autoalg.com)
