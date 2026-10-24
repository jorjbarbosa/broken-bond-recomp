// Compresses the payload of a shader package with Zstandard, one frame
// (build-embedded-shader-cache.py). A host tool of the build, with the Zstandard
// of the XenosRecomp dependencies, the same the renderer links to decode it, so
// the build needs no system codec and the renderer no system library.
//
// Usage: shader-compress <payload> <output>. Exits 1 when the payload does not
// read, compress or write.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>
#include <zstd.h>

namespace {
template <typename Char>
int Run(int argc, Char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: shader-compress <payload> <output>\n");
    return 2;
  }
  const std::filesystem::path input(argv[1]), output(argv[2]);
  std::error_code failed;
  const uintmax_t size = std::filesystem::file_size(input, failed);
  if (failed || size > (uintmax_t(1) << 30)) {
    std::fprintf(stderr, "shader-compress: cannot read the payload (or it exceeds 1 GiB)\n");
    return 1;
  }
  std::vector<char> payload(static_cast<size_t>(size));
  std::ifstream in(input, std::ios::binary);
  if (!in.read(payload.data(), std::streamsize(payload.size()))) {
    std::fprintf(stderr, "shader-compress: cannot read the payload\n");
    return 1;
  }
  std::vector<char> packed(ZSTD_compressBound(payload.size()));
  const size_t written = ZSTD_compress(packed.data(), packed.size(), payload.data(), payload.size(), 9);
  if (ZSTD_isError(written)) {
    std::fprintf(stderr, "shader-compress: %s\n", ZSTD_getErrorName(written));
    return 1;
  }
  std::ofstream out(output, std::ios::binary | std::ios::trunc);
  out.write(packed.data(), std::streamsize(written));
  out.close();
  if (!out) {
    std::fprintf(stderr, "shader-compress: cannot write the output\n");
    return 1;
  }
  std::printf("shader-compress: %zu bytes, %zu packed\n", payload.size(), written);
  return 0;
}
}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) { return Run(argc, argv); }
#else
int main(int argc, char** argv) { return Run(argc, argv); }
#endif
