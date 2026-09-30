-- Tween Menu for The Elder Scrolls IV: Oblivion Remastered (OBSE64 plugin).
-- A tween-style hub menu built at runtime from the game's own widgets and art, modelled on Skyrim's Tween Menu
-- Overhaul. It opens on Wait's key binding (Select and T by default - Wait becomes one of its options), and Start
-- opens System on Save & Load (the owner, 2026-09-29; plan: 4. plans/tween-menu-oblivion/PLAN.md).
-- rule 45: no build-machine paths in any compiled object - set BEFORE includes() so CommonLibOB64's own library
-- target gets it too (a std::source_location in an OBSE header reached through the PCH's absolute -FI path).
-- /d1trimfile strips the project folder from __FILE__ and std::source_location. The flag is wrapped in a TABLE so
-- xmake passes it as one quoted argument: a bare string is split on the path's spaces, and /d1trimfile:"<dir>"
-- reaches cl with the quote characters in the prefix, which then matches nothing (measured 2026-09-29). No trailing
-- separator. /PDBALTPATH:%_PDB% makes the debug directory record only the PDB's file name (it ships beside the DLL).
add_cxflags({"/d1trimfile:$(projectdir)"}, {force = true, expand = false})
add_shflags("/PDBALTPATH:%_PDB%", {force = true})

includes("lib/commonlibob64")

set_project("TweenMenu")
set_version("1.0.1")
set_license("GPL-3.0-or-later")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
add_requires("nlohmann_json")
add_rules("plugin.vsxmake.autoupdate")

target("TweenMenu")
    add_rules("commonlibob64.plugin", {
        name = "TweenMenu",
        author = "ApocryphaRealm",
        description = "Tween Menu - a tween-style hub menu from the game's own art (Oblivion Remastered)"
    })
    add_syslinks("user32")
    add_packages("nlohmann_json")
    on_load(function (target)
        target:add("defines", "TWM_VERSION=\"" .. (target:version() or "0.0.0") .. "\"")
    end)
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")
