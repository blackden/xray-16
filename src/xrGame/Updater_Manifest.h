#pragma once

// Manifest schema for the in-game updater (issue #39). The endpoint serves a
// tiny INI document; ParseManifest copies the well-known keys into an
// UpdateManifest struct. Format example:
//
//     [update]
//     version   = 1.6.fork.2026.05.21
//     channel   = stable
//     asset_url = http://updates.vg.lan/xray-16/2026.05.21.zip
//     sha256    = 64-hex-chars
//     size      = 15728640
//     notes     = First test build
//
// INI rather than JSON to reuse the engine's existing CInifile parser; same
// information density without pulling in a JSON dependency. See plan file
// (~/.claude/plans/lazy-twirling-hejlsberg.md) for the wider context.

struct UpdateManifest
{
    shared_str Version;
    shared_str Channel;
    shared_str AssetUrl;
    shared_str Sha256;   // 64-char lowercase hex
    u32        Size{0};  // bytes
    shared_str Notes;
};

// Parse a manifest from an in-memory body. Returns true if the [update]
// section exists with the four mandatory string fields (version, channel,
// asset_url, sha256). size and notes are optional.
bool ParseUpdateManifest(const char* body, u32 length, UpdateManifest& out);
