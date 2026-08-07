#include <cstdio>
#include <cstdlib>

namespace
{

constexpr int EXPECTED_DRIVER_PREFERENCE = 1;
constexpr int FAILURE_EXIT_CODE = 1;

}  // namespace

extern "C" __declspec(dllimport) int AmdPowerXpressRequestHighPerformance;

int main()
{
    const bool has_expected_driver_preference =
        AmdPowerXpressRequestHighPerformance == EXPECTED_DRIVER_PREFERENCE;
    if (!has_expected_driver_preference)
    {
        std::fprintf(stderr, "Trinity stub DLL did not load correctly\n");
        return FAILURE_EXIT_CODE;
    }

    std::puts("{\"event\":\"trinity_native_smoke\",\"status\":\"pass\"}");
    return EXIT_SUCCESS;
}
