#include <cstdio>
#include <string>

#include "CCPMemory.h"
#include "HostBitmap.h"

const char* g_moduleName = "ithax-imageio-native-smoke";

namespace
{

constexpr unsigned BITMAP_WIDTH = 4;
constexpr unsigned BITMAP_HEIGHT = 2;
constexpr unsigned INITIAL_MIP_COUNT = 1;
constexpr unsigned FULL_MIP_COUNT = 3;
constexpr unsigned CHANNEL_COUNT = 4;
constexpr int FAILURE_EXIT_CODE = 1;

bool HasExpectedPixel(float red, float green, float blue, float alpha)
{
    return red > 0.99f && green < 0.01f && blue < 0.01f && alpha > 0.99f;
}

}  // namespace

int main()
{
    ImageIO::HostBitmap bitmap;
    if (!bitmap.Create(
            BITMAP_WIDTH,
            BITMAP_HEIGHT,
            INITIAL_MIP_COUNT,
            ImageIO::PIXEL_FORMAT_B8G8R8A8_UNORM))
    {
        std::fprintf(stderr, "ImageIO failed to create a bitmap\n");
        return FAILURE_EXIT_CODE;
    }

    char* pixels = bitmap.GetRawData();
    pixels[0] = 0;
    pixels[1] = 0;
    pixels[2] = static_cast<char>(0xff);
    pixels[3] = static_cast<char>(0xff);

    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 0.0f;
    if (!bitmap.GetPixel(0, 0, red, green, blue, alpha) ||
        !HasExpectedPixel(red, green, blue, alpha) ||
        !bitmap.GenerateMipMaps(FULL_MIP_COUNT) ||
        bitmap.GetMipCount() != FULL_MIP_COUNT)
    {
        std::fprintf(stderr, "ImageIO bitmap operations failed\n");
        return FAILURE_EXIT_CODE;
    }

    const size_t expected_size =
        BITMAP_WIDTH * BITMAP_HEIGHT * CHANNEL_COUNT;
    if (bitmap.GetRawDataSize() <= expected_size)
    {
        std::fprintf(stderr, "ImageIO mip data was not generated\n");
        return FAILURE_EXIT_CODE;
    }

    std::puts(
        "{\"event\":\"imageio_native_smoke\",\"status\":\"pass\"}");
    return 0;
}
