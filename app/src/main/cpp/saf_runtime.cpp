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
    "/data/adb/ivanna_omega/SAF_model_total.json";


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


