#include "rendercheck/run.h"
#include "rendercheck/checks.h"
#include "rendercheck/config.h"
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
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
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
};

struct RunResult {
    int exit_code = 1;
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

RuntimeEnvironment detect_runtime_environment() {
    RuntimeEnvironment runtime;
    runtime.software_renderer = env_truthy("LIBGL_ALWAYS_SOFTWARE") ||
                                env_truthy("RENDERCHECK_SOFTWARE_RENDERER");

#if defined(__linux__)
    runtime.display_present = env_has_value("DISPLAY") || env_has_value("WAYLAND_DISPLAY");
    runtime.xvfb_available = executable_in_path("xvfb-run") && executable_in_path("Xvfb");
    runtime.auto_headless_disabled = env_falsey("RENDERCHECK_HEADLESS_AUTO");

    if (!runtime.display_present && !runtime.auto_headless_disabled && runtime.xvfb_available) {
        runtime.use_xvfb = true;
        runtime.software_renderer = true;
    }
#endif

    return runtime;
}

RunResult run_process(const std::string& command,
                      const fs::path& cwd,
                      std::string_view test_name,
                      const fs::path& output_dir,
                      bool capture_enabled,
                      const ValidationConfig& validation,
                      const RuntimeEnvironment& runtime) {
    RunResult result;
    const auto started = std::chrono::steady_clock::now();

#if defined(__APPLE__) || defined(__linux__)
    const pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "  [fail] could not fork renderer process\n";
        return result;
    }

    if (pid == 0) {
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
        }

        if (capture_enabled) {
            const std::string capture = fs::absolute(rendercheck::capture_path(test_name)).string();
            ::setenv("RENDERCHECK_CAPTURE_PATH", capture.c_str(), 1);
            ::setenv("RENDERCHECK_CAPTURE_FORMAT", "ppm-rgb8", 1);
        } else {
            ::unsetenv("RENDERCHECK_CAPTURE_PATH");
            ::unsetenv("RENDERCHECK_CAPTURE_FORMAT");
        }

        if (!prepare_child_checks(test_name, output_dir, validation)) _exit(125);

        if (::chdir(cwd.c_str()) != 0) {
            std::perror("rendercheck: chdir");
            _exit(126);
        }

#if defined(__linux__)
        if (runtime.use_xvfb) {
            execlp("xvfb-run", "xvfb-run", "-a", "/bin/sh", "-c", command.c_str(), static_cast<char*>(nullptr));
            std::perror("rendercheck: xvfb-run");
            _exit(127);
        }
#endif

        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        std::perror("rendercheck: exec");
        _exit(127);
    }

    int status = 0;
    for (;;) {
        const pid_t waited = waitpid(pid, &status, 0);
        if (waited == pid) break;
        if (waited < 0 && errno == EINTR) continue;
        std::perror("rendercheck: waitpid");
        return result;
    }

    if (WIFEXITED(status)) result.exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) result.exit_code = 128 + WTERMSIG(status);
#else
    (void)command;
    (void)cwd;
    (void)test_name;
    (void)output_dir;
    (void)capture_enabled;
    (void)validation;
    (void)runtime;
    std::cerr << "  [fail] process execution is unsupported on this platform\n";
#endif

    const auto finished = std::chrono::steady_clock::now();
    result.milliseconds = std::chrono::duration<double, std::milli>(finished - started).count();
    return result;
}

std::string build_command(const Config& config, const TestConfig* test) {
    std::string command = test && !test->command.empty() ? test->command : config.project.command;
    if (test && !test->args.empty()) {
        command += ' ';
        command += test->args;
    }
    return command;
}

fs::path working_directory(const Config& config, const TestConfig* test) {
    if (test && !test->cwd.empty()) return test->cwd;
    return config.project.cwd;
}

} // namespace

int run_tests(std::string_view filter) {
    Config config;
    std::string error;
    if (!load_config("rendercheck.toml", config, error)) {
        std::cerr << "rendercheck: " << error << '\n';
        return 2;
    }

    struct SelectedTest {
        std::string name;
        const TestConfig* test;
    };

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
        if (!filter.empty()) std::cerr << "rendercheck: no enabled test named '" << filter << "'\n";
        else std::cerr << "rendercheck: no enabled tests\n";
        return 2;
    }

    const RuntimeEnvironment runtime = detect_runtime_environment();

    std::cout << "RendererCheck run\n\n";
#if defined(__linux__)
    if (runtime.use_xvfb) {
        std::cout << "headless: automatic Xvfb + Mesa software renderer\n\n";
    } else if (!runtime.display_present && runtime.auto_headless_disabled) {
        std::cout << "headless: automatic fallback disabled by RENDERCHECK_HEADLESS_AUTO\n\n";
    } else if (!runtime.display_present && !runtime.xvfb_available) {
        std::cout << "headless: no display and Xvfb tooling not found; running command directly\n\n";
    } else if (runtime.software_renderer) {
        std::cout << "renderer: software mode detected\n\n";
    }
#endif

    std::size_t passed = 0;
    std::size_t failed = 0;
    std::vector<TestReport> reports;

    for (const auto& selected_test : selected) {
        TestReport report;
        report.name = selected_test.name;
        bool test_passed = true;

        const fs::path output_dir = capture_output_dir(selected_test.name);
        std::error_code ec;
        fs::create_directories(output_dir, ec);
        if (ec) {
            std::cerr << selected_test.name << "\n  [fail] could not create " << output_dir.string() << "\n\n";
            report.detail = "could not create output directory";
            reports.push_back(report);
            ++failed;
            continue;
        }

        const VisualConfig& visual = visual_config(config, selected_test.test);
        const PerformanceConfig performance = performance_config(config, selected_test.test);
        fs::remove(metrics_path(selected_test.name), ec);
        ec.clear();
        fs::remove(validation_path(selected_test.name), ec);
        ec.clear();
        fs::remove(output_dir / "software-renderer", ec);
        if (runtime.software_renderer) {
            std::ofstream marker(output_dir / "software-renderer");
            if (marker) marker << (runtime.use_xvfb ? "xvfb\n" : "environment\n");
        }
        if (visual.capture) {
            ec.clear();
            fs::remove(capture_path(selected_test.name), ec);
            ec.clear();
            fs::remove(output_dir / "diff.ppm", ec);
        }

        const std::string command = build_command(config, selected_test.test);
        const fs::path cwd = working_directory(config, selected_test.test);
        std::cout << selected_test.name << '\n' << "  command: " << command << '\n';

        const RunResult run = run_process(command, cwd, selected_test.name, output_dir, visual.capture, config.validation, runtime);
        report.process_ms = run.milliseconds;
        if (run.exit_code == 0) {
            std::cout << "  [ok] process (" << static_cast<long long>(run.milliseconds) << " ms)\n";
        } else {
            std::cout << "  [fail] process exited " << run.exit_code << " (" << static_cast<long long>(run.milliseconds) << " ms)\n";
            report.detail = "process exited " + std::to_string(run.exit_code);
            test_passed = false;
        }

        if (config.validation.vulkan) {
            report.validation_checked = true;
            const ValidationResult validation = analyze_validation(selected_test.name, config.validation);
            report.validation_errors = validation.errors;
            report.validation_warnings = validation.warnings;
            report.validation_vuids = validation.vuids;
            std::cout << "  validation: " << validation.errors << " errors, " << validation.warnings
                      << " warnings, " << validation.vuids << " VUIDs\n";
            if (!validation.passed) {
                std::cout << "  [fail] Vulkan validation policy\n";
                if (report.detail.empty()) report.detail = "Vulkan validation policy failed";
                test_passed = false;
            } else if (validation.warnings != 0) {
                std::cout << "  [warn] Vulkan validation warnings allowed\n";
            } else {
                std::cout << "  [ok] Vulkan validation\n";
            }
        }

        const PerformanceResult perf = analyze_performance(selected_test.name, run.milliseconds, performance);
        report.gpu_samples = perf.gpu_samples;
        report.gpu_average_ms = perf.gpu_average_ms;
        report.gpu_max_ms = perf.gpu_max_ms;
        if (performance.max_process_ms > 0.0) {
            std::cout << std::fixed << std::setprecision(3)
                      << "  process budget: " << run.milliseconds << " / " << performance.max_process_ms << " ms\n";
        }
        if (perf.gpu_samples != 0) {
            std::cout << std::fixed << std::setprecision(3);
            if (runtime.software_renderer) {
                std::cout << "  render (software): avg " << perf.gpu_average_ms << " ms, max " << perf.gpu_max_ms
                          << " ms (" << perf.gpu_samples << " samples)\n";
            } else {
                std::cout << "  gpu: avg " << perf.gpu_average_ms << " ms, max " << perf.gpu_max_ms
                          << " ms (" << perf.gpu_samples << " samples)\n";
            }
        }
        if (runtime.software_renderer && performance.max_gpu_ms > 0.0) {
            std::cout << "  [info] GPU budget skipped because this run uses a software renderer\n";
        }
        if (perf.gpu_missing) {
            std::cout << "  [fail] GPU timing required but renderer reported no gpu_ms samples\n";
            if (report.detail.empty()) report.detail = "GPU timing sample missing";
        }
        if (perf.process_over_budget) {
            std::cout << "  [fail] process time exceeded budget\n";
            if (report.detail.empty()) report.detail = "process time exceeded budget";
        }
        if (perf.gpu_over_budget) {
            std::cout << "  [fail] GPU time exceeded budget of " << performance.max_gpu_ms << " ms\n";
            if (report.detail.empty()) report.detail = "GPU time exceeded budget";
        }
        if (!perf.passed) test_passed = false;

        if (run.exit_code == 0 && visual.capture) {
            report.visual_checked = true;
            VisualResult visual_result;
            if (!evaluate_capture(config, selected_test.name, selected_test.test, visual_result, error)) {
                if (visual_result.baseline_missing) {
                    std::cout << "  [fail] " << error << '\n'
                              << "  actual: " << visual_result.actual_path.string() << '\n'
                              << "  approve: rendercheck approve " << selected_test.name << '\n';
                } else {
                    std::cout << "  [fail] image: " << error << '\n';
                }
                if (report.detail.empty()) report.detail = error;
                test_passed = false;
            } else {
                report.changed_percent = visual_result.changed_percent;
                std::cout << std::fixed << std::setprecision(3)
                          << "  capture: " << visual_result.width << 'x' << visual_result.height << " PPM\n"
                          << "  changed: " << visual_result.changed_percent << "% ("
                          << visual_result.changed_pixels << '/' << visual_result.total_pixels << " pixels)\n"
                          << "  rmse: " << visual_result.rmse << '\n';
                if (!visual_result.passed) {
                    std::cout << "  [fail] visual regression\n" << "  diff: " << visual_result.diff_path.string() << '\n';
                    if (report.detail.empty()) report.detail = "visual regression";
                    test_passed = false;
                } else {
                    std::cout << "  [ok] image matches baseline\n";
                }
            }
        }

        report.passed = test_passed;
        if (test_passed) ++passed;
        else ++failed;
        reports.push_back(report);
        std::cout << '\n';
    }

    write_reports(reports, passed, failed);
    std::cout << "Summary: " << passed << " passed, " << failed << " failed\n"
              << "Report: .rendercheck/report.md\n";
    return failed == 0 ? 0 : 1;
}

} // namespace rendercheck
