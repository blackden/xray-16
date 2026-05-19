// xrUnpack -- minimal standalone unpacker for OpenXRay/CoP .db? archives.
//
// macOS / Linux have no working unpacker for the X-Ray archive format
// (the existing xrCompress writes archives but has no inverse). This
// tool replays the same logic the engine uses at runtime to mount a
// .db at boot -- it just dumps the contents to disk instead of
// registering them in the virtual FS.
//
// Usage:
//   xrUnpack <archive.db?> <out_dir>
//
// The archive layout (see src/xrCore/LocatorAPI.cpp:393-468 and
// src/xrCore/LocatorAPI.cpp:2056-2070 for the reference reader):
//
//   chunk 1                  -- file table (one archive_file_header
//                               per entry, may be CFS_CompressMark'ed
//                               and packed with the LZ huffman pair)
//   chunk N (free format)    -- raw or rtc_compress'd payload, located
//                               by ptr + size_compr from the table
//
// We mirror the runtime path:
//   1. Walk chunks looking for chunk id 1, optionally LZ-decompress it.
//   2. Iterate archive_file_header records; for each entry mmap the
//      payload range, rtc_decompress when size_real != size_compr,
//      and write to <out_dir>/<archive-relative path>.

#include "Common/Common.hpp"
#include "xrCore/xrCore.h"
#include "xrCore/FS.h"
#include "xrCore/FS_internal.h"
#include "xrCore/LocatorAPI.h"
#include "xrCore/Compression/rt_compressor.h"
#include "xrCore/lzhuf.h"

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

// Mirrored from src/xrCore/FS.h (CFS_CompressMark) and the convention
// used in LocatorAPI -- chunk id 1 is the file table.
static constexpr u32 kCFS_CompressMark = 1u << 31;
static constexpr u32 kFileTableChunkID = 1u;

// Walk the archive looking for chunk `want_id`; return an in-memory
// IReader over the (possibly decompressed) chunk payload. Caller owns
// the reader. Returns nullptr if not found.
static IReader *find_chunk(int fd, u32 want_id, size_t file_size)
{
    if (::lseek(fd, 0, SEEK_SET) == off_t(-1))
        return nullptr;

    while (true)
    {
        u32 dwType = 0;
        u32 dwSize = 0;
        ssize_t n = ::read(fd, &dwType, sizeof(dwType));
        if (n != sizeof(dwType))
            return nullptr;
        n = ::read(fd, &dwSize, sizeof(dwSize));
        if (n != sizeof(dwSize))
            return nullptr;

        if ((dwType & ~kCFS_CompressMark) == want_id)
        {
            u8 *src = static_cast<u8 *>(xr_malloc(dwSize));
            if (::read(fd, src, dwSize) != ssize_t(dwSize))
            {
                xr_free(src);
                return nullptr;
            }
            if (dwType & kCFS_CompressMark)
            {
                u8 *dest = nullptr;
                size_t dest_sz = 0;
                bool ok = _decompressLZ(&dest, &dest_sz, src, dwSize, file_size);
                xr_free(src);
                if (!ok)
                    return nullptr;
                return xr_new<CTempReader>(dest, int(dest_sz), 0);
            }
            return xr_new<CTempReader>(src, int(dwSize), 0);
        }

        if (::lseek(fd, dwSize, SEEK_CUR) == off_t(-1))
            return nullptr;
    }
}

// Normalize archive-stored path (Windows-style "textures\\xxx.dds") to
// a POSIX path under out_root, creating parent dirs as needed.
static fs::path resolve_out_path(const fs::path &out_root, const char *entry)
{
    std::string s(entry);
    for (char &c : s)
        if (c == '\\')
            c = '/';
    fs::path out = out_root / s;
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    return out;
}

static int list_archive(const char *archive_path)
{
    int fd = ::open(archive_path, O_RDONLY);
    if (fd < 0)
    {
        std::fprintf(stderr, "open(%s): %s\n", archive_path, std::strerror(errno));
        return 1;
    }
    struct stat st{};
    if (::fstat(fd, &st) != 0)
    {
        std::fprintf(stderr, "fstat(%s): %s\n", archive_path, std::strerror(errno));
        ::close(fd);
        return 1;
    }
    IReader *hdr = find_chunk(fd, kFileTableChunkID, size_t(st.st_size));
    if (!hdr)
    {
        std::fprintf(stderr, "%s: file table (chunk %u) not found\n",
            archive_path, kFileTableChunkID);
        ::close(fd);
        return 2;
    }

    std::printf("%10s %10s %10s  %s\n", "size_real", "size_compr", "crc", "name");
    u32 count = 0;
    u64 total_real = 0;
    u64 total_compr = 0;
    while (!hdr->eof())
    {
        CLocatorAPI::archive_file_header h{*hdr};
        std::printf("%10u %10u %10u  %s\n", h.size_real, h.size_compr, h.crc, h.name);
        ++count;
        total_real += h.size_real;
        total_compr += h.size_compr;
    }
    std::printf("---\n%u entries, %.1f MB real / %.1f MB compressed (%.0f%%)\n",
        count, double(total_real) / (1024.0 * 1024.0),
        double(total_compr) / (1024.0 * 1024.0),
        total_real ? 100.0 * double(total_compr) / double(total_real) : 0.0);

    hdr->close();
    ::close(fd);
    return 0;
}

static int unpack(const char *archive_path, const char *out_dir)
{
    int fd = ::open(archive_path, O_RDONLY);
    if (fd < 0)
    {
        std::fprintf(stderr, "open(%s): %s\n", archive_path, std::strerror(errno));
        return 1;
    }
    struct stat st{};
    if (::fstat(fd, &st) != 0)
    {
        std::fprintf(stderr, "fstat(%s): %s\n", archive_path, std::strerror(errno));
        ::close(fd);
        return 1;
    }
    const size_t archive_size = size_t(st.st_size);

    IReader *hdr = find_chunk(fd, kFileTableChunkID, archive_size);
    if (!hdr)
    {
        std::fprintf(stderr, "%s: file table (chunk %u) not found\n",
            archive_path, kFileTableChunkID);
        ::close(fd);
        return 2;
    }

    std::error_code ec;
    fs::create_directories(out_dir, ec);

    const long page_size = ::sysconf(_SC_PAGE_SIZE);
    u32 file_count = 0;
    u64 total_real = 0;
    u32 errors = 0;

    while (!hdr->eof())
    {
        CLocatorAPI::archive_file_header h{*hdr};

        // Directory marker entries (zero payload, name ends with '\').
        // CoP's configs.db packs them for completeness; we just need
        // the actual files, the parent dirs get created on demand.
        const size_t name_len = std::strlen(h.name);
        if (h.size_compr == 0 ||
            (name_len > 0 && (h.name[name_len - 1] == '\\' || h.name[name_len - 1] == '/')))
        {
            continue;
        }

        // Map a page-aligned window covering [h.ptr, h.ptr + h.size_compr).
        const size_t aligned_start = (h.ptr / page_size) * page_size;
        const size_t offset_in_window = h.ptr - aligned_start;
        const size_t window_size = offset_in_window + h.size_compr;

        void *map = ::mmap(nullptr, window_size, PROT_READ, MAP_SHARED, fd, aligned_start);
        if (map == MAP_FAILED)
        {
            std::fprintf(stderr, "  mmap(%s): %s -- skipped\n", h.name, std::strerror(errno));
            ++errors;
            continue;
        }
        const u8 *data = static_cast<const u8 *>(map) + offset_in_window;

        const fs::path out_path = resolve_out_path(out_dir, h.name);
        std::ofstream ofs(out_path, std::ios::binary | std::ios::trunc);
        if (!ofs)
        {
            std::fprintf(stderr, "  open out %s: failed -- skipped\n", out_path.c_str());
            ::munmap(map, window_size);
            ++errors;
            continue;
        }

        if (h.size_real == h.size_compr)
        {
            ofs.write(reinterpret_cast<const char *>(data), h.size_real);
        }
        else
        {
            u8 *dest = static_cast<u8 *>(xr_malloc(h.size_real));
            rtc_decompress(dest, h.size_real, data, h.size_compr);
            ofs.write(reinterpret_cast<const char *>(dest), h.size_real);
            xr_free(dest);
        }

        ::munmap(map, window_size);
        ++file_count;
        total_real += h.size_real;
    }

    hdr->close();
    ::close(fd);

    std::printf("Unpacked %u files (%.1f MB) from %s -> %s%s\n",
        file_count, double(total_real) / (1024.0 * 1024.0),
        archive_path, out_dir,
        errors ? " (with errors, see above)" : "");
    return errors ? 3 : 0;
}

static void print_usage(const char *prog)
{
    std::printf(
        "Usage:\n"
        "  %s <archive.db?> <out_dir>     Extract archive contents into out_dir.\n"
        "  %s --list <archive.db?>        Print the archive file table (no extract).\n"
        "\n"
        "Reads X-Ray .db archives (CoP/CS resources.db?, configs.db),\n"
        "preserving archive-relative paths when extracting.\n",
        prog, prog);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    const bool list_mode = (std::strcmp(argv[1], "--list") == 0);
    if (list_mode && argc < 3)
    {
        print_usage(argv[0]);
        return 1;
    }
    if (!list_mode && argc < 3)
    {
        print_usage(argv[0]);
        return 1;
    }

    // xrCore init -- needed so rtc_decompress and the _decompressLZ
    // workmem allocators behave. We deliberately do *not* init the
    // virtual FS (third arg false): we touch the archive directly with
    // raw POSIX calls.
    Core.Initialize("xrUnpack", nullptr, false);
    rtc_initialize();

    const int rc = list_mode
        ? list_archive(argv[2])
        : unpack(argv[1], argv[2]);

    Core._destroy();
    return rc;
}
