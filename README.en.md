# LMDB Archiver

English · **[한국어](README.md)**

LMDB Archiver is a C++17/Qt 6 desktop archive manager that integrates naturally with Windows Explorer. It uses the ACID transactions and memory-mapped I/O of [LMDB (Lightning Memory-Mapped Database)](https://github.com/LMDB/lmdb) as its storage layer, and stores **files as raw bytes** (optional gzip compression is self-describing through the key, not the value). No headers or wrappers — a pure schema that is byte-compatible with the LucioraEla source system and any standard LMDB tool.

![Browsing folders and files in LMDB Archiver](docs/images/archive-browser.png)

**[Download the latest Windows installer (MSI)](https://github.com/embistel/LMDBArchiver/releases/latest)** · [User guide](docs/USER_GUIDE.ko.md) · [Build instructions](docs/DEVELOPMENT.md)

## Why LMDB?

[LMDB](https://github.com/LMDB/lmdb) is a small, fast embedded key-value database originally developed for the OpenLDAP project. This project vendors the `LMDB_0.9.33` source from the official GitHub mirror and links it statically through CMake.

- **Memory mapping and zero-copy reads**: Leverages the operating system's virtual memory and buffer cache for efficient lookups without a separate application cache.
- **Full ACID transactions with MVCC**: Preserves the pre-commit state even when an archive update fails or is cancelled midway.
- **Read-dominated concurrency**: Multiple read transactions never block each other or a writer; writes are serialized to avoid deadlocks.
- **Sorted B+tree store**: Keys are kept sorted, which fits sequential traversal and prefix-path lookups over archive entries well.
- **Small, low-maintenance C library**: No separate server, log-based recovery process, or background compaction service is required.

LMDB itself is not a compression format. LMDB Archiver uses it as-is — **the key is a UTF-8 path, the value is the raw file bytes**. No magic, headers, or compression are added, so Python `lmdb`, C liblmdb, C#, or any standard LMDB tool can read and write the archive directly. See the [LMDB technical overview](https://www.symas.com/lmdb.php) and the [OpenLDAP project](https://project.openldap.org/) for the underlying principles.

## Key features

- Add files and entire directories to a single `.lmdb` archive
- Browse the tree, search, overwrite, delete, and extract selected or all entries of an existing archive
- Two-way drag and drop with Windows Explorer, paste copied files with `Ctrl+V`, and copy archive entries with `Ctrl+C` to paste into Explorer
- Double-click a `.lmdb` to open it, plus right-click menus for **Extract here** and **Pack folder into an LMDB archive**
- UTF-8 paths and path-traversal protection
- LMDB transactions for non-destructive updates
- Readability verification of every record, with archive checking in both the GUI and CLI
- Atomic archive compaction that reclaims empty LMDB pages after deletion
- **Byte-level compatibility with LucioraEla and all standard LMDB tools** — read and write archives without this program
- **Optional gzip compression** — store compressed with standard gzip on add (self-describing `.<hash>.gz` key marker). Extraction auto-decompresses by default; you can also `gunzip` the file by hand
- Targets Qt 6.2 and above, with local builds verified on Qt 6.11/MSVC 2022 and CI set up for Windows/Linux/macOS

> **Full portability**: the key is a UTF-8 path string and the value is the raw file bytes. With no magic, headers, or compression, Python `lmdb`, C liblmdb, or C# can retrieve the original file directly via `mdb_get(key)`.

### Reading from Python

```python
import lmdb
env = lmdb.open("wafer.lmdb", subdir=False, readonly=True, lock=False)
with env.begin() as txn:
    bmp_bytes = txn.get(b"Roi_0/Defect_000123.bmp")
    # Save as a BMP file and the original image is restored verbatim
```

## Build

Required tools are Qt 6 (`Core`, `Widgets`, and `Test` for testing), CMake 3.21+, a C++17 compiler, and Git. The LMDB 0.9.33 source is fetched by CMake from the official repository.

PowerShell/MSVC example:

```powershell
$env:QT_ROOT = 'F:\Qt\6.11.0\msvc2022_64'
cmake --preset qt-msvc-release
cmake --build --preset release
ctest --preset release
```

If your Qt is installed elsewhere, just change `QT_ROOT`. Run this from a Visual Studio Developer PowerShell.

## Usage

On Windows, we recommend installing `LMDBArchiver-<version>-x64.msi` from [GitHub Releases](https://github.com/embistel/LMDBArchiver/releases/latest). The MSI installs the program into `Program Files` and automatically registers the system-wide `.lmdb` file association and Explorer context menus. On Windows 11 the menus may appear under **Show more options**.

1. Use **File → New archive** to create a `.lmdb` file.
2. Add entries with the **Add file** / **Add folder** toolbar buttons, drag and drop, or by copying files in Explorer and pressing `Ctrl+V`.
3. Select multiple entries in the tree to extract or delete them. Re-adding to the same internal path replaces the entry with the latest content.
4. If you use the portable ZIP, run **Tools → Windows Explorer integration** to install the file association and context menus for the current user. For the MSI install, this happens automatically.

For details, see the [user guide](docs/USER_GUIDE.ko.md) and the [developer guide](docs/DEVELOPMENT.md).

### Command-line automation

`LMDBArchiverCLI.exe` in the distribution package uses the same core without any dialogs.

```powershell
LMDBArchiverCLI create photos.lmdb C:\Photos
LMDBArchiverCLI create photos.lmdb C:\Photos --compress      # store gzip-compressed
LMDBArchiverCLI add photos.lmdb C:\More --destination imports
LMDBArchiverCLI list photos.lmdb
LMDBArchiverCLI extract photos.lmdb C:\Restored Photos\2026
LMDBArchiverCLI extract photos.lmdb C:\GzOnly --no-decompress  # leave .gz files as-is
LMDBArchiverCLI remove photos.lmdb imports\obsolete.jpg
LMDBArchiverCLI test photos.lmdb
LMDBArchiverCLI compact photos.lmdb
```

`create` will not overwrite an existing archive. Omitting the internal path in `extract` extracts everything.

## Packaging

Recommended MSI install package:

```powershell
scripts\build-msi.ps1 -BuildDirectory build\release -QtBin F:\Qt\6.11.0\msvc2022_64\bin
```

The script prepares a pinned WiX Toolset 5 in the local build directory, stages the portable distribution files, then produces and validates `out/LMDBArchiver-<version>-x64.msi`. The generated MSI is not code-signed yet, so we recommend signing it with a Windows code-signing certificate before external distribution.

Portable ZIP:

```powershell
scripts\package.ps1 -BuildDirectory build\release -QtBin F:\Qt\6.11.0\msvc2022_64\bin
```

The script uses `windeployqt` to collect the required Qt DLLs and plugins, the portable MSVC runtime, and the third-party notices, then produces `out/LMDBArchiver-<version>-win64.zip`.

## Current integration scope

A virtual folder implementation identical to Windows' Compressed Folders requires a COM namespace extension with separate installation and signing. The current version delivers what can be distributed reliably: file association, Explorer context menus, drag and drop, and clipboard copy. A future namespace extension can reuse the core library.

## License

MIT. The bundled LMDB is distributed under the OpenLDAP Public License.
