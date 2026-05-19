# Contributing to mcp-safe-lua

Thank you for your interest in `mcp-safe-lua`.

## Contribution Policy

To maintain a consistent architectural vision and a specific design philosophy, the core implementation and refactoring of the main codebase are handled exclusively by the primary developer.

### Core Logic & Bug Reports

**We do not accept Pull Requests that modify the core logic, even for bug fixes.**

The primary developer maintains a strict standard for the codebase's structure and design patterns. If you discover a bug, please do not submit a PR. Instead, report it by opening an **Issue** with the following details:

- A clear description of the bug.
- Precise steps to reproduce the issue.
- Your environment details (OS, MCP client version).
- A minimal reproduction Lua script.

### Feature Requests

We welcome suggestions and discussions regarding new features or improvements via **Issues**.

However, please be aware that proposals that conflict with the project's fundamental design principles—specifically **Statelessness, API Restrictions, Output Limits, and Output Sanitization** as detailed in the `README.md`—or those that compromise the security model (e.g., requests to relax the sandbox) will be rejected.

### Documentation & Testing

Contributions that enhance the project's stability and usability are strongly encouraged and welcomed:

- **Test Suite Expansion**: Adding new test cases to ensure robustness and cover edge cases.
- **Documentation Improvements**: Expanding the user guide, adding detailed technical specifications, or improving the clarity and accuracy of existing documentation.

**Important Note on Project Identity:**

While we welcome documentation improvements, we do not accept changes that modify the **Motivation**, **Design Philosophy**, or the unique tone and intent expressed by the developer in the `README.md`. These elements are central to the project's identity and must remain intact.

## Participation Flow

### Reporting Issues & Feedback

If you find a bug or have a suggestion, please open an **Issue**.

- Be as technical and specific as possible.
- Include your environment (OS, MCP client).
- Provide a minimal reproduction case.

### Submitting Pull Requests

Pull Requests are accepted **only** for tests and documentation:

1. Fork the repository.
2. Create a new branch for your changes.
3. Submit a Pull Request with a clear description of the improvements.

We appreciate your understanding and your support in making `mcp-safe-lua` a better tool.
