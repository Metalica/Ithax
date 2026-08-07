#include <Python.h>
#include <windows.h>

const char *g_moduleName = "carbon-audio-stub";

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
    }

    return TRUE;
}

static PyModuleDef kModuleDefinition = {
    PyModuleDef_HEAD_INIT,
    "_audio2_debug",
    "Carbon Audio stub module",
    -1,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

extern "C" PyMODINIT_FUNC PyInit__audio2_debug(void)
{
    return PyModule_Create(&kModuleDefinition);
}
