#include "rendercheck/run.h"
#include "rendercheck/checks.h"
#include "rendercheck/config.h"
#include "rendercheck/doctor.h"
#include "rendercheck/run_performance.h"
#include "rendercheck/visual.h"

#include <cerrno>
#include <chrono>
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

struct RuntimeEnvironment {
    bool display_present = false;
    bool xvfb_available = false;
    bool use_xvfb = false;
    bool software_renderer = false;
    bool auto_headless_disabled = false;
    std::string renderer_mode = "driver_default";
    std::string headless_backend = "none";
    std::string error;
};

struct RunResult {
    int exit_code = 1;
    int signal = 0;
    bool timed_out = false;
    double milliseconds = 0.0;
};

bool env_has_value(const char* name) {
    const char* value = std::getenv(name);
    return value && *value;
}

bool env_truthy(const char* name) {
    const char* value = std::getenv(name);
    if (!value) return false;
    const std::string_view v(value);
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

bool env_falsey(const char* name) {
    const char* value = std::getenv(name);
    if (!value) return false;
    const std::string_view v(value);
    return v == "0" || v == "false" || v == "no" || v == "off";
}

bool executable_in_path(const char* name) {
    const char* raw_path = std::getenv("PATH");
    if (!raw_path) return false;
    std::stringstream stream(raw_path);
    std::string dir;
    while (std::getline(stream, dir, ':')) {
        if (dir.empty()) dir = ".";
        const fs::path candidate = fs::path(dir) / name;
#if defined(__APPLE__) || defined(__linux__)
        if (::access(candidate.c_str(), X_OK) == 0) return true;
#else
        if (fs::exists(candidate)) return true;
#endif
    }
    return false;
}

HeadlessMode resolved_headless_mode(const Config& config) {
    const char* env = std::getenv("RENDERCHECK_HEADLESS_MODE");
    if (env && *env) {
        const std::string_view value(env);
        if (value == "xvfb") return HeadlessMode::Xvfb;
        if (value == "none") return HeadlessMode::None;
        if (value == "auto") return HeadlessMode::Auto;
    }
    if (env_falsey("RENDERCHECK_HEADLESS_AUTO")) return HeadlessMode::None;
    return config.project.headless;
}

RendererMode resolved_renderer_mode(const Config& config) {
    const char* env = std::getenv("RENDERCHECK_RENDERER_MODE");
    if (env && *env) {
        const std::string_view value(env);
        if (value == "software") return RendererMode::Software;
        if (value == "hardware") return RendererMode::Hardware;
        if (value == "auto") return RendererMode::Auto;
    }
    return config.project.renderer;
}

RuntimeEnvironment detect_runtime_environment(const Config& config) {
    RuntimeEnvironment runtime;
    const HeadlessMode headless = resolved_headless_mode(config);
    const RendererMode renderer = resolved_renderer_mode(config);
    runtime.software_renderer = renderer == RendererMode::Software || env_truthy("LIBGL_ALWAYS_SOFTWARE") ||
                                env_truthy("RENDERCHECK_SOFTWARE_RENDERER");
    runtime.renderer_mode = runtime.software_renderer ? "software" : (renderer == RendererMode::Hardware ? "hardware" : "driver_default");

#if defined(__linux__)
    runtime.display_present = env_has_value("DISPLAY") || env_has_value("WAYLAND_DISPLAY");
    runtime.xvfb_available = executable_in_path("xvfb-run") && executable_in_path("Xvfb");
    runtime.auto_headless_disabled = headless == HeadlessMode::None;

    if (headless == HeadlessMode::Xvfb) {
        if (!runtime.xvfb_available) runtime.error = "Xvfb requested but xvfb-run/Xvfb were not found";
        else runtime.use_xvfb = true;
    } else if (headless == HeadlessMode::Auto && !runtime.display_present && runtime.xvfb_available) {
        runtime.use_xvfb = true;
    }

    if (runtime.use_xvfb) {
        runtime.software_renderer = true;
        runtime.renderer_mode = "software";
        runtime.headless_backend = "xvfb";
    } else if (!runtime.display_present && headless == HeadlessMode::None) {
        runtime.headless_backend = "none";
    } else if (!runtime.display_present) {
        runtime.headless_backend = "direct";
    }
#else
    if (headless == HeadlessMode::Xvfb) runtime.error = "Xvfb headless mode is only supported on Linux";
#endif
    return runtime;
}

#if defined(__APPLE__) || defined(__linux__)
void set_nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

bool drain_fd(int fd,
              std::ostream& terminal,
              std::ofstream& log,
              std::ofstream* validation_copy = nullptr) {
    char buffer[4096];
    bool read_any = false;
    for (;;) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            read_any = true;
            terminal.write(buffer, n);
            terminal.flush();
            if (log) { log.write(buffer, n); log.flush(); }
            if (validation_copy && *validation_copy) { validation_copy->write(buffer, n); validation_copy->flush(); }
            continue;
        }
        if (n == 0) return read_any;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return read_any;
        return read_any;
    }
}
#endif

RunResult run_process(const std::string& command,
                      const fs::path& cwd,
                      std::string_view test_name,
                      const fs::path& output_dir,
                      const VisualConfig& visual,
                      const ValidationConfig& validation,
                      const RuntimeEnvironment& runtime,
                      double timeout_ms) {
    RunResult result;
    const auto started = std::chrono::steady_clock::now();

#if defined(__APPLE__) || defined(__linux__)
    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        std::perror("renderercheck: pipe");
        if (out_pipe[0] >= 0) { close(out_pipe[0]); close(out_pipe[1]); }
        if (err_pipe[0] >= 0) { close(err_pipe[0]); close(err_pipe[1]); }
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        std::perror("renderercheck: fork");
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

        const std::string test(test_name);
        const std::string output = fs::absolute(output_dir).string();
        ::setenv("RENDERCHECK", "1", 1);
        ::setenv("RENDERCHECK_TEST", test.c_str(), 1);
        ::setenv("RENDERCHECK_OUTPUT_DIR", output.c_str(), 1);

        if (runtime.use_xvfb) {
            ::setenv("RENDERCHECK_HEADLESS", "1", 1);
            ::setenv("RENDERCHECK_HEADLESS_BACKEND", "xvfb", 1);
            ::setenv("RENDERCHECK_SOFTWARE_RENDERER", "1", 1);
            ::setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
        } else if (runtime.software_renderer) {
            ::setenv("RENDERCHECK_SOFTWARE_RENDERER", "1", 1);
            ::setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
        } else {
            ::unsetenv("RENDERCHECK_SOFTWARE_RENDERER");
        }

        const std::uint64_t frame_limit = visual.capture ? static_cast<std::uint64_t>(visual.warmup_frames) + 1U : 1U;
        const std::string frame_limit_text = std::to_string(frame_limit);
        ::setenv("RENDERCHECK_FRAME_LIMIT", frame_limit_text.c_str(), 1);

        if (visual.capture) {
            const std::string capture = fs::absolute(rendercheck::capture_path(test_name)).string();
            const std::string capture_frame = std::to_string(visual.warmup_frames);
            ::setenv("RENDERCHECK_CAPTURE_PATH", capture.c_str(), 1);
            ::setenv("RENDERCHECK_CAPTURE_FORMAT", "ppm-rgb8", 1);
            ::setenv("RENDERCHECK_CAPTURE_FRAME", capture_frame.c_str(), 1);
        } else {
            ::unsetenv("RENDERCHECK_CAPTURE_PATH");
            ::unsetenv("RENDERCHECK_CAPTURE_FORMAT");
            ::unsetenv("RENDERCHECK_CAPTURE_FRAME");
        }

        if (!prepare_child_checks(test_name, output_dir, validation)) _exit(125);
        if (::chdir(cwd.c_str()) != 0) { std::perror("renderercheck: chdir"); _exit(126); }

#if defined(__linux__)
        if (runtime.use_xvfb) {
            execlp("xvfb-run", "xvfb-run", "-a", "/bin/sh", "-c", command.c_str(), static_cast<char*>(nullptr));
            std::perror("renderercheck: xvfb-run");
            _exit(127);
        }
#endif
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        std::perror("renderercheck: exec");
        _exit(127);
    }

    close(out_pipe[1]);
    close(err_pipe[1]);
    (void)setpgid(pid, pid);
    set_nonblocking(out_pipe[0]);
    set_nonblocking(err_pipe[0]);

    std::ofstream stdout_log(rendercheck::stdout_path(test_name), std::ios::binary | std::ios::trunc);
    std::ofstream stderr_log(rendercheck::stderr_path(test_name), std::ios::binary | std::ios::trunc);
    std::ofstream validation_log;
    if (validation.vulkan) validation_log.open(rendercheck::validation_path(test_name), std::ios::binary | std::ios::trunc);

    int status = 0;
    bool exited = false;
    bool term_sent = false;
    auto term_at = started;

    for (;;) {
        drain_fd(out_pipe[0], std::cout, stdout_log);
        drain_fd(err_pipe[0], std::cerr, stderr_log, validation.vulkan ? &validation_log : nullptr);

        const pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) { exited = true; break; }
        if (waited < 0 && errno != EINTR) { std::perror("renderercheck: waitpid"); break; }

        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double, std::milli>(now - started).count();
        if (!term_sent && timeout_ms > 0.0 && elapsed > timeout_ms) {
            result.timed_out = true;
            term_sent = true;
            term_at = now;
            if (kill(-pid, SIGTERM) != 0 && errno != ESRCH) std::perror("renderercheck: SIGTERM");
        } else if (term_sent && std::chrono::duration<double, std::milli>(now - term_at).count() > 250.0) {
            if (kill(-pid, SIGKILL) != 0 && errno != ESRCH) std::perror("renderercheck: SIGKILL");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
        const bool a = drain_fd(out_pipe[0], std::cout, stdout_log);
        const bool b = drain_fd(err_pipe[0], std::cerr, stderr_log, validation.vulkan ? &validation_log : nullptr);
        if (!a && !b) break;
    }
    close(out_pipe[0]);
    close(err_pipe[0]);

    if (result.timed_out) result.exit_code = 124;
    else if (exited && WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
    else if (exited && WIFSIGNALED(status)) {
        result.signal = WTERMSIG(status);
        result.exit_code = 128 + result.signal;
    }
#else
    (void)command; (void)cwd; (void)test_name; (void)output_dir; (void)visual; (void)validation; (void)runtime; (void)timeout_ms;
    std::cerr << "  [fail] process execution is unsupported on this platform\n";
#endif

    const auto finished = std::chrono::steady_clock::now();
    result.milliseconds = std::chrono::duration<double, std::milli>(finished - started).count();
    return result;
}

std::string build_command(const Config& config, const TestConfig* test) {
    std::string command = test && !test->command.empty() ? test->command : config.project.command;
    if (test && !test->args.empty()) { command += ' '; command += test->args; }
    return command;
}

fs::path working_directory(const Config& config, const TestConfig* test) {
    if (test && !test->cwd.empty()) return test->cwd;
    return config.project.cwd;
}

void add_failure(TestReport& report, const std::string& failure) {
    report.failures.push_back(failure);
}

void print_run_performance(const RunPerformanceResult& result,
                           double regression_percent)
{
    std::cout << "  run performance:\n";
    for (const auto& comparison : result.comparisons)
    {
        std::cout << std::fixed << std::setprecision(3)
                  << "    " << comparison.name
                  << " median " << comparison.current_median << " ms"
                  << ", p95 " << comparison.current_p95 << " ms";

        if (comparison.baseline_missing)
        {
            std::cout << "  [missing baseline]\n";
            continue;
        }

        std::cout << " | baseline " << comparison.baseline_median
                  << " / " << comparison.baseline_p95 << " ms"
                  << " | delta " << std::showpos << comparison.median_delta_percent
                  << "% / " << comparison.p95_delta_percent << "%" << std::noshowpos
                  << (comparison.regressed ? "  [regression]" : "  [ok]")
                  << '\n';
    }

    if (!result.passed)
    {
        std::cout << "  [fail] run performance baseline (allowed +"
                  << std::fixed << std::setprecision(3)
                  << regression_percent << "%)\n";
    }
    else
    {
        std::cout << "  [ok] run performance baseline\n";
    }
}

} // namespace

int run_tests(std::string_view filter) {
    Config config;
    std::string error;
    if (!load_config("rendercheck.toml", config, error)) {
        std::cerr << "renderercheck: " << error << '\n';
        return 2;
    }

    struct SelectedTest { std::string name; const TestConfig* test; };
    std::vector<SelectedTest> selected;
    if (config.tests.empty()) {
        const std::string name = config.project.name.empty() ? "project" : config.project.name;
        if (filter.empty() || filter == name) selected.push_back({name, nullptr});
    } else {
        for (const auto& test : config.tests) {
            if (!test.enabled) continue;
            if (!filter.empty() && filter != test.name) continue;
            selected.push_back({test.name, &test});
        }
    }
    if (selected.empty()) {
        if (!filter.empty()) std::cerr << "renderercheck: no enabled test named '" << filter << "'\n";
        else std::cerr << "renderercheck: no enabled tests\n";
        return 2;
    }

    std::error_code ec;
    fs::create_directories(".rendercheck", ec);
    fs::remove(".rendercheck/report.md", ec);
    ec.clear();
    fs::remove(".rendercheck/results.json", ec);

    if (config.validation.vulkan) {
        std::string detail;
        if (!vulkan_validation_available(detail)) {
            std::cerr << "RendererCheck run\n\n[fail] Vulkan validation unavailable: " << detail << '\n';
            std::vector<TestReport> reports;
            for (const auto& item : selected) {
                TestReport report;
                report.name = item.name;
                report.validation_checked = true;
                report.validation_available = false;
                add_failure(report, "Vulkan validation unavailable: " + detail);
                reports.push_back(report);
            }
            write_reports(reports, 0, reports.size());
            return 1;
        }
    }

    const RuntimeEnvironment runtime = detect_runtime_environment(config);

    std::cout << "RendererCheck run\n\n";
#if defined(__linux__)
    if (runtime.use_xvfb) std::cout << "headless: automatic Xvfb + Mesa software renderer\n\n";
    else if (!runtime.display_present && runtime.auto_headless_disabled) std::cout << "headless: fallback disabled; running command directly\n\n";
    else if (!runtime.display_present && !runtime.xvfb_available) std::cout << "headless: no display and Xvfb tooling not found; running command directly\n\n";
    else if (runtime.software_renderer) std::cout << "renderer: software mode\n\n";
#endif

    std::size_t passed = 0, failed = 0;
    std::vector<TestReport> reports;

    for (const auto& selected_test : selected) {
        TestReport report;
        report.name = selected_test.name;
        report.renderer_mode = runtime.renderer_mode;
        report.headless_backend = runtime.headless_backend;
        report.stdout_path = stdout_path(selected_test.name).string();
        report.stderr_path = stderr_path(selected_test.name).string();
        bool test_passed = true;

        const fs::path output_dir = capture_output_dir(selected_test.name);
        fs::remove_all(output_dir, ec);
        ec.clear();
        fs::create_directories(output_dir, ec);
        if (ec) {
            std::cerr << selected_test.name << "\n  [fail] could not create " << output_dir.string() << "\n\n";
            add_failure(report, "could not create output directory");
            reports.push_back(report);
            ++failed;
            continue;
        }

        if (!runtime.error.empty()) {
            std::cout << selected_test.name << "\n  [fail] " << runtime.error << "\n\n";
            add_failure(report, runtime.error);
            reports.push_back(report);
            ++failed;
            continue;
        }

        const VisualConfig visual = visual_config(config, selected_test.test);
        const PerformanceConfig performance = performance_config(config, selected_test.test);
        const double timeout_ms = timeout_config(config, selected_test.test);
        const std::string command = build_command(config, selected_test.test);
        const fs::path cwd = working_directory(config, selected_test.test);
        std::cout << selected_test.name << "\n  command: " << command << '\n';
        if (visual.capture && visual.warmup_frames != 0) std::cout << "  capture frame: " << visual.warmup_frames << " after warmup\n";
        if (timeout_ms > 0.0) std::cout << std::fixed << std::setprecision(0) << "  timeout: " << timeout_ms << " ms\n";

        const RunResult run = run_process(command, cwd, selected_test.name, output_dir, visual, config.validation, runtime, timeout_ms);
        report.process_ms = run.milliseconds;
        report.exit_code = run.exit_code;
        report.signal = run.signal;
        report.timed_out = run.timed_out;
        if (run.timed_out) {
            std::cout << "  [fail] process timed out after " << static_cast<long long>(run.milliseconds) << " ms\n";
            add_failure(report, "process timed out");
            test_passed = false;
        } else if (run.exit_code == 0) {
            std::cout << "  [ok] process (" << static_cast<long long>(run.milliseconds) << " ms)\n";
        } else {
            std::cout << "  [fail] process exited " << run.exit_code << " (" << static_cast<long long>(run.milliseconds) << " ms)\n";
            add_failure(report, "process exited " + std::to_string(run.exit_code));
            test_passed = false;
        }

        if (config.validation.vulkan) {
            report.validation_checked = true;
            const ValidationResult validation = analyze_validation(selected_test.name, config.validation);
            report.validation_errors = validation.errors;
            report.validation_warnings = validation.warnings;
            report.validation_vuids = validation.vuids;
            std::cout << "  validation: " << validation.errors << " errors, " << validation.warnings << " warnings, " << validation.vuids << " VUIDs\n";
            if (!validation.passed) {
                std::cout << "  [fail] Vulkan validation policy\n";
                add_failure(report, "Vulkan validation policy failed");
                test_passed = false;
            } else if (validation.warnings != 0) std::cout << "  [warn] Vulkan validation warnings allowed\n";
            else std::cout << "  [ok] Vulkan validation\n";
        }

        const PerformanceResult perf = analyze_performance(selected_test.name, run.milliseconds, performance, runtime.software_renderer);
        report.gpu_samples = perf.gpu_samples;
        report.gpu_average_ms = perf.gpu_average_ms;
        report.gpu_max_ms = perf.gpu_max_ms;
        report.metrics = perf.metrics;
        if (performance.max_process_ms > 0.0) {
            std::cout << std::fixed << std::setprecision(3) << "  process budget: " << run.milliseconds << " / " << performance.max_process_ms << " ms\n";
        }
        if (perf.gpu_samples != 0) {
            std::cout << std::fixed << std::setprecision(3)
                      << (runtime.software_renderer ? "  render (software): avg " : "  gpu: avg ")
                      << perf.gpu_average_ms << " ms, max " << perf.gpu_max_ms << " ms (" << perf.gpu_samples << " samples)\n";
        }
        if (runtime.software_renderer && performance.max_gpu_ms > 0.0) std::cout << "  [info] GPU budget skipped because this run uses a software renderer\n";
        if (perf.gpu_missing) { std::cout << "  [fail] GPU timing required but renderer reported no gpu_ms samples\n"; add_failure(report, "GPU timing sample missing"); }
        if (perf.process_over_budget) { std::cout << "  [fail] process time exceeded budget\n"; add_failure(report, "process time exceeded budget"); }
        if (perf.gpu_over_budget) { std::cout << "  [fail] GPU time exceeded budget of " << performance.max_gpu_ms << " ms\n"; add_failure(report, "GPU time exceeded budget"); }
        if (!perf.passed) test_passed = false;

        if (visual.capture)
        {
            const bool valid_process = run.exit_code == 0 && !run.timed_out;
            std::string run_performance_error;
            if (!save_run_performance_latest(
                    selected_test.name,
                    run.milliseconds,
                    perf.metrics,
                    valid_process,
                    run_performance_error))
            {
                std::cout << "  [fail] " << run_performance_error << '\n';
                add_failure(report, run_performance_error);
                test_passed = false;
            }
            else if (valid_process)
            {
                const RunPerformanceResult run_performance = evaluate_run_performance(
                        selected_test.name,
                        run.milliseconds,
                        perf.metrics,
                        performance.regression_percent
                    );
                report.run_performance_checked = true;
                report.run_performance_baseline_missing = run_performance.baseline_missing;
                report.run_performance = run_performance.comparisons;
                print_run_performance(run_performance, performance.regression_percent);

                for (const auto& failure : run_performance.failures)
                {
                    add_failure(report, failure);
                }

                if (run_performance.baseline_missing)
                {
                    std::cout << "  approve: renderercheck approve " << selected_test.name << '\n';
                }

                if (!run_performance.passed)
                {
                    test_passed = false;
                }
            }
        }

        if (run.exit_code == 0 && !run.timed_out && visual.capture) {
            report.visual_checked = true;
            VisualResult visual_result;
            if (!evaluate_capture(config, selected_test.name, selected_test.test, visual_result, error)) {
                report.actual_path = visual_result.actual_path.string();
                report.baseline_path = visual_result.baseline_path.string();
                report.diff_png_path = visual_result.diff_png_path.string();
                report.actual_png_path = visual_result.actual_png_path.string();
                report.baseline_png_path = visual_result.baseline_png_path.string();
                if (visual_result.baseline_missing) {
                    std::cout << "  [fail] " << error << '\n'
                              << "  actual: " << visual_result.actual_path.string() << '\n'
                              << "  preview: " << visual_result.actual_png_path.string() << '\n'
                              << "  approve: renderercheck approve " << selected_test.name << '\n';
                } else std::cout << "  [fail] image: " << error << '\n';
                add_failure(report, error);
                test_passed = false;
            } else {
                report.capture_width = visual_result.width;
                report.capture_height = visual_result.height;
                report.changed_pixels = visual_result.changed_pixels;
                report.total_pixels = visual_result.total_pixels;
                report.changed_percent = visual_result.changed_percent;
                report.rmse = visual_result.rmse;
                report.max_channel_delta = visual_result.max_channel_delta;
                report.actual_path = visual_result.actual_path.string();
                report.baseline_path = visual_result.baseline_path.string();
                report.diff_path = visual_result.diff_path.string();
                report.actual_png_path = visual_result.actual_png_path.string();
                report.baseline_png_path = visual_result.baseline_png_path.string();
                report.diff_png_path = visual_result.diff_png_path.string();
                std::cout << std::fixed << std::setprecision(3)
                          << "  capture: " << visual_result.width << 'x' << visual_result.height << " PPM\n"
                          << "  changed: " << visual_result.changed_percent << "% (" << visual_result.changed_pixels << '/' << visual_result.total_pixels << " pixels)\n"
                          << "  rmse: " << visual_result.rmse << '\n';
                if (!visual_result.passed) {
                    std::cout << "  [fail] visual regression\n  diff: " << visual_result.diff_png_path.string() << '\n';
                    add_failure(report, "visual regression");
                    test_passed = false;
                } else std::cout << "  [ok] image matches baseline\n";
            }
        }

        report.passed = test_passed;
        if (test_passed) ++passed; else ++failed;
        reports.push_back(report);
        std::cout << '\n';
    }

    write_reports(reports, passed, failed);
    std::cout << "Summary: " << passed << " passed, " << failed << " failed\nReport: .rendercheck/report.md\n";
    return failed == 0 ? 0 : 1;
}

} // namespace rendercheck
