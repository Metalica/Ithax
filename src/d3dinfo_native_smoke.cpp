#include <cstdio>
#include <cstdlib>

#include <windows.h>

namespace
{

constexpr int FAILURE_EXIT_CODE = 1;
constexpr char MODULE_INIT_SYMBOL[] = "PyInit__d3dinfo_debug";
constexpr char MODULE_NAME[] = "_d3dinfo_debug.pyd";

}  // namespace

int main()
{
    HMODULE module = LoadLibraryA(MODULE_NAME);
    if (module == nullptr)
    {
        std::fprintf(stderr,
                     "{\"event\":\"d3dinfo_native_smoke\","
                     "\"status\":\"fail\","
                     "\"error\":\"LoadLibrary failed\","
                     "\"code\":%lu}\n",
                     GetLastError());
        return FAILURE_EXIT_CODE;
    }

    FARPROC module_init = GetProcAddress(module, MODULE_INIT_SYMBOL);
    if (module_init == nullptr)
    {
        std::fprintf(stderr,
                     "{\"event\":\"d3dinfo_native_smoke\","
                     "\"status\":\"fail\","
                     "\"error\":\"Python module entry point missing\"}\n");
        FreeLibrary(module);
        return FAILURE_EXIT_CODE;
    }

    FreeLibrary(module);
    std::puts(
        "{\"event\":\"d3dinfo_native_smoke\",\"status\":\"pass\"}");
    return EXIT_SUCCESS;
}
