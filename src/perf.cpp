#include "rendercheck/perf.h"

#include "rendercheck/checks.h"
#include "rendercheck/config.h"
#include "rendercheck/version.h"
#include "rendercheck/visual.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace rendercheck {
namespace {

struct ResolvedPerfCase {
    std::string name;
    std::string command;
    fs::path cwd;
    double warmup_ms = 0.0;
    double sample_ms = 0.0;
    double regression_percent = 0.0;
    std::uint32_t min_samples = 1;
    double timeout_ms = 0.0;
};

struct ChildResult {
    int exit_code = 1;
    int signal = 0;
    bool timed_out = false;
    bool duration_terminated = false;
    double process_ms = 0.0;
};

struct PerfCaseResult {
    std::string name;
    bool passed = false;
    int exit_code = 0;
    int signal = 0;
    bool timed_out = false;
    bool duration_terminated = false;
    double process_ms = 0.0;
    double regression_percent = 0.0;
    std::uint32_t min_samples = 0;
    std::vector<MetricSummary> metrics;
    std::vector<std::string> failures;
};

bool env_truthy(const char* name) {
    const char* value = std::getenv(name);
    if (!value) return false;
    const std::string_view v(value);
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

bool contains_software_override(std::string_view env) {
    return env.find("LIBGL_ALWAYS_SOFTWARE=1") != std::string_view::npos ||
           env.find("RENDERCHECK_SOFTWARE_RENDERER=1") != std::string_view::npos;
}

std::string json_escape(std::string_view value) {
    std::string out;
    for (const unsigned char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    std::ostringstream hex;
                    hex << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c);
                    out += hex.str();
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

void json_string(std::ostream& out, std::string_view value) {
    out << '"' << json_escape(value) << '"';
}

std::string ms_env_value(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << value;
    return out.str();
}

ResolvedPerfCase resolve_case(const Config& config, const PerfCaseConfig& perf_case) {
    ResolvedPerfCase resolved;
    resolved.name = perf_case.name;
    resolved.command = perf_case.command.empty() ? config.project.command : perf_case.command;
    if (!perf_case.args.empty()) {
        resolved.command += ' ';
        resolved.command += perf_case.args;
    }
    if (!perf_case.env.empty()) {
        resolved.command = perf_case.env + ' ' + resolved.command;
    }
    resolved.cwd = perf_case.cwd.empty() ? config.project.cwd : perf_case.cwd;
    resolved.warmup_ms = perf_case.has_warmup_ms ? perf_case.warmup_ms : config.performance.warmup_ms;
    resolved.sample_ms = perf_case.has_sample_ms ? perf_case.sample_ms : config.performance.sample_ms;
    resolved.regression_percent = perf_case.has_regression_percent
        ? perf_case.regression_percent
        : config.performance.regression_percent;
    resolved.min_samples = perf_case.has_min_samples ? perf_case.min_samples : config.performance.min_samples;

    const double requested_duration = resolved.warmup_ms + resolved.sample_ms;
    const double default_timeout = std::max(config.project.timeout_ms, requested_duration + 2000.0);
    resolved.timeout_ms = perf_case.has_timeout_ms ? perf_case.timeout_ms : default_timeout;
    if (resolved.timeout_ms <= 0.0) resolved.timeout_ms = requested_duration + 2000.0;
    return resolved;
}

fs::path perf_root() {
    return fs::path(".rendercheck") / "performance";
}

fs::path case_output_dir(std::string_view name) {
    return perf_root() / "runs" / safe_test_name(name);
}

#if defined(__APPLE__) || defined(__linux__)
void set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

bool drain_fd(int fd, std::ofstream& log) {
    char buffer[4096];
    bool read_any = false;
    for (;;) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            read_any = true;
            if (log) {
                log.write(buffer, n);
                log.flush();
            }
            continue;
        }
        if (n == 0) return read_any;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return read_any;
        return read_any;
    }
}
#endif

ChildResult run_case_process(const ResolvedPerfCase& perf_case, const fs::path& output_dir) {
    ChildResult result;
    const auto started = std::chrono::steady_clock::now();

#if defined(__APPLE__) || defined(__linux__)
    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        std::perror("renderercheck perf: pipe");
        if (out_pipe[0] >= 0) { close(out_pipe[0]); close(out_pipe[1]); }
        if (err_pipe[0] >= 0) { close(err_pipe[0]); close(err_pipe[1]); }
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        std::perror("renderercheck perf: fork");
        close(out_pipe[0]); close(out_pipe[1]); close(err_pipe[0]); close(err_pipe[1]);
        return result;
    }

    if (pid == 0) {
        (void)setpgid(0, 0);
        close(out_pipe[0]);
        close(err_pipe[0]);
        if (dup2(out_pipe[1], STDOUT_FILENO) < 0 || dup2(err_pipe[1], STDERR_FILENO) < 0) _exit(125);
        close(out_pipe[1]);
        close(err_pipe[1]);

        const std::string metrics = fs::absolute(output_dir / "metrics.txt").string();
        const std::string warmup = ms_env_value(perf_case.warmup_ms);
        const std::string duration = ms_env_value(perf_case.warmup_ms + perf_case.sample_ms);
        ::setenv("RENDERCHECK", "1", 1);
        ::setenv("RENDERCHECK_PERF", "1", 1);
        ::setenv("RENDERCHECK_PERF_CASE", perf_case.name.c_str(), 1);
        ::setenv("RENDERCHECK_PERF_WARMUP_MS", warmup.c_str(), 1);
        ::setenv("RENDERCHECK_PERF_DURATION_MS", duration.c_str(), 1);
        ::setenv("RENDERCHECK_METRICS_PATH", metrics.c_str(), 1);

        ::unsetenv("RENDERCHECK_TEST");
        ::unsetenv("RENDERCHECK_FRAME_LIMIT");
        ::unsetenv("RENDERCHECK_CAPTURE_PATH");
        ::unsetenv("RENDERCHECK_CAPTURE_FORMAT");
        ::unsetenv("RENDERCHECK_CAPTURE_FRAME");

        if (::chdir(perf_case.cwd.c_str()) != 0) {
            std::perror("renderercheck perf: chdir");
            _exit(126);
        }

        execl("/bin/sh", "sh", "-c", perf_case.command.c_str(), static_cast<char*>(nullptr));
        std::perror("renderercheck perf: exec");
        _exit(127);
    }

    close(out_pipe[1]);
    close(err_pipe[1]);
    (void)setpgid(pid, pid);
    set_nonblocking(out_pipe[0]);
    set_nonblocking(err_pipe[0]);

    std::ofstream stdout_log(output_dir / "stdout.log", std::ios::binary | std::ios::trunc);
    std::ofstream stderr_log(output_dir / "stderr.log", std::ios::binary | std::ios::trunc);

    int status = 0;
    bool exited = false;
    bool duration_term_sent = false;
    bool kill_sent = false;
    auto duration_term_at = started;
    const double requested_duration = perf_case.warmup_ms + perf_case.sample_ms;

    for (;;) {
        drain_fd(out_pipe[0], stdout_log);
        drain_fd(err_pipe[0], stderr_log);

        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) { exited = true; break; }
        if (waited < 0 && errno != EINTR) { std::perror("renderercheck perf: waitpid"); break; }

        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double, std::milli>(now - started).count();

        if (!duration_term_sent && requested_duration > 0.0 && elapsed > requested_duration + 250.0) {
            duration_term_sent = true;
            result.duration_terminated = true;
            duration_term_at = now;
            if (kill(-pid, SIGTERM) != 0 && errno != ESRCH) std::perror("renderercheck perf: SIGTERM");
        } else if (duration_term_sent && !kill_sent &&
                   std::chrono::duration<double, std::milli>(now - duration_term_at).count() > 250.0) {
            kill_sent = true;
            if (kill(-pid, SIGKILL) != 0 && errno != ESRCH) std::perror("renderercheck perf: SIGKILL");
        }

        if (perf_case.timeout_ms > 0.0 && elapsed > perf_case.timeout_ms) {
            result.timed_out = true;
            if (kill(-pid, SIGKILL) != 0 && errno != ESRCH) std::perror("renderercheck perf: timeout SIGKILL");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (!exited) {
        for (;;) {
            const pid_t waited = waitpid(pid, &status, 0);
            if (waited == pid) { exited = true; break; }
            if (waited < 0 && errno == EINTR) continue;
            break;
        }
    }

    for (int i = 0; i < 4; ++i) {
        const bool a = drain_fd(out_pipe[0], stdout_log);
        const bool b = drain_fd(err_pipe[0], stderr_log);
        if (!a && !b) break;
    }
    close(out_pipe[0]);
    close(err_pipe[0]);

    if (result.timed_out) {
        result.exit_code = 124;
    } else if (result.duration_terminated) {
        result.exit_code = 0;
    } else if (exited && WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (exited && WIFSIGNALED(status)) {
        result.signal = WTERMSIG(status);
        result.exit_code = 128 + result.signal;
    }
#else
    (void)perf_case;
    (void)output_dir;
    std::cerr << "renderercheck perf: process execution is unsupported on this platform\n";
#endif

    const auto finished = std::chrono::steady_clock::now();
    result.process_ms = std::chrono::duration<double, std::milli>(finished - started).count();
    return result;
}

bool metric_is_timing(const MetricSummary& metric) {
    return metric.name.size() >= 3u && metric.name.ends_with("_ms");
}

void write_perf_reports(const std::vector<PerfCaseResult>& results, bool approve) {
    std::error_code ec;
    fs::create_directories(perf_root(), ec);
    if (ec) return;

    std::size_t passed = 0;
    for (const auto& result : results) if (result.passed) ++passed;
    const std::size_t failed = results.size() - passed;

    {
        std::ofstream out(perf_root() / "report.md", std::ios::trunc);
        if (out) {
            out << "# RendererCheck performance report\n\n"
                << "RendererCheck " << RENDERCHECK_VERSION << " — **"
                << passed << " passed, " << failed << " failed**\n\n";
            if (approve) out << "> Approval requested; baseline persistence is handled by the baseline stage.\n\n";
            for (const auto& result : results) {
                out << "## " << result.name << " — " << (result.passed ? "PASS" : "FAIL") << "\n\n"
                    << "| Metric | Samples | Min | Average | Median | p95 | Max |\n"
                    << "|---|---:|---:|---:|---:|---:|---:|\n";
                for (const auto& metric : result.metrics) {
                    out << "| " << metric.name << " | " << metric.samples
                        << " | " << std::fixed << std::setprecision(3) << metric.minimum
                        << " | " << metric.average
                        << " | " << metric.median
                        << " | " << metric.p95
                        << " | " << metric.maximum << " |\n";
                }
                if (!result.failures.empty()) {
                    out << "\nFailures:\n";
                    for (const auto& failure : result.failures) out << "- " << failure << '\n';
                }
                out << '\n';
            }
        }
    }

    {
        std::ofstream out(perf_root() / "results.json", std::ios::trunc);
        if (out) {
            out << "{\n  \"schema_version\": 1,\n  \"renderercheck_version\": ";
            json_string(out, RENDERCHECK_VERSION);
            out << ",\n  \"approved\": " << (approve ? "true" : "false")
                << ",\n  \"passed\": " << passed
                << ",\n  \"failed\": " << failed
                << ",\n  \"cases\": [\n";
            for (std::size_t i = 0; i < results.size(); ++i) {
                const auto& result = results[i];
                out << "    {\n      \"name\": "; json_string(out, result.name);
                out << ",\n      \"passed\": " << (result.passed ? "true" : "false")
                    << ",\n      \"exit_code\": " << result.exit_code
                    << ",\n      \"signal\": " << result.signal
                    << ",\n      \"timed_out\": " << (result.timed_out ? "true" : "false")
                    << ",\n      \"duration_terminated\": " << (result.duration_terminated ? "true" : "false")
                    << ",\n      \"process_ms\": " << std::fixed << std::setprecision(3) << result.process_ms
                    << ",\n      \"metrics\": [\n";
                for (std::size_t m = 0; m < result.metrics.size(); ++m) {
                    const auto& metric = result.metrics[m];
                    out << "        {\"name\": "; json_string(out, metric.name);
                    out << ", \"samples\": " << metric.samples
                        << ", \"minimum\": " << metric.minimum
                        << ", \"average\": " << metric.average
                        << ", \"median\": " << metric.median
                        << ", \"p95\": " << metric.p95
                        << ", \"maximum\": " << metric.maximum << '}';
                    if (m + 1u != result.metrics.size()) out << ',';
                    out << '\n';
                }
                out << "      ],\n      \"failures\": [";
                for (std::size_t f = 0; f < result.failures.size(); ++f) {
                    if (f) out << ',';
                    json_string(out, result.failures[f]);
                }
                out << "]\n    }";
                if (i + 1u != results.size()) out << ',';
                out << '\n';
            }
            out << "  ]\n}\n";
        }
    }
}

} // namespace

int run_perf(std::string_view filter, bool approve) {
    Config config;
    std::string error;
    if (!load_config("rendercheck.toml", config, error)) {
        std::cerr << "renderercheck: " << error << '\n';
        return 2;
    }

    if (config.project.renderer == RendererMode::Software ||
        env_truthy("LIBGL_ALWAYS_SOFTWARE") ||
        env_truthy("RENDERCHECK_SOFTWARE_RENDERER")) {
        std::cerr << "renderercheck perf: software rendering is disabled for performance runs\n";
        return 2;
    }

    std::vector<PerfCaseConfig> fallback;
    const std::vector<PerfCaseConfig>* cases = &config.perf_cases;
    if (cases->empty()) {
        PerfCaseConfig perf_case;
        perf_case.name = config.project.name.empty() ? "project" : config.project.name;
        fallback.push_back(perf_case);
        cases = &fallback;
    }

    std::vector<const PerfCaseConfig*> selected;
    for (const auto& perf_case : *cases) {
        if (!perf_case.enabled) continue;
        if (!filter.empty() && filter != perf_case.name) continue;
        selected.push_back(&perf_case);
    }
    if (selected.empty()) {
        if (!filter.empty()) std::cerr << "renderercheck perf: no enabled case named '" << filter << "'\n";
        else std::cerr << "renderercheck perf: no enabled performance cases\n";
        return 2;
    }

    std::vector<PerfCaseResult> results;
    results.reserve(selected.size());

    std::cout << "RendererCheck performance\n";
    for (const PerfCaseConfig* configured : selected) {
        const ResolvedPerfCase perf_case = resolve_case(config, *configured);
        PerfCaseResult result;
        result.name = perf_case.name;
        result.regression_percent = perf_case.regression_percent;
        result.min_samples = perf_case.min_samples;

        if (contains_software_override(configured->env)) {
            result.failures.push_back("performance case requests a software renderer");
            results.push_back(result);
            std::cout << perf_case.name << "\n  [fail] software-renderer override is not allowed\n\n";
            continue;
        }

        const fs::path output_dir = case_output_dir(perf_case.name);
        std::error_code ec;
        fs::remove_all(output_dir, ec);
        ec.clear();
        fs::create_directories(output_dir, ec);
        if (ec) {
            result.failures.push_back("could not create performance output directory");
            results.push_back(result);
            std::cout << perf_case.name << "\n  [fail] could not create output directory\n\n";
            continue;
        }

        std::cout << perf_case.name << "\n"
                  << "  warmup: " << std::fixed << std::setprecision(0) << perf_case.warmup_ms << " ms\n"
                  << "  sample: " << perf_case.sample_ms << " ms\n";

        const ChildResult child = run_case_process(perf_case, output_dir);
        result.exit_code = child.exit_code;
        result.signal = child.signal;
        result.timed_out = child.timed_out;
        result.duration_terminated = child.duration_terminated;
        result.process_ms = child.process_ms;
        result.metrics = summarize_metrics_file(output_dir / "metrics.txt");

        if (child.timed_out) {
            result.failures.push_back("process timed out");
        } else if (child.exit_code != 0) {
            result.failures.push_back("process exited " + std::to_string(child.exit_code));
        }

        bool timing_metric_found = false;
        for (const auto& metric : result.metrics) {
            if (!metric_is_timing(metric)) continue;
            timing_metric_found = true;
            std::cout << "  " << std::left << std::setw(20) << metric.name << std::right
                      << " median " << std::fixed << std::setprecision(3) << metric.median
                      << " ms  p95 " << metric.p95
                      << " ms  n=" << metric.samples << '\n';
        }
        if (!timing_metric_found) result.failures.push_back("renderer reported no *_ms performance metrics");

        result.passed = result.failures.empty();
        std::cout << (result.passed ? "  [ok] performance samples\n\n" : "  [fail] performance case\n\n");
        results.push_back(std::move(result));
    }

    write_perf_reports(results, approve);

    std::size_t failed = 0;
    for (const auto& result : results) if (!result.passed) ++failed;
    std::cout << "Summary: " << (results.size() - failed) << " passed, " << failed << " failed\n"
              << "Report: .rendercheck/performance/report.md\n";
    return failed == 0 ? 0 : 1;
}

} // namespace rendercheck
