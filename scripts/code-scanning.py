#!/usr/bin/env python3
"""Read GitHub CodeQL code-scanning status and open alerts on main, and verify CodeQL SARIF output (D06 T02 s11).

Modes:
    python scripts/code-scanning.py
        Verify the latest codeql.yml run on main succeeded, a matching CodeQL C++ analysis
        with empty error exists for that run's headSha on refs/heads/main, and list every
        open alert on refs/heads/main (paginated).
        Exits 0 when the latest scan succeeded with a matching analysis and 0 open alerts,
        1 when any open alert exists or the latest scan failed/cancelled or no matching analysis exists,
        2 when the latest codeql.yml run on main is still queued or in progress,
        3 when GitHub / gh could not be read or returned malformed records (never 0).

    python scripts/code-scanning.py --scheduled
        The recto-code-scanning automation: exits 1 once per new open alert (in bounded batches
        fitting alert.py's 60-line email tail) and once per failed workflow run (keyed by
        databaseId) until build/automations/recto-code-scanning/last.json confirms the reporting
        run's email was sent; persists state atomically per (repo, branch) in
        build/code-scanning/state.json.

    python scripts/code-scanning.py --verify-sarif <dir>
        Verify that <dir> holds CodeQL SARIF 2.1.0 output with the security-extended query suite
        (medium-precision security rules and config summary when present) and extracted
        first-party C++ translation units across all production source roots.

Stdlib only.
"""
from __future__ import annotations

import argparse
import calendar
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
REPO_DEFAULT = "rizonesoft/Recto"
WORKFLOW_NAME = "codeql.yml"
CODEQL_CATEGORY = "/language:c-cpp"
BRANCH_DEFAULT = "main"
TASK_NAME = "recto-code-scanning"
STATE_FILE = Path("build") / "code-scanning" / "state.json"
MAX_ALERTS_PER_EMAIL = 15
MIN_SECURITY_RULES = 10
MIN_MEDIUM_SECURITY_RULES = 10
TRANSLATION_UNIT_SUFFIXES = (".cpp", ".cc", ".cxx", ".c")
REQUIRED_SOURCE_ROOTS = (
    "src/pdf/",
    "src/core/",
    "src/services/",
    "src/ui/",
    "src/app/",
    "src/cli/",
)


class Unavailable(Exception):
    """GitHub / gh could not be read or returned malformed data; never treated as clean."""


def run_gh(args: list[str], *, timeout: int = 60) -> tuple[int, str, str]:
    kwargs: dict = {"capture_output": True, "text": True, "timeout": timeout}
    if os.name == "nt":
        kwargs["creationflags"] = 0x08000000
    try:
        proc = subprocess.run(["gh", *args], **kwargs)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise Unavailable(f"gh {args[0]} failed: {exc}") from None
    return proc.returncode, proc.stdout, proc.stderr


def _parse_json(text: str, context: str):
    try:
        return json.loads(text)
    except json.JSONDecodeError as exc:
        raise Unavailable(f"{context}: invalid JSON: {exc}") from None


def _is_int(value) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def fetch_latest_workflow_run(runner, *, repo: str = REPO_DEFAULT, branch: str = BRANCH_DEFAULT) -> dict | None:
    rc, out, err = runner([
        "run", "list",
        "--repo", repo,
        "--workflow", WORKFLOW_NAME,
        "--branch", branch,
        "--limit", "5",
        "--json", "databaseId,status,conclusion,headSha,url,event,createdAt",
    ])
    if rc != 0:
        raise Unavailable(f"gh run list failed (exit {rc}): {(err or out).strip()}")
    runs = _parse_json(out, "gh run list")
    if not isinstance(runs, list):
        raise Unavailable("gh run list returned non-list JSON")
    if not runs:
        return None
    if not isinstance(runs[0], dict):
        raise Unavailable("gh run list returned non-object run entry")
    return runs[0]


def _analysis_matches(analysis: dict, *, head_sha: str | None, category: str) -> bool:
    if not isinstance(analysis, dict):
        return False
    tool = analysis.get("tool")
    tool_name = str(tool.get("name") or "") if isinstance(tool, dict) else ""
    if "codeql" not in tool_name.lower():
        return False
    if str(analysis.get("category") or "") != category:
        return False
    if str(analysis.get("error") or "").strip():
        return False
    if head_sha and str(analysis.get("commit_sha") or "") != head_sha:
        return False
    return True


def fetch_latest_analysis(
    runner,
    repo: str,
    ref: str,
    *,
    head_sha: str | None = None,
    category: str = CODEQL_CATEGORY,
) -> dict | None:
    rc, out, err = runner([
        "api",
        f"repos/{repo}/code-scanning/analyses?ref={ref}&per_page=20",
    ])
    if rc != 0:
        combined = f"{err}\n{out}".lower()
        if "404" in combined or "no analysis found" in combined:
            return None
        raise Unavailable(f"gh api code-scanning/analyses failed (exit {rc}): {(err or out).strip()}")
    analyses = _parse_json(out, "code-scanning/analyses")
    if not isinstance(analyses, list):
        raise Unavailable("code-scanning/analyses returned non-list JSON")
    for item in analyses:
        if _analysis_matches(item, head_sha=head_sha, category=category):
            return item
    return None


def fetch_open_alerts(runner, repo: str, ref: str) -> list[dict]:
    alerts: list[dict] = []
    page = 1
    while True:
        rc, out, err = runner([
            "api",
            f"repos/{repo}/code-scanning/alerts?state=open&ref={ref}&per_page=100&page={page}",
        ])
        if rc != 0:
            raise Unavailable(f"gh api code-scanning/alerts page {page} failed (exit {rc}): {(err or out).strip()}")
        batch = _parse_json(out, f"code-scanning/alerts page {page}")
        if not isinstance(batch, list):
            raise Unavailable(f"code-scanning/alerts page {page} returned non-list JSON")
        for idx, item in enumerate(batch):
            if not isinstance(item, dict):
                raise Unavailable(f"code-scanning/alerts page {page} entry {idx} is not an object")
            state = item.get("state")
            if not _is_int(item.get("number")) or not isinstance(state, str) or not state:
                raise Unavailable(
                    f"code-scanning/alerts page {page} entry {idx} missing valid number or state"
                )
            if state != "open":
                continue
            inst = item.get("most_recent_instance")
            if isinstance(inst, dict) and inst.get("ref") and inst.get("ref") != ref:
                continue
            alerts.append(item)
        if len(batch) < 100:
            break
        page += 1
    alerts.sort(key=lambda a: int(a["number"]))
    return alerts


def format_alert(alert: dict) -> str:
    number = alert.get("number", "?")
    rule = alert.get("rule") if isinstance(alert.get("rule"), dict) else {}
    rule_id = rule.get("id") or "unknown-rule"
    severity = rule.get("security_severity_level") or rule.get("severity") or "unknown"
    inst = alert.get("most_recent_instance") if isinstance(alert.get("most_recent_instance"), dict) else {}
    loc = inst.get("location") if isinstance(inst.get("location"), dict) else {}
    path = loc.get("path") or "<unknown>"
    line = loc.get("start_line") or 0
    url = alert.get("html_url") or ""
    return f"#{number} [{severity}] {rule_id} {path}:{line} {url}".rstrip()


def check_ordinary(*, runner=run_gh, repo: str = REPO_DEFAULT, branch: str = BRANCH_DEFAULT, out=print) -> int:
    ref = f"refs/heads/{branch}"
    try:
        latest_run = fetch_latest_workflow_run(runner, repo=repo, branch=branch)
        if latest_run is None:
            out(f"code-scanning: no {WORKFLOW_NAME} workflow run found on {branch}")
            return 1
        status = str(latest_run.get("status") or "")
        conclusion = str(latest_run.get("conclusion") or "")
        run_url = str(latest_run.get("url") or "")
        head_sha = str(latest_run.get("headSha") or "")
        if status != "completed":
            out(f"code-scanning: latest {WORKFLOW_NAME} run on {branch} is {status} ({run_url})")
            return 2
        if conclusion != "success":
            out(f"code-scanning: latest {WORKFLOW_NAME} run on {branch} ended with {conclusion} ({run_url})")
            return 1
        if not head_sha:
            out(f"code-scanning: latest {WORKFLOW_NAME} run on {branch} has no headSha ({run_url})")
            return 1
        analysis = fetch_latest_analysis(runner, repo=repo, ref=ref, head_sha=head_sha)
        if analysis is None:
            out(
                f"code-scanning: no clean CodeQL {CODEQL_CATEGORY} analysis found on {ref} "
                f"for {head_sha[:12]}"
            )
            return 1
        alerts = fetch_open_alerts(runner, repo=repo, ref=ref)
    except Unavailable as exc:
        out(f"code-scanning: unavailable: {exc}")
        return 3
    if alerts:
        out(f"code-scanning: {len(alerts)} open alert(s) on {branch}:")
        for alert in alerts:
            out(f"  {format_alert(alert)}")
        return 1
    commit_sha = str(analysis.get("commit_sha") or "")[:12]
    out(f"code-scanning: 0 open alerts on {branch} (latest analysis {commit_sha})")
    return 0


def _read_json_file(path: Path) -> dict | None:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) else None
    except (OSError, json.JSONDecodeError):
        return None


def _write_json_atomic(path: Path, data: dict, *, replace=os.replace) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=str(path.parent))
    tmp_path = Path(tmp_name)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            json.dump(data, fh, indent=2, sort_keys=True)
            fh.write("\n")
        replace(str(tmp_path), str(path))
    except BaseException:
        try:
            tmp_path.unlink(missing_ok=True)
        except OSError:
            pass
        raise


def _epoch(stamp) -> int | None:
    try:
        return calendar.timegm(time.strptime(str(stamp), "%Y-%m-%dT%H:%M:%SZ"))
    except (TypeError, ValueError):
        return None


def alert_delivered(root: Path, raised_at: float) -> bool:
    last = _read_json_file(Path(root) / "build" / "automations" / TASK_NAME / "last.json")
    if not isinstance(last, dict) or last.get("alert") != "sent" or last.get("exit") != 1:
        return False
    began, ended = _epoch(last.get("started")), _epoch(last.get("finished"))
    return began is not None and ended is not None and began - 1 <= raised_at <= ended + 1


def _load_scoped_state(raw: dict | None, scope_key: str) -> tuple[dict, dict]:
    """Return (root_state_dict, bucket_dict) namespaced by '<repo>@<branch>'."""
    default_key = f"{REPO_DEFAULT}@{BRANCH_DEFAULT}"
    if isinstance(raw, dict) and isinstance(raw.get("scopes"), dict):
        root_state = copy.deepcopy(raw)
    elif isinstance(raw, dict) and any(k in raw for k in ("acknowledged", "raised_alerts", "raised_at")):
        # Migrate legacy flat state into the default scope.
        root_state = {"scopes": {default_key: copy.deepcopy(raw)}}
    else:
        root_state = {"scopes": {}}
    bucket = root_state["scopes"].get(scope_key)
    if not isinstance(bucket, dict):
        bucket = {}
    return root_state, bucket


def _save_scoped_state(
    state_path: Path,
    raw_on_disk: dict | None,
    root_state: dict,
    scope_key: str,
    bucket: dict,
    *,
    replace=os.replace,
) -> dict:
    updated = copy.deepcopy(root_state)
    updated.setdefault("scopes", {})[scope_key] = bucket
    if updated != raw_on_disk:
        _write_json_atomic(state_path, updated, replace=replace)
    return updated


def check_scheduled(
    *,
    runner=run_gh,
    repo: str = REPO_DEFAULT,
    branch: str = BRANCH_DEFAULT,
    root: Path = ROOT,
    now=time.time,
    out=print,
    replace=os.replace,
    batch_limit: int = MAX_ALERTS_PER_EMAIL,
) -> int:
    ref = f"refs/heads/{branch}"
    scope_key = f"{repo}@{branch}"
    state_path = Path(root) / STATE_FILE
    raw_on_disk = _read_json_file(state_path)
    root_state, bucket = _load_scoped_state(raw_on_disk, scope_key)

    acknowledged = {int(n) for n in bucket.get("acknowledged", []) if _is_int(n)}
    acknowledged_failed_runs = [int(n) for n in bucket.get("acknowledged_failed_runs", []) if _is_int(n)]
    raised_at = bucket.get("raised_at")
    raised_alerts = [int(n) for n in bucket.get("raised_alerts", []) if _is_int(n)]
    raised_failed_run = bucket.get("raised_failed_run") if _is_int(bucket.get("raised_failed_run")) else None

    # Persist confirmed delivery receipts BEFORE any GitHub calls or early returns so a subsequent
    # pending poll or outage cannot overwrite last.json and lose the receipt.
    if isinstance(raised_at, (int, float)) and (raised_alerts or raised_failed_run is not None):
        if alert_delivered(root, float(raised_at)):
            if raised_alerts:
                acknowledged.update(raised_alerts)
            if raised_failed_run is not None and raised_failed_run not in acknowledged_failed_runs:
                acknowledged_failed_runs.append(raised_failed_run)
                acknowledged_failed_runs = acknowledged_failed_runs[-20:]
            raised_alerts = []
            raised_failed_run = None
            raised_at = None
            confirmed_bucket = {
                "acknowledged": sorted(acknowledged),
                "acknowledged_failed_runs": acknowledged_failed_runs,
                "raised_alerts": [],
                "raised_failed_run": None,
                "raised_at": None,
            }
            root_state = _save_scoped_state(
                state_path, raw_on_disk, root_state, scope_key, confirmed_bucket, replace=replace
            )
            raw_on_disk = copy.deepcopy(root_state)

    try:
        latest_run = fetch_latest_workflow_run(runner, repo=repo, branch=branch)
        if latest_run is None:
            out(f"code-scanning: no {WORKFLOW_NAME} workflow run found on {branch}")
            return 1
        status = str(latest_run.get("status") or "")
        conclusion = str(latest_run.get("conclusion") or "")
        run_url = str(latest_run.get("url") or "")
        run_id = latest_run.get("databaseId") if _is_int(latest_run.get("databaseId")) else None
        head_sha = str(latest_run.get("headSha") or "")

        run_error: str | None = None
        if status == "completed":
            if conclusion != "success":
                run_error = f"code-scanning: latest {WORKFLOW_NAME} run on {branch} ended with {conclusion} ({run_url})"
            elif not head_sha:
                run_error = f"code-scanning: latest {WORKFLOW_NAME} run on {branch} has no headSha ({run_url})"
            else:
                analysis = fetch_latest_analysis(runner, repo=repo, ref=ref, head_sha=head_sha)
                if analysis is None:
                    run_error = (
                        f"code-scanning: no clean CodeQL {CODEQL_CATEGORY} analysis found on {ref} "
                        f"for {head_sha[:12]}"
                    )

        # Always read open alerts (even while a newer push scan is in_progress) so existing
        # unreported alerts from the preceding scan are never delayed by a running workflow.
        alerts = fetch_open_alerts(runner, repo=repo, ref=ref)
    except Unavailable as exc:
        out(f"code-scanning: unavailable: {exc}")
        return 3

    open_numbers = {int(a["number"]) for a in alerts}
    acknowledged &= open_numbers
    unacked = [a for a in alerts if int(a["number"]) not in acknowledged]

    if unacked:
        batch = unacked[:batch_limit]
        remaining = len(unacked) - len(batch)
        stamp = float(now())
        next_bucket = {
            "acknowledged": sorted(acknowledged),
            "acknowledged_failed_runs": acknowledged_failed_runs,
            "raised_alerts": [int(a["number"]) for a in batch],
            "raised_failed_run": None,
            "raised_at": stamp,
        }
        _save_scoped_state(state_path, raw_on_disk, root_state, scope_key, next_bucket, replace=replace)
        out(f"code-scanning: {len(batch)} new open alert(s) on {branch} (showing {len(batch)} of {len(unacked)}):")
        for alert in batch:
            out(f"  {format_alert(alert)}")
        if remaining > 0:
            out(f"  ... and {remaining} more open alert(s) queued for the next poll")
        return 1

    if run_error is not None:
        if run_id is not None and run_id in acknowledged_failed_runs:
            clean_bucket = {
                "acknowledged": sorted(acknowledged),
                "acknowledged_failed_runs": acknowledged_failed_runs,
                "raised_alerts": [],
                "raised_failed_run": None,
                "raised_at": None,
            }
            _save_scoped_state(state_path, raw_on_disk, root_state, scope_key, clean_bucket, replace=replace)
            out(f"{run_error} (already reported for run {run_id})")
            return 0
        stamp = float(now())
        fail_bucket = {
            "acknowledged": sorted(acknowledged),
            "acknowledged_failed_runs": acknowledged_failed_runs,
            "raised_alerts": [],
            "raised_failed_run": run_id,
            "raised_at": stamp,
        }
        _save_scoped_state(state_path, raw_on_disk, root_state, scope_key, fail_bucket, replace=replace)
        out(run_error)
        return 1

    clean_bucket = {
        "acknowledged": sorted(acknowledged),
        "acknowledged_failed_runs": acknowledged_failed_runs,
        "raised_alerts": [],
        "raised_failed_run": None,
        "raised_at": None,
    }
    _save_scoped_state(state_path, raw_on_disk, root_state, scope_key, clean_bucket, replace=replace)
    if status != "completed":
        out(f"code-scanning: latest {WORKFLOW_NAME} run on {branch} is {status} ({run_url}); 0 unacknowledged open alerts")
    else:
        out(f"code-scanning: 0 unacknowledged open alerts on {branch} ({len(alerts)} total open)")
    return 0


def _normalize_sarif_uri(uri: str) -> str:
    text = unquote(str(uri or "")).replace("\\", "/")
    if text.lower().startswith("file:///"):
        text = text[8:]
    elif text.lower().startswith("file://"):
        text = text[7:]
    return text.lstrip("/").lower()


def _matches_source_root_tu(path: str, root: str) -> bool:
    root_l = root.lower()
    if not path.endswith(TRANSLATION_UNIT_SUFFIXES):
        return False
    return path.startswith(root_l) or f"/{root_l}" in path


def verify_sarif_dir(sarif_dir: Path, *, required_roots: tuple[str, ...] = REQUIRED_SOURCE_ROOTS) -> tuple[int, list[str], dict]:
    sarif_files = sorted(Path(sarif_dir).glob("*.sarif"))
    if not sarif_files:
        return 1, [f"no *.sarif files found in {sarif_dir}"], {}

    errors: list[str] = []
    extracted_paths: set[str] = set()
    rule_ids: set[str] = set()
    security_rule_ids: set[str] = set()
    medium_security_rule_ids: set[str] = set()
    has_codeql_driver = False
    saw_config_summary = False
    config_uses_security_extended = False

    for path in sarif_files:
        try:
            doc = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"{path.name}: unreadable SARIF: {exc}")
            continue
        if not isinstance(doc, dict) or doc.get("version") != "2.1.0":
            errors.append(f"{path.name}: missing SARIF version 2.1.0")
            continue
        runs = doc.get("runs")
        if not isinstance(runs, list) or not runs:
            errors.append(f"{path.name}: empty runs[]")
            continue
        for run in runs:
            if not isinstance(run, dict):
                continue
            tool = run.get("tool") if isinstance(run.get("tool"), dict) else {}
            driver = tool.get("driver") if isinstance(tool.get("driver"), dict) else {}
            driver_name = driver.get("name")
            if isinstance(driver_name, str) and "codeql" in driver_name.lower():
                has_codeql_driver = True

            # Check CodeQL config summary when emitted in run.properties.
            run_props = run.get("properties") if isinstance(run.get("properties"), dict) else {}
            cfg = run_props.get("codeqlConfigSummary")
            if isinstance(cfg, dict) and isinstance(cfg.get("queries"), list):
                saw_config_summary = True
                for entry in cfg["queries"]:
                    if isinstance(entry, dict) and "security-extended" in str(entry.get("uses") or "").lower():
                        config_uses_security_extended = True

            # Collect rules from driver.rules and tool.extensions[].rules.
            rule_lists = []
            if isinstance(driver.get("rules"), list):
                rule_lists.append(driver["rules"])
            for ext in tool.get("extensions") or []:
                if isinstance(ext, dict) and isinstance(ext.get("rules"), list):
                    rule_lists.append(ext["rules"])
            for rlist in rule_lists:
                for rule in rlist:
                    if not isinstance(rule, dict):
                        continue
                    rid = rule.get("id")
                    if not isinstance(rid, str) or not rid:
                        continue
                    rule_ids.add(rid)
                    props = rule.get("properties") if isinstance(rule.get("properties"), dict) else {}
                    tags = [str(t).lower() for t in (props.get("tags") or [])] if isinstance(props.get("tags"), list) else []
                    precision = str(props.get("precision") or "").lower()
                    if "security" in tags:
                        security_rule_ids.add(rid)
                        if precision == "medium":
                            medium_security_rule_ids.add(rid)

            for inv in run.get("invocations") or []:
                if isinstance(inv, dict) and inv.get("executionSuccessful") is False:
                    errors.append(f"{path.name}: invocation executionSuccessful is false")

            for art in run.get("artifacts") or []:
                if isinstance(art, dict):
                    loc = art.get("location") if isinstance(art.get("location"), dict) else {}
                    uri = loc.get("uri")
                    if isinstance(uri, str) and uri:
                        extracted_paths.add(_normalize_sarif_uri(uri))

    if not has_codeql_driver:
        errors.append("no CodeQL tool.driver.name found in SARIF runs")
    if saw_config_summary and not config_uses_security_extended:
        errors.append("codeqlConfigSummary does not declare security-extended")
    if len(security_rule_ids) < MIN_SECURITY_RULES or len(medium_security_rule_ids) < MIN_MEDIUM_SECURITY_RULES:
        errors.append(
            "security-extended query suite not verified "
            f"(security_rules={len(security_rule_ids)}, medium_security_rules={len(medium_security_rule_ids)}, "
            f"total_rules={len(rule_ids)})"
        )
    missing_roots = [
        root for root in required_roots
        if not any(_matches_source_root_tu(p, root) for p in extracted_paths)
    ]
    if missing_roots:
        errors.append(f"missing extracted first-party C++ translation units for: {', '.join(missing_roots)}")

    summary = {
        "sarif_files": len(sarif_files),
        "extracted_files": len(extracted_paths),
        "rules": len(rule_ids),
        "security_rules": len(security_rule_ids),
        "medium_security_rules": len(medium_security_rule_ids),
        "missing_roots": missing_roots,
    }
    return (0 if not errors else 1), errors, summary


def main(argv: list[str] | None = None, *, runner=run_gh, root: Path = ROOT, out=print) -> int:
    parser = argparse.ArgumentParser(description="Check GitHub CodeQL code-scanning status and open alerts.")
    parser.add_argument("--repo", default=REPO_DEFAULT, help="GitHub owner/repo (default: rizonesoft/Recto)")
    parser.add_argument("--branch", default=BRANCH_DEFAULT, help="Branch to inspect (default: main)")
    parser.add_argument("--scheduled", action="store_true", help="Alert-once mode for recto-code-scanning automation")
    parser.add_argument("--verify-sarif", metavar="DIR", help="Verify CodeQL SARIF extraction and suite in DIR")
    args = parser.parse_args(argv)

    if args.verify_sarif:
        code, errors, summary = verify_sarif_dir(Path(args.verify_sarif))
        for err in errors:
            out(f"code-scanning --verify-sarif: ERROR: {err}")
        if code == 0:
            out(
                f"code-scanning --verify-sarif: OK ({summary['sarif_files']} SARIF file(s), "
                f"{summary['extracted_files']} extracted artifact(s), {summary['rules']} rule(s), "
                f"{summary['medium_security_rules']} medium-precision security rule(s), "
                f"all {len(REQUIRED_SOURCE_ROOTS)} first-party C++ TU roots present)"
            )
        return code

    if args.scheduled:
        return check_scheduled(runner=runner, repo=args.repo, branch=args.branch, root=root, out=out)
    return check_ordinary(runner=runner, repo=args.repo, branch=args.branch, out=out)


if __name__ == "__main__":
    sys.exit(main())
