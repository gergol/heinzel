#!/usr/bin/env python3
"""Run Heinzel's Google Benchmark suite and turn its JSON into docs/performance-<arch>.md, or gate a
change against the pinned old tree. This is the one bench tool — it folds in the old tools/bench_gate.py
as the `gate` subcommand.

    tools/bench_report.py run --build build/rw-bench [--core 4] [--filter REGEX] [--bench NAME ...]
                               [--repetitions 7] [--min-time 0.3s] [--force]
    tools/bench_report.py report --build build/rw-bench [--out docs/performance-<arch>.md] [--compare]
                                  [--baseline benchmarks/baseline-<arch>.json]
    tools/bench_report.py record --build build/rw-bench [--baseline ...]
    tools/bench_report.py gate --build build/rw-bench [--main-build build/main-bench] [--core 4] [--all] [--force]
    tools/bench_report.py baseline --from benchmarks/baseline-zen2.json --main-build build/main-bench \
                                   --out benchmarks/baseline-<arch>.json      # another machine's gate, main live
    tools/bench_report.py all --build ...             # run, then report — the one command that
                                                       # produces docs/performance-<arch>.md

Benchmark names follow `<workload>/<arm>[/<size>]` (Google Benchmark's trailing /real_time is
stripped first). `<workload>` may itself contain slashes. `<arm>` is the first path segment (left to
right) whose base — the part before an optional `-<variant>` — is in {heinzel, naive, hand, eigen,
juce, kfr, faust, klang, cmajor, crv, std}; segments before it are the workload, segments after are the size.
`heinzel` is the nest path (the headline arm). An arm whose variant holds `par` (`heinzel-par`) splits
its work among threads: `run`
runs those rows on the physical cores (`--par-cores`) instead of pinned to one, and `gate` never gates
them. A name with no such segment is reported with arm "?" and flagged in the report's warnings.

`run` only ever reads /proc/cpuinfo, .../cpufreq/scaling_governor (on macOS `sysctl`), the build dir
and the repo. It discovers `bench_*` executables under <build>/benchmarks/, runs each pinned with
taskset (repetitions + min-time; on macOS, which has no taskset, unpinned and `--core` ignored, one
process at a time as everywhere), and writes Google Benchmark JSON to <build>/results/<bench>.json plus
a meta.json describing the machine, compiler and build. `report` only reads that JSON (it runs nothing) and
writes the markdown. `record` folds the current results into the "recorded" section of
benchmarks/baseline-<arch>.json (median_ns + gate, keyed by exact benchmark name), preserving
everything else in the file. `gate` is unchanged from the old tools/bench_gate.py: it measures the
baseline's hand-written "workloads" rows live, optionally against a live old-tree build, and exits
0 (pass) / 1 (a gated row regressed) / 77 (skipped: unknown CPU or a loaded box without --force).
"""
import argparse
import json
import math
import os
import re
import statistics
import subprocess
import sys
import tempfile
from datetime import datetime

NS_PER = {"ns": 1.0, "us": 1e3, "ms": 1e6, "s": 1e9}
SKIPPED = 77
VOCAB = {"heinzel", "naive", "hand", "eigen", "juce", "kfr", "faust", "klang", "cmajor", "crv", "std"}
ARCH_TABLE = (("Ryzen 9 3950X", "zen2"), ("Ryzen 9 7950X", "zen4"), ("Apple M1", "m1"))
COUNTER_KEYS = ("GFLOP/s", "bytes_per_second", "items_per_second", "max_rel_err")

# small shared helpers -----------

def physical_cores(cpuinfo_text):
    """The first logical CPU of each physical core (distinct `physical id`, `core id`), as a taskset list."""
    firsts, cpu, package = {}, None, "0"
    for line in cpuinfo_text.splitlines():
        key, _, value = (part.strip() for part in line.partition(":"))
        if key == "processor":
            cpu = int(value)
        elif key == "physical id":
            package = value
        elif key == "core id" and cpu is not None:
            firsts.setdefault((package, value), cpu)
    return ",".join(str(c) for c in sorted(firsts.values())) or "0"

def on_macos():
    return sys.platform == "darwin"

def sysctl(key):
    """`sysctl -n key`, or None where it has no value."""
    try:
        done = subprocess.run(["sysctl", "-n", key], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    except OSError:
        return None
    return done.stdout.strip() if done.returncode == 0 and done.stdout.strip() else None

def default_par_cores():
    if on_macos():
        return None  # no affinity to give: unpinned
    try:
        with open("/proc/cpuinfo") as f:
            return physical_cores(f.read())
    except OSError:
        return "0"

def cpu_model():
    if on_macos():
        return sysctl("machdep.cpu.brand_string") or "unknown"
    with open("/proc/cpuinfo") as f:
        for line in f:
            if line.startswith("model name"):
                return line.split(":", 1)[1].strip()
    return "unknown"

def pinned(core):
    """What a run is prefixed with to keep it on `core` (a number or a taskset list): nothing where it is not to be
    pinned (`None`) or cannot be (macOS has no affinity: a run goes where the scheduler puts it, the performance
    cores at the default quality of service)."""
    return [] if core is None or on_macos() else ["taskset", "-c", str(core)]

def unpinned_on_macos(args):
    """macOS: `--core` and `--par-cores` ignored, said once."""
    if on_macos():
        print("macOS: no taskset — every run is unpinned (one at a time); --core and --par-cores are ignored")
        args.core = None
        if hasattr(args, "par_cores"):
            args.par_cores = None

def arch_key(model):
    for needle, key in ARCH_TABLE:
        if needle in model:
            return key
    return re.sub(r"[^a-z0-9]+", "-", model.lower()).strip("-") or "unknown"

def escape(name):
    return "".join("\\" + c if c in r"\.^$*+?()[]{}|" else c for c in name)

def strip_rt(name):
    return name[: -len("/real_time")] if name.endswith("/real_time") else name

def read_json(path):
    try:
        with open(path) as f:
            text = f.read().strip()
    except OSError:
        return None
    if not text:
        return None
    try:
        return json.loads(text)
    except ValueError:
        return None

def repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def git_sha(root):
    try:
        sha = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root, stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL, text=True, check=True).stdout.strip()
        dirty = subprocess.run(["git", "status", "--porcelain"], cwd=root, stdout=subprocess.PIPE,
                                stderr=subprocess.DEVNULL, text=True, check=True).stdout.strip() != ""
        return sha + ("-dirty" if dirty else "")
    except (OSError, subprocess.SubprocessError):
        return "unknown"

def default_baseline(arch):
    return os.path.join(repo_root(), "benchmarks", f"baseline-{arch}.json")

# naming contract -----------------

def is_parallel(arm):
    """Does this arm split its work among threads (a `par` variant: `heinzel-par`)?"""
    return "par" in arm.split("-")[1:]

def parse_name(name):
    """Split a benchmark name (without /real_time) into (workload, arm, size)."""
    segs = name.split("/")
    for i, seg in enumerate(segs):
        if seg.split("-", 1)[0] in VOCAB:
            return "/".join(segs[:i]), seg, "/".join(segs[i + 1:])
    return name, "?", ""

# `run` ---------------------------

def list_tests(binary):
    done = subprocess.run([binary, "--benchmark_list_tests"], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, text=True)
    if done.returncode != 0:
        return None
    return [strip_rt(line.strip()) for line in done.stdout.splitlines() if line.strip()]

def run_bench(binary, names, core, repetitions, min_time, out_json):
    """One pinned run of exactly `names` (on core `core`: a number, or a taskset list); Google Benchmark's
    JSON lands in `out_json`. Returns what went wrong, or None."""
    pattern = "^(" + "|".join(escape(n) for n in names) + ")(/real_time)?$"
    cmd = pinned(core) + [binary, f"--benchmark_filter={pattern}",
                          f"--benchmark_repetitions={repetitions}", f"--benchmark_min_time={min_time}",
                          f"--benchmark_out={out_json}", "--benchmark_out_format=json"]
    done = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    return None if done.returncode == 0 else f"{binary} failed ({done.returncode}):\n{done.stderr}"

def apply_filter(names, filter_re):
    if not filter_re:
        return names
    pat = re.compile(filter_re)
    return [n for n in names if pat.search(strip_rt(n))]

def cmake_cache_value(build, key):
    try:
        with open(os.path.join(build, "CMakeCache.txt")) as f:
            for line in f:
                if line.startswith(key + ":"):
                    return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return None

def host_values(build):
    """`heinzel::host` as HEINZEL_AUTOTUNE wrote it (<build>/heinzel_tune/include/heinzel/host_machine.hpp): each field
    it sets, `field value` in declaration order, an OpCost as its latency and slots; None without the header."""
    try:
        with open(os.path.join(build, "heinzel_tune", "include", "heinzel", "host_machine.hpp")) as f:
            text = f.read()
    except OSError:
        return None
    return [f"{field} {value.strip('{}').replace(',', '')}"
            for field, value in re.findall(r"^\s+\.(\w+) = (.+),$", text, re.M)]

def compile_flags(build, libraries=False):
    """The flags a bench TU is really compiled with — the `heinzel` target injects some that the cache
    never shows — from Ninja's own file; the cache's CMAKE_CXX_FLAGS under any other generator. With
    `libraries`, a comparison library's file instead (bench_compare_*, not its Heinzel arms' object
    library): those do not link `heinzel` and lack its numeric flags (Plan/23 P7); None when there is none."""
    try:
        with open(os.path.join(build, "build.ninja")) as f:
            in_bench = False
            for line in f:
                if line.startswith("build "):
                    compare = "benchmarks/CMakeFiles/bench_compare_" in line and "_heinzel.dir/" not in line
                    in_bench = compare if libraries else "benchmarks/CMakeFiles/bench_" in line and not compare
                elif in_bench and line.strip().startswith("FLAGS ="):
                    return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return None if libraries else cmake_cache_value(build, "CMAKE_CXX_FLAGS") or ""

def compiler_version(compiler):
    if not compiler:
        return "unknown"
    done = subprocess.run([compiler, "--version"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    return done.stdout.splitlines()[0].strip() if done.returncode == 0 and done.stdout else "unknown"

def read_governor(core):
    if core is None:
        return "n/a (unpinned)"
    try:
        with open(f"/sys/devices/system/cpu/cpu{core}/cpufreq/scaling_governor") as f:
            return f.read().strip()
    except OSError:
        return "unknown"

def run_bench_split(binary, names, core, par_cores, repetitions, min_time, out_json):
    """`run_bench`, the rows of a parallel arm in a second run on `par_cores`, their results appended to the
    same JSON. Returns what went wrong, or None."""
    serial = [n for n in names if not is_parallel(parse_name(strip_rt(n))[1])]
    parallel = [n for n in names if n not in serial]
    failure = run_bench(binary, serial, core, repetitions, min_time, out_json) if serial else None
    if failure or not parallel:
        return failure
    with tempfile.NamedTemporaryFile(suffix=".json") as extra:
        failure = run_bench(binary, parallel, par_cores, repetitions, min_time, extra.name)
        if failure:
            return failure
        merged = read_json(out_json) if serial else None
        added = read_json(extra.name) or {}
        merged = merged or {"context": added.get("context", {}), "benchmarks": []}
        merged["benchmarks"] += added.get("benchmarks", [])
    with open(out_json, "w") as f:
        json.dump(merged, f)
    return None

def write_meta(build, core, repetitions, min_time, binaries, failed, load, loaded, par_cores="0"):
    model = cpu_model()
    compiler = cmake_cache_value(build, "CMAKE_CXX_COMPILER")
    machine = cmake_cache_value(build, "HEINZEL_BENCH_MACHINE") or "zen2"
    meta = {
        "cpu": model,
        "arch": arch_key(model),
        "core": core,
        "par_cores": par_cores,
        "compiler": compiler or "unknown",
        "compiler_version": compiler_version(compiler),
        "cxx_flags": compile_flags(build),
        "library_flags": compile_flags(build, libraries=True),
        "build_type": cmake_cache_value(build, "CMAKE_BUILD_TYPE") or "unknown",
        "machine": machine,
        **({"host": host_values(build)} if machine == "host" else {}),
        "git_sha": git_sha(repo_root()),
        "date": datetime.now().astimezone().isoformat(timespec="seconds"),
        "load_avg": round(load, 2),
        "governor": read_governor(core),
        "repetitions": repetitions,
        "min_time": min_time,
        "loaded": loaded,
        "failed": failed,
        "binaries": binaries,
    }
    results_dir = os.path.join(build, "results")
    os.makedirs(results_dir, exist_ok=True)
    with open(os.path.join(results_dir, "meta.json"), "w") as f:
        json.dump(meta, f, indent=2, sort_keys=True)
        f.write("\n")

def cmd_run(args):
    results_dir = os.path.join(args.build, "results")
    os.makedirs(results_dir, exist_ok=True)
    unpinned_on_macos(args)

    load = os.getloadavg()[0]
    loaded = load > 1.0
    print(f"load average {load:.2f}")
    if loaded and not args.force:
        print("SKIP (load average > 1.0 — a loaded box reports false drops); --force overrides")
        return SKIPPED

    bench_dir = os.path.join(args.build, "benchmarks")
    binaries = []
    if os.path.isdir(bench_dir):
        for fn in sorted(os.listdir(bench_dir)):
            path = os.path.join(bench_dir, fn)
            if fn.startswith("bench_") and os.path.isfile(path) and os.access(path, os.X_OK):
                binaries.append(fn)
    if args.bench:  # --bench NAME restricts which bench_* binaries run, by exact binary name
        binaries = [b for b in binaries if b in args.bench]

    failed = []
    for binary in binaries:
        path = os.path.join(bench_dir, binary)
        listed = list_tests(path)
        if listed is None:
            print(f"{binary}: --benchmark_list_tests failed", file=sys.stderr)
            failed.append(binary)
            continue
        names = apply_filter(listed, args.filter)
        if not names:
            continue
        print(f"running {binary} ({len(names)} rows)...")
        failure = run_bench_split(path, names, args.core, args.par_cores, args.repetitions, args.min_time,
                                  os.path.join(results_dir, f"{binary}.json"))
        if failure:
            print(failure, file=sys.stderr)
            failed.append(binary)

    write_meta(args.build, args.core, args.repetitions, args.min_time, binaries, failed, load, loaded, args.par_cores)
    return 1 if failed else 0

# reading results -----------------

def load_bench_json(path):
    """(names in first-appearance order, {name: {"ns": median-or-None, "error": str-or-None,
    "counters": {key: median}}}) for one Google Benchmark JSON file. Never raises: a missing, empty
    or malformed file simply yields no rows."""
    data = read_json(path)
    order, seen, times, counters, errors = [], set(), {}, {}, {}
    for r in (data or {}).get("benchmarks", []):
        name = strip_rt(r.get("run_name", r.get("name", "")))
        if not name:
            continue
        if name not in seen:
            seen.add(name)
            order.append(name)
        if r.get("error_occurred") or r.get("skipped"):
            errors[name] = r.get("error_message") or r.get("skip_message") or "failed"
            continue
        if r.get("run_type") != "iteration":
            continue
        times.setdefault(name, []).append(r["real_time"] * NS_PER[r["time_unit"]])
        for k in COUNTER_KEYS:
            if k in r:
                counters.setdefault(name, {}).setdefault(k, []).append(r[k])
    results = {n: {"ns": statistics.median(times[n]) if n in times else None,
                   "error": errors.get(n),
                   "counters": {k: statistics.median(v) for k, v in counters.get(n, {}).items()}}
               for n in order}
    return order, results

# markdown formatting --------------

def sigfig(x):
    """x formatted to 3 significant digits, trailing zeros kept (2.798 -> "2.80")."""
    if x == 0:
        return "0.00"
    digits = max(3 - 1 - math.floor(math.log10(abs(x))), 0)
    return f"{x:.{digits}f}"

def format_time(ns):
    if ns is None:
        return "–"
    if ns >= 1e9:
        return f"{sigfig(ns / 1e9)} s"
    if ns >= 1e6:
        return f"{sigfig(ns / 1e6)} ms"
    if ns >= 1e3:
        return f"{sigfig(ns / 1e3)} µs"
    return f"{sigfig(ns)} ns"

def format_rate(value, unit, space_before_unit):
    for prefix, div in (("G", 1e9), ("M", 1e6), ("k", 1e3)):
        if value >= div:
            sep = " " if space_before_unit else ""
            return f"{value / div:.2f} {prefix}{sep}{unit}"
    return f"{value:.2f} {unit}"

def format_throughput(counters):
    if "GFLOP/s" in counters:
        return f"{counters['GFLOP/s']:.1f} GFLOP/s"
    if "bytes_per_second" in counters:
        return format_rate(counters["bytes_per_second"], "B/s", False)
    if "items_per_second" in counters:
        return format_rate(counters["items_per_second"], "items/s", True)
    return "–"

def verdict_for(cur, rec, base_rule):
    if rec is None:
        return "–" if cur is None else "new"
    if cur is None or cur.get("ns") is None:
        return "MISSING"
    if not rec.get("gate", False):
        return "info"
    ratio = cur["ns"] / rec["median_ns"]
    return "PASS" if ratio <= rec.get("factor", base_rule) else "FAIL"

def is_heinzel(arm):
    return arm.split("-", 1)[0] == "heinzel"

def table_layout(order, recorded):
    """Rows (workload, size) and arms in first-appearance order — `heinzel` first, then its variants —
    and the benchmark name behind each cell."""
    rows, arms, cell = [], [], {}
    for name in order:
        w, a, s = parse_name(name)
        if (w, s) not in rows:
            rows.append((w, s))
        if a not in arms:
            arms.append(a)
        cell[(w, s, a)] = name
    for name in sorted(recorded):  # a recorded heinzel row that vanished still shows, as MISSING
        w, a, s = parse_name(name)
        if a == "heinzel" and (w, s) not in rows:
            rows.append((w, s))
    arms.sort(key=lambda a: 0 if a == "heinzel" else 1 if is_heinzel(a) else 2)
    workloads = list(dict.fromkeys(w for w, _ in rows))
    rows.sort(key=lambda row: workloads.index(row[0]))  # a workload's sizes stay together
    return rows, arms, cell

def time_of(results, name):
    info = results.get(name)
    return info["ns"] if info and not info["error"] else None

def numerator(row, cell, results):
    """The benchmark a row's ratios are taken from: its nest arm, if that ran."""
    name = cell.get((*row, "heinzel"))
    return name if time_of(results, name) is not None else None

def time_cell(results, name, failures):
    info = results.get(name)
    if info and info["error"]:
        failures.append((name, info["error"]))
        return "FAILED"
    return format_time(info["ns"] if info else None)

def rivals_of(arms):
    """The arms Heinzel is measured against — none where a bench has no Heinzel arm to measure."""
    return [a for a in arms if not is_heinzel(a)] if any(map(is_heinzel, arms)) else []

PARAM = "heinzel-param"  # the arm whose coefficients are known only at run time, as its rivals' are

def param_rivals(rows, arms, cell, results):
    """The rivals `heinzel-param` is measured against too: those sharing a row with it that both ran."""
    if PARAM not in arms:
        return []
    return [a for a in rivals_of(arms)
            if any(time_of(results, cell.get((*r, PARAM))) and time_of(results, cell.get((*r, a))) for r in rows)]

def render_row(row, arms, cell, results, columns, failures, by_param):
    num = numerator(row, cell, results)
    counters = results[num]["counters"] if num else {}
    out = list(row) + [time_cell(results, cell.get((*row, a)), failures) for a in arms]
    for a in rivals_of(arms):
        t = time_of(results, cell.get((*row, a)))
        out.append(sigfig(results[num]["ns"] / t) if num and t else "–")
    p = time_of(results, cell.get((*row, PARAM)))
    for a in by_param:
        t = time_of(results, cell.get((*row, a)))
        out.append(sigfig(p / t) if p and t else "–")
    if "throughput" in columns:
        out.append(format_throughput(counters))
    if "max rel err" in columns:
        out.append(f"{counters['max_rel_err']:.1e}" if "max_rel_err" in counters else "–")
    return out

def render_bench_section(bench, order, results, compare, recorded, rule):
    """(markdown for one bench binary, whether a gated row failed against the recorded baseline)."""
    rows, arms, cell = table_layout(order, recorded if compare else {})
    counters = [results[n]["counters"] for n in (numerator(r, cell, results) for r in rows) if n]
    by_param = param_rivals(rows, arms, cell, results)
    columns = ["workload", "size"] + arms + [f"÷ {a}" for a in rivals_of(arms)] + [f"{PARAM} ÷ {a}" for a in by_param]
    columns += ["throughput"] if any(set(c) - {"max_rel_err"} for c in counters) else []
    columns += ["max rel err"] if any("max_rel_err" in c for c in counters) else []
    columns += ["verdict"] if compare else []

    body, failures, failing = [], [], False
    for row in rows:
        out = render_row(row, arms, cell, results, columns, failures, by_param)
        if compare:
            name = row[0] + "/heinzel" + (f"/{row[1]}" if row[1] else "")
            verdict = verdict_for(results.get(name), recorded.get(name), rule)
            failing = failing or verdict == "FAIL" or (verdict == "MISSING" and recorded[name].get("gate", False))
            out.append(verdict)
        body.append(out)

    lines = [f"## {bench}", "", "| " + " | ".join(columns) + " |", "|" + "---|" * len(columns)]
    lines += ["| " + " | ".join(out) + " |" for out in body] + [""]
    if any(is_parallel(a) for a in arms):
        lines += ["A `-par` arm splits its work among one thread per physical core (the header's cores); every other arm "
                  "runs on one pinned core. `heinzel-par` ÷ `heinzel` is the parallel speedup's inverse.", ""]
    if by_param:
        lines += [f"`{PARAM}` reads its coefficients at run time, as its rivals do; `heinzel` has them as constants. "
                  f"`{PARAM} ÷` is its time over the rival's.", ""]
    lines += [f"- `{n}` FAILED: {msg}" for n, msg in failures] + ([""] if failures else [])
    return "\n".join(lines), failing

def coverage(bench, order):
    """(workloads with reference arms only, names outside the convention) of one bench binary."""
    arms_of, stray = {}, []
    for name in order:
        w, a, _ = parse_name(name)
        if a == "?":
            stray.append((bench, name))
        else:
            arms_of.setdefault(w, set()).add(a)
    ref = [(bench, w) for w, arms in arms_of.items() if not any(map(is_heinzel, arms))]
    return ref, stray

def pinning_line(meta):
    if "core" in meta and meta["core"] is None:
        return ("- Pinned core: none — macOS has no affinity, so every row ran unpinned, one process at a time "
                "(`--core` ignored)")
    return f"- Pinned core: {meta.get('core', '?')}; arms with a `par` variant: cores {meta.get('par_cores', '?')}"

def render_header(meta, arch):
    if not meta:
        return (f"# Heinzel performance — {arch}\n\n"
                "*(no meta.json under this build's results/ — run `bench_report.py run` first)*\n")
    dirty = meta.get("git_sha", "").endswith("-dirty")
    warn = []
    if meta.get("loaded"):
        warn.append("box was loaded (load average > 1.0) during this run")
    if dirty:
        warn.append("working tree was dirty")
    lines = [f"# Heinzel performance — {arch}", ""]
    if warn:
        lines.append(f"**Warning: {'; '.join(warn)} — numbers may not be reliable.**")
        lines.append("")
    lines += [
        f"- CPU: {meta.get('cpu', 'unknown')}",
        f"- Planned for: `{meta.get('machine', 'zen2')}` (`HEINZEL_BENCH_MACHINE`)",
        *([f"- `host`, as HEINZEL_AUTOTUNE measured it (a field not named keeps `Machine`'s default): "
           + (", ".join(meta["host"]) if meta.get("host") else "*(no heinzel_tune/include/heinzel/host_machine.hpp)*")]
          if meta.get("machine") == "host" else []),
        pinning_line(meta),
        f"- Compiler: {meta.get('compiler', '?')} ({meta.get('compiler_version', '?')})",
        f"- Flags: {meta.get('cxx_flags', '')} ({meta.get('build_type', '?')})",
        *([f"- The comparison libraries' files and their hand arms (no Heinzel flags): {meta['library_flags']}"]
          if meta.get("library_flags") else []),
        f"- Git SHA: {meta.get('git_sha', 'unknown')}",
        f"- Date: {meta.get('date', 'unknown')}",
        f"- Load average: {meta.get('load_avg', '?')}",
        f"- Governor: {meta.get('governor', 'unknown')}",
        f"- Repetitions × min-time: {meta.get('repetitions', '?')} × {meta.get('min_time', '?')}",
        "",
        f"All times are medians of {meta.get('repetitions', 'N')} repetitions, real time. "
        "Ratio = heinzel ÷ competitor time; < 1 means Heinzel is faster.",
        "",
        "Generated by tools/bench_report.py — do not edit by hand.",
        "",
    ]
    return "\n".join(lines)

def render_not_yet_section(ref_only):
    lines = ["## Not yet on the nest path", ""]
    if ref_only:
        lines.append("Reference arms only (no Heinzel arm at all):")
        lines += [f"- {b}: {w}" for b, w in ref_only] + [""]
    else:
        lines += ["(none)", ""]
    return "\n".join(lines)

def render_warnings_section(nonconforming, failed_binaries):
    lines, any_warning = ["## Warnings", ""], False
    if nonconforming:
        any_warning = True
        lines.append("Names that do not follow the naming convention (arm reported as `?`):")
        lines += [f"- {b}: `{n}`" for b, n in nonconforming] + [""]
    if failed_binaries:
        any_warning = True
        lines.append("Bench binaries that failed to run:")
        lines += [f"- {b}" for b in failed_binaries] + [""]
    if not any_warning:
        lines += ["(none)", ""]
    return "\n".join(lines)

def result_benches(results_dir):
    """The bench binaries that have a results JSON, sorted."""
    if not os.path.isdir(results_dir):
        return []
    return sorted(f[:-len(".json")] for f in os.listdir(results_dir) if f.endswith(".json") and f != "meta.json")

def generate_report(results_dir, meta, arch, compare, baseline_path):
    recorded = (read_json(baseline_path) or {}).get("recorded", {}) if compare else {}
    sections, ref_only, nonconforming, any_gate_failure = [], [], [], False
    for bench in result_benches(results_dir):
        order, results = load_bench_json(os.path.join(results_dir, bench + ".json"))
        text, failure = render_bench_section(bench, order, results, compare,
                                             recorded.get("rows", {}).get(bench, {}), recorded.get("rule", 1.10))
        sections.append(text)
        any_gate_failure = any_gate_failure or failure
        ref, stray = coverage(bench, order)
        ref_only += ref
        nonconforming += stray

    parts = [render_header(meta, arch)]
    if not sections:
        parts.append(f"*(no results found under `{results_dir}`)*\n")
    parts += sections
    parts.append(render_not_yet_section(ref_only))
    parts.append(render_warnings_section(nonconforming, meta.get("failed", [])))
    return "\n".join(parts), any_gate_failure

def cmd_report(args):
    results_dir = os.path.join(args.build, "results")
    meta = read_json(os.path.join(results_dir, "meta.json")) or {}
    arch = meta.get("arch") or arch_key(cpu_model())
    out = args.out or os.path.join(repo_root(), "docs", f"performance-{arch}.md")
    baseline_path = args.baseline or default_baseline(arch)
    text, failing = generate_report(results_dir, meta, arch, args.compare, baseline_path)
    out_dir = os.path.dirname(out)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with open(out, "w") as f:
        f.write(text)
    print(f"wrote {out}")
    return 1 if (args.compare and failing) else 0

# `record` ------------------------

def dump_baseline(base):
    """Hand-rolled writer: one JSON object per line for `workloads` and for each recorded row, so
    the baseline file stays diffable (json.dump's nested indentation is not)."""
    lines = ["{", f'  "cpu": {json.dumps(base["cpu"])},', f'  "rule": {json.dumps(base["rule"])},']
    if "note" in base:
        lines.append(f'  "note": {json.dumps(base["note"])},')
    workloads = base.get("workloads", [])
    lines.append('  "workloads": [')
    lines += [f"    {json.dumps(row)}" + ("," if i < len(workloads) - 1 else "")
              for i, row in enumerate(workloads)]
    has_recorded = "recorded" in base
    lines.append("  ]" + ("," if has_recorded else ""))
    if has_recorded:
        rec = base["recorded"]
        lines += ["  \"recorded\": {", f'    "sha": {json.dumps(rec["sha"])},',
                  f'    "date": {json.dumps(rec["date"])},', f'    "rule": {json.dumps(rec["rule"])},',
                  '    "rows": {']
        benches = sorted(rec.get("rows", {}))
        for bi, bench in enumerate(benches):
            lines.append(f"      {json.dumps(bench)}: {{")
            names = sorted(rec["rows"][bench])
            for ni, name in enumerate(names):
                entry = json.dumps(rec["rows"][bench][name], sort_keys=True)
                lines.append(f"        {json.dumps(name)}: {entry}" + ("," if ni < len(names) - 1 else ""))
            lines.append("      }" + ("," if bi < len(benches) - 1 else ""))
        lines += ["    }", "  }"]
    lines.append("}")
    return "\n".join(lines) + "\n"

def cmd_record(args):
    results_dir = os.path.join(args.build, "results")
    meta = read_json(os.path.join(results_dir, "meta.json")) or {}
    arch = meta.get("arch") or arch_key(cpu_model())
    baseline_path = args.baseline or default_baseline(arch)
    base = read_json(baseline_path) or {"cpu": meta.get("cpu", cpu_model()), "rule": 1.10, "workloads": []}
    old_rows = base.get("recorded", {}).get("rows", {})

    new_rows = {}
    for bench in result_benches(results_dir):
        _, results = load_bench_json(os.path.join(results_dir, bench + ".json"))
        rows = {}
        for name, info in results.items():
            if info["ns"] is None:
                continue
            prev = old_rows.get(bench, {}).get(name, {})
            entry = {"median_ns": round(info["ns"], 1),
                     "gate": prev["gate"] if "gate" in prev else parse_name(name)[1] == "heinzel"}
            if "factor" in prev:
                entry["factor"] = prev["factor"]
            rows[name] = entry
        if rows:
            new_rows[bench] = rows

    # The run's SHA and date, not this moment's; and a rule of its own — the gate's is for two arms
    # measured minutes apart, too tight for medians that are days old.
    base["recorded"] = {"sha": meta.get("git_sha", "unknown"), "date": meta.get("date", "unknown"),
                        "rule": base.get("recorded", {}).get("rule", 1.10), "rows": new_rows}
    with open(baseline_path, "w") as f:
        f.write(dump_baseline(base))
    print(f"wrote {baseline_path}")
    return 0

# `gate` (== old bench_gate.py) ----

def measured(binary, names, core):
    """({name: median real time in ns}, None) of one pinned live run of `binary` filtered to `names`, or ({}, what went
    wrong): not built, a failed run, or a benchmark whose own correctness check failed (it reports real_time 0 —
    never a time to gate on)."""
    if not os.access(binary, os.X_OK):
        return {}, f"{binary}: not built"
    with tempfile.NamedTemporaryFile(suffix=".json") as out:
        failure = run_bench(binary, names, core, 7, "0.3s", out.name)
        if failure:
            return {}, failure
        _, results = load_bench_json(out.name)
    for name, info in results.items():
        if info["error"]:
            return {}, f"{binary}: {name} did not run: {info['error']}"
    return {name: info["ns"] for name, info in results.items()}, None

def run_medians(binary, names, core):
    """`measured`'s medians, or exit with what went wrong."""
    got, failure = measured(binary, names, core)
    if failure:
        sys.exit(failure)
    return got

def short_sha(root):
    """`git rev-parse --short HEAD` of the tree at `root`, or "unknown"."""
    try:
        return subprocess.run(["git", "-C", root, "rev-parse", "--short", "HEAD"], stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL, text=True, check=True).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return "unknown"

def regression_report(rows, meta):
    """The regression guard's verdict as markdown (`gate --out`). `rows`: per gated row, bench, name,
    main_ns, live, now_ns (None: missing), ratio (None: missing), factor, verdict. `meta`: arch, date,
    sha, main_sha, cpu, load, rule."""
    rule = meta["rule"]
    failing = [r for r in rows if r["verdict"].startswith(("FAIL", "MISSING"))]
    open_rows = [r for r in rows if r["ratio"] is not None and r["ratio"] > rule]
    lines = [f"# Heinzel rewrite — regression guard against main ({meta['arch']})", "",
             "Generated by `tools/bench_report.py gate --out`; do not edit — rerun the gate.", "",
             f"- date: {meta['date']}",
             f"- this tree: `{meta['sha']}`; main: `{meta['main_sha']}`, measured live in the same run unless marked recorded",
             f"- cpu: {meta['cpu']}; load average {meta['load']:.2f} at the start",
             f"- rule: a row slower than {rule}× main is open", ""]
    lines += [f"## Verdict — CLEAN" if not failing else f"## Verdict — NOT CLEAN ({len(failing)} rows over their factor)", "",
              "CLEAN means every gated row is within its recorded factor of `main` (Plan/archive/make-default D-2).", ""]
    if failing:
        lines += [f"- `{r['name']}`: {r['verdict']}" + (f", {r['ratio']:.2f}× main (factor {r['factor']})" if r["ratio"] is not None else "")
                  for r in failing] + [""]
    lines += ["## Rows", "", "| workload | main ns | now ns | ratio | factor | verdict |", "|---|---:|---:|---:|---:|---|"]
    for r in sorted(rows, key=lambda r: (r["bench"], r["name"])):
        main = f"{r['main_ns']:.1f}" + ("" if r["live"] else " (recorded)")
        now = "—" if r["now_ns"] is None else f"{r['now_ns']:.1f}"
        ratio = "—" if r["ratio"] is None else f"{r['ratio']:.2f}"
        lines.append(f"| `{r['name']}` | {main} | {now} | {ratio} | {r['factor']} | {r['verdict']} |")
    lines += ["", f"## Open — rows slower than {rule}× main", ""]
    lines += [f"- `{r['name']}`: {r['ratio']:.2f}× (factor {r['factor']})" for r in sorted(open_rows, key=lambda r: (r["bench"], r["name"]))] or ["None."]
    lines += ["", "## Not in the guard", "",
              f"Rows without a `main` counterpart are reported in `performance-{meta['arch']}.md`, not here.", ""]
    return "\n".join(lines)

def gated_rows(workloads, keep_all):
    """The workloads rows a gate measures: the gated ones (all with --all), never a parallel arm's."""
    return [r for r in workloads if (r["gate"] or keep_all) and not is_parallel(r["arm"])]

def cmd_gate(args):
    baseline_path = args.baseline or default_baseline(arch_key(cpu_model()))
    with open(baseline_path) as f:
        base = json.load(f)
    if cpu_model() != base["cpu"]:
        print(f"SKIP (no baseline for {cpu_model()})")
        return SKIPPED

    rows = gated_rows(base["workloads"], args.all)
    if not rows:
        print("nothing gated yet")
        return 0

    load = os.getloadavg()[0]
    print(f"load average {load:.2f}")
    if load > 1.0 and not args.force:
        print("SKIP (load average > 1.0 — a loaded box reports false drops); --force overrides")
        return SKIPPED
    unpinned_on_macos(args)

    by_bench = {}
    for r in rows:
        by_bench.setdefault(r["bench"], []).append(r)

    failed = False
    measured = []  # what `--out` reports
    print(f"{'workload':44} {'main ns':>14} {'now ns':>14} {'ratio':>8}  verdict")
    for bench, bench_rows in by_bench.items():
        names = [r["name"].format(arm=r["arm"]) for r in bench_rows]
        got = run_medians(os.path.join(args.build, "benchmarks", bench), names, args.core)
        if args.main_build:
            for main_bench in {r["main_bench"] for r in bench_rows if "main_bench" in r}:
                live = run_medians(os.path.join(args.main_build, "benchmarks", main_bench),
                                    [r["main_name"] for r in bench_rows if r.get("main_bench") == main_bench],
                                    args.core)
                for r in bench_rows:
                    if r.get("main_bench") == main_bench and r["main_name"] in live:
                        r["main_median_ns"], r["live"] = live[r["main_name"]], True
        for r, name in zip(bench_rows, names):
            row = {"bench": bench, "name": name, "main_ns": r["main_median_ns"], "live": bool(r.get("live")),
                   "now_ns": None, "ratio": None, "factor": r.get("factor", base["rule"]), "verdict": "MISSING"}
            measured.append(row)
            if name not in got:
                print(f"{name:44} {r['main_median_ns']:14.1f} {'-':>14} {'-':>8}  MISSING")
                failed = failed or r["gate"]
                continue
            ratio = got[name] / r["main_median_ns"]
            ok = ratio <= row["factor"]
            verdict = ("PASS" if ok else "FAIL") if r["gate"] else "info"
            row.update(now_ns=got[name], ratio=ratio, verdict=verdict)
            if not r.get("live"):
                verdict += " (recorded)"
            failed = failed or (r["gate"] and not ok)
            print(f"{name:44} {r['main_median_ns']:14.1f} {got[name]:14.1f} {ratio:8.2f}  {verdict}")
    if args.out:
        main_root = cmake_cache_value(args.main_build, "CMAKE_HOME_DIRECTORY") if args.main_build else None
        meta = {"arch": arch_key(cpu_model()), "date": datetime.now().astimezone().isoformat(timespec="seconds"),
                "sha": short_sha(repo_root()), "main_sha": short_sha(main_root) if main_root else "recorded only",
                "cpu": cpu_model(), "load": load, "rule": base["rule"]}
        with open(args.out, "w") as f:
            f.write(regression_report(measured, meta))
    return 1 if failed else 0

# `baseline` ----------------------

def cmd_baseline(args):
    """A baseline for this machine made from another machine's (`--from`): its workloads with their gates and factors,
    this CPU, and each row's `main` median measured here, live, from `--main-build` (Plan/23 P6: the M1's from zen2's).
    A row whose `main` arm did not run is left out, and named. The gate measures `main` live again; the medians
    written here are its fallback."""
    with open(args.source) as f:
        base = json.load(f)
    unpinned_on_macos(args)
    cpu, date = cpu_model(), datetime.now().astimezone().isoformat(timespec="seconds")
    live, why = {}, {}
    for main_bench in sorted({r["main_bench"] for r in base["workloads"] if "main_bench" in r}):
        names = [r["main_name"] for r in base["workloads"] if r.get("main_bench") == main_bench]
        got, why[main_bench] = measured(os.path.join(args.main_build, "benchmarks", main_bench), names, args.core)
        live.update({(main_bench, name): ns for name, ns in got.items()})
    kept = []
    for r in base["workloads"]:
        ns = live.get((r.get("main_bench"), r.get("main_name")))
        if ns is None:
            print(f"left out {r['bench']} {r['name']}: {why.get(r.get('main_bench')) or 'no live median of main'}", file=sys.stderr)
            continue
        kept.append(dict(r, main_median_ns=round(ns, 1), source=f"main measured live on {cpu}, {date}"))
    made = {"cpu": cpu, "rule": base["rule"], "workloads": kept,
            "note": f"made by `bench_report.py baseline` from {os.path.basename(args.source)}: its workloads, gates and "
                    f"factors, main's medians measured live here ({date}); the gate measures main live again"}
    with open(args.out, "w") as f:
        f.write(dump_baseline(made))
    print(f"wrote {args.out}: {len(kept)} of {len(base['workloads'])} rows")
    return 0 if kept else 1

# CLI ------------------------------

def add_run_args(p):
    p.add_argument("--core", type=int, default=4)
    p.add_argument("--par-cores", default=default_par_cores(),
                   help="taskset list the rows of a `-par` arm run on (default: one logical CPU per physical core)")
    p.add_argument("--filter", help="regex applied to each candidate name (workload/arm/size)")
    p.add_argument("--bench", action="append", default=[], help="run only this bench_* binary (repeatable)")
    p.add_argument("--repetitions", type=int, default=7)
    p.add_argument("--min-time", default="0.3s")
    p.add_argument("--force", action="store_true", help="run even when the box is loaded")

def add_report_args(p):
    p.add_argument("--out")
    p.add_argument("--compare", action="store_true")
    p.add_argument("--baseline")

def parse_args(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)

    run_p = sub.add_parser("run")
    run_p.add_argument("--build", required=True)
    add_run_args(run_p)

    report_p = sub.add_parser("report")
    report_p.add_argument("--build", required=True)
    add_report_args(report_p)

    record_p = sub.add_parser("record")
    record_p.add_argument("--build", required=True)
    record_p.add_argument("--baseline")

    gate_p = sub.add_parser("gate")
    gate_p.add_argument("--build", required=True)
    gate_p.add_argument("--main-build")
    gate_p.add_argument("--core", type=int, default=4)
    gate_p.add_argument("--all", action="store_true")
    gate_p.add_argument("--force", action="store_true")
    gate_p.add_argument("--baseline")
    gate_p.add_argument("--out", help="also write the verdict as a generated markdown report here")

    baseline_p = sub.add_parser("baseline")
    baseline_p.add_argument("--from", dest="source", required=True, help="the baseline whose workloads to take")
    baseline_p.add_argument("--main-build", required=True)
    baseline_p.add_argument("--out", required=True)
    baseline_p.add_argument("--core", type=int, default=4)

    all_p = sub.add_parser("all")
    all_p.add_argument("--build", required=True)
    add_run_args(all_p)
    add_report_args(all_p)

    return ap.parse_args(argv)

def cmd_all(args):
    run_rc = cmd_run(args)
    if run_rc == SKIPPED:
        return SKIPPED
    return cmd_report(args) or run_rc  # a binary that failed is named in the report, which is still written

def main(argv=None):
    args = parse_args(argv)
    return {"run": cmd_run, "report": cmd_report, "record": cmd_record,
            "gate": cmd_gate, "baseline": cmd_baseline, "all": cmd_all}[args.command](args)

if __name__ == "__main__":
    sys.exit(main())
