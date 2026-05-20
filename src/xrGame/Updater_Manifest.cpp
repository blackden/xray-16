#include "StdAfx.h"
#include "Updater_Manifest.h"

#include "xrCore/xr_ini.h"
#include "xrCore/FS.h"

bool ParseUpdateManifest(const char* body, u32 length, UpdateManifest& out)
{
    if (!body || length == 0)
        return false;

    // CInifile's IReader-ctor variant reads from the buffer in place — no copy.
    // IReader does not own the memory; the body must outlive this call.
    IReader reader(const_cast<char*>(body), length);
    CInifile ini(&reader, /*path*/ nullptr);

    if (!ini.section_exist("update"))
        return false;

    if (!ini.line_exist("update", "version") ||
        !ini.line_exist("update", "channel") ||
        !ini.line_exist("update", "asset_url") ||
        !ini.line_exist("update", "sha256"))
        return false;

    out.Version  = ini.r_string("update", "version");
    out.Channel  = ini.r_string("update", "channel");
    out.AssetUrl = ini.r_string("update", "asset_url");
    out.Sha256   = ini.r_string("update", "sha256");
    out.Size     = ini.line_exist("update", "size") ? ini.r_u32("update", "size") : 0;
    out.Notes    = ini.line_exist("update", "notes") ? ini.r_string("update", "notes") : "";

    return true;
}
