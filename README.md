# mcp-safe-lua

**Strictly sandboxed Lua execution server for Model Context Protocol (MCP).**

`mcp-safe-lua` is a specialized personal MCP server that allows LLMs to execute Lua 5.5 scripts in a secure, resource-constrained environment. It provides a safe way to perform complex calculations, data transformations, and algorithmic tasks that are difficult for LLMs to perform reliably using text alone, without compromising the host system's security.

## 💡 Motivation: Stop Making LLMs Do Mental Math

Why on earth do we force LLMs to do mental arithmetic, burning 1,000,000x the cost and energy, when we already have the ultimate calculating machine—the CPU—sitting right there?

There is nothing more comically ridiculous than watching a literal genius sweating over calculating trigonometric, logarithmic, or exponential functions in their head while a state-of-the-art scientific calculator sits completely idle right next to them. It is a spectacular waste of electricity.

LLMs are brilliant at reasoning, understanding context, and generating logic. They shouldn't be wasting their parameters trying to guess the next term of an infinite series. `mcp-safe-lua` gives them the tool they actually need: a strict, sandboxed environment to hand off exact calculations to the CPU.

Let the LLM reason. Let the CPU compute.

## 📐 Design Philosophy

### Stateless

`mcp-safe-lua` does not persist state between tool calls.

Statelessness is a distinct advantage. Under a completely stateless environment, the LLM must recall its past memory every single time, fully leveraging its self-attention mechanism.

If a stateful mode were added, the LLM would stop recalling its past memory and eventually suffer from memory loss. When an error occurred between its new code and past code, the LLM—despite having written it itself—would claim that it is the environment's fault, stop using the calculator, and ultimately resort to doing mental math.

### API Restrictions

Through a strict allowlist approach, the runtime environment blocks all potentially hazardous capabilities. You cannot perform file or network operations.

True to its name, `mcp-safe-lua` provides a secure computing environment. No matter how wildly the LLM behaves, this environment ensures that accidents like deleting critical files will never happen, and it is completely isolated from the host system.

### Output Limit

Standard output is capped at 64 KB by default, but this limit is configurable.

Allowing a script to return unbounded, limitless text to an LLM would instantly overwhelm its context window. Even with massive modern context windows, flooding them with raw, runaway logs obliterates the model's attention span and nukes your token budget.

### Output Sanitization

`mcp-safe-lua` strictly sanitizes all outputs, ensuring that invalid byte sequences and hazardous control characters are converted into a valid plain text representation.

This is not just about preventing JSON-RPC communication crashes—it is a critical requirement for steering the LLM toward proper self-debugging. If an LLM accidentally dumps raw binary data directly to standard output, raw null bytes would instantly terminate the STDIO channel or cause invisible, garbled text. Left unable to clearly observe its own output, the LLM loses track of the bug it just introduced. As a result, it blindly blames the environment for the failure and stops using the tool entirely. Forcing every raw byte into a valid, unbroken text format guarantees perfect observability, forcing the model to recognize its own mistakes and self-debug effectively.

## 🚀 Key Features

- **Strict Sandboxing**: The Lua environment is stripped of all dangerous capabilities. Only a minimal set of safe libraries is available.
- **Resource Constraints**: Hard limits on memory and execution time are enforced to prevent Denial-of-Service (DoS) attacks or infinite loops.
- **Performance**: Built with **C++23**, leveraging **glaze** for efficient JSON serialization/deserialization.
- **MCP Compliant**: Implements the Model Context Protocol for integration with MCP clients.

## 🛡️ Security Model

Security is the core priority of `mcp-safe-lua`. The following layers of protection are implemented:

### API Allowlisting

Instead of loading all standard libraries collectively and then removing unnecessary components, it initializes an empty environment and explicitly registers only verified, safe functions and modules. Dangerous utilities like `io`, `os`, `package`, and `debug` are completely omitted.

The explicitly exposed API consists strictly of the following:

- **Standard Modules**: `math`, `string`, `table`, and `utf8`.
- **Core Globals**: `_VERSION`, `assert`, `error`, `getmetatable`, `setmetatable`, `ipairs`, `pairs`, `next`, `pcall`, `xpcall`, `rawequal`, `rawget`, `rawlen`, `rawset`, `select`, `tonumber`, `tostring`, and `type`.
- **Host Globals**: A tailored `print` function equipped with low-level UTF-8 validation and output buffering checks.

### Memory Usage Limit

A custom Lua allocator is implemented to track and limit memory usage.

- **Default Limit**: 1 GB (Configurable).
- If a script exceeds this limit, the execution is immediately terminated with an "out of memory" error.

### Execution Time Limit

To prevent infinite loops or CPU exhaustion, `lua_sethook` is used to monitor execution.

- **Default Timeout**: 10 seconds (Configurable).
- Scripts that run longer than the timeout are aborted.

### Output Limits & Sanitization

A custom `print` function is implemented to protect the communication channel and context window.

- **Default Limit**: 64 KB (Configurable).
- If the output exceeds this limit, the text is automatically truncated.
- If the output contains invalid UTF-8 bytes or control characters, they are converted into literal hexadecimal escapes (`\xXX`).

## ⚙️ Installation & Build Guide

### Prerequisites

- **C++23 Compatible Compiler**
- [xmake](https://xmake.io/)

### Build Steps

```bash
# Clone the repository
git clone https://github.com/demiquartz/mcp-safe-lua.git
cd mcp-safe-lua

# Configure and build
xmake
```

## 📖 Usage

`mcp-safe-lua` is an MCP server. It is designed to be used by an MCP client rather than by a human user directly in a terminal. The server communicates via **STDIO**, meaning the client sends requests and receives responses through the standard input/output streams. Depending on your client's capabilities, you may connect directly or use a proxy.

### Direct STDIO Connection

For clients that support direct STDIO communication, add the following to your configuration:

```json
{
  "mcpServers": {
    "mcp-safe-lua": {
      "command": "/path/to/your/build/mcp-safe-lua",
      "args": []
    }
  }
}
```

### Connection via HTTP Proxy

For clients that require HTTP connections, you will need an external proxy tool to bridge HTTP requests to the server's STDIO interface.

Simply configure your proxy to wrap the `mcp-safe-lua` binary, and connect your MCP client to the proxy's HTTP endpoint.

## 🛠️ Tools

### execute_lua

The server provides a single tool called `execute_lua` to run sandboxed Lua scripts.

#### Example: Prime Factorization

Suppose you want to find the prime factors of a large number.

```lua
local n = 2083972139
local d = 2
local f = {}
print("Factoring number: " .. n)
while d * d <= n do
    if n % d == 0 then
        table.insert(f, d)
        n = n // d
    else
        d = d + 1
    end
end
if n > 1 then table.insert(f, n) end
print("Found " .. #f .. " prime factors.")
return table.unpack(f)
```

The client sends the script as a JSON string:

```json
{
  "script": "local n = 2083972139\nlocal d = 2\nlocal f = {}\nprint(\"Factoring number: \" .. n)\nwhile d * d <= n do\n    if n % d == 0 then\n        table.insert(f, d)\n        n = n // d\n    else\n        d = d + 1\n    end\nend\nif n > 1 then table.insert(f, n) end\nprint(\"Found \" .. #f .. \" prime factors.\")\nreturn table.unpack(f)"
}
```

The server returns the execution logs in `output` and the return value in `result`.

```json
{
  "content": [
    {
      "type": "text",
      "text": "{\"output\":\"Factoring number: 2083972139\\nFound 2 prime factors.\\n\",\"result\":[\"31847\",\"65437\"]}"
    }
  ],
  "isError": false
}
```

## 🗺️ Roadmap
- [ ] Code refactoring and architectural improvements
- [ ] Performance optimizations
- [ ] Verify strict compliance with MCP specifications
- [ ] Support for remote MCP connections
- [ ] Implement comprehensive automated testing
- [ ] Expand and refine technical documentation

## 📄 License

This project is licensed under the **Apache License 2.0**. See the [LICENSE](LICENSE) file for details.
