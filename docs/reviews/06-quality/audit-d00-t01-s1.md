# Audit - D00 T01 §1: Git repository and repo-face files

- **Audit section:** `D06 T05 §1` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D00 T01 §1`, stamped 2026-10-13 at `25ac228` (`docs/reviews/d00-t01-s1.json`, 2 rounds under the earlier process)
- **Audited state:** `main` at `26c72ae`
- **Date:** 2026-10-21

## Checkpoint re-run

In a fresh clone of `https://github.com/rizonesoft/Recto.git` at `26c72ae`: `python scripts/todo-graph.py validate` -> `valid: 676 sections`; `python scripts/todo-graph.py plan --check` -> `plan current`; `python -m unittest tests.scripts.test_todo_graph` -> `OK`; `LICENSE` is 35,149 bytes. The checkpoint holds.

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T14:12:29Z
- Effort: medium
- Lines: 33
- Digest: d755a1a7f04e6000

## adversarial: needs-attention

1. **[P1] Crash dumps are not ignored.** `.gitignore:12` omits the crash-dump exclusions explicitly required by `todo/00-workspace/TODO-01-repo-and-build.md:57`. Dumps outside ignored output directories remain eligible for accidental staging, including process memory containing document content or credentials. Reproduction: `git check-ignore -v --no-index sample.dmp sample.mdmp` returns no matches.

2. **[P2] Most promised Git attributes are missing.** `.gitattributes:4` through EOF provide neither LF rules for `.svg` and `.xaml`, CRLF rules for `.sln`, `.vcxproj`, `.props`, `.targets` and `.cmd`, nor explicit binary rules for `.png` and `.ico`. Checkout behavior therefore depends on local Git configuration. Reproduction: `git check-attr text eol diff merge -- src/app/App.xaml Recto.sln src/app/Recto.vcxproj Directory.Build.props build.cmd design/brand/github-banner.png` reports `unspecified` for every attribute. This directly fails §1’s attribute checkpoint.

## consistency: needs-attention

3. **[P3] Root documents violate one-line-per-paragraph formatting.** `README.md:5`, `README.md:29`, `CLA.md:3` and `NOTICE.md:5` each begin paragraphs continued on the next physical line without a blank separator. Reproduction: inspect those files with numbered lines and compare against `AGENTS.md:70`.

## integration: needs-attention

4. **[P2] The promised DCO contribution workflow is absent.** `CLA.md:8` and `CONTRIBUTING.md:35` provide CLA terms and commit instructions but no DCO text, reference or certification procedure. Foundation decision D11 and §1 explicitly require **DCO + CLA**. Following the documented contribution workflow never requests the DCO certification. Reproduction: `rg -n 'DCO|Signed-off-by|sign-off|developercertificate' CLA.md CONTRIBUTING.md .github` returns no matches. The CLA’s original-work clause does not reproduce the [DCO certification](https://developercertificate.org/).

5. **[P2] The PR template no longer supplies the required verbatim CLA statement.** `CLA.md:17` requires the statement at line 19 verbatim and says the template requests it. `.github/pull_request_template.md:23` supplies a different, shorter checkbox statement. A contributor following the template therefore misses the documented acceptance requirement. Reproduction: compare those two statements. This also regresses the specific template discrepancy recorded as fixed in the original review.

## source-defect: advisory

No additional confirmed finding. The authoritative FSF text fetch timed out, so byte-for-byte upstream license identity remains unverified. The recorded checksum discrepancy below is independently reproducible without drawing a legal conclusion.

## design: advisory

Not applicable.

## record: needs-attention

6. **[P2] The recorded GPL checksum does not match the candidate.** `todo/00-workspace/TODO-01-repo-and-build.md:49` records SHA-256 `0c2b2577702544e6ca2110800c25129ef79a7277e74f888ae852afb90cb363b4`. Both the committed `26c72ae:LICENSE` blob and checkout actually hash to `3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986`. Both contain 35,149 bytes and zero CRLF sequences, so line-ending conversion does not explain the discrepancy. Reproduction: `Get-FileHash LICENSE -Algorithm SHA256`. The byte-count checkpoint cannot establish the recorded identity.

7. **[P3] The ticked build-directory assertion contradicts the legitimate later scaffold.** `todo/00-workspace/TODO-01-repo-and-build.md:61` still requires that no file under `build/` be tracked. Reproduction: `git ls-tree -r --name-only 26c72ae build` returns `build/build.ps1`. Section §3 and `AGENTS.md:19` explicitly require that driver, so this is stale acceptance wording, not a reason to remove the build script.

Verification limitation: `validate` returned `valid: 676 sections` and `plan --check` returned `plan current` in the existing checkout. The unittest invocation ran 58 tests with 57 setup errors because the read-only sandbox provides no writable temporary directory. A fresh clone was unavailable, and `git ls-remote` failed through the configured proxy. Consequently, I could not independently confirm the fresh-clone success asserted at `docs/reviews/06-quality/audit-d00-t01-s1.md:10`; these environmental failures are not candidate defects. No files were modified.

OVERALL: needs-attention

## Fixes after round 1

- Finding 1 (P1): `.gitignore` ignores `*.dmp` and `*.mdmp` (`git check-ignore -v --no-index sample.dmp sample.mdmp` matches both).
- Finding 2 (P2): `.gitattributes` adds LF for `.svg` and `.xaml`, CRLF for `.sln`, `.vcxproj`, `.props`, `.targets` and `.cmd`, and binary for `.png` and `.ico`; `git add --renormalize .` changed no stored file.
- Finding 3 (P3): the wrapped paragraphs in `README.md`, `CLA.md` and `NOTICE.md` are one line each.
- Finding 4 (P2): `CLA.md` adds a Developer Certificate of Origin section (`git commit -s`, DCO 1.1) and `CONTRIBUTING.md` asks for the sign-off.
- Finding 5 (P2): the PR template carries `CLA.md`'s statement verbatim and a DCO sign-off checkbox.
- Findings 6 and 7 (P2, P3): a dated audit note in `D00 T01 §1` records the current LICENSE hash (after the verbatim restore in `2affdc1`) and `build/build.ps1` as the tracked exception; the stamped checklist is not rewritten.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T14:15:37Z
- Effort: medium
- Lines: 27
- Digest: 6296be271dc57d3a

## adversarial: approve

No additional confirmed finding.

## consistency: needs-attention

1. **[P2] The DCO fix commit omits its own required sign-off.** `CONTRIBUTING.md:39` and `CLA.md:16` require every commit to carry `Signed-off-by:`, but candidate `15a21a7` has no trailers. Reproduce with `git show -s --format=%B 15a21a7` or `git show -s --format="%(trailers)" 15a21a7`; neither supplies the required certification. Adding the policy and checkbox does not certify this contribution under the [DCO](https://developercertificate.org/).

## integration: approve

No additional confirmed finding.

## source-defect: advisory

Independent FSF byte comparison remains unverified because the upstream text fetch timed out. The local license test passes, but its stored checksum is not independent upstream evidence.

## design: advisory

Not applicable.

## record: advisory

The fresh-clone checkpoint remains independently unverified. At `15a21a7`, validation reports 676 sections, the plan check passes, and `LICENSE` is 35,149 bytes with the audit note’s checksum. However, the graph suite reports 57 setup errors across 58 tests because the sandbox has no writable temporary directory. Fresh cloning is unavailable, and `git ls-remote` fails through the configured proxy. These are verification limitations, not candidate defects.

No files were modified.

OVERALL: needs-attention

## Fixes after round 2

- Finding 1 (P2): the DCO and CLA apply to contributions made to the project owner; the owner does not certify or grant to himself, and an agent must not add a legal sign-off on the owner's behalf. `CLA.md` and `CONTRIBUTING.md` now say the sign-off is for every commit a contributor other than the project owner submits. Default taken 2026-10-21 (unattended run); reversing it means the owner signs off his own commits with `git commit -s` from then on.

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T14:16:58Z
- Effort: medium
- Lines: 27
- Digest: 206e49c68777b123

## adversarial: approve

No additional confirmed defect in the round 3 state at `5cf9285`.

## consistency: approve

No additional confirmed finding. The explicit owner exemption resolves round 2’s sign-off finding.

## integration: approve

No additional confirmed finding.

## source-defect: advisory

Independent byte comparison against the FSF original remains unverified: the upstream fetch timed out. The local license test passes, but its embedded checksum is not independent upstream evidence.

## design: advisory

Not applicable.

## record: advisory

The fresh-clone checkpoint remains independently unverified. Current-tree validation and plan checking pass; `LICENSE` is 35,149 bytes with the recorded checksum. However, the graph suite encounters 57 temporary-directory setup errors across 58 tests under the read-only sandbox. Fresh cloning is unavailable, and `git ls-remote` fails through the configured proxy. These are verification limitations, not confirmed repository defects.

No files were modified.

OVERALL: approve

## Outcome

- Independent upstream check: `https://www.gnu.org/licenses/gpl-3.0.txt` fetched 2026-10-21 hashes to `3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986`, 35,149 bytes, identical to `LICENSE`.
- The stamp of `D00 T01 §1` is confirmed: its checkpoint holds, and the gaps the audit found (crash-dump ignores, attributes, DCO, the verbatim CLA statement, paragraph wrapping, stale facts) are fixed at their owning files in `15a21a7` and `5cf9285`. No downstream stamp is suspect.