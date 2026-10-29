# JSON for Modern C++

`nlohmann/json.hpp` is [JSON for Modern C++](https://github.com/nlohmann/json)
by Niels Lohmann, version 3.11.3: the single header of its release
(`https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp`,
SHA-256 `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6`),
under the MIT licence (in the header, and in the SD card's
`licenses/amiberry.txt`).

Amiberry's libretro core uses it, and its Makefile would fetch it from
GitHub on the first build (`NLOHMANN_JSON_VERSION` in
`third_party/amiberry/libretro/Makefile`). BMAmiga keeps the copy here
instead (`src/port/amiberry.mk` points the build at it), so that a build
needs no download. When Amiberry moves to another version, replace it with
that release's `json.hpp`.
