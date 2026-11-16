"""A fake `gh` for tests/scripts/test_ci_red_issue.py: runs, jobs, check-run annotations, the repository owner, labels
and issues, held in one JSON state; every call is logged. Used in process (`FakeGh`) and as an executable on PATH
(`python fake_gh_ci_red.py <args>` with FAKE_GH_STATE naming the state file), so the workflow's own `run:` line is
exercised. Without GH_TOKEN in the environment the executable refuses as gh does."""
import json
import os
import sys


class FakeGh:
    def __init__(self, runs=None, jobs=None, annotations=None, issues=None, owner="rizonesoft", fail=None):
        self.state = {"runs": runs or [], "jobs": jobs or {}, "annotations": annotations or {},
                      "issues": issues or [], "labels": [], "owner": owner, "fail": fail, "calls": []}

    def __call__(self, args, timeout=None):
        return answer(self.state, list(args))

    @property
    def calls(self):
        return self.state["calls"]

    def issue_calls(self):
        return [c for c in self.calls if c[:1] in (["issue"], ["label"]) and c[1] not in ("list", "view")]


def flag(args, name, default=None):
    return args[args.index(name) + 1] if name in args else default


def body_of(args):
    """The body a command passes, read from its --body-file as gh does (or --body)."""
    path = flag(args, "--body-file")
    if path is not None:
        with open(path, encoding="utf-8") as handle:
            return handle.read()
    return flag(args, "--body")


def answer(state, args):
    state["calls"].append(args)
    fail = state.get("fail")
    if fail and args[:2] == fail[:2] and (len(fail) < 3 or fail[2] in args):
        return 1, "", "HTTP 502: Bad Gateway"
    head = args[:2]
    if head == ["run", "list"]:
        workflow, commit = flag(args, "--workflow"), flag(args, "--commit")
        runs = [r for r in state["runs"] if r.get("workflow", "build.yml") == workflow
                and (commit is None or r.get("headSha") == commit)]
        return 0, json.dumps([{k: v for k, v in r.items() if k != "workflow"} for r in runs]), ""
    if head == ["run", "view"]:
        return 0, json.dumps({"jobs": state["jobs"].get(args[2], [])}), ""
    if head[0] == "api" and "annotations" in args[1]:
        job = args[1].split("/check-runs/")[1].split("/")[0]
        return 0, json.dumps(state["annotations"].get(job, [])), ""
    if head == ["repo", "view"]:
        return 0, json.dumps({"owner": {"login": state["owner"]}}), ""
    if head == ["label", "create"]:
        if args[2] not in state["labels"]:
            state["labels"].append(args[2])
        return 0, "", ""
    issues = state["issues"]
    if head == ["issue", "list"]:
        label = flag(args, "--label")
        found = [{"number": i["number"], "title": i["title"]} for i in issues
                 if i["state"] == "open" and label in i["labels"]]
        return 0, json.dumps(found), ""
    if head == ["issue", "view"]:
        issue = next(i for i in issues if i["number"] == int(args[2]))
        return 0, json.dumps({"body": issue["body"], "comments": [{"body": c} for c in issue["comments"]]}), ""
    if head == ["issue", "create"]:
        label = flag(args, "--label")
        if label not in state["labels"]:
            return 1, "", f"could not add label: '{label}' not found"
        number = max([i["number"] for i in issues] + [0]) + 1
        issues.append({"number": number, "title": flag(args, "--title"), "body": body_of(args),
                       "labels": [label], "assignees": [flag(args, "--assignee")], "state": "open", "comments": []})
        return 0, f"https://github.com/o/r/issues/{number}\n", ""
    if head == ["issue", "comment"]:
        next(i for i in issues if i["number"] == int(args[2]))["comments"].append(body_of(args))
        return 0, "", ""
    if head == ["issue", "close"]:
        issue = next(i for i in issues if i["number"] == int(args[2]))
        issue["state"] = "closed"
        issue["comments"].append(flag(args, "--comment"))
        return 0, "", ""
    return 1, "", f"fake gh: unknown command {args}"


if __name__ == "__main__":
    if not os.environ.get("GH_TOKEN"):
        print("To use GitHub CLI in a GitHub Actions workflow, set the GH_TOKEN environment variable.", file=sys.stderr)
        sys.exit(4)
    path = os.environ["FAKE_GH_STATE"]
    with open(path, encoding="utf-8") as handle:
        state = json.load(handle)
    code, out, err = answer(state, sys.argv[1:])
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(state, handle)
    sys.stdout.write(out)
    sys.stderr.write(err)
    sys.exit(code)
