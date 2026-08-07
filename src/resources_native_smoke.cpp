#include <cstdio>
#include <string>

#include "Enums.h"

namespace
{

constexpr int EXPECTED_MAJOR_VERSION = 4;
constexpr int EXPECTED_MINOR_VERSION = 3;
constexpr int EXPECTED_PATCH_VERSION = 2;
constexpr int FAILURE_EXIT_CODE = 1;

}  // namespace

int main()
{
    std::string description;
    const bool has_description = CarbonResources::ResultTypeToString(
        CarbonResources::ResultType::SUCCESS, description);
    const bool has_expected_version =
        CarbonResources::VERSION_MAJOR == EXPECTED_MAJOR_VERSION &&
        CarbonResources::VERSION_MINOR == EXPECTED_MINOR_VERSION &&
        CarbonResources::VERSION_PATCH == EXPECTED_PATCH_VERSION;

    if (!has_description || description.empty() || !has_expected_version)
    {
        std::fprintf(stderr, "Carbon Resources returned invalid metadata\n");
        return FAILURE_EXIT_CODE;
    }

    std::puts(
        "{\"event\":\"resources_native_smoke\",\"status\":\"pass\"}");
    return 0;
}
