"""Aggregate and verify MSVC /analyze per-TU SARIF 2.1.0 logs into one log per project (D06 T02 s10)."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
import tempfile
from urllib.parse import unquote, urlparse

DEFAULT_PROJECTS = (
    "recto-pdf",
    "recto-core",
    "recto-services",
    "recto-ui",
    "Recto",
    "recto-cli",
    "recto-tests",
    "recto-fuzz",
)


def _atomic_write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=str(path.parent))
    tmp_path = Path(tmp_name)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(payload, fh, indent=2)
            fh.write("\n")
        os.replace(tmp_path, path)
    except BaseException:
        tmp_path.unlink(missing_ok=True)
        raise


def _normalize_path_or_uri(raw: str) -> str:
    text = raw.strip()
    if text.lower().startswith("file://"):
        parsed = urlparse(text)
        p = unquote(parsed.path)
        if len(p) >= 3 and p[0] == "/" and p[2] == ":":
            p = p[1:]
        text = p
    return os.path.normcase(os.path.normpath(text.replace("/", os.sep)))


def _resolve_tu_sarif(int_dir: Path, obj_file_name: str, stem: str) -> Path:
    candidates: list[Path] = []
    if obj_file_name:
        obj_path = Path(obj_file_name)
        if obj_path.suffix.lower() == ".obj":
            candidates.append(obj_path.with_suffix(".nativecodeanalysis.sarif"))
            candidates.append(obj_path.parent / f"{stem}.nativecodeanalysis.sarif")
        else:
            candidates.append(obj_path / f"{stem}.nativecodeanalysis.sarif")
    candidates.append(int_dir / f"{stem}.nativecodeanalysis.sarif")
    for cand in candidates:
        if cand.is_file():
            return cand
    return candidates[0]


def _parse_manifest(manifest_path: Path) -> tuple[dict[str, str], list[dict[str, str]]]:
    project_meta: dict[str, str] = {}
    units: list[dict[str, str]] = []
    # MSBuild metadata like PREfastAdditionalPlugins may span lines; reassemble logical records first.
    records: list[str] = []
    for raw in manifest_path.read_text(encoding="utf-8-sig").splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith(("project|", "unit|")):
            records.append(line)
        elif records:
            records[-1] += line
    for line in records:
        parts = line.split("|")
        kind = parts[0]
        if kind == "project" and len(parts) >= 6:
            project_meta = {
                "name": parts[1],
                "int_dir": parts[2],
                "ruleset": parts[3],
                "core_check": parts[4],
                "run_code_analysis": parts[5],
            }
        elif kind == "unit" and len(parts) >= 8:
            units.append(
                {
                    "source": parts[1],
                    "excluded": parts[2],
                    "enable_prefast": parts[3],
                    "ruleset": parts[4],
                    "plugins": parts[5],
                    "object_file_name": parts[6],
                    "stem": parts[7],
                }
            )
    return project_meta, units


def _format_location(result: dict, artifacts: list[str] | None = None) -> str:
    locs = result.get("locations") or []
    if not locs:
        return "<unknown>:0"
    phys = (locs[0] or {}).get("physicalLocation") or {}
    art = phys.get("artifactLocation") or {}
    uri = art.get("uri") or ""
    if not uri and artifacts is not None and isinstance(art.get("index"), int):
        idx = art["index"]
        if 0 <= idx < len(artifacts):
            uri = artifacts[idx]
    if not uri:
        uri = "<unknown>"
    line = (phys.get("region") or {}).get("startLine") or 0
    return f"{uri}:{line}"


def merge_and_verify(
    manifest_dir: Path,
    out_dir: Path,
    expected_projects: list[str],
    expected_ruleset_name: str,
    min_mtime: float | None = None,
    allow_extra_projects: bool = False,
) -> tuple[int, list[str], list[str]]:
    """Return (exit_code, errors, finding_lines)."""
    out_dir.mkdir(parents=True, exist_ok=True)
    errors: list[str] = []
    findings: list[str] = []

    manifests: dict[str, Path] = {}
    if manifest_dir.is_dir():
        for p in sorted(manifest_dir.glob("*.txt")):
            manifests[p.stem] = p

    expected_set = set(expected_projects)
    for proj in expected_projects:
        if proj not in manifests:
            errors.append(f"missing analyze manifest for project '{proj}' in {manifest_dir}")
    if not allow_extra_projects:
        for extra in sorted(set(manifests.keys()) - expected_set):
            errors.append(f"unexpected analyze manifest for project '{extra}' in {manifest_dir}")

    # When extra projects (rebuilt dependencies of -Project) are allowed, verify and emit SARIF for them too.
    projects_to_process = list(expected_projects)
    if allow_extra_projects:
        for extra in sorted(set(manifests.keys()) - expected_set):
            projects_to_process.append(extra)

    for proj in projects_to_process:
        manifest_path = manifests.get(proj)
        if manifest_path is None:
            continue
        project_meta, units = _parse_manifest(manifest_path)
        if not project_meta:
            errors.append(f"{proj}: manifest {manifest_path} has no project header")
            continue
        int_dir = Path(project_meta["int_dir"])
        if project_meta["run_code_analysis"].lower() != "true":
            errors.append(f"{proj}: RunCodeAnalysis is '{project_meta['run_code_analysis']}', expected 'true'")
        if project_meta["core_check"].lower() != "true":
            errors.append(f"{proj}: EnableCppCoreCheck is '{project_meta['core_check']}', expected 'true'")
        if expected_ruleset_name and Path(project_meta["ruleset"]).name.lower() != expected_ruleset_name.lower():
            errors.append(
                f"{proj}: CodeAnalysisRuleSet is '{project_meta['ruleset']}', expected '{expected_ruleset_name}'"
            )

        eligible_units = [u for u in units if u["excluded"].lower() != "true"]
        if not eligible_units:
            errors.append(f"{proj}: manifest lists 0 eligible translation units")
            continue

        # Keep each TU's SARIF run intact inside the project SARIF 2.1.0 runs[] array so ruleIndex,
        # artifactLocation.index, artifacts[], and codeFlows references remain 100% faithful.
        preserved_runs: list[dict] = []
        analyzed_tus: list[str] = []

        for unit in eligible_units:
            src = unit["source"]
            expected_src_norm = _normalize_path_or_uri(src)
            if unit["enable_prefast"].lower() != "true":
                errors.append(f"{proj}: {src}: EnablePREfast is '{unit['enable_prefast']}', expected 'true'")
            if expected_ruleset_name and Path(unit["ruleset"]).name.lower() != expected_ruleset_name.lower():
                errors.append(
                    f"{proj}: {src}: PREfastRulesetFile is '{unit['ruleset']}', expected '{expected_ruleset_name}'"
                )
            if "espxengine.dll" not in unit["plugins"].lower():
                errors.append(f"{proj}: {src}: EspXEngine.dll missing from PREfastAdditionalPlugins ('{unit['plugins']}')")

            sarif_path = _resolve_tu_sarif(int_dir, unit["object_file_name"], unit["stem"])
            if not sarif_path.is_file():
                errors.append(f"{proj}: {src}: missing per-TU SARIF log {sarif_path}")
                continue
            stat = sarif_path.stat()
            if stat.st_size == 0:
                errors.append(f"{proj}: {src}: empty per-TU SARIF log {sarif_path}")
                continue
            if min_mtime is not None and stat.st_mtime < min_mtime:
                errors.append(
                    f"{proj}: {src}: stale per-TU SARIF log {sarif_path} "
                    f"(mtime {stat.st_mtime:.1f} < run start {min_mtime:.1f})"
                )
                continue
            try:
                doc = json.loads(sarif_path.read_text(encoding="utf-8-sig"))
            except (OSError, json.JSONDecodeError) as exc:
                errors.append(f"{proj}: {src}: unreadable SARIF log {sarif_path}: {exc}")
                continue
            runs = doc.get("runs") if isinstance(doc, dict) else None
            if not isinstance(doc, dict) or doc.get("version") != "2.1.0" or not isinstance(runs, list) or not runs:
                errors.append(f"{proj}: {src}: invalid or empty SARIF 2.1.0 runs in {sarif_path}")
                continue

            tu_target_matched = False
            tu_runs_valid = True
            for run in runs:
                if not isinstance(run, dict):
                    errors.append(f"{proj}: {src}: non-object run entry in {sarif_path}")
                    tu_runs_valid = False
                    continue
                tool = run.get("tool")
                driver = tool.get("driver") if isinstance(tool, dict) else None
                driver_name = driver.get("name") if isinstance(driver, dict) else None
                if not isinstance(tool, dict) or not isinstance(driver, dict) or not isinstance(driver_name, str) or not driver_name.strip():
                    errors.append(f"{proj}: {src}: SARIF run missing required tool.driver.name in {sarif_path}")
                    tu_runs_valid = False
                invocations = run.get("invocations")
                if not isinstance(invocations, list) or not invocations:
                    errors.append(f"{proj}: {src}: missing invocations in {sarif_path}")
                    tu_runs_valid = False
                elif any(not isinstance(inv, dict) or inv.get("executionSuccessful") is not True for inv in invocations):
                    errors.append(f"{proj}: {src}: invocation executionSuccessful is not true in {sarif_path}")
                    tu_runs_valid = False

                run_artifacts: list[str] = []
                for art in run.get("artifacts") or []:
                    if not isinstance(art, dict):
                        run_artifacts.append("")
                        continue
                    uri = str(((art.get("location") or {}).get("uri")) or "")
                    run_artifacts.append(uri)
                    roles = art.get("roles") or []
                    if isinstance(roles, list) and "analysisTarget" in roles and uri:
                        if _normalize_path_or_uri(uri) == expected_src_norm:
                            tu_target_matched = True

                preserved_runs.append(run)
                for res in run.get("results") or []:
                    if not isinstance(res, dict):
                        continue
                    rule_id = str(res.get("ruleId") or "unknown")
                    loc = _format_location(res, run_artifacts)
                    msg = str(((res.get("message") or {}).get("text")) or "").strip()
                    findings.append(f"{proj} {rule_id} {loc}: {msg}")

            if not tu_target_matched:
                errors.append(f"{proj}: {src}: SARIF analysisTarget does not match expected source in {sarif_path}")
                tu_runs_valid = False
            if tu_runs_valid:
                analyzed_tus.append(src)

        if preserved_runs:
            out_doc = {
                "$schema": "https://json.schemastore.org/sarif-2.1.0.json",
                "version": "2.1.0",
                "runs": preserved_runs,
                "properties": {
                    "project": proj,
                    "ruleset": project_meta["ruleset"],
                    "analyzedTranslationUnits": analyzed_tus,
                },
            }
            _atomic_write_json(out_dir / f"{proj}.sarif", out_doc)

    exit_code = 0 if (not errors and not findings) else 1
    return exit_code, errors, findings


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Merge and verify per-project MSVC /analyze SARIF 2.1.0 logs.")
    parser.add_argument("--manifest-dir", type=Path, default=Path("build/analyze-manifest"))
    parser.add_argument("--out-dir", type=Path, default=Path("build/analyze"))
    parser.add_argument("--expected-projects", default=",".join(DEFAULT_PROJECTS))
    parser.add_argument("--ruleset", default="RectoAnalyze.ruleset")
    parser.add_argument("--min-mtime", type=float, default=None)
    parser.add_argument("--allow-extra-projects", action="store_true")
    args = parser.parse_args(argv)

    expected = [p.strip() for p in args.expected_projects.split(",") if p.strip()]
    code, errors, findings = merge_and_verify(
        manifest_dir=args.manifest_dir,
        out_dir=args.out_dir,
        expected_projects=expected,
        expected_ruleset_name=args.ruleset,
        min_mtime=args.min_mtime,
        allow_extra_projects=args.allow_extra_projects,
    )
    for line in findings:
        print(f"ANALYZE FINDING: {line}")
    for err in errors:
        print(f"ANALYZE ERROR: {err}", file=sys.stderr)
    if code == 0:
        print(f"analyze sarif: {len(expected)} project log(s) verified clean in {args.out_dir}")
    else:
        print(
            f"analyze sarif: FAILED ({len(findings)} finding(s), {len(errors)} verification error(s))",
            file=sys.stderr,
        )
    return code


if __name__ == "__main__":
    raise SystemExit(main())
