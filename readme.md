# C-Agent

A lightweight implemented from scratch in C. It features a robust **ReAct (Reasoning and Acting)** engine, allowing it to solve complex tasks by autonomously executing shell commands and reasoning through multi-turn feedback loops.

## 🌟 Key Features

- **ReAct Architecture**: A sophisticated "Thought-Action-Observation" loop that enables the LLM to interact with the local operating system.
- **Asynchronous UI Rendering**: Implements a dedicated rendering thread via `pthread`. The UI remains fluid with smooth spinner animations even while the main thread is blocked by network I/O or heavy shell executions.
- **Low-Level System Integration**: Leverages Linux system primitives (`fork`, `pipe`, `dup2`, `exec`) to capture stdout/stderr from subprocesses directly.
- **Zero External Dependencies**: Built with standard C libraries and `cJSON` for maximum portability and speed.
- **Fail-Fast Memory Management**: Robust wrappers (`xmalloc`, `xasprintf`) ensure memory safety and immediate error reporting.

## 🏗️ Technical Architecture

The project is architected to separate concerns across four primary layers:

1. **Decision Layer (`agent/`)**: Orchestrates the conversation history and manages the state machine for reasoning turns.
2. **Communication Layer (`llm_client/`, `http/`)**: A custom-built HTTP/1.1 client using raw TCP sockets to interface with OpenAI-compatible APIs.
3. **Action Layer (`tools/`)**: A safe execution environment that manages the lifecycle of shell commands and provides feedback to the Agent.
4. **Presentation Layer (`ui/`)**: A producer-consumer model for terminal UI updates, using ANSI escape codes for dynamic region refreshes.

## 🧠 Technical Highlights

- **Asynchronous UI Synchronization**: Designed a Producer-Consumer event queue using `pthread_mutex_t` and `pthread_cond_t`. This ensures the CLI spinner runs at a smooth 80ms frame rate while the main thread is blocked by synchronous TCP network reads.
- **Safe Subprocess Management**: Utilized `fork()` and `execl()` to spawn isolated shell environments. Implemented anonymous pipes (`pipe()`) and file descriptor duplication (`dup2()`) to safely capture both `stdout` and `stderr` without terminal corruption.
- **Memory Safety**: Custom wrappers prevent silent OOM crashes, while rigorous deep-freezing (`llm_response_free`) prevents memory leaks during long-running, multi-turn LLM conversations.

## 🚀 Getting Started

### 1. Prerequisites
Ensure you have a C compiler (`gcc` or `clang`) and `make` or `CMake` installed. Set your environment variables:

```bash
export API_KEY="your_api_key_here"
export MODEL_ID="your_model_id_here"
export LLM_HOST="127.0.0.1"
export LLM_PORT=18080
```

### 2. Build from Source
The project uses a custom Makefile for optimized incremental builds:

```bash
make clean && make
```

or CMake:

```bash
cmake -B build
cmake --build build
```

### 3. Run
```bash
./build/c-agent
```

## 📸 Interactive UI

Thanks to the **UI/Rendering separation**, the agent provides real-time visual feedback:
- `⠋ Thinking...`: Displayed during active LLM inference.
- `✓ bash { "command": "ls -la" } (0.15s)`: Success status with real-time execution timing.
- `✗ bash { "command": "grep ..." } (0.05s)`: Error reporting with captured stderr feedback.