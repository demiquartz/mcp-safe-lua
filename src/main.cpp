// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2026 Takaaki Sato
/**
 * @file  main.cpp
 * @brief Provides the main entry point and dispatches JSON-RPC requests.
 */

#include <print>

#include <glaze/ext/jsonrpc.hpp>
#include <lua.hpp>

#include "config.hpp"
#include "stream.hpp"

namespace SafeLua {

constexpr glz::opts RequireAllKeys{.error_on_unknown_keys = false, .error_on_missing_keys = true};

struct Config {
    std::size_t maxExecutionMs;
    std::size_t maxMemoryBytes;
    std::size_t maxBufferBytes;
    std::size_t maxStdoutBytes;
    std::size_t maxStderrBytes;
    std::size_t maxResultBytes;
    std::size_t maxPacketBytes;
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
constexpr auto ProtocolVersion = "2025-11-25";
constexpr auto IsValid = std::to_array<bool (*)(int)>({
    [](int c) -> bool { return c >= 0x20 || c == '\b' || c == '\t' || c == '\n' || c == '\f' || c == '\r'; },
    [](int) -> bool { return false; },
    [](int c) -> bool { return c >= 0x80; },
    [](int c) -> bool { return c >= 0x0800 && c < 0xd800 || c >= 0xe000; },
    [](int c) -> bool { return c >= 0x010000 && c < 0x110000; },
});

auto Decode(std::string_view s, int n) -> int
{
    auto c = s[0] & 0x7f >> n;
    for (auto i = 1; i < n; ++i) {
        c = s[i] & 0x3f | c << 6;
    }
    return c;
}

void Sanitize(std::string_view source, std::size_t cutoff, std::string& target)
{
    auto buffer = std::string{};
    auto escape = [&buffer, &target] -> void {
        for (auto c : buffer) {
            std::format_to(std::back_inserter(target), "\\x{:02X}", c);
        }
        buffer.clear();
    };
    for (auto c : source) {
        if (target.size() >= cutoff) [[unlikely]] {
            return;
        }
        if (std::countl_one<unsigned char>(c) != 1) {
            escape();
        }
        buffer.push_back(c);
        auto i = std::countl_one<unsigned char>(buffer.front());
        if (i < static_cast<int>(IsValid.size())) {
            if (i != 0 && i > static_cast<int>(buffer.size())) {
                continue;
            }
            if (IsValid[i](Decode(buffer, i))) [[likely]] {
                target.append(buffer);
                buffer.clear();
                continue;
            }
        }
        escape();
    }
    escape();
}

void Sanitize(lua_State* state, int index, std::size_t cutoff, std::string& target)
{
    auto length = 0ZU;
    Sanitize({luaL_tolstring(state, index, &length), length}, cutoff, target);
    lua_settop(state, -2);
}

auto Print(lua_State* state) -> int
{
    auto buffer = static_cast<std::string*>(lua_touserdata(state, lua_upvalueindex(1)));
    auto cutoff = static_cast<std::size_t*>(lua_touserdata(state, lua_upvalueindex(2)));
    auto offset = buffer->size();
    if (!buffer->ends_with(OutputTruncated)) {
        auto target = std::string{};
        for (auto i = 1, n = lua_gettop(state); i <= n; ++i) {
            target.clear();
            Sanitize(state, i, *cutoff, target);
            if (buffer->size() + target.size() + OutputTruncated.size() + 1 >= *cutoff) {
                buffer->append(OutputTruncated);
                break;
            }
            buffer->append(target);
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

auto Fetch(std::span<char> buffer, std::string_view& rest, std::string& line) -> bool
{
    line.clear();
    do {
        auto pos = rest.find('\n');
        line.append_range(rest.substr(0, std::min(pos, buffer.size() - line.size())));
        if (pos != std::string_view::npos) {
            rest.remove_prefix(pos + 1);
            if (buffer.size() == line.size()) {
                line.clear();
                continue;
            }
            return true;
        }
        rest = {buffer.data(), Stream::Read(buffer)};
    } while (!rest.empty());
    line.clear();
    return false;
}

auto Execute(const Config& config, const std::string& script) -> std::tuple<std::string, bool>
{
    auto content = Content{};
    auto quota = config.maxResultBytes;
    auto avail = config.maxMemoryBytes;
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(config.maxExecutionMs);
    using UniqueState = std::unique_ptr<lua_State, void (*)(lua_State*)>;
    if (auto state = UniqueState{lua_newstate(Allocate, &avail, luaL_makeseed(nullptr)), lua_close}) {
        auto popError = [&config, &content, &state] -> std::tuple<std::string, bool> {
            auto length = 0ZU;
            auto cutoff = content.output.size() + config.maxStderrBytes - 1;
            Sanitize({lua_tolstring(state.get(), -1, &length), length}, cutoff, content.output);
            content.output.push_back('\n');
            lua_settop(state.get(), -2);
            auto buffer = std::string{};
            if (auto pe = glz::write<RequireAllKeys>(content, buffer)) {
                std::println(stderr, "{}", glz::format_error(pe, buffer));
            }
            return {buffer, true};
        };
        if (luaL_loadstring(state.get(), script.c_str())) {
            return popError();
        }
        lua_newtable(state.get());
        RegisterCoreBindings(state.get());
        RegisterHostBindings(state.get(), content.output, config.maxStdoutBytes);
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
            return popError();
        }
        for (auto i = 1, n = lua_gettop(state.get()); i <= n; ++i) {
            Sanitize(state.get(), i, config.maxResultBytes, content.result.emplace_back());
            if (quota < content.result.back().size()) {
                content.output.append("result too large\n");
                content.result.clear();
                break;
            }
            quota -= content.result.back().size();
        }
    }
    else {
        throw std::bad_alloc();
    }
    auto buffer = std::string{};
    if (auto pe = glz::write<RequireAllKeys>(content, buffer)) {
        std::println(stderr, "{}", glz::format_error(pe, buffer));
    }
    return {buffer, false};
}

void Serve(const Config& config, auto&& handler)
{
    auto buffer = std::vector<char>(std::max(config.maxBufferBytes, 1ZU));
    auto rest = std::string_view{};
    auto line = std::string{};
    auto step = std::max(config.maxPacketBytes, 4ZU);
    while (Fetch(buffer, rest, line)) {
        if (auto result = handler(line); !result.empty()) {
            result.push_back('\n');
            for (auto i = 0ZU, n = result.size(); i < n; i += step) {
                auto s = std::string_view(result).substr(i, step);
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
                Stream::Write(s);
            }
        }
    }
}

} // namespace

} // namespace SafeLua

auto main(int argc, char** argv) -> int
{
    auto config = SafeLua::Config{
        .maxExecutionMs = 10000ZU,
        .maxMemoryBytes = 1ZU << 30,
        .maxBufferBytes = 1ZU << 16,
        .maxStdoutBytes = 1ZU << 16,
        .maxStderrBytes = 1ZU << 12,
        .maxResultBytes = 1ZU << 12,
        .maxPacketBytes = 1ZU << 9,
    };
    auto paths = std::vector<std::string_view>{};
    paths.reserve(argc);
    paths.emplace_back("config.json");
    paths.append_range(std::span(argv, argc).subspan(1));
    auto buffer = std::string{};
    for (auto path : paths | std::views::filter([](auto path) -> auto { return std::filesystem::exists(path); })) {
        if (auto pe = glz::read_file_json(config, path, buffer)) {
            std::println(stderr, "{}", glz::format_error(pe, buffer));
        }
    }
    try {
        // clang-format off
        auto server = glz::rpc::server<
            glz::rpc::method<"notifications/initialized", SafeLua::ParamsNotifications, SafeLua::ResultNotifications>,
            glz::rpc::method<"initialize"               , SafeLua::ParamsInitialize   , SafeLua::ResultInitialize   >,
            glz::rpc::method<"tools/list"               , SafeLua::ParamsToolsList    , SafeLua::ResultToolsList    >,
            glz::rpc::method<"tools/call"               , SafeLua::ParamsToolsCall    , SafeLua::ResultToolsCall    >
        >{};
        // clang-format on
        server.on<"notifications/initialized">([](const auto&) -> auto { return SafeLua::ResultNotifications{}; });
        server.on<"initialize">([](const auto&) -> auto {
            // clang-format off
            return SafeLua::ResultInitialize{
                .protocolVersion = SafeLua::ProtocolVersion,
                .capabilities = {
                    .tools = {
                        .listChanged = true,
                    },
                },
                .serverInfo = {
                    .name = SafeLua::Project,
                    .version = SafeLua::Version,
                },
            };
            // clang-format on
        });
        server.on<"tools/list">([](const auto&) -> auto {
            // clang-format off
            return SafeLua::ResultToolsList{
                .tools = {
                    {
                        .name = "execute_lua",
                        .description =
                            "Executes strictly sandboxed Lua 5.5 scripts. "
                            "Must be used for solving or validating structural and deterministic tasks, "
                            "no matter how simple or trivial. "
                            "Available: 'math', 'string', 'table', 'utf8', and restricted basic functions ONLY. "
                            "Stateless, binary-safe, and resource-limited. "
                            "Output mapping: 'return' -> 'result' array, 'print()' -> 'output' string.",
                        .inputSchema = {
                            .properties = {
                                .script = {
                                    .title = "script",
                                    .type = "string",
                                    .description = "Raw Lua code to execute. Do NOT wrap in markdown blocks.",
                                },
                            },
                            .required = {
                                "script",
                            },
                            .title = "result",
                            .type = "object",
                        },
                    },
                },
            };
            // clang-format on
        });
        server.on<"tools/call">([&config](const auto& params) -> auto {
            auto result = SafeLua::ResultToolsCall{};
            if (params.name == "execute_lua") {
                using namespace std::chrono_literals;
                auto results = SafeLua::Execute(config, params.arguments.script);
                result.content[0].type = "text";
                result.content[0].text = std::get<0>(results);
                result.isError = std::get<1>(results);
            }
            else {
                result.isError = true;
            }
            return result;
        });
        SafeLua::Serve(config, [&server](const auto& request) -> auto { return server.call(request); });
    }
    catch (const std::exception& e) {
        std::println(stderr, "{}", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
