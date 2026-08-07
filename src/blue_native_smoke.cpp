#include <cstdio>

#include "BlueExposure.h"
#include "IBlueOS.h"

const char* g_moduleName = "ithax-blue-native-smoke";

int main()
{
    BlueModuleStartup();

    IBlueOS* blue_os = BlueGetBeOS();
    if (blue_os == nullptr || blue_os->GetInfo() == nullptr)
    {
        std::fprintf(stderr, "Blue native startup returned no OS state\n");
        return 1;
    }

    std::puts("{\"event\":\"blue_native_smoke\",\"status\":\"pass\"}");
    return 0;
}
