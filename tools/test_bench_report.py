#!/usr/bin/env python3
"""Plain-assert tests for bench_report.py against synthetic Google Benchmark JSON. No pytest, no
fixture files in the repo — everything is built in a tmp dir. Run: python3 tools/test_bench_report.py
"""
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench_report as br  # noqa: E402


def iteration(name, ns, **counters):
    return {"run_name": name, "run_type": "iteration", "real_time": ns, "cpu_time": ns,
            "time_unit": "ns", **counters}


def aggregate(name, ns):  # must be ignored by load_bench_json — median comes from iterations only
    return {"run_name": name, "run_type": "aggregate", "aggregate_name": "median",
            "real_time": ns, "cpu_time": ns, "time_unit": "ns"}


def error_row(name, msg):
    return {"run_name": name, "error_occurred": True, "error_message": msg}


def write_json(path, benchmarks):
    with open(path, "w") as f:
        json.dump({"context": {}, "benchmarks": benchmarks}, f)


def test_parse_name():
    assert br.parse_name("img/brightness/heinzel") == ("img/brightness", "heinzel", "")
    assert br.parse_name("elem/heinzel/N4096") == ("elem", "heinzel", "N4096")
    assert br.parse_name("matmul/hand-ikj/128") == ("matmul", "hand-ikj", "128")
    assert br.parse_name("osc_sine/fm/heinzel-executor") == ("osc_sine/fm", "heinzel-executor", "")
    assert br.parse_name("additive/crv-unrolled-row-4x4") == ("additive", "crv-unrolled-row-4x4", "")
    assert br.parse_name("BM_Something") == ("BM_Something", "?", "")


def test_is_parallel():
    assert br.is_parallel("heinzel-par") and br.is_parallel("hand-par-fused")
    assert not br.is_parallel("heinzel") and not br.is_parallel("heinzel-executor") and not br.is_parallel("hand-parity")
    assert br.parse_name("blurxy/heinzel-par/H1024_W2048") == ("blurxy", "heinzel-par", "H1024_W2048")


CPUINFO_SMT = "".join(f"processor\t: {p}\nphysical id\t: {pkg}\ncore id\t\t: {core}\n\n"
                      for p, pkg, core in [(0, 0, 0), (1, 0, 1), (2, 1, 0), (3, 1, 1), (4, 0, 0), (5, 0, 1), (6, 1, 0), (7, 1, 1)])


def test_physical_cores():
    assert br.physical_cores(CPUINFO_SMT) == "0,1,2,3"  # two packages of two cores, each with a sibling
    assert br.physical_cores("") == "0"


def test_run_bench_split_runs_parallel_rows_on_their_cores(tmpdir):
    calls = []

    def fake(binary, names, core, repetitions, min_time, out_json):
        calls.append((list(names), core))
        write_json(out_json, [iteration(n, 100 if "par" not in n else 10) for n in names])
        return None

    real, br.run_bench = br.run_bench, fake
    try:
        out = os.path.join(tmpdir, "bench_x.json")
        names = ["a/heinzel/N4", "a/heinzel-par/N4", "a/naive/N4"]
        assert br.run_bench_split("bench_x", names, 4, "0-15", 7, "0.3s", out) is None
        assert calls == [(["a/heinzel/N4", "a/naive/N4"], 4), (["a/heinzel-par/N4"], "0-15")]
        order, rows = br.load_bench_json(out)
        assert order == names[:1] + names[2:] + names[1:2] and rows["a/heinzel-par/N4"]["ns"] == 10
        calls.clear()
        assert br.run_bench_split("bench_x", ["b/heinzel-par/N4"], 4, "0-15", 7, "0.3s", out) is None
        assert calls == [(["b/heinzel-par/N4"], "0-15")] and br.load_bench_json(out)[0] == ["b/heinzel-par/N4"]
    finally:
        br.run_bench = real


def test_gate_never_gates_a_parallel_arm():
    rows = [{"arm": "heinzel", "gate": True}, {"arm": "heinzel-par", "gate": True}, {"arm": "heinzel", "gate": False}]
    assert br.gated_rows(rows, False) == rows[:1] and br.gated_rows(rows, True) == [rows[0], rows[2]]


def test_format_time_and_rate():
    assert br.format_time(365.2) == "365 ns"
    assert br.format_time(45923.5) == "45.9 µs"
    assert br.format_time(2798000) == "2.80 ms"
    assert br.format_time(1234000000) == "1.23 s"
    assert br.format_time(None) == "–"
    assert br.format_throughput({"GFLOP/s": 111.23}) == "111.2 GFLOP/s"
    assert br.format_throughput({"items_per_second": 3.1e9}) == "3.10 G items/s"
    assert br.format_throughput({}) == "–"


def test_load_bench_json_medians_errors_and_aggregates(tmpdir):
    path = os.path.join(tmpdir, "b.json")
    write_json(path, [
        iteration("a/heinzel/N4", 100), iteration("a/heinzel/N4", 300), iteration("a/heinzel/N4", 200),
        aggregate("a/heinzel/N4", 999999),  # would poison the median if not ignored
        error_row("a/naive/N4", "boom"),
        iteration("a/eigen/N4", 10, **{"GFLOP/s": 5.0}), iteration("a/eigen/N4", 20, **{"GFLOP/s": 7.0}),
    ])
    order, rows = br.load_bench_json(path)
    assert order == ["a/heinzel/N4", "a/naive/N4", "a/eigen/N4"]
    assert rows["a/heinzel/N4"]["ns"] == 200  # median of 100/300/200, aggregate ignored
    assert rows["a/naive/N4"]["error"] == "boom"
    assert rows["a/eigen/N4"]["counters"]["GFLOP/s"] == 6.0


def test_load_bench_json_missing_and_empty(tmpdir):
    assert br.load_bench_json(os.path.join(tmpdir, "nope.json")) == ([], {})
    empty = os.path.join(tmpdir, "empty.json")
    open(empty, "w").close()
    assert br.load_bench_json(empty) == ([], {})


def test_bench_section_table(tmpdir):
    path = os.path.join(tmpdir, "bench_x.json")
    write_json(path, [
        iteration("matmul/heinzel/128", 100, **{"GFLOP/s": 100.0}),
        iteration("matmul/naive/128", 500),
        iteration("matmul/hand-ikj/128", 150),
        iteration("blur/naive/N50", 20),             # reference-arm-only workload
        iteration("BM_Weird", 1),                    # non-conforming name
    ])
    order, results = br.load_bench_json(path)
    text, failure = br.render_bench_section("bench_x", order, results, False, {}, 1.10)
    assert "| workload | size | heinzel | naive | hand-ikj | ? | ÷ naive | ÷ hand-ikj | ÷ ? | throughput |" in text
    assert "| matmul | 128 | 100 ns | 500 ns | 150 ns | – | 0.200 | 0.667 | – | 100.0 GFLOP/s |" in text
    assert "| blur | N50 | – | 20.0 ns | – | – | – | – | – | – |" in text
    assert not failure
    assert br.coverage("bench_x", order) == ([("bench_x", "blur")], [("bench_x", "BM_Weird")])


def test_bench_section_with_crv_as_a_competitor(tmpdir):
    path = os.path.join(tmpdir, "bench_compare_crv.json")
    write_json(path, [iteration("additive/heinzel", 40), iteration("additive/crv", 60),
                      iteration("additive/crv-unrolled-row-4x4", 50)])
    order, results = br.load_bench_json(path)
    text, _ = br.render_bench_section("bench_compare_crv", order, results, False, {}, 1.10)
    assert "| additive |  | 40.0 ns | 60.0 ns | 50.0 ns | 0.667 | 0.800 |" in text
    assert br.coverage("bench_compare_crv", order) == ([], [])


def test_bench_section_with_a_parallel_arm(tmpdir):
    path = os.path.join(tmpdir, "bench_p.json")
    write_json(path, [iteration("blurxy/heinzel/H1024", 800), iteration("blurxy/heinzel-par/H1024", 100),
                      iteration("blurxy/hand-fused/H1024", 900)])
    order, results = br.load_bench_json(path)
    text, _ = br.render_bench_section("bench_p", order, results, False, {}, 1.10)
    assert "| workload | size | heinzel | heinzel-par | hand-fused | ÷ hand-fused |" in text  # a variant of heinzel, next to it
    assert "| blurxy | H1024 | 800 ns | 100 ns | 900 ns | 0.889 |" in text
    assert "A `-par` arm splits its work among one thread per physical core" in text
    serial_only = br.render_bench_section("bench_p", order[:1] + order[2:], results, False, {}, 1.10)[0]
    assert "-par` arm" not in serial_only


def test_bench_section_with_a_param_arm(tmpdir):
    path = os.path.join(tmpdir, "bench_q.json")
    write_json(path, [iteration("eq/heinzel", 100), iteration("eq/heinzel-param", 200), iteration("eq/juce", 400),
                      iteration("fir/heinzel", 50), iteration("fir/hand", 100)])  # hand shares no row with the param arm
    order, results = br.load_bench_json(path)
    text, _ = br.render_bench_section("bench_q", order, results, False, {}, 1.10)
    assert "| workload | size | heinzel | heinzel-param | juce | hand | ÷ juce | ÷ hand | heinzel-param ÷ juce |" in text
    assert "| eq |  | 100 ns | 200 ns | 400 ns | – | 0.250 | – | 0.500 |" in text
    assert "| fir |  | 50.0 ns | – | – | 100 ns | – | 0.500 | – |" in text
    assert "`heinzel-param` reads its coefficients at run time" in text
    without = br.render_bench_section("bench_q", [n for n in order if "param" not in n], results, False, {}, 1.10)[0]
    assert "heinzel-param" not in without


def test_bench_section_missing_arm_and_failed_row(tmpdir):
    path = os.path.join(tmpdir, "bench_y.json")
    write_json(path, [
        iteration("elem/heinzel/N4", 10),
        error_row("elem/eigen/N4", "SkipWithError: not built"),
    ])
    order, results = br.load_bench_json(path)
    text, *_ = br.render_bench_section("bench_y", order, results, False, {}, 1.10)
    assert "FAILED" in text
    assert "not built" in text
    # naive column was never present at all -> must not appear (missing arms are never a fabricated column)
    assert "naive" not in text


def test_dump_baseline_and_compare_roundtrip(tmpdir):
    baseline_path = os.path.join(tmpdir, "baseline-zen2.json")
    base = {"cpu": "Test CPU", "rule": 1.05, "note": "n",
            "workloads": [{"bench": "bench_x", "name": "elem/{arm}/N4", "arm": "heinzel", "gate": True,
                            "main_median_ns": 12.0}]}
    with open(baseline_path, "w") as f:
        f.write(br.dump_baseline(base))
    reloaded = json.load(open(baseline_path))
    assert reloaded["workloads"] == base["workloads"]  # untouched by a record that adds no new baseline

    results_dir = os.path.join(tmpdir, "results")
    os.makedirs(results_dir)
    write_json(os.path.join(results_dir, "bench_x.json"), [iteration("elem/heinzel/N4", 100)])
    json.dump({"cpu": "Test CPU", "arch": "zen2", "failed": []}, open(os.path.join(results_dir, "meta.json"), "w"))

    class Args:
        build = tmpdir
        baseline = baseline_path
    br.cmd_record(Args())
    recorded = json.load(open(baseline_path))
    assert recorded["workloads"] == base["workloads"]  # preserved verbatim
    row = recorded["recorded"]["rows"]["bench_x"]["elem/heinzel/N4"]
    assert row == {"gate": True, "median_ns": 100.0}

    # now a regression: median jumps from 100 to 300 (ratio 3.0 > rule 1.05) -> FAIL, and add an
    # unrecorded new row plus drop a previously-recorded one to exercise "new" / MISSING.
    write_json(os.path.join(results_dir, "bench_x.json"), [
        iteration("elem/heinzel/N4", 300),
        iteration("widget/heinzel/N1", 50),  # a heinzel row with nothing recorded yet -> "new"
    ])
    text, failing = br.generate_report(results_dir, {"arch": "zen2", "failed": []}, "zen2", True, baseline_path)
    assert failing
    assert "FAIL" in text
    assert "new" in text


def gate_row(name, ratio, factor=1.05, verdict="PASS", live=True):
    return {"bench": "bench_x", "name": name, "main_ns": 100.0, "live": live, "now_ns": 100.0 * ratio,
            "ratio": ratio, "factor": factor, "verdict": verdict}


GATE_META = {"arch": "zen2", "date": "2026-09-25T12:00:00+02:00", "sha": "abc1234", "main_sha": "c4012b8",
             "cpu": "Test CPU", "load": 0.4, "rule": 1.05}


def test_regression_report_clean_with_open_rows():
    rows = [gate_row("b/heinzel", 0.9), gate_row("a/heinzel", 1.25, factor=1.33)]
    text = br.regression_report(rows, GATE_META)
    assert "## Verdict — CLEAN" in text and "NOT CLEAN" not in text
    assert text.index("`a/heinzel`") < text.index("`b/heinzel`")  # sorted by bench, then name
    open_part = text.split("## Open")[1].split("## Not in the guard")[0]
    assert "`a/heinzel`: 1.25× (factor 1.33)" in open_part and "b/heinzel" not in open_part
    assert "`abc1234`" in text and "`c4012b8`" in text and "performance-zen2.md" in text


def test_regression_report_not_clean_names_the_row():
    rows = [gate_row("a/heinzel", 1.5, factor=1.33, verdict="FAIL"), gate_row("b/heinzel", 1.0)]
    missing = dict(gate_row("c/heinzel", 1.0), now_ns=None, ratio=None, verdict="MISSING")
    text = br.regression_report(rows + [missing], GATE_META)
    assert "## Verdict — NOT CLEAN (2 rows over their factor)" in text
    assert "- `a/heinzel`: FAIL, 1.50× main (factor 1.33)" in text and "- `c/heinzel`: MISSING" in text
    assert "| `c/heinzel` | 100.0 | — | — |" in text


def test_regression_report_marks_a_recorded_main_and_an_empty_open_list():
    text = br.regression_report([gate_row("a/heinzel", 1.0, live=False)], GATE_META)
    assert "| `a/heinzel` | 100.0 (recorded) |" in text
    assert text.split("## Open")[1].split("## Not in the guard")[0].strip().endswith("None.")


def test_macos_cpu_pinning_and_header():
    real = (br.on_macos, br.sysctl)
    br.on_macos, br.sysctl = (lambda: True), (lambda key: "Apple M1 Pro" if key == "machdep.cpu.brand_string" else None)
    try:
        assert br.cpu_model() == "Apple M1 Pro" and br.arch_key(br.cpu_model()) == "m1"
        assert br.pinned(4) == [] and br.pinned("0-15") == [] and br.default_par_cores() is None

        class Args:
            core = 4
            par_cores = "0-15"
        args = Args()
        br.unpinned_on_macos(args)
        assert args.core is None and args.par_cores is None
    finally:
        br.on_macos, br.sysctl = real
    if not br.on_macos():
        assert br.pinned(4) == ["taskset", "-c", "4"] and br.pinned("0-15") == ["taskset", "-c", "0-15"]
    assert br.pinned(None) == [] and br.read_governor(None) == "n/a (unpinned)"
    mac = br.render_header({"cpu": "Apple M1 Pro", "core": None, "par_cores": None, "machine": "m1_pro"}, "m1")
    assert "- Planned for: `m1_pro` (`HEINZEL_BENCH_MACHINE`)" in mac
    assert "- Pinned core: none — macOS has no affinity, so every row ran unpinned" in mac
    zen2 = br.render_header({"cpu": "AMD", "core": 4, "par_cores": "0-15"}, "zen2")
    assert "- Pinned core: 4; arms with a `par` variant: cores 0-15" in zen2 and "- Planned for: `zen2`" in zen2


def test_host_values_in_the_header(tmpdir):
    assert br.host_values(tmpdir) is None
    header = os.path.join(tmpdir, "heinzel_tune", "include", "heinzel")
    os.makedirs(header)
    with open(os.path.join(header, "host_machine.hpp"), "w") as f:
        f.write("// Each field: ...\n//   relay_pays_after: 24, relay.cpp\n#include \"heinzel/schedule/machine.hpp\"\n"
                "namespace heinzel {\n\ninline constexpr Machine host{\n    .vector_bytes = 32,\n"
                "    .relay_pays_after = 24,\n    .fma = true,\n    .core = {60, 60},\n};\n\n} // namespace heinzel\n")
    values = br.host_values(tmpdir)
    assert values == ["vector_bytes 32", "relay_pays_after 24", "fma true", "core 60 60"]
    text = br.render_header({"cpu": "AMD", "core": 4, "par_cores": "0-15", "machine": "host", "host": values}, "zen2")
    assert "- Planned for: `host` (`HEINZEL_BENCH_MACHINE`)" in text
    assert ("- `host`, as HEINZEL_AUTOTUNE measured it (a field not named keeps `Machine`'s default): vector_bytes 32, "
            "relay_pays_after 24, fma true, core 60 60") in text
    assert "*(no heinzel_tune/include/heinzel/host_machine.hpp)*" in br.render_header({"machine": "host"}, "zen2")
    assert "`host`, as" not in br.render_header({"machine": "zen2"}, "zen2")


def test_baseline_for_another_machine(tmpdir):
    source = os.path.join(tmpdir, "baseline-zen2.json")
    rows = [{"bench": "bench_x", "name": "a/{arm}", "arm": "heinzel", "gate": True, "factor": 1.2, "main_median_ns": 10.0,
             "source": "zen2", "main_bench": "main_x", "main_name": "a/hz"},
            {"bench": "bench_x", "name": "b/{arm}", "arm": "heinzel", "gate": True, "main_median_ns": 20.0,
             "source": "zen2", "main_bench": "main_y", "main_name": "b/hz"},
            {"bench": "bench_x", "name": "c/{arm}", "arm": "heinzel", "gate": False, "main_median_ns": 30.0,
             "source": "zen2", "main_bench": "main_x", "main_name": "c/hz"}]
    with open(source, "w") as f:
        f.write(br.dump_baseline({"cpu": "AMD", "rule": 1.05, "workloads": rows, "recorded": {
            "sha": "abc", "date": "d", "rule": 1.1, "rows": {}}}))
    calls = []

    def fake(binary, names, core):
        calls.append((os.path.basename(binary), names))
        return ({"a/hz": 123.44, "c/hz": 7.0}, None) if binary.endswith("main_x") else ({}, "main_y: not built")

    real = (br.measured, br.cpu_model)
    br.measured, br.cpu_model = fake, (lambda: "Apple M1 Pro")
    try:
        class Args:
            main_build = tmpdir
            out = os.path.join(tmpdir, "baseline-m1.json")
            core = 4
        args = Args()
        args.source = source
        assert br.cmd_baseline(args) == 0
        made = json.load(open(args.out))
    finally:
        br.measured, br.cpu_model = real
    assert calls == [("main_x", ["a/hz", "c/hz"]), ("main_y", ["b/hz"])]
    assert made["cpu"] == "Apple M1 Pro" and made["rule"] == 1.05 and "recorded" not in made and "baseline-zen2.json" in made["note"]
    assert [r["name"] for r in made["workloads"]] == ["a/{arm}", "c/{arm}"]  # b's main did not run: left out
    a, c = made["workloads"]
    assert a["main_median_ns"] == 123.4 and a["factor"] == 1.2 and a["main_name"] == "a/hz" and "Apple M1 Pro" in a["source"]
    assert c["main_median_ns"] == 7.0 and c["gate"] is False


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for t in tests:
        with tempfile.TemporaryDirectory() as tmp:
            import inspect
            t(tmp) if "tmpdir" in inspect.signature(t).parameters else t()
        print(f"ok  {t.__name__}")
    print(f"{len(tests)} tests passed")


if __name__ == "__main__":
    main()
