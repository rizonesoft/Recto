#!/usr/bin/env python3
"""Report GoogleTest run times, enforce a per-test time budget, and prove a sharded run covered every test once."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import math
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

VALID_MODES = ("debug", "release", "asan")


@dataclass(frozen=True)
class TestCaseTiming:
    full_name: str
    suite: str
    seconds: float
    status: str


@dataclass(frozen=True)
class SuiteReport:
    cases: list[TestCaseTiming]
    suite_totals: dict[str, float]
    total_seconds: float


class UsageOrInputError(Exception):
    """Raised when an XML report or budget configuration is invalid (exit 2)."""


def _parse_seconds(raw: str | None, context: str) -> float:
    if raw is None or not raw.strip():
        raise UsageOrInputError(f"{context}: missing 'time' attribute")
    try:
        value = float(raw)
    except ValueError as exc:
        raise UsageOrInputError(f"{context}: invalid time {raw!r}") from exc
    if not math.isfinite(value) or value < 0.0:
        raise UsageOrInputError(f"{context}: invalid negative or non-finite time {raw!r}")
    return value


def _require_positive_float(value: object, context: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise UsageOrInputError(context)
    parsed = float(value)
    if not math.isfinite(parsed) or parsed <= 0.0:
        raise UsageOrInputError(context)
    return parsed


def load_xml_reports(xml_paths: list[Path]) -> SuiteReport:
    cases: list[TestCaseTiming] = []
    suite_totals: dict[str, float] = {}
    total_seconds = 0.0

    for path in xml_paths:
        if not path.is_file():
            raise UsageOrInputError(f"XML report not found: {path}")
        try:
            tree = ET.parse(path)
        except ET.ParseError as exc:
            raise UsageOrInputError(f"Malformed XML report {path}: {exc}") from exc

        root = tree.getroot()
        if root.tag == "testsuite":
            suites = [root]
        elif root.tag == "testsuites":
            suites = root.findall("testsuite")
        else:
            raise UsageOrInputError(f"Unexpected XML root element <{root.tag}> in {path}")

        for suite_el in suites:
            suite_name = (suite_el.attrib.get("name") or "").strip() or "<unnamed>"
            suite_cases: list[TestCaseTiming] = []
            case_sum = 0.0
            for case_el in suite_el.findall("testcase"):
                name = (case_el.attrib.get("name") or "").strip()
                classname = (case_el.attrib.get("classname") or "").strip() or suite_name
                if not name:
                    raise UsageOrInputError(f"XML {path} suite {suite_name}: <testcase> missing 'name'")
                full_name = f"{classname}.{name}" if classname else name
                secs = _parse_seconds(case_el.attrib.get("time"), f"{path} {full_name}")
                status = (case_el.attrib.get("status") or "run").strip()
                suite_cases.append(TestCaseTiming(full_name=full_name, suite=suite_name, seconds=secs, status=status))
                case_sum += secs

            cases.extend(suite_cases)
            raw_suite_time = suite_el.attrib.get("time")
            suite_secs = _parse_seconds(raw_suite_time, f"{path} suite {suite_name}") if raw_suite_time is not None else case_sum
            suite_totals[suite_name] = suite_totals.get(suite_name, 0.0) + suite_secs
            total_seconds += suite_secs

    if not cases:
        joined = ", ".join(str(p) for p in xml_paths)
        raise UsageOrInputError(f"XML report contains 0 tests: {joined}")

    return SuiteReport(cases=cases, suite_totals=suite_totals, total_seconds=total_seconds)


def load_budget(budget_path: Path, known_tests: set[str]) -> tuple[dict[str, float], dict[tuple[str, str], float]]:
    if not budget_path.is_file():
        raise UsageOrInputError(f"Budget file not found: {budget_path}")
    try:
        data = json.loads(budget_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise UsageOrInputError(f"Malformed budget JSON {budget_path}: {exc}") from exc

    if not isinstance(data, dict):
        raise UsageOrInputError(f"Budget file {budget_path} must contain a JSON object")

    defaults: dict[str, float] = {}
    for mode in VALID_MODES:
        defaults[mode] = _require_positive_float(
            data.get(mode),
            f"Budget file {budget_path} must define positive numeric '{mode}' limit",
        )

    raw_exceptions = data.get("exceptions")
    if not isinstance(raw_exceptions, list):
        raise UsageOrInputError(f"Budget file {budget_path} must define an 'exceptions' list")

    exceptions: dict[tuple[str, str], float] = {}
    for idx, entry in enumerate(raw_exceptions):
        label = f"exceptions[{idx}]"
        if not isinstance(entry, dict):
            raise UsageOrInputError(f"Invalid budget entry {label}: expected object")
        test_name = entry.get("test")
        if not isinstance(test_name, str) or not test_name.strip():
            raise UsageOrInputError(f"Invalid budget entry {label}: missing or empty 'test'")
        test_name = test_name.strip()

        mode = entry.get("mode")
        if not isinstance(mode, str) or mode not in VALID_MODES:
            raise UsageOrInputError(f"Invalid budget entry {label} ({test_name}): mode must be one of {VALID_MODES}")

        parsed_seconds = _require_positive_float(
            entry.get("seconds"),
            f"Invalid budget entry {label} ({test_name}): 'seconds' must be a positive number",
        )

        reason = entry.get("reason")
        if not isinstance(reason, str) or not reason.strip():
            raise UsageOrInputError(f"Budget exception for '{test_name}' ({mode}) has no reason")

        if test_name not in known_tests:
            raise UsageOrInputError(f"Budget exception names unknown test '{test_name}' not present in XML report")

        key = (test_name, mode)
        if key in exceptions:
            raise UsageOrInputError(f"Duplicate budget exception for '{test_name}' in mode '{mode}'")
        exceptions[key] = parsed_seconds

    return defaults, exceptions


def cmd_report(xml_paths: list[Path]) -> int:
    report = load_xml_reports(xml_paths)
    print(f"Total: {len(report.cases)} tests in {report.total_seconds:.3f}s")
    print("Slowest 15 tests:")
    slowest = sorted(report.cases, key=lambda c: (-c.seconds, c.full_name))[:15]
    for case in slowest:
        print(f"  {case.seconds:8.3f}s  {case.full_name}")
    print("Suites:")
    for suite_name, secs in sorted(report.suite_totals.items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"  {secs:8.3f}s  {suite_name}")
    return 0


def cmd_check(xml_paths: list[Path], budget_path: Path, mode: str) -> int:
    report = load_xml_reports(xml_paths)
    known_tests = {case.full_name for case in report.cases}
    defaults, exceptions = load_budget(budget_path, known_tests)
    default_limit = defaults[mode]

    violations: list[tuple[str, float, float]] = []
    max_time = 0.0
    for case in report.cases:
        limit = exceptions.get((case.full_name, mode), default_limit)
        if case.seconds > max_time:
            max_time = case.seconds
        if case.seconds > limit:
            violations.append((case.full_name, case.seconds, limit))

    if violations:
        violations.sort(key=lambda item: (-item[1], item[0]))
        print(
            f"FAIL: {len(violations)} test(s) exceeded {mode} time budget:",
            file=sys.stderr,
        )
        for full_name, secs, limit in violations:
            print(f"  {full_name}: {secs:.3f}s > {limit:.3f}s", file=sys.stderr)
        return 1

    active_exceptions = sum(1 for (_, exc_mode) in exceptions if exc_mode == mode)
    print(
        f"OK: {len(report.cases)} tests within {mode} time budget "
        f"(total={report.total_seconds:.3f}s, max={max_time:.3f}s, default={default_limit:.3f}s, "
        f"exceptions={active_exceptions})"
    )
    return 0


def parse_test_list(list_path: Path) -> list[str]:
    """Full test names from `recto-tests.exe --gtest_list_tests` output, in order.

    A suite line starts at column 0 and ends with '.' (after any `# TypeParam = ...` comment is cut); a test line is
    indented (its `# GetParam() = ...` comment cut). Any other column-0 line (gtest_main's `Running main() from ...`)
    is not a test.
    """
    if not list_path.is_file():
        raise UsageOrInputError(f"test list not found: {list_path}")
    try:
        text = list_path.read_text(encoding="utf-8-sig")
    except (OSError, UnicodeDecodeError) as exc:
        raise UsageOrInputError(f"cannot read test list {list_path}: {exc}") from exc
    names: list[str] = []
    suite: str | None = None
    for lineno, raw in enumerate(text.splitlines(), start=1):
        if not raw.strip():
            continue
        body = raw.split("#", 1)[0].rstrip()
        if raw[0] not in " \t":
            suite = body if body.endswith(".") and " " not in body else None
            continue
        name = body.strip()
        if not name:
            continue
        if suite is None:
            raise UsageOrInputError(f"{list_path}:{lineno}: test {name!r} listed outside a test suite")
        names.append(suite + name)
    if not names:
        raise UsageOrInputError(f"test list {list_path} names no tests")
    return names


def _matches_disabled(part: str) -> bool:
    # GoogleTest's kDisableTestFilter, "DISABLED_*:*/DISABLED_*", on the suite name and on the test name.
    return part.startswith("DISABLED_") or "/DISABLED_" in part


def is_disabled(full_name: str, suite: str) -> bool:
    name = full_name[len(suite) + 1:] if full_name.startswith(suite + ".") else full_name
    return _matches_disabled(suite) or _matches_disabled(name)


def cmd_coverage(list_path: Path, xml_paths: list[Path]) -> int:
    """Every listed test reported exactly once across the reports, enabled tests run and disabled tests not."""
    listed = parse_test_list(list_path)
    report = load_xml_reports(xml_paths)
    problems: list[str] = []
    listed_set: set[str] = set()
    for name in listed:
        if name in listed_set:
            problems.append(f"listed twice by --gtest_list_tests: {name}")
        listed_set.add(name)
    reported: dict[str, list[TestCaseTiming]] = {}
    for case in report.cases:
        reported.setdefault(case.full_name, []).append(case)
    for name in listed:
        if name not in reported:
            problems.append(f"missing from every report: {name}")
    enabled = disabled = 0
    for name, cases in sorted(reported.items()):
        if name not in listed_set:
            problems.append(f"reported but not listed: {name}")
            continue
        if len(cases) > 1:
            problems.append(f"reported {len(cases)} times: {name}")
            continue
        case = cases[0]
        if is_disabled(name, case.suite):
            disabled += 1
            if case.status != "notrun":
                problems.append(f"disabled test reported as {case.status!r}, not 'notrun': {name}")
        else:
            enabled += 1
            if case.status != "run":
                problems.append(f"enabled test reported as {case.status!r}, not 'run': {name}")
    if problems:
        print(f"FAIL: test coverage over {len(xml_paths)} report(s): {len(problems)} problem(s):", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1
    print(
        f"OK: {len(listed)} listed tests each reported exactly once across {len(xml_paths)} report(s) "
        f"({enabled} enabled run, {disabled} disabled not run)"
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Report test timings and check per-test budgets.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    report_p = subparsers.add_parser("report", help="Print total, 15 slowest tests, and per-suite times.")
    report_p.add_argument("xml", nargs="+", type=Path, help="GoogleTest XML file(s)")

    check_p = subparsers.add_parser("check", help="Check test times against a JSON budget.")
    check_p.add_argument("xml", nargs="+", type=Path, help="GoogleTest XML file(s)")
    check_p.add_argument("--budget", required=True, type=Path, help="Path to time-budget.json")
    check_p.add_argument("--mode", required=True, choices=VALID_MODES, help="Budget mode to check")

    coverage_p = subparsers.add_parser(
        "coverage", help="Check every test --gtest_list_tests names is reported exactly once across the XML reports."
    )
    coverage_p.add_argument("list", type=Path, help="Output of recto-tests.exe --gtest_list_tests")
    coverage_p.add_argument("xml", nargs="+", type=Path, help="GoogleTest XML file(s), one per shard")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "report":
            return cmd_report(args.xml)
        if args.command == "coverage":
            return cmd_coverage(args.list, args.xml)
        return cmd_check(args.xml, args.budget, args.mode)
    except UsageOrInputError as exc:
        print(f"test-times: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
