#pragma once

#include "GameVersions.h"

// The disc release the game code was compiled for (VERSION, set per executable
// by CMake's mp_add_release). The game's own code differs between releases, so
// an executable plays only its own; given the other release's disc, it hands
// over to the package's other executable (platform/main.cpp).
namespace PortRelease {

#if VERSION == VERSION_GM8P_00
// Game id and maker, as the disc header and memory card file name have them.
inline constexpr char kDiscId[] = "GM8P01";
inline constexpr char kGameCode[] = "GM8P";
// In messages: "Metroid Prime for the GameCube, <kName>".
inline constexpr char kName[] = "European version";
// Dolphin's folder name for the region (GC/EUR/Card A).
inline constexpr char kDolphinRegion[] = "EUR";
// The ROM set's name for an image of it, for the usage line.
inline constexpr char kImageName[] = "Metroid Prime (Europe) (En,Fr,De,Es,It)";
// The embedded default font in the DOL's .rodata (config/GM8P01_00/symbols.txt).
inline constexpr unsigned kDefaultFontData = 0x803B5180;
inline constexpr unsigned kDefaultFontDataSize = 0x904;
inline constexpr unsigned kDefaultFontTexture = 0x803B5A84;
inline constexpr unsigned kDefaultFontTextureSize = 0x6FC;
// The other release: its disc, and the executable (or Android library) that plays it.
inline constexpr char kOtherDiscId[] = "GM8E01";
inline constexpr char kOtherExe[] = "metroid_prime_port";
#else
inline constexpr char kDiscId[] = "GM8E01";
inline constexpr char kGameCode[] = "GM8E";
inline constexpr char kName[] = "USA version 1.00";
inline constexpr char kDolphinRegion[] = "USA";
inline constexpr char kImageName[] = "Metroid Prime (USA) (v1.00)";
// config/GM8E01_00/symbols.txt
inline constexpr unsigned kDefaultFontData = 0x803CB3A0;
inline constexpr unsigned kDefaultFontDataSize = 0x650;
inline constexpr unsigned kDefaultFontTexture = 0x803CB9F0;
inline constexpr unsigned kDefaultFontTextureSize = 0x45C;
inline constexpr char kOtherDiscId[] = "GM8P01";
inline constexpr char kOtherExe[] = "metroid_prime_port_eur";
#endif
// Both supported releases are disc 0, revision 0.
inline constexpr unsigned kDiscVersion = 0;

} // namespace PortRelease
