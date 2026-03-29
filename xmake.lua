set_project("stfctool")
set_version("0.1.0")

set_languages("c++17")
set_warnings("all", "error")

add_rules("mode.debug", "mode.release")

-- FTXUI: functional terminal UI
add_requires("ftxui", {configs = {shared = false}})

-- OpenSSL for HTTPS (system package)
add_requires("openssl", {system = true})

target("stfctool")
    set_kind("binary")
    set_rundir("$(projectdir)")
    add_files("src/main.cpp", "src/data/*.cpp", "src/util/*.cpp", "src/core/*.cpp")
    add_includedirs("src")
    add_sysincludedirs("include")  -- vendored headers: suppress warnings
    add_packages("ftxui", "openssl")
    add_syslinks("pthread")

target("smoke_test")
    set_kind("binary")
    set_default(false)  -- only build when explicitly requested: xmake build smoke_test
    set_rundir("$(projectdir)")
    add_files("src/smoke_test.cpp", "src/data/api_client.cpp", "src/util/csv_import.cpp", "src/core/crew_optimizer.cpp")
    add_includedirs("src")
    add_sysincludedirs("include")
    add_packages("openssl")
    add_syslinks("pthread")
