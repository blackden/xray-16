#include "StdAfx.h"
#include "Updater_Manifest.h"

#include <cstring>
#include <string>

// Hand-rolled key=value parser. The natural choice was CInifile, but xrEngine's
// ini parser treats `//` as a line comment marker, which silently truncates
// our asset_url field at the first `//` of "http://...". Rather than escape
// URLs in the manifest, parse with a simpler grammar that has no `//`
// special-casing: one `key = value` per line, `;` for line comments,
// whitespace trimmed around both sides.

namespace
{
inline void TrimEdges(const char*& s, const char*& e)
{
    while (s < e && (*s == ' ' || *s == '\t' || *s == '\r')) ++s;
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) --e;
}

inline bool MatchKey(const char* s, const char* e, const char* key)
{
    const size_t len = std::strlen(key);
    return (size_t)(e - s) == len && std::strncmp(s, key, len) == 0;
}
}

bool ParseUpdateManifest(const char* body, u32 length, UpdateManifest& out)
{
    if (!body || length == 0)
        return false;

    bool have_version = false, have_channel = false, have_asset = false, have_sha = false;

    const char* p   = body;
    const char* end = body + length;

    while (p < end)
    {
        const char* line_start = p;
        while (p < end && *p != '\n') ++p;
        const char* line_end = p;
        if (p < end) ++p; // consume \n

        // Strip CR / trailing whitespace.
        const char *ls = line_start, *le = line_end;
        TrimEdges(ls, le);
        if (ls == le) continue;
        if (*ls == ';' || *ls == '#') continue; // comment
        if (*ls == '[') continue;               // ignore [section] headers — single flat namespace

        // key = value
        const char* eq = std::find(ls, le, '=');
        if (eq == le) continue;

        const char *ks = ls, *ke = eq;
        TrimEdges(ks, ke);
        const char *vs = eq + 1, *ve = le;
        TrimEdges(vs, ve);
        if (ks == ke) continue; // empty key

        const auto value = std::string(vs, ve);

        if (MatchKey(ks, ke, "version"))        { out.Version  = value.c_str(); have_version = true; }
        else if (MatchKey(ks, ke, "channel"))   { out.Channel  = value.c_str(); have_channel = true; }
        else if (MatchKey(ks, ke, "asset_url")) { out.AssetUrl = value.c_str(); have_asset   = true; }
        else if (MatchKey(ks, ke, "sha256"))    { out.Sha256   = value.c_str(); have_sha     = true; }
        else if (MatchKey(ks, ke, "size"))      { out.Size     = (u32)std::strtoul(value.c_str(), nullptr, 10); }
        else if (MatchKey(ks, ke, "notes"))     { out.Notes    = value.c_str(); }
    }

    return have_version && have_channel && have_asset && have_sha;
}
