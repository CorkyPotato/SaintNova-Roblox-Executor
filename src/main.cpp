#include <windows.h>

#include <atomic>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "saintnova/core.hpp"
#include "saintnova/harness_backend.hpp"
#include "saintnova/luau_vm.hpp"
#include "saintnova/server_app.hpp"

namespace {

std::atomic<bool> g_stop{false};

BOOL WINAPI on_ctrl(DWORD) {
    g_stop = true;
    return TRUE;
}

void print_line(const saintnova::OutputLine& line) {
    std::ostream& out = (line.level == saintnova::LogLevel::Error) ? std::cerr : std::cout;
    out << '[' << saintnova::to_string(line.level) << "] " << line.text << '\n';
}

bool read_file(const std::string& path, std::string& contents, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    contents = buffer.str();
    return true;
}

int usage() {
    std::cout << "SaintNova " SAINTNOVA_VERSION " - Luau execution framework\n\n"
                 "usage: saintnova <command> [options]\n\n"
                 "commands:\n"
                 "  serve [--pipe NAME]        host the named-pipe server\n"
                 "  run <file> [--timeout MS] [--backend ID]\n"
                 "                             run a script once and print output\n"
                 "  compile <file> [-o OUT]    compile to Luau bytecode\n"
                 "  backends                   list registered backends\n"
                 "  repl                       interactive harness session\n"
                 "  version                    print version\n";
    return 2;
}

void print_backends(const std::vector<saintnova::BackendInfo>& backends) {
    for (const auto& backend : backends) {
        std::cout << backend.id << "  [" << saintnova::to_string(backend.kind) << "]  "
                  << (backend.available ? "available" : "unavailable") << "\n    " << backend.name << "\n    "
                  << backend.detail << "\n";
    }
}

int cmd_serve(const std::vector<std::string>& args) {
    std::string pipe = saintnova::default_pipe_name();
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--pipe" && i + 1 < args.size()) {
            pipe = args[++i];
        }
    }

    saintnova::SaintNovaServer server;
    std::string error;
    if (!server.start(pipe, error)) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }

    SetConsoleCtrlHandler(on_ctrl, TRUE);
    std::cout << "SaintNova " SAINTNOVA_VERSION << " listening on \\\\.\\pipe\\" << pipe << '\n' << std::flush;

    while (!g_stop.load() && !server.stop_requested()) {
        Sleep(50);
    }

    server.stop();
    std::cout << "stopped\n";
    return 0;
}

int cmd_run(const std::vector<std::string>& args) {
    if (args.empty()) return usage();

    std::string path;
    std::string backend_id;
    std::uint32_t timeout = 5000;

    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--timeout" && i + 1 < args.size()) {
            timeout = static_cast<std::uint32_t>(std::stoul(args[++i]));
        } else if (args[i] == "--backend" && i + 1 < args.size()) {
            backend_id = args[++i];
        } else if (path.empty()) {
            path = args[i];
        }
    }

    std::string script;
    std::string error;
    if (!read_file(path, script, error)) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }

    auto core = saintnova::make_default_core();

    saintnova::ExecutionRequest request;
    request.id = "cli";
    request.script = std::move(script);
    request.chunk_name = "=" + path;
    request.timeout_ms = timeout;

    const auto result = core->execute(request, backend_id, print_line, nullptr);

    std::cout << (result.ok ? "ok" : "failed") << " in " << (result.duration_us / 1000.0) << " ms\n";
    if (!result.ok) {
        std::cerr << "error: " << result.error << '\n';
        return 1;
    }
    return 0;
}

int cmd_compile(const std::vector<std::string>& args) {
    if (args.empty()) return usage();

    std::string path = args.front();
    std::string output = path + ".luac";
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-o" && i + 1 < args.size()) output = args[++i];
    }

    std::string source;
    std::string error;
    if (!read_file(path, source, error)) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }

    std::string bytecode;
    if (!saintnova::LuauVm::compile_to_bytecode(source, bytecode, error)) {
        std::cerr << "error: " << error << '\n';
        return 1;
    }

    std::ofstream out(output, std::ios::binary);
    if (!out) {
        std::cerr << "error: cannot write " << output << '\n';
        return 1;
    }
    out.write(bytecode.data(), static_cast<std::streamsize>(bytecode.size()));
    std::cout << "wrote " << bytecode.size() << " bytes to " << output << '\n';
    return 0;
}

int cmd_backends() {
    auto core = saintnova::make_default_core();
    print_backends(core->backends());
    return 0;
}

int cmd_repl() {
    saintnova::HarnessBackend backend;
    std::cout << "SaintNova harness REPL. Type 'exit' to leave.\n";
    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (line == "exit" || line == "quit") break;
        if (line.empty()) continue;

        std::string source = line;
        if (line[0] == '=') source = "print(" + line.substr(1) + ")";

        saintnova::ExecutionRequest request;
        request.id = "repl";
        request.script = std::move(source);
        request.chunk_name = "=repl";
        request.timeout_ms = 5000;

        const auto result = backend.execute(request, print_line, nullptr);
        if (!result.ok) std::cerr << "error: " << result.error << '\n';
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) return usage();

    const std::string command = args.front();
    args.erase(args.begin());

    if (command == "serve") return cmd_serve(args);
    if (command == "run") return cmd_run(args);
    if (command == "compile") return cmd_compile(args);
    if (command == "backends") return cmd_backends();
    if (command == "repl") return cmd_repl();
    if (command == "version" || command == "--version") {
        std::cout << "SaintNova " SAINTNOVA_VERSION " (protocol " << saintnova::kProtocolVersion << ")\n";
        return 0;
    }
    return usage();
}
