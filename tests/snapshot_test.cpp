// SPDX-License-Identifier: MIT

// Unit tests for snapshots (save states): memory images (src/common/memory_image.h).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include "check.h"
#include "common/memory_image.h"

using namespace gcsf;

namespace
{

constexpr std::size_t kPage = MemoryImage::kPageSize;

std::vector<uint8_t> Restored(const MemoryImage& image)
{
    std::vector<uint8_t> bytes(image.Size(), 0xAA);
    image.Restore(bytes.data());

    return bytes;
}

void TestMemoryImages()
{
    // Ten and a half pages: random ones, zero ones and a partial one at the end.
    std::mt19937 rng(1);
    std::vector<uint8_t> memory(10 * kPage + kPage / 2);
    for (std::size_t i = 0; i < memory.size(); i++)
    {
        const std::size_t page = i / kPage;
        memory[i] = static_cast<uint8_t>(page == 2 || page == 7 ? 0 : rng());
    }

    const MemoryImage first(memory.data(), memory.size());
    GCSF_CHECK(first.Size() == memory.size());
    GCSF_CHECK(Restored(first) == memory);
    // Every page except the two zero pages takes memory. The partial page takes a whole page.
    GCSF_CHECK(first.BytesNotIn(nullptr) == 9 * kPage);

    // Change a byte in page 4, the last byte of the partial page, and turn page 5 into zeros and page 7 into data.
    std::vector<uint8_t> changed = memory;
    changed[4 * kPage + 100] ^= 1;
    changed.back() ^= 0x80;
    std::fill(changed.begin() + static_cast<std::ptrdiff_t>(5 * kPage),
              changed.begin() + static_cast<std::ptrdiff_t>(6 * kPage), uint8_t{0});
    changed[7 * kPage + 7] = 1;

    const MemoryImage second(changed.data(), changed.size(), &first);
    GCSF_CHECK(Restored(second) == changed);
    GCSF_CHECK(Restored(first) == memory); // the first image doesn't change
    // Pages 4, 7 and 10 are new. Page 5 is zeros and costs nothing. The rest are shared.
    GCSF_CHECK(second.BytesNotIn(&first) == 3 * kPage);
    GCSF_CHECK(second.BytesNotIn(nullptr) == 9 * kPage);

    // An image of unchanged memory shares everything.
    const MemoryImage third(changed.data(), changed.size(), &second);
    GCSF_CHECK(third.BytesNotIn(&second) == 0);
    GCSF_CHECK(Restored(third) == changed);

    // Restoring writes every byte, zero pages included.
    std::vector<uint8_t> target(changed.size(), 0x55);
    first.Restore(target.data());
    GCSF_CHECK(target == memory);

    // A previous image of a different size shares what it can.
    std::vector<uint8_t> longer = memory;
    longer.resize(12 * kPage, 3);
    const MemoryImage grown(longer.data(), longer.size(), &first);
    GCSF_CHECK(Restored(grown) == longer);
    GCSF_CHECK(grown.BytesNotIn(&first) == 2 * kPage); // pages 10 (now whole) and 11

    // An empty block.
    const MemoryImage empty(nullptr, 0);
    GCSF_CHECK(empty.Size() == 0);
    GCSF_CHECK(empty.BytesNotIn(nullptr) == 0);
}

} // namespace

int main()
{
    TestMemoryImages();

    if (failures)
    {
        std::fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }

    std::printf("snapshot tests passed\n");

    return 0;
}
