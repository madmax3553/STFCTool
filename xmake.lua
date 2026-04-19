set_project("stfctool")
set_version("0.6.0")

set_languages("c++17")
set_warnings("all", "error")

add_rules("mode.debug", "mode.release")

-- FTXUI: functional terminal UI
add_requires("ftxui", {configs = {shared = false}})

-- OpenSSL for HTTPS (system package)
add_requires("openssl", {system = true})

target("stfctool")
    set_kind("binary")
    set_filename("stfctool")
    set_rundir("$(projectdir)")
    add_files("src/main.cpp", "src/data/api_client.cpp", "src/data/ingress_server.cpp")
    add_includedirs("src")
    add_sysincludedirs("include")  -- vendored headers: suppress warnings
    add_packages("ftxui", "openssl")
    add_syslinks("pthread")

target("smoke_test")
    set_kind("binary")
    set_default(false)  -- only build when explicitly requested: xmake build smoke_test
    set_rundir("$(projectdir)")
    add_files("src/smoke_test.cpp", "src/app/account_snapshot.cpp",
              "src/data/api_client.cpp",
              "src/data/ingress_server.cpp",
              "src/core/crew_optimizer.cpp", "src/core/planner.cpp", "src/core/ship_prompt.cpp", "src/core/officer_prompt.cpp", "src/core/strategic_prompt.cpp",
              "src/core/ai_crew_engine.cpp", "src/core/crew_advisor.cpp",
              "src/core/account_state.cpp", "src/core/officer_groups.cpp",
              "src/core/ai_history.cpp", "src/core/meta_cache.cpp",
              "src/data/llm_client.cpp", "src/data/claude_provider.cpp",
              "src/data/gemini_provider.cpp", "src/data/ollama_provider.cpp",
              "src/data/ssh_tunnel.cpp")
    add_includedirs("src")
    add_sysincludedirs("include")
    add_packages("openssl")
    add_syslinks("pthread")

target("ingress")
    set_kind("binary")
    set_default(false)
    set_rundir("$(projectdir)")
    add_files("src/ingress_standalone.cpp", "src/data/ingress_server.cpp")
    add_includedirs("src")
    add_sysincludedirs("include")
    add_packages("openssl")
    add_syslinks("pthread")
