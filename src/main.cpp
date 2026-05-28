// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Takaaki Sato
/**
 * @file  main.cpp
 * @brief Provides the main entry point and dispatches JSON-RPC requests.
 */

#include <iostream>
#include <print>

#include <glaze/ext/jsonrpc.hpp>
#include <lua.hpp>

#include "config.hpp"

constexpr glz::opts RequireAllKeys{.error_on_unknown_keys = false, .error_on_missing_keys = true};

struct Config {
    std::size_t maxExecutionMs;
    std::size_t maxMemoryBytes;
    std::size_t maxOutputBytes;
    std::size_t maxPacketBytes;
    std::size_t maxResultBytes;
};

struct Content {
    std::string output;
    std::vector<std::string> result;
};

struct Tool {
    std::string name;
    std::string description;
    struct {
        struct {
            struct {
                std::string title;
                std::string type;
                std::string description;
            } script;
        } properties;
        std::array<std::string, 1> required;
        std::string title;
        std::string type;
    } inputSchema;
};

struct ParamsInitialize {
    std::string protocolVersion;
    struct {
    } capabilities;
    struct {
        std::string name;
        std::string version;
    } clientInfo;
};

struct ResultInitialize {
    std::string protocolVersion;
    struct {
        struct {
            bool listChanged;
        } tools;
    } capabilities;
    struct {
        std::string name;
        std::string version;
    } serverInfo;
};

struct ParamsNotifications {};

struct ResultNotifications {};

struct ParamsToolsList {};

struct ResultToolsList {
    std::vector<Tool> tools;
};

struct ParamsToolsCall {
    std::string name;
    struct {
        std::string script;
    } arguments;
};

struct ResultToolsCall {
    struct Content {
        std::string type;
        std::string text;
    };
    std::array<Content, 1> content;
    bool isError;
};

namespace {

using namespace std::string_view_literals;
constexpr auto OutputTruncated = "<output_truncated/>\n"sv;
constexpr auto ProtocolVersion = "2025-11-25"sv;

void Sanitize(std::string_view source, std::string& result)
{
    std::string buffer;
    for (auto c : source) {
        if (~c & 0x80 || c & 0x40) {
            for (auto c : buffer) {
                std::format_to(std::back_inserter(result), "\\x{:02X}", c);
            }
            buffer.clear();
        }
        if (~c & 0x80) {
            if (c < 0x20 && c != '\b' && c != '\t' && c != '\n' && c != '\f' && c != '\r') {
                std::format_to(std::back_inserter(result), "\\x{:02X}", c);
            }
            else {
                result.push_back(c);
            }
            continue;
        }
        buffer.push_back(c);
        if (~buffer.front() & 0x40) {
            std::format_to(std::back_inserter(result), "\\x{:02X}", c);
            buffer.clear();
            continue;
        }
        if (~buffer.front() & 0x20) {
            if (buffer.size() == 2) {
                auto c = buffer[0] & 0x1f;
                c = c << 6 | (buffer[1] & 0x3f);
                if (c < 0x80) [[unlikely]] {
                    for (auto c : buffer) {
                        std::format_to(std::back_inserter(result), "\\x{:02X}", c);
                    }
                }
                else {
                    result.append(buffer);
                }
                buffer.clear();
            }
            continue;
        }
        if (~buffer.front() & 0x10) {
            if (buffer.size() == 3) {
                auto c = buffer[0] & 0x0f;
                c = c << 6 | (buffer[1] & 0x3f);
                c = c << 6 | (buffer[2] & 0x3f);
                if (c < 0x0800 || (c >= 0xd800 && c < 0xe000)) [[unlikely]] {
                    for (auto c : buffer) {
                        std::format_to(std::back_inserter(result), "\\x{:02X}", c);
                    }
                }
                else {
                    result.append(buffer);
                }
                buffer.clear();
            }
            continue;
        }
        if (~buffer.front() & 0x08) {
            if (buffer.size() == 4) {
                auto c = buffer[0] & 0x07;
                c = c << 6 | (buffer[1] & 0x3f);
                c = c << 6 | (buffer[2] & 0x3f);
                c = c << 6 | (buffer[3] & 0x3f);
                if (c < 0x010000 || c >= 0x110000) [[unlikely]] {
                    for (auto c : buffer) {
                        std::format_to(std::back_inserter(result), "\\x{:02X}", c);
                    }
                }
                else {
                    result.append(buffer);
                }
                buffer.clear();
            }
            continue;
        }
        std::format_to(std::back_inserter(result), "\\x{:02X}", c);
        buffer.clear();
    }
    for (auto c : buffer) {
        std::format_to(std::back_inserter(result), "\\x{:02X}", c);
    }
}

auto Print(lua_State* state) -> int
{
    auto buffer = static_cast<std::string*>(lua_touserdata(state, lua_upvalueindex(1)));
    auto cutoff = static_cast<std::size_t*>(lua_touserdata(state, lua_upvalueindex(2)));
    auto offset = buffer->size();
    if (!buffer->ends_with(OutputTruncated)) {
        std::string result;
        std::size_t length;
        for (auto i = 1, n = lua_gettop(state); i <= n; ++i) {
            result.clear();
            Sanitize({luaL_tolstring(state, i, &length), length}, result);
            lua_settop(state, -2);
            if (buffer->size() + result.size() + 1 >= *cutoff - OutputTruncated.size()) {
                buffer->append(OutputTruncated);
                break;
            }
            buffer->append(result);
            buffer->push_back('\t');
        }
        if (buffer->size() == offset) {
            buffer->push_back('\n');
        }
        else {
            buffer->back() = '\n';
        }
    }
    return 0;
}

void RegisterCoreBindings(lua_State* state)
{
    luaopen_base(state);
    lua_getfield(state, -1, "_VERSION");
    lua_setfield(state, -3, "_VERSION");
    lua_getfield(state, -1, "assert");
    lua_setfield(state, -3, "assert");
    lua_getfield(state, -1, "error");
    lua_setfield(state, -3, "error");
    lua_getfield(state, -1, "getmetatable");
    lua_setfield(state, -3, "getmetatable");
    lua_getfield(state, -1, "ipairs");
    lua_setfield(state, -3, "ipairs");
    lua_getfield(state, -1, "next");
    lua_setfield(state, -3, "next");
    lua_getfield(state, -1, "pairs");
    lua_setfield(state, -3, "pairs");
    lua_getfield(state, -1, "pcall");
    lua_setfield(state, -3, "pcall");
    lua_getfield(state, -1, "rawequal");
    lua_setfield(state, -3, "rawequal");
    lua_getfield(state, -1, "rawget");
    lua_setfield(state, -3, "rawget");
    lua_getfield(state, -1, "rawlen");
    lua_setfield(state, -3, "rawlen");
    lua_getfield(state, -1, "rawset");
    lua_setfield(state, -3, "rawset");
    lua_getfield(state, -1, "select");
    lua_setfield(state, -3, "select");
    lua_getfield(state, -1, "setmetatable");
    lua_setfield(state, -3, "setmetatable");
    lua_getfield(state, -1, "tonumber");
    lua_setfield(state, -3, "tonumber");
    lua_getfield(state, -1, "tostring");
    lua_setfield(state, -3, "tostring");
    lua_getfield(state, -1, "type");
    lua_setfield(state, -3, "type");
    lua_getfield(state, -1, "xpcall");
    lua_setfield(state, -3, "xpcall");
    lua_settop(state, -2);
    luaopen_math(state);
    lua_setfield(state, -2, "math");
    luaopen_string(state);
    lua_setfield(state, -2, "string");
    luaopen_table(state);
    lua_setfield(state, -2, "table");
    luaopen_utf8(state);
    lua_setfield(state, -2, "utf8");
}

void RegisterHostBindings(lua_State* state, std::string& buffer, std::size_t cutoff)
{
    lua_pushlightuserdata(state, &buffer);
    lua_pushlightuserdata(state, &cutoff);
    lua_pushcclosure(state, Print, 2);
    lua_setfield(state, -2, "print");
}

auto Allocate(void* ud, void* ptr, std::size_t osize, std::size_t nsize) -> void*
{
    auto avail = static_cast<std::size_t*>(ud);
    if (!ptr) {
        osize = 0;
    }
    if (!nsize) {
        std::free(ptr);
        *avail += osize;
        return nullptr;
    }
    if (*avail + osize < nsize) {
        return nullptr;
    }
    auto nptr = std::realloc(ptr, nsize);
    if (!nptr) {
        return nullptr;
    }
    *avail += osize - nsize;
    return nptr;
}

auto Execute(const Config& config, const std::string& script) -> std::tuple<std::string, bool>
{
    Content content;
    auto quota = config.maxResultBytes;
    auto avail = config.maxMemoryBytes;
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(config.maxExecutionMs);
    if (auto state = std::unique_ptr<lua_State, void (*)(lua_State*)>(
            lua_newstate(Allocate, &avail, luaL_makeseed(nullptr)), lua_close)) {
        if (luaL_loadstring(state.get(), script.c_str())) {
            std::size_t length;
            content.output.append(lua_tolstring(state.get(), -1, &length), length);
            content.output.push_back('\n');
            lua_settop(state.get(), -2);
            std::string buffer;
            if (auto pe = glz::write<RequireAllKeys>(content, buffer)) {
                std::println(stderr, "{}", glz::format_error(pe, buffer));
            }
            return {buffer, true};
        }
        lua_newtable(state.get());
        RegisterCoreBindings(state.get());
        RegisterHostBindings(state.get(), content.output, config.maxOutputBytes);
        lua_pushvalue(state.get(), -1);
        lua_setfield(state.get(), -2, "_G");
        lua_setupvalue(state.get(), -2, 1);
        auto extra = reinterpret_cast<std::byte*>(state.get()) - LUA_EXTRASPACE;
        reinterpret_cast<std::chrono::steady_clock::time_point**>(extra)[0] = &until;
        lua_sethook(
            state.get(),
            [](lua_State* state, lua_Debug*) -> void {
                auto extra = reinterpret_cast<std::byte*>(state) - LUA_EXTRASPACE;
                auto until = reinterpret_cast<std::chrono::steady_clock::time_point**>(extra)[0];
                if (*until < std::chrono::steady_clock::now()) {
                    luaL_error(state, "timeout exceeded");
                }
            },
            LUA_MASKCOUNT, 0x10000);
        if (lua_pcall(state.get(), 0, LUA_MULTRET, 0)) {
            std::size_t length;
            content.output.append(lua_tolstring(state.get(), -1, &length), length);
            content.output.push_back('\n');
            lua_settop(state.get(), -2);
            std::string buffer;
            if (auto pe = glz::write<RequireAllKeys>(content, buffer)) {
                std::println(stderr, "{}", glz::format_error(pe, buffer));
            }
            return {buffer, true};
        }
        for (auto i = 1, n = lua_gettop(state.get()); i <= n; ++i) {
            std::size_t length;
            content.result.emplace_back(luaL_tolstring(state.get(), i, &length), length);
            if (quota < length) {
                content.output.append("result too large\n");
                content.result.clear();
                break;
            }
            quota -= length;
        }
    }
    else {
        throw std::bad_alloc();
    }
    std::string buffer;
    if (auto pe = glz::write<RequireAllKeys>(content, buffer)) {
        std::println(stderr, "{}", glz::format_error(pe, buffer));
    }
    return {buffer, false};
}

void Serve(const Config& config, auto&& handler)
{
    std::string request;
    while (std::getline(std::cin, request)) {
        if (auto response = handler(request); !response.empty()) {
            response.push_back('\n');
            for (auto i = 0ZU, n = response.size(); i < n; i += config.maxPacketBytes) {
                auto s = std::string_view(response).substr(i, config.maxPacketBytes);
                for (auto [j, c] : s | std::views::reverse | std::views::take(4) | std::views::enumerate) {
                    auto k = std::countl_one<unsigned char>(c);
                    if (k == 0) {
                        break;
                    }
                    if (k == 1) {
                        continue;
                    }
                    if (k >= 2 + j) {
                        i -= 1 + j;
                        s.remove_suffix(1 + j);
                    }
                    break;
                }
                std::fwrite(s.data(), s.size(), 1, stdout);
                std::fflush(stdout);
            }
        }
    }
}

} // namespace

auto main(int argc, char** argv) -> int
{
    Config config{
        .maxExecutionMs = 10000ZU,
        .maxMemoryBytes = 1ZU << 30,
        .maxOutputBytes = 1ZU << 16,
        .maxPacketBytes = 1ZU << 9,
        .maxResultBytes = 1ZU << 12,
    };
    std::vector<std::string_view> paths;
    paths.reserve(argc);
    paths.emplace_back("config.json");
    paths.append_range(std::span(argv, argc).subspan(1));
    std::string buffer;
    for (auto path : paths | std::views::filter([](auto path) -> auto { return std::filesystem::exists(path); })) {
        if (auto pe = glz::read_file_json(config, path, buffer)) {
            std::println(stderr, "{}", glz::format_error(pe, buffer));
        }
    }
    try {
        glz::rpc::server<glz::rpc::method<"notifications/initialized", ParamsNotifications, ResultNotifications>,
                         glz::rpc::method<"initialize", ParamsInitialize, ResultInitialize>,
                         glz::rpc::method<"tools/list", ParamsToolsList, ResultToolsList>,
                         glz::rpc::method<"tools/call", ParamsToolsCall, ResultToolsCall>>
            server;
        server.on<"notifications/initialized">([](const auto&) -> auto { return ResultNotifications{}; });
        server.on<"initialize">([](const auto&) -> auto {
            ResultInitialize result;
            result.protocolVersion = ProtocolVersion;
            result.capabilities.tools.listChanged = true;
            result.serverInfo.name = SafeLua::Project;
            result.serverInfo.version = SafeLua::Version;
            return result;
        });
        server.on<"tools/list">([](const auto&) -> auto {
            ResultToolsList result;
            auto& tool = result.tools.emplace_back();
            tool.name = "execute_lua";
            tool.description = "Executes strictly sandboxed Lua 5.5 scripts. "
                               "Must be used for solving or validating structural and deterministic tasks, "
                               "no matter how simple or trivial. "
                               "Available: 'math', 'string', 'table', 'utf8', and restricted basic functions ONLY. "
                               "Stateless, binary-safe, and resource-limited. "
                               "Output mapping: 'return' -> 'result' array, 'print()' -> 'output' string.";
            tool.inputSchema.properties.script.title = "script";
            tool.inputSchema.properties.script.type = "string";
            tool.inputSchema.properties.script.description = "Raw Lua code to execute. Do NOT wrap in markdown blocks.";
            tool.inputSchema.required[0] = "script";
            tool.inputSchema.title = "result";
            tool.inputSchema.type = "object";
            return result;
        });
        server.on<"tools/call">([&config](const auto& params) -> auto {
            ResultToolsCall result;
            if (params.name == "execute_lua") {
                using namespace std::chrono_literals;
                auto results = Execute(config, params.arguments.script);
                result.content[0].type = "text";
                result.content[0].text = std::get<0>(results);
                result.isError = std::get<1>(results);
            }
            else {
                result.isError = true;
            }
            return result;
        });
        Serve(config, [&server](const auto& request) -> auto { return server.call(request); });
    }
    catch (const std::exception& e) {
        std::println(stderr, "{}", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
