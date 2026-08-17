#include "saf_runtime.h"

SAFState g_saf_state;


// ==========================================================
// SAF MODEL LOADER
// ==========================================================

#include <fstream>
#include <string>
#include "SafModelLoader.hpp"


static bool g_saf_model_loaded=false;


bool SAF_LoadModel()
{

    const char* path =
    "/data/adb/ivanna_omega/SAF_model.json";  // FIX 2026-08-09: unificar con customize.sh y SaFEngine.kt (path canónico app)


    Ivanna::SafModelLoader loader;


    if(!loader.load(path))
    {
        return false;
    }


    g_saf_model_loaded=true;


    return true;

}


bool SAF_IsModelLoaded()
{

    return g_saf_model_loaded;

}


