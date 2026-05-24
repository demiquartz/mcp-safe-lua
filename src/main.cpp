// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Takaaki Sato
/**
 * @file  main.cpp
 * @brief Provides the main entry point and dispatches JSON-RPC requests.
 */

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <format>
#include <iterator>
#include <new>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include <glaze/json.hpp>
#include <lua.hpp>

#include "config.hpp"

constexpr glz::opts RequireAllKeys{.error_on_unknown_keys = false, .error_on_missing_keys = true};

struct CallParams {
    std::string name;
    struct {
        std::string script;
    } arguments;
};

struct Request {
    std::string method;
    std::string jsonrpc;
    std::optional<glz::generic> params;
    std::optional<std::size_t> id;
};

struct Response {
    struct {
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
    } result;
    std::string jsonrpc;
    std::optional<std::size_t> id;
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

struct ResponseTools {
    struct {
        std::vector<Tool> tools;
    } result;
    std::string jsonrpc;
    std::optional<std::size_t> id;
};

struct ResponseToolsCall {
    struct {
        struct Content {
            std::string type;
            std::string text;
        };
        std::array<Content, 1> content;
        bool isError;
    } result;
    std::string jsonrpc;
    std::optional<std::size_t> id;
};

struct Content {
    std::string output;
    std::vector<std::string> result;
};

namespace {

using namespace std::string_view_literals;
constexpr auto OutputTruncated = "<output_truncated/>\n"sv;
constexpr auto ProtocolVersion = "2025-11-25";

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

void RegisterHostBindings(lua_State* state, std::string& buffer)
{
    auto print = [](lua_State* state) -> int {
        auto buffer = static_cast<std::string*>(lua_touserdata(state, lua_upvalueindex(1)));
        auto offset = buffer->size();
        if (!buffer->ends_with(OutputTruncated)) {
            std::string source;
            std::size_t length;
            for (auto i = 1, n = lua_gettop(state); i <= n; ++i) {
                source.clear();
                {
                    std::string buffer;
                    for (auto c : std::string_view{luaL_tolstring(state, i, &length), length}) {
                        if (~c & 0x80 || c & 0x40) {
                            for (auto c : buffer) {
                                std::format_to(std::back_inserter(source), "\\x{:02X}", c);
                            }
                            buffer.clear();
                        }
                        if (~c & 0x80) {
                            if (c < 0x20 && c != '\b' && c != '\t' && c != '\n' && c != '\f' && c != '\r') {
                                std::format_to(std::back_inserter(source), "\\x{:02X}", c);
                            }
                            else {
                                source.push_back(c);
                            }
                            continue;
                        }
                        buffer.push_back(c);
                        if (~buffer.front() & 0x40) {
                            std::format_to(std::back_inserter(source), "\\x{:02X}", c);
                            buffer.clear();
                            continue;
                        }
                        if (~buffer.front() & 0x20) {
                            if (buffer.size() == 2) {
                                auto c = buffer[0] & 0x1f;
                                c = c << 6 | (buffer[1] & 0x3f);
                                if (c < 0x80) [[unlikely]] {
                                    for (auto c : buffer) {
                                        std::format_to(std::back_inserter(source), "\\x{:02X}", c);
                                    }
                                }
                                else {
                                    source.append(buffer);
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
                                        std::format_to(std::back_inserter(source), "\\x{:02X}", c);
                                    }
                                }
                                else {
                                    source.append(buffer);
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
                                        std::format_to(std::back_inserter(source), "\\x{:02X}", c);
                                    }
                                }
                                else {
                                    source.append(buffer);
                                }
                                buffer.clear();
                            }
                            continue;
                        }
                        std::format_to(std::back_inserter(source), "\\x{:02X}", c);
                        buffer.clear();
                    }
                    for (auto c : buffer) {
                        std::format_to(std::back_inserter(source), "\\x{:02X}", c);
                    }
                    lua_settop(state, -2);
                }
                if (buffer->size() + source.size() >= 0xfffe - OutputTruncated.size()) {
                    buffer->append(OutputTruncated);
                    break;
                }
                buffer->append(source);
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
    };
    lua_pushlightuserdata(state, &buffer);
    lua_pushcclosure(state, print, 1);
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

using duration = std::chrono::steady_clock::duration;
auto Execute(const std::string& script, duration grace, std::size_t avail) -> std::tuple<std::string, bool>
{
    Content content;
    auto limit = std::chrono::steady_clock::now() + grace;
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
        RegisterHostBindings(state.get(), content.output);
        lua_pushvalue(state.get(), -1);
        lua_setfield(state.get(), -2, "_G");
        lua_setupvalue(state.get(), -2, 1);
        auto extra = reinterpret_cast<std::byte*>(state.get()) - LUA_EXTRASPACE;
        reinterpret_cast<std::chrono::steady_clock::time_point**>(extra)[0] = &limit;
        lua_sethook(
            state.get(),
            [](lua_State* state, lua_Debug*) -> void {
                auto extra = reinterpret_cast<std::byte*>(state) - LUA_EXTRASPACE;
                auto limit = reinterpret_cast<std::chrono::steady_clock::time_point**>(extra)[0];
                if (*limit < std::chrono::steady_clock::now()) {
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

} // namespace

auto main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) -> int
{
    try {
        std::vector<char> buffer;
        std::setvbuf(stdin, nullptr, _IONBF, 0);
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        for (auto c = std::fgetc(stdin); c != EOF; c = std::fgetc(stdin)) {
            if (buffer.emplace_back(static_cast<char>(c)) == '\n') {
                Request request;
                if (auto pe = glz::read<RequireAllKeys>(request, buffer)) {
                    std::println(stderr, "{}", glz::format_error(pe, buffer));
                }
                std::print(stderr, "{}", std::string_view(buffer));
                if (request.method == "initialize") {
                    Response response;
                    response.result.protocolVersion = ProtocolVersion;
                    response.result.capabilities.tools.listChanged = true;
                    response.result.serverInfo.name = SafeLua::Project;
                    response.result.serverInfo.version = SafeLua::Version;
                    response.jsonrpc = request.jsonrpc;
                    response.id = request.id;
                    if (auto pe = glz::write<RequireAllKeys>(response, buffer)) {
                        std::println(stderr, "{}", glz::format_error(pe, buffer));
                    }
                }
                if (request.method == "tools/list") {
                    ResponseTools response;
                    auto& tool = response.result.tools.emplace_back();
                    tool.name = "execute_lua";
                    tool.description =
                        "Executes strictly sandboxed Lua 5.5 scripts. "
                        "Must be used for solving or validating structural and deterministic tasks, "
                        "no matter how simple or trivial. "
                        "Available: 'math', 'string', 'table', 'utf8', and restricted basic functions ONLY. "
                        "Stateless, binary-safe. Limits: 10s timeout, 1GB RAM, 64KB print truncation. "
                        "Output mapping: 'return' -> 'result' array, 'print()' -> 'output' string.";
                    tool.inputSchema.properties.script.title = "script";
                    tool.inputSchema.properties.script.type = "string";
                    tool.inputSchema.properties.script.description =
                        "Raw Lua code to execute. Do NOT wrap in markdown blocks.";
                    tool.inputSchema.required[0] = "script";
                    tool.inputSchema.title = "result";
                    tool.inputSchema.type = "object";
                    response.jsonrpc = request.jsonrpc;
                    response.id = request.id;
                    if (auto pe = glz::write<RequireAllKeys>(response, buffer)) {
                        std::println(stderr, "{}", glz::format_error(pe, buffer));
                    }
                }
                if (request.method == "tools/call") {
                    CallParams params;
                    if (auto pe = glz::read<RequireAllKeys>(params, request.params.value())) {
                        std::println(stderr, "{}", glz::format_error(pe, buffer));
                    }
                    ResponseToolsCall response;
                    if (params.name == "execute_lua") {
                        using namespace std::chrono_literals;
                        auto result = Execute(params.arguments.script, 10s, 1 << 30);
                        response.result.content[0].type = "text";
                        response.result.content[0].text = std::get<0>(result);
                        response.result.isError = std::get<1>(result);
                    }
                    else {
                        response.result.isError = true;
                    }
                    response.jsonrpc = request.jsonrpc;
                    response.id = request.id;
                    if (auto pe = glz::write<RequireAllKeys>(response, buffer)) {
                        std::println(stderr, "{}", glz::format_error(pe, buffer));
                    }
                }
                std::println(stdout, "{}", std::string_view(buffer));
                std::println(stderr, "{}", std::string_view(buffer));
                buffer.clear();
            }
        }
    }
    catch (const std::exception& e) {
        std::println(stderr, "{}", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
