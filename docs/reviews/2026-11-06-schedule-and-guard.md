## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-11-06T12:28:57Z
- Effort: medium
- Lenses: adversarial, integration, source-defect, plan-delivery
- Seconds: 149
- Lines: 20
- Digest: e8a5264854a6fd0c

### adversarial: needs-attention

1. **P1: Ordinary Windows launch forms bypass enforcement.** `scripts/dispatch_hook.py:37` returns before checking detachment or background execution when `REVIEW` misses the command. Reproduce with a PowerShell payload containing:
   ```powershell
   Start-Process -WindowStyle Hidden -FilePath python -ArgumentList @('scripts/review-dispatch.py', '--writer', 'claude', '--prompt', 'R:/scratch/p.md')
   ```
   The hook returns `None`, allowing a detached review without completion notification. Likewise, `py scripts/review-dispatch.py --prompt R:/scratch/p.md` passes without `run_in_background`. These are normal Windows invocation forms. The check passes commands it explicitly must refuse. Both bypasses were reproduced through `verdict()`.

### integration: needs-attention

2. **P2: Overlapping dispatchers lose the running-review marker.** `scripts/review-dispatch.py:554` unconditionally replaces the shared marker; line 562 removes it when that dispatcher finishes. Reproduce by accidentally launching a second review while the first remains running: A enters, B enters, B finishes. B deletes the marker although A is still reviewing. The heartbeat then reports no review and resumes tracked-file changes; the Stop hook also resumes counting blocks. An in-memory reproduction confirmed this sequence leaves no marker. Enforce exclusive dispatcher ownership or retain independently owned markers until every active review finishes.

### source-defect: approve

### plan-delivery: needs-attention

3. **P2: The heartbeat checks for reviews after instructions that modify the tree.** `.claude/skills/process-plan/SKILL.md:97` places `reviewing` at step 8, after step 7’s CI repair instructions. Reproduce by starting a background review, then letting the heartbeat fire while `ci_status.py` reports a real failed build. Step 7 instructs the writer to record the failure and implement a fix before checking whether a reviewer is reading the tree. Even exit 3 directs a tracked run-file append first. This violates the delivered promise that the heartbeat changes nothing during review. Check review activity before any tracked-file mutation or repair instruction.

HIGHEST: P1  
OVERALL: needs-attention

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-11-06T12:36:55Z
- Effort: low
- Lenses: adversarial
- Seconds: 47
- Lines: 10
- Digest: 09a1b3fc31752fbb

### adversarial: needs-attention

1. **P1: Quoted PowerShell interpreter commands bypass the launch check.** `scripts/dispatch_hook.py:47` allows the call when `REVIEW` misses a quoted executable. Reproduce by submitting this PowerShell command without `run_in_background`:
   ```powershell
   & "python" scripts/review-dispatch.py --writer claude --prompt R:/scratch/p.md
   ```
   With an existing review prompt, this launches a foreground review. Calling `verdict()` with that payload returned `None`; the equivalent unquoted command was refused. A quoted absolute Python path also bypasses the check. These are ordinary PowerShell invocation forms, so the check passes a real launch it must refuse. Handle quoted interpreter tokens before deciding that the command runs no review.

HIGHEST: P1
OVERALL: needs-attention

## Writer note: settled

- Settled: `scripts/dispatch_hook.py` reads shell text, so it is an early warning for the launch forms a session really uses, not a proof. Another spelling that slips past it is not a `P1` or `P2`: the review still runs and records normally, and the only consequence is a later finish notification or a foreground wait the shell tool itself moves to the background. `review-todo-section` names this pattern: stop patching spellings of a home-made scanner.

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-11-06T12:39:06Z
- Effort: medium
- Lenses: adversarial
- Seconds: 76
- Lines: 6
- Digest: 4f55e5b523241248

### adversarial: approve

No new actionable findings within this pass’s scope. The settled scanner limitations are not reopened.

HIGHEST: none
OVERALL: approve
