# C-Agent

A lightweight implemented from scratch in C. It features a robust **ReAct (Reasoning and Acting)** engine, allowing it to solve complex tasks by autonomously executing shell commands and reasoning through multi-turn feedback loops.

## 🌟 Key Features

- **ReAct Architecture**: A sophisticated "Thought-Action-Observation" loop that enables the LLM to interact with the local operating system dynamically.
- **Extensible Tool Registry**: A decoupled, static registry system that allows seamless addition of new LLM-callable tools without modifying the core reasoning loop.
- **Parallel Execution Engine**: Leverages POSIX threads (`pthreads`) to execute batches of read-only tools concurrently, maximizing throughput while maintaining strict order invariants for the LLM context.
- **Environment-Aware Networking**: Features heuristic protocol detection, automatically routing HTTP/HTTPS and resolving API endpoints (e.g., DeepSeek, OpenAI, Ollama) with zero-configuration required.
- **Security-First Sandboxing**: Implements strict filesystem containment using kernel-level path canonicalization to prevent directory traversal and symlink attacks.

## 🏗️ Technical Architecture

The project is architected to separate concerns across four primary layers:

1. **Decision Layer (`agent/`)**: Orchestrates the conversation history and manages the state machine for reasoning turns.
2. **Communication Layer (`llm_client/`, `http/`)**: A custom-built HTTP/1.1 client using raw TCP sockets to interface with OpenAI-compatible APIs.
3. **Action Layer (`tools/`)**: A safe execution environment that manages the lifecycle of shell commands and provides feedback to the Agent.
4. **Presentation Layer (`ui/`)**: A producer-consumer model for terminal UI updates, using ANSI escape codes for dynamic region refreshes.

## 🧠 Technical Highlights

- **Concurrency & State Isolation**: Developed a dispatcher that groups read-only tool calls (e.g., `read_file`) for parallel execution via `pthread`, while falling back to strict serial execution for state-mutating tools (e.g., `bash`, `edit_file`) to prevent race conditions.
- **Defensive Path Sandboxing**: Bridged the gap between LLM relative paths and kernel absolute paths using a canonicalize-then-check workflow (`realpath(3)`). The sandbox safely handles non-existent write targets without introducing silent `mkdir` vulnerabilities.
- **Asynchronous UI Synchronization**: Designed a Producer-Consumer event queue using `pthread_mutex_t` and `pthread_cond_t`. This ensures the CLI spinner runs at a smooth 80ms frame rate while the main thread is blocked by synchronous network I/O.
- **Fail-Fast Memory Management**: Custom wrappers (`xmalloc`, `xasprintf`) prevent silent OOM crashes, while rigorous deep-freezing (`llm_response_free`) prevents memory leaks during long-running, multi-turn LLM conversations.

## 🚀 Getting Started

### 1. Clone
```bash
git clone https://github.com/qingafun/C-Agent.git
cd C-Agent
```

### 2. Prerequisites
Ensure you have a C compiler (`gcc` or `clang`) and `make` or `CMake` installed. Set your environment variables:

```bash
export API_KEY="your_api_key_here"
export MODEL_ID="your_model_id_here"
export LLM_HOST="your_llm_host"
export LLM_PORT=443
```

### 3. Build from Source
The project uses a custom Makefile for optimized incremental builds:

```bash
make clean && make
```

or CMake:

```bash
cmake -B build
cmake --build build
```

### 4. Run
```bash
./build/c-agent
```

## 📸 Interactive UI

Thanks to the **UI/Rendering separation**, the agent provides real-time visual feedback:
- `⠋ Thinking...`: Displayed during active LLM inference.
- `✓ bash { "command": "ls -la" } (0.15s)`: Success status with real-time execution timing.
- `✗ bash { "command": "grep ..." } (0.05s)`: Error reporting with captured stderr feedback.