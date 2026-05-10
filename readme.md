# C-Agent

A lightweight AI coding agent implemented from scratch in C. It features a robust **ReAct (Reasoning and Acting)** engine, allowing it to solve complex tasks by autonomously executing shell commands and reasoning through multi-turn feedback loops. Supports **Linux** and **Windows** from a single codebase.

## 🌟 Key Features

- **ReAct Architecture**: A sophisticated "Thought-Action-Observation" loop that enables the LLM to interact with the local operating system dynamically.
- **Cross-Platform**: Single codebase compiles natively on Linux and Windows. The `compat.h` platform abstraction layer maps threading, networking, filesystem, and terminal I/O to the OS-native APIs (`pthread` on Linux, Win32 on Windows) — zero dependencies beyond OpenSSL.
- **Extensible Tool Registry**: A decoupled, static registry system that allows seamless addition of new LLM-callable tools without modifying the core reasoning loop.
- **Parallel Execution Engine**: Groups batches of read-only tools for concurrent execution, maximizing throughput while maintaining strict order invariants for the LLM context.
- **Environment-Aware Networking**: Features heuristic protocol detection, automatically routing HTTP/HTTPS and resolving API endpoints (e.g., DeepSeek, OpenAI, Ollama) with zero-configuration required.
- **Security-First Sandboxing**: Implements strict filesystem containment using kernel-level path canonicalization to prevent directory traversal and symlink attacks.

## 🏗️ Technical Architecture

The project is architected to separate concerns across four primary layers:

1. **Decision Layer (`agent/`)**: Orchestrates the conversation history and manages the state machine for reasoning turns.
2. **Communication Layer (`http/`, `https/`)**: A custom-built HTTP/1.1 client using raw TCP sockets to interface with OpenAI-compatible APIs.
3. **Action Layer (`tools/`)**: A safe execution environment that manages the lifecycle of shell commands and provides feedback to the Agent.
4. **Presentation Layer (`ui/`)**: A producer-consumer model for terminal UI updates, using ANSI escape codes for dynamic region refreshes.

## 🧠 Technical Highlights

- **Platform Abstraction**: `compat.h` centralizes all OS-specific code behind a uniform API — `mutex_lock`, `thread_create`, `socket_close`, etc. — so the rest of the codebase never touches `#ifdef _WIN32`.
- **Concurrency & State Isolation**: A dispatcher groups read-only tool calls for parallel execution, while falling back to strict serial execution for state-mutating tools (`bash`, `edit_file`) to prevent race conditions.
- **Defensive Path Sandboxing**: Bridges the gap between LLM relative paths and kernel absolute paths using a canonicalize-then-check workflow. The sandbox safely handles non-existent write targets without introducing silent `mkdir` vulnerabilities.
- **Asynchronous UI Synchronization**: A Producer-Consumer event queue ensures the CLI spinner runs at a smooth 80ms frame rate while the main thread is blocked by synchronous network I/O.
- **Fail-Fast Memory Management**: Custom wrappers (`xmalloc`, `xasprintf`) prevent silent OOM crashes, while rigorous deep-freezing (`llm_response_free`) prevents memory leaks during long-running, multi-turn LLM conversations.

## 🚀 Getting Started

### 1. Clone
```bash
git clone https://github.com/qingafun/C-Agent.git
cd C-Agent
```

### 2. Prerequisites

- C11 compiler (`gcc`, `clang`, or MSVC)
- `CMake` 3.10+ (recommended), or `make`
- OpenSSL (optional on Windows, required on Linux)

**Environment variables:**

| Variable | Default | Description |
|----------|---------|-------------|
| `API_KEY` | `none` | API key for the LLM service |
| `MODEL_ID` | `deepseek-chat` | Model name |
| `LLM_HOST` | `api.deepseek.com` | API server hostname |
| `LLM_PORT` | `443` | API server port |
| `MAX_TOKENS` | `8000` | Maximum tokens per response |
| `LLM_PATH` | auto | API endpoint path |

### 3. Build from Source

**Linux:**
```bash
cmake -B build
cmake --build build
```

**Windows (MSVC):**
```powershell
cmake -B build
cmake --build build
```

**Alternatively with Make (Linux only):**
```bash
make clean && make
```

### 4. Run

**Linux:**
```bash
export API_KEY="your_api_key_here"
export MODEL_ID="your_model_id_here"
export LLM_HOST="your_llm_host"
export LLM_PORT=443
./build/c-agent
```

**Windows (PowerShell):**
```powershell
$env:API_KEY="your_api_key_here"
$env:MODEL_ID="your_model_id_here"
$env:LLM_HOST="your_llm_host"
$env:LLM_PORT=443
.\build\Debug\c-agent.exe
```

## 📸 Interactive UI

Thanks to the **UI/Rendering separation**, the agent provides real-time visual feedback:
- `⠋ Thinking...`: Displayed during active LLM inference.
- `✓ bash { "command": "ls -la" } (0.15s)`: Success status with real-time execution timing.
- `✗ bash { "command": "grep ..." } (0.05s)`: Error reporting with captured stderr feedback.