from __future__ import annotations

import csv
import ctypes
from ctypes import wintypes
import html
import os
import pathlib
import platform
import shutil
import statistics
import subprocess
import sys
from datetime import datetime, timezone

try:
    import resource
except ImportError:
    resource = None

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = pathlib.Path(os.environ.get("BUILD_DIR", ROOT / "build"))
RESULTS_ROOT = pathlib.Path(os.environ.get("RESULTS_DIR", ROOT / "results"))
RUN_TIMESTAMP = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S-%f")
DEFAULT_OUTPUT = RESULTS_ROOT / f"benchmark-{RUN_TIMESTAMP}" / "benchmark.csv"
OUTPUT = pathlib.Path(os.environ.get("BENCHMARK_OUTPUT", DEFAULT_OUTPUT))
SKIP_BUILD = os.environ.get("SKIP_BUILD", "0") == "1"
SAMPLES = int(os.environ.get("RUNS", "1"))
WARMUPS = int(os.environ.get("WARMUPS", "0"))
BUILD_TYPE = os.environ.get("BUILD_TYPE", "Release")
DATASET = pathlib.Path(
    os.environ.get("ITCH_DATASET", ROOT / "data" / "07302019.NASDAQ_ITCH50.gz")
)
ITCH_SYMBOL = os.environ.get("ITCH_SYMBOL", "ALL").strip()
ITCH_RUNS = int(os.environ.get("ITCH_RUNS", "3"))
ITCH_LATENCY_RUNS = int(os.environ.get("ITCH_LATENCY_RUNS", "1"))
SKIP_TESTS = os.environ.get("SKIP_TESTS", "0") == "1"


def _tool_path(name: str) -> str:
    configured = shutil.which(name)
    if configured:
        return configured
    if os.name == "nt":
        for candidate in (
            pathlib.Path(r"C:\mingw64\bin") / f"{name}.exe",
            pathlib.Path(r"C:\Program Files\CMake\bin") / f"{name}.exe",
        ):
            if candidate.exists():
                return str(candidate)
    return name


def _executable(name: str) -> pathlib.Path:
    executable_name = f"{name}.exe" if os.name == "nt" else name
    candidates = [BUILD / executable_name, BUILD / BUILD_TYPE / executable_name]
    executable = next((path for path in candidates if path.exists()), None)
    if executable is None:
        expected = ", ".join(str(path) for path in candidates)
        raise FileNotFoundError(f"executable not found; checked: {expected}")
    return executable


def _key_values(output: str) -> dict[str, str]:
    values = {}
    for line in output.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    return values


def _unix_child_metrics(before: object, after: object) -> dict[str, int | float | str]:
    if resource is None:
        return {}
    before_usage = before
    after_usage = after
    return {
        "cpu_user_seconds": after_usage.ru_utime - before_usage.ru_utime,
        "cpu_system_seconds": after_usage.ru_stime - before_usage.ru_stime,
        "peak_rss_kib": after_usage.ru_maxrss,
        "minor_page_faults": after_usage.ru_minflt - before_usage.ru_minflt,
        "major_page_faults": after_usage.ru_majflt - before_usage.ru_majflt,
        "voluntary_context_switches": after_usage.ru_nvcsw - before_usage.ru_nvcsw,
        "involuntary_context_switches": after_usage.ru_nivcsw - before_usage.ru_nivcsw,
        "filesystem_inputs": after_usage.ru_inblock - before_usage.ru_inblock,
        "filesystem_outputs": after_usage.ru_oublock - before_usage.ru_oublock,
        "signals_received": after_usage.ru_nsignals - before_usage.ru_nsignals,
        "swaps": after_usage.ru_nswap - before_usage.ru_nswap,
        "cache_references": "unavailable_without_hardware_counters",
        "cache_misses": "unavailable_without_hardware_counters",
        "cpu_cycles": "unavailable_without_hardware_counters",
    }


def _windows_process_metrics(process: subprocess.Popen[str]) -> dict[str, int | float | str]:
    if os.name != "nt":
        return {}

    class FileTime(ctypes.Structure):
        _fields_ = [("low", wintypes.DWORD), ("high", wintypes.DWORD)]

    class MemoryCounters(ctypes.Structure):
        _fields_ = [
            ("cb", wintypes.DWORD),
            ("page_fault_count", wintypes.DWORD),
            ("peak_working_set_size", ctypes.c_size_t),
            ("working_set_size", ctypes.c_size_t),
            ("quota_peak_paged_pool_usage", ctypes.c_size_t),
            ("quota_paged_pool_usage", ctypes.c_size_t),
            ("quota_peak_non_paged_pool_usage", ctypes.c_size_t),
            ("quota_non_paged_pool_usage", ctypes.c_size_t),
            ("pagefile_usage", ctypes.c_size_t),
            ("peak_pagefile_usage", ctypes.c_size_t),
        ]

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    creation = FileTime()
    exit_time = FileTime()
    kernel = FileTime()
    user = FileTime()
    memory = MemoryCounters(ctypes.sizeof(MemoryCounters))
    get_times = kernel32.GetProcessTimes
    get_times.argtypes = [wintypes.HANDLE] * 5
    get_times.restype = wintypes.BOOL
    get_memory = psapi.GetProcessMemoryInfo
    get_memory.argtypes = [wintypes.HANDLE, ctypes.POINTER(MemoryCounters), wintypes.DWORD]
    get_memory.restype = wintypes.BOOL

    def filetime_seconds(value: FileTime) -> float:
        ticks = (value.high << 32) | value.low
        return ticks / 10_000_000

    metrics: dict[str, int | float | str] = {
        "voluntary_context_switches": "unavailable",
        "involuntary_context_switches": "unavailable",
        "filesystem_inputs": "unavailable",
        "filesystem_outputs": "unavailable",
        "signals_received": "unavailable",
        "swaps": "unavailable",
        "cache_references": "unavailable_without_hardware_counters",
        "cache_misses": "unavailable_without_hardware_counters",
        "cpu_cycles": "unavailable_without_hardware_counters",
    }
    if get_times(process._handle, ctypes.byref(creation), ctypes.byref(exit_time), ctypes.byref(kernel), ctypes.byref(user)):
        metrics["cpu_user_seconds"] = filetime_seconds(user)
        metrics["cpu_system_seconds"] = filetime_seconds(kernel)
    if get_memory(process._handle, ctypes.byref(memory), ctypes.sizeof(memory)):
        metrics["peak_rss_kib"] = memory.peak_working_set_size // 1024
        metrics["total_page_faults"] = memory.page_fault_count
    metrics["minor_page_faults"] = "unavailable"
    return metrics


def _run_sample(executable: pathlib.Path) -> dict[str, str]:
    before = resource.getrusage(resource.RUSAGE_CHILDREN) if resource else None
    process = subprocess.Popen([str(executable)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    stdout, stderr = process.communicate()
    if process.returncode:
        raise subprocess.CalledProcessError(process.returncode, process.args, stdout, stderr)
    values = dict(line.split("=", 1) for line in stdout.splitlines())
    if resource:
        values.update({key: str(value) for key, value in _unix_child_metrics(before, resource.getrusage(resource.RUSAGE_CHILDREN)).items()})
    else:
        values.update({key: str(value) for key, value in _windows_process_metrics(process).items()})
    return values


def _command_output(command: list[str], fallback: str = "unavailable") -> str:
    try:
        completed = subprocess.run(command, check=True, capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError):
        return fallback
    return completed.stdout.strip().splitlines()[0] if completed.stdout.strip() else fallback


def _build_and_test() -> None:
    cmake = _tool_path("cmake")
    ctest = _tool_path("ctest")
    BUILD.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [cmake, "-S", str(ROOT), "-B", str(BUILD), f"-DCMAKE_BUILD_TYPE={BUILD_TYPE}"],
        check=True,
    )
    subprocess.run([cmake, "--build", str(BUILD), "--config", BUILD_TYPE], check=True)
    if not SKIP_TESTS:
        subprocess.run(
            [ctest, "--test-dir", str(BUILD), "--output-on-failure", "-C", BUILD_TYPE],
            check=True,
        )


def _run_itch_once(latency: bool, run_number: int, run_kind: str) -> dict[str, str]:
    if not DATASET.exists():
        raise FileNotFoundError(f"ITCH dataset not found: {DATASET}")
    command = [str(_executable("itch_benchmark")), str(DATASET)]
    if ITCH_SYMBOL and ITCH_SYMBOL.upper() not in {"ALL", "*"}:
        command.append(ITCH_SYMBOL)
    if latency:
        command.append("--latency")
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=None,
        text=True,
    )
    stdout, _ = process.communicate()
    if process.returncode:
        raise subprocess.CalledProcessError(process.returncode, command, stdout)
    values = _key_values(stdout)
    values["report_scope"] = ITCH_SYMBOL.upper() if ITCH_SYMBOL else "ALL"
    values["replay_run_number"] = str(run_number)
    values["replay_run_kind"] = run_kind
    values["run_timestamp_utc"] = datetime.now(timezone.utc).isoformat()
    values["build_type"] = _cmake_build_type()
    print(
        f"itch_replay_completed kind={run_kind} "
        f"run={run_number} messages={values.get('messages', 'unavailable')}",
        flush=True,
    )
    return values


def _percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return 0.0
    rank = max(1, int((fraction * len(ordered)) + 0.999999999))
    index = min(len(ordered) - 1, rank - 1)
    return ordered[index]


def _add_run_statistics(values: dict[str, str], key: str, runs: list[dict[str, str]]) -> None:
    numbers = [float(run[key]) for run in runs if key in run]
    if not numbers:
        return
    values[f"{key}_min"] = str(min(numbers))
    values[f"{key}_max"] = str(max(numbers))
    values[f"{key}_stdev"] = str(statistics.stdev(numbers)) if len(numbers) > 1 else "unavailable"


def _run_itch_benchmark() -> dict[str, str]:
    if ITCH_RUNS < 1 or ITCH_LATENCY_RUNS < 0:
        raise ValueError("ITCH_RUNS must be positive and ITCH_LATENCY_RUNS must be non-negative")
    throughput_runs = [
        _run_itch_once(False, run_number, "throughput")
        for run_number in range(1, ITCH_RUNS + 1)
    ]
    latency_runs = [
        _run_itch_once(True, run_number, "latency")
        for run_number in range(1, ITCH_LATENCY_RUNS + 1)
    ]
    values = dict(throughput_runs[0])
    values["throughput_run_count"] = str(len(throughput_runs))
    values["latency_run_count"] = str(len(latency_runs))
    for key in ("elapsed_ns", "messages_per_second"):
        numbers = [float(run[key]) for run in throughput_runs if key in run]
        if numbers:
            values[f"{key}_median"] = str(statistics.median(numbers))
            values[f"{key}_p95"] = str(_percentile(numbers, 0.95))
            _add_run_statistics(values, key, throughput_runs)
    for key in ("latency_avg_ns", "latency_p50_ns", "latency_p95_ns", "latency_p99_ns", "latency_p999_ns", "latency_max_ns"):
        numbers = [float(run[key]) for run in latency_runs if key in run]
        if numbers:
            values[f"{key}_median"] = str(statistics.median(numbers))
            values[f"{key}_p95"] = str(_percentile(numbers, 0.95))
            _add_run_statistics(values, key, latency_runs)
    return values


def _write_key_value_csv(output: pathlib.Path, values: dict[str, str]) -> None:
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["metric", "value"])
        writer.writerows(values.items())


def _write_bar_chart(output: pathlib.Path, title: str, values: dict[str, int], color: str) -> None:
    width, height = 1100, 620
    left, right, top, bottom = 80, 35, 65, 110
    plot_width = width - left - right
    plot_height = height - top - bottom
    maximum = max(values.values(), default=1)
    bar_width = plot_width / max(len(values), 1) * 0.72
    gap = plot_width / max(len(values), 1)
    bars = []
    labels = []
    for index, (label, value) in enumerate(values.items()):
        x = left + index * gap + (gap - bar_width) / 2
        bar_height = value / maximum * plot_height
        y = top + plot_height - bar_height
        bars.append(
            f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_width:.1f}" height="{bar_height:.1f}" fill="{color}">'
            f'<title>{html.escape(label)}: {value}</title></rect>'
        )
        labels.append(
            f'<text x="{x + bar_width / 2:.1f}" y="{height - bottom + 25}" text-anchor="middle" '
            f'font-size="13" fill="#52606d">{html.escape(label)}</text>'
        )
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
<rect width="100%" height="100%" fill="#ffffff"/>
<text x="{left}" y="35" font-family="Segoe UI, sans-serif" font-size="22" font-weight="600" fill="#182230">{html.escape(title)}</text>
<line x1="{left}" y1="{top}" x2="{left}" y2="{height - bottom}" stroke="#52606d"/>
<line x1="{left}" y1="{height - bottom}" x2="{width - right}" y2="{height - bottom}" stroke="#52606d"/>
{''.join(bars)}
{''.join(labels)}
</svg>
'''
    output.write_text(svg, encoding="utf-8")


def _cmake_build_type() -> str:
    cache = BUILD / "CMakeCache.txt"
    if not cache.exists():
        return BUILD_TYPE
    prefix = "CMAKE_BUILD_TYPE:STRING="
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(prefix):
            return line.removeprefix(prefix) or BUILD_TYPE
    return BUILD_TYPE


def _cpu_model() -> str:
    if sys.platform.startswith("win"):
        return platform.processor() or "unavailable"
    try:
        for line in pathlib.Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
            if line.lower().startswith("model name"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or "unavailable"


def _metadata(executable: pathlib.Path) -> dict[str, str]:
    compiler_name = os.environ.get("CXX", "c++")
    compiler_path = shutil.which(compiler_name) or compiler_name
    affinity_count = getattr(os, "sched_getaffinity", lambda _: set(range(os.cpu_count() or 0)))(0)
    return {
        "run_timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "git_commit": _command_output(["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"]),
        "git_worktree": "dirty" if _command_output(["git", "-C", str(ROOT), "status", "--porcelain"], "") else "clean",
        "os": platform.platform(),
        "architecture": platform.machine(),
        "cpu_model": _cpu_model(),
        "logical_cpu_count": str(os.cpu_count() or "unavailable"),
        "affinity_cpu_count": str(len(affinity_count)),
        "python_version": sys.version.split()[0],
        "cmake_version": _command_output([_tool_path("cmake"), "--version"]).removeprefix("cmake version "),
        "compiler": _command_output([compiler_path, "--version"]),
        "build_type": _cmake_build_type(),
        "executable": str(executable),
        "warmup_runs": str(WARMUPS),
        "sample_count": str(SAMPLES),
        "simulator_seed": "42",
    }


def _numeric_series(samples: list[dict[str, str]], key: str) -> list[tuple[int, float]]:
    series = []
    for sample in samples:
        try:
            series.append((int(sample["sample_index"]), float(sample[key])))
        except (KeyError, ValueError):
            return []
    return series


def _write_svg_chart(
    output: pathlib.Path,
    title: str,
    y_label: str,
    series: list[tuple[int, float]],
    color: str,
) -> None:
    width, height = 960, 540
    left, right, top, bottom = 90, 35, 65, 75
    plot_width = width - left - right
    plot_height = height - top - bottom
    values = [value for _, value in series]
    low = min(values)
    high = max(values)
    if low == high:
        padding = max(abs(low) * 0.05, 1.0)
        low -= padding
        high += padding
    else:
        padding = (high - low) * 0.08
        low -= padding
        high += padding

    def x_position(index: int) -> float:
        if len(series) == 1:
            return left + plot_width / 2
        return left + (index - 1) * plot_width / (len(series) - 1)

    def y_position(value: float) -> float:
        return top + (high - value) * plot_height / (high - low)

    points = " ".join(f"{x_position(index):.1f},{y_position(value):.1f}" for index, value in series)
    grid = []
    for tick in range(5):
        value = low + (high - low) * tick / 4
        y = y_position(value)
        grid.append(
            f'<line x1="{left}" y1="{y:.1f}" x2="{width - right}" y2="{y:.1f}" '
            'stroke="#d9dee7" stroke-width="1"/>'
            f'<text x="{left - 12}" y="{y + 5:.1f}" text-anchor="end" '
            f'font-size="13" fill="#52606d">{value:.3g}</text>'
        )
    markers = []
    for index, value in series:
        x, y = x_position(index), y_position(value)
        markers.append(
            f'<circle cx="{x:.1f}" cy="{y:.1f}" r="5" fill="{color}">'
            f'<title>sample {index}: {value:.6g}</title></circle>'
        )
    x_label = "Sample index"
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
<rect width="100%" height="100%" fill="#ffffff"/>
<text x="{left}" y="35" font-family="Segoe UI, sans-serif" font-size="22" font-weight="600" fill="#182230">{html.escape(title)}</text>
{''.join(grid)}
<line x1="{left}" y1="{top}" x2="{left}" y2="{height - bottom}" stroke="#52606d"/>
<line x1="{left}" y1="{height - bottom}" x2="{width - right}" y2="{height - bottom}" stroke="#52606d"/>
<polyline points="{points}" fill="none" stroke="{color}" stroke-width="3" stroke-linejoin="round" stroke-linecap="round"/>
{''.join(markers)}
<text x="{width / 2:.1f}" y="{height - 18}" text-anchor="middle" font-family="Segoe UI, sans-serif" font-size="14" fill="#52606d">{x_label}</text>
<text x="20" y="{height / 2:.1f}" text-anchor="middle" transform="rotate(-90 20 {height / 2:.1f})" font-family="Segoe UI, sans-serif" font-size="14" fill="#52606d">{html.escape(y_label)}</text>
</svg>
'''
    output.write_text(svg, encoding="utf-8")


def _write_graphs(samples: list[dict[str, str]]) -> list[pathlib.Path]:
    charts = [
        ("latency.svg", "Order latency", "Average order latency (ns)", "avg_order_latency_ns", "#c2410c"),
        ("throughput.svg", "Order throughput", "Orders per second", "throughput_orders_per_second", "#0369a1"),
        ("cpu_time.svg", "CPU time", "User CPU time (seconds)", "cpu_user_seconds", "#15803d"),
        ("peak_memory.svg", "Peak memory", "Peak RSS (KiB)", "peak_rss_kib", "#7c3aed"),
    ]
    outputs = []
    for filename, title, y_label, key, color in charts:
        series = _numeric_series(samples, key)
        if not series:
            continue
        output = OUTPUT.parent / filename
        _write_svg_chart(output, title, y_label, series, color)
        outputs.append(output)

    dashboard = OUTPUT.parent / "benchmark_dashboard.html"
    links = "\n".join(f'<p><a href="{path.name}"><img src="{path.name}" alt="{path.stem}"></a></p>' for path in outputs)
    dashboard.write_text(
        '<!doctype html><meta charset="utf-8"><title>Benchmark charts</title>'
        '<style>body{font-family:Segoe UI,sans-serif;max-width:1000px;margin:2rem auto}'
        'img{max-width:100%;height:auto;border:1px solid #d9dee7}</style>' + links,
        encoding="utf-8",
    )
    outputs.append(dashboard)
    return outputs


def _write_dataset_outputs(metrics: dict[str, str]) -> list[pathlib.Path]:
    csv_output = OUTPUT.parent / "itch_metrics.csv"
    _write_key_value_csv(csv_output, metrics)
    message_keys = (
        "system_messages",
        "stock_directory_messages",
        "add_messages",
        "cancel_messages",
        "execute_messages",
        "delete_messages",
        "replace_messages",
        "trade_messages",
        "unknown_messages",
    )
    message_counts = {}
    for key in message_keys:
        try:
            message_counts[key.removesuffix("_messages")] = int(metrics[key])
        except (KeyError, ValueError):
            pass
    chart_output = OUTPUT.parent / "itch_message_types.svg"
    _write_bar_chart(chart_output, "ITCH message types", message_counts, "#0369a1")
    dashboard = OUTPUT.parent / "itch_dashboard.html"
    dashboard.write_text(
        '<!doctype html><meta charset="utf-8"><title>ITCH replay metrics</title>'
        '<style>body{font-family:Segoe UI,sans-serif;max-width:1100px;margin:2rem auto}'
        'img{max-width:100%;height:auto;border:1px solid #d9dee7}</style>'
        f'<p><a href="{csv_output.name}">Download metrics CSV</a></p>'
        f'<p><img src="{chart_output.name}" alt="ITCH message types"></p>',
        encoding="utf-8",
    )
    return [csv_output, chart_output, dashboard]


def run() -> None:
    if SAMPLES < 1 or WARMUPS < 0:
        raise ValueError("RUNS must be positive and WARMUPS must be non-negative")
    if not SKIP_BUILD:
        _build_and_test()
    elif not SKIP_TESTS:
        subprocess.run(
            [_tool_path("ctest"), "--test-dir", str(BUILD), "--output-on-failure", "-C", BUILD_TYPE],
            check=True,
        )

    executable = _executable("trading_demo")
    dataset_metrics = _run_itch_benchmark()
    metadata = _metadata(executable)
    for _ in range(WARMUPS):
        _run_sample(executable)

    samples = []
    for sample_index in range(1, SAMPLES + 1):
        values = _run_sample(executable)
        values.update(metadata)
        values["sample_index"] = str(sample_index)
        samples.append(values)

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    with OUTPUT.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(samples[0]))
        writer.writeheader()
        writer.writerows(samples)
    graph_outputs = _write_graphs(samples)
    dataset_outputs = _write_dataset_outputs(dataset_metrics)

    elapsed_times = [int(sample["elapsed_ns"]) for sample in samples]
    print(f"wrote {OUTPUT}")
    for output in [*graph_outputs, *dataset_outputs]:
        print(f"wrote {output}")
    print(f"itch_messages={dataset_metrics.get('messages', 'unavailable')}")
    print(f"itch_messages_per_second={dataset_metrics.get('messages_per_second', 'unavailable')}")
    print(f"itch_throughput_runs={dataset_metrics.get('throughput_run_count', 'unavailable')}")
    print(f"itch_latency_runs={dataset_metrics.get('latency_run_count', 'unavailable')}")
    print(f"itch_latency_p50_ns={dataset_metrics.get('latency_p50_ns_median', 'unavailable')}")
    print(f"itch_latency_p95_ns={dataset_metrics.get('latency_p95_ns_median', 'unavailable')}")
    print(f"itch_latency_p99_ns={dataset_metrics.get('latency_p99_ns_median', 'unavailable')}")
    print(f"itch_dataset_truncated={dataset_metrics.get('dataset_truncated', 'unavailable')}")
    print(f"samples={SAMPLES}")
    print(
        "cpp_elapsed_ns "
        f"median={statistics.median(elapsed_times):.0f} "
        f"p95={_percentile(elapsed_times, 0.95):.0f} "
        f"min={min(elapsed_times):.0f} "
        f"max={max(elapsed_times):.0f} "
        f"stdev={statistics.stdev(elapsed_times) if len(elapsed_times) > 1 else 'unavailable'}"
    )
    throughputs = [float(sample["throughput_orders_per_second"]) for sample in samples]
    print(
        "throughput_orders_per_second "
        f"median={statistics.median(throughputs):.2f} "
        f"p95={_percentile(throughputs, 0.95):.2f} "
        f"min={min(throughputs):.2f} "
        f"max={max(throughputs):.2f} "
        f"stdev={statistics.stdev(throughputs) if len(throughputs) > 1 else 'unavailable'}"
    )
    for metric in (
        "cpu_user_seconds",
        "cpu_system_seconds",
        "peak_rss_kib",
        "minor_page_faults",
        "major_page_faults",
        "total_page_faults",
        "voluntary_context_switches",
        "involuntary_context_switches",
        "filesystem_inputs",
        "filesystem_outputs",
        "signals_received",
        "swaps",
    ):
        values = []
        for sample in samples:
            try:
                values.append(float(sample[metric]))
            except (KeyError, ValueError):
                values = []
                break
        if values:
            percentile_index = max(0, int(len(values) * 0.95) - 1)
            print(
                f"{metric} "
                f"median={statistics.median(values):.2f} "
                f"p95={sorted(values)[percentile_index]:.2f}"
            )


if __name__ == "__main__":
    run()
