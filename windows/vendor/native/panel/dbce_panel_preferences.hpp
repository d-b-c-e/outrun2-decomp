#pragma once
#include "dbce_panel.h"

namespace dbce { namespace panel {
// Shared production write seam, also exercised with disposable INIs without a renderer or window.
// A held preference is session-only, not a failed write. The host guard may explain the hold.
inline bool PersistPreference(const Config& config,const char* key,const char* value){
    if(!config.iniPath||(config.canSavePreferences&&!config.canSavePreferences()))return true;
    return WritePrivateProfileStringA("Panel",key,value,config.iniPath)!=0;
}
}}
