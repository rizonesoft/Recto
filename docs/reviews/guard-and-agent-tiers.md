# Review: run-guard and agent-tier reader repairs

- **Candidate:** `c2609f3`, `5d25e5f`

## Round 1: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T20:16:37Z
- Effort: medium
- Lines: 28
- Digest: ea0bcb2063958072

### adversarial: needs-attention

1. **P1: Malformed quoted and block values still pass.** `scripts/verify-agent-tiers.mjs:173` accepts `description: "a"b"` because the quoted-value regex permits unescaped interior quotes. At line 161, block collection also accepts `description: |\n  first\n second` despite inconsistent indentation. Both bypass the new plain-value validation. Reproduced in memory: `readAgentFrontmatter` returns `error: null`; PyYAML 6.0.3 rejects both with `ParserError`. These are existing gaps left open by the claimed repair. Reproduce with either description alongside `name: a`, `model: sonnet`, and `tools: Read`.

2. **P2: Windows still follows a file replaced by a symlink after inspection.** `scripts/campaign_guard.py:599` substitutes zero for unavailable `O_NOFOLLOW` and `O_NONBLOCK`; both are absent on the tested Windows interpreter. Replace an untracked regular file with a symlink between line 644’s `lstat` and line 601’s `os.open`. The open follows the link, and `fstat` accepts a regular target, so its contents are read. The new protection does not cover this race on the primary platform. This finding is based on source inspection and verified flag availability; I did not create a symlink.

### consistency: approve

No additional findings.

### integration: needs-attention

Finding 1 leaves the validator accepting agent definitions that a YAML loader rejects. Valid model and tool fields do not prevent this failure.

### source-defect: needs-attention

Finding 1 conflicts with YAML’s quoted-scalar and block-indentation grammar. The new plain-scalar restriction does not validate the earlier branches. See the [YAML 1.2.2 specification](https://yaml.org/spec/1.2.2/).

### design: advisory

No user-facing surface changed.

### record: advisory

Ran `python -B -m unittest tests.scripts.test_verify_agent_tiers`: all three tests passed despite the reproduced malformed-value acceptance. The new tests do not exercise these quoted or block-scalar cases. The guard test named `test_the_fingerprint_never_reads_a_link_or_a_special_file` mocks a FIFO but does not exercise a symlink or the replacement race. I did not run the guard suite because its fixtures write files.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 1

`python -m unittest tests.scripts.test_verify_agent_tiers` -> `Ran 4 tests`, `OK`; `python -m unittest tests.scripts.test_campaign_guard` -> `Ran 33 tests`, `OK`; `node scripts/verify-agent-tiers.mjs` -> 5 definitions valid. Against the previous commits the new cases fail.

- Finding 1 (P1): a quoted value may hold no quote of its own kind (and, as before, no backslash), with or without a trailing comment; a block scalar's content lines must be space-indented at least as deep as its first line. `description: "a"b"`, `'a'b'`, and a block with a shallower or tab-indented line now refuse; a deeper line stays allowed (`test_quoted_and_block_values_yaml_refuses_are_refused`).
- Finding 2 (P2): `_read_regular` compares the opened file's device and inode with what the `lstat` saw, so on Windows (which has neither `O_NOFOLLOW` nor `O_NONBLOCK`) a file swapped for a link to another file between the two is not read; the test opens a different file under the first file's identity and gets nothing.

## Round 2: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T20:25:29Z
- Effort: medium
- Lines: 32
- Digest: affae9d4798f5e09

## adversarial: needs-attention

All reproductions below used frontmatter containing `name: a`, `model: sonnet`, and `tools: Read`. In-memory checks returned `error: null`, accepted the model and reported no tool problems; PyYAML 6.0.3 rejected each document.

1. **P1: Leading blank lines bypass block indentation validation.** `scripts/verify-agent-tiers.mjs:168` ignores whitespace-only lines when checking indentation. Reproduce with `description: |\n    \n  first`: the blank line contains four spaces, followed by content indented two. PyYAML raises `ParserError`. The round 1 fix therefore still accepts malformed block scalars.

2. **P1: Nested hook values bypass the repaired scalar checks.** `scripts/verify-agent-tiers.mjs:277` accepts arbitrary double-quoted escapes. Reproduce with `hooks:\n  Stop:\n    - matcher: "\q"`; PyYAML raises `ScannerError`. Separately, line 245 skips block content without checking its actual indentation: `hooks:\n  Stop:\n    - command: |\n        first\n       second` passes despite PyYAML raising `ParserError`. These existing bypasses remain within the repair’s stated validation scope.

3. **P1: Forbidden control characters still pass.** `scripts/verify-agent-tiers.mjs:213` validates plain-scalar punctuation but not YAML’s character repertoire. Reproduce with `description: a` followed by an actual U+0001 character and `b`. The reader accepts it; PyYAML raises `ReaderError`.

## consistency: approve

No additional findings.

## integration: needs-attention

Findings 1-3 allow the validation gate to accept definitions that fail YAML parsing before the agent can load. Actual Claude Code loading was not exercised.

## source-defect: needs-attention

Findings 1-3 violate YAML’s rules for leading blank-line indentation, permitted escapes and printable characters. See [YAML 1.2.2 sections 8.1.1.1, 5.7 and 5.1](https://yaml.org/spec/1.2.2/).

## design: advisory

No user-facing surface changed.

## record: advisory

Ran `python -B -m unittest tests.scripts.test_verify_agent_tiers`: all four tests passed despite the reproduced bypasses. The guard suite was not rerun because its fixtures write files; its recorded 33-test result remains unverified in this review. No files were modified.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 2

`python -m unittest tests.scripts.test_verify_agent_tiers` -> `Ran 5 tests`, `OK`; `node scripts/verify-agent-tiers.mjs` -> 5 definitions valid.

- Finding 1 (P1): one block-scalar check (`blockScalarProblem`) serves top-level values and the `hooks:` block: blank lines before the first content line may not carry more spaces than it, every content line is space-indented at least as deep as the first, and a tab refuses.
- Finding 2 (P1): `hooks:` double-quoted values accept only YAML's escape set (5.7), so `"\q"` refuses, and `hooks:` block scalars go through the same indentation check.
- Finding 3 (P1): the whole frontmatter must be YAML-printable: a C0 or C1 control character (tab and line breaks aside), U+FFFE, U+FFFF or a lone surrogate refuses.

## Round 3: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T20:31:13Z
- Effort: medium
- Lines: 34
- Digest: 1dadbb66c028a025

## adversarial: needs-attention

All reproductions below used frontmatter containing `name: a`, `model: sonnet`, and `tools: Read`. The reader returned `error: null`, accepted the model, and reported no tool problems. PyYAML 6.0.3 rejected every example. Here, `\n` and `\t` represent actual newline and tab characters.

1. **P1: Invalid hook structure still passes.** `scripts/verify-agent-tiers.mjs:239` checks individual lines without validating their structural relationship; line 331 also accepts repeated inline mapping prefixes. Reproduce with `hooks:\n  Stop:\n    - matcher: a: b` (ScannerError), or `hooks:\n  Stop:\n    - matcher: a\n   hooks: b` (ParserError). Both allow unloadable definitions through the gate.

2. **P1: Hook block-scalar headers are accepted without enforcing their meaning.** `scripts/verify-agent-tiers.mjs:316` accepts invalid `|++`, while line 243 discards explicit indentation indicators. Reproduce with `hooks:\n  Stop:\n    - command: |++\n        first`, or replace `|++` with `|9`. The former has duplicate chomping indicators; the latter has insufficient indentation. Both pass this reader and fail YAML parsing.

3. **P1: Whitespace-only lines bypass tab validation.** `scripts/verify-agent-tiers.mjs:269` skips whitespace-only lines before checking for tabs. Reproduce with `description: |\n\t\n  first`. The reader accepts it, but PyYAML raises ScannerError. The shared block checker therefore still accepts forbidden indentation.

4. **P1: Unicode escapes lack a code-point bound.** `scripts/verify-agent-tiers.mjs:309` accepts any eight hexadecimal digits after `\U`. Reproduce with the literal YAML value `hooks:\n  Stop:\n    - matcher: "\U00110000"`. This exceeds Unicode’s maximum; the reader accepts it while PyYAML raises ValueError.

## consistency: approve

No additional findings.

## integration: needs-attention

Findings 1-4 leave the validation gate accepting agent frontmatter that fails YAML loading. Actual Claude Code loading was not exercised.

## source-defect: needs-attention

Findings 1-4 conflict with YAML’s block structure, scalar-header, indentation, and character-escape rules. See [YAML 1.2.2, sections 5.7, 6.1, 8.1 and 8.2](https://yaml.org/spec/1.2.2/).

## design: advisory

No user-facing surface changed.

## record: advisory

Ran `python -B -m unittest tests.scripts.test_verify_agent_tiers`: all five tests passed despite these reproduced bypasses. The recorded guard-suite result was not independently rerun because its fixtures write files. No files were modified; final Git status was clean.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 3

`python -m unittest tests.scripts.test_verify_agent_tiers` -> `Ran 5 tests`, `OK`; `node scripts/verify-agent-tiers.mjs` -> 5 definitions valid.

Findings 1 to 4 (P1) were all in the readers for the nested `hooks:` block and block scalars, the fourth round in a row to find another YAML rule those readers missed. Recto's five agents use neither form (each is flat `name`, `description`, `model`, `tools`), so Recto's dialect is now flat `key: value` lines only: `hooks:`, a block scalar (`|`, `>`, with any header), or any indented continuation line refuses with a reason, and the block and hooks readers are removed. Values stay plain or quoted on one line, in YAML's printable characters, with the plain-scalar and quote rules from rounds 1 and 2 (`test_nested_blocks_and_block_values_are_refused`, `test_quoted_values_yaml_refuses_are_refused`).

## Round 4: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T20:37:16Z
- Effort: medium
- Lines: 32
- Digest: 8c1baa728d35daaf

## adversarial: needs-attention

1. **P1: Bare carriage returns hide malformed YAML and duplicate fields.** `scripts/verify-agent-tiers.mjs:111` splits only on LF or CRLF; line 118 then skips everything beginning with `#`, including content after a bare CR. Reproduce with this string, where `\r` is an actual carriage return:

   ```text
   ---\nname: a\nmodel: sonnet\ntools: Read\n# comment\rdescription: @bad\n---\n
   ```

   In-memory execution returned `error: null`, model `sonnet`, and no tool problems. PyYAML rejected it with `ScannerError`. Replacing `description: @bad` with `model: haiku` also passes the checker as Sonnet, while PyYAML reads Haiku. Reject unsupported line endings before skipping comments, or tokenize all YAML line endings consistently.

## consistency: approve

No additional findings.

## integration: needs-attention

Finding 1 bypasses the frontmatter gate: an accepted definition can fail YAML loading or expose a different model field to the consumer. Actual Claude Code loading was not exercised.

## source-defect: needs-attention

Finding 1 conflicts with [YAML 1.2.2 section 5.4](https://yaml.org/spec/1.2.2/#54-line-break-characters), which recognizes a bare CR as a line break. Restricting frontmatter to flat mappings does not resolve this tokenizer mismatch.

## design: advisory

No user-facing surface changed.

## record: advisory

Ran `python -B -m unittest tests.scripts.test_verify_agent_tiers`: all five tests passed despite the reproduced bypass. The recorded guard-suite results were not independently rerun because its fixtures write files. No files were modified; Git status was clean.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 4

`python -m unittest tests.scripts.test_verify_agent_tiers` -> `OK` (the new cases fail against `76330ff`); `node scripts/verify-agent-tiers.mjs` -> 5 definitions valid.

- Finding 1 (P1): frontmatter lines end only in LF or CRLF; a bare CR, and NEL, LS or PS (line breaks to some YAML loaders), refuse, so no key can hide behind a comment or be read two ways (`# comment\rmodel: haiku`, `\x85`, ` ` cases).

## Round 5: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T20:42:08Z
- Effort: medium
- Lines: 32
- Digest: 50ad23ee2a6a74e1

## adversarial: needs-attention

1. **P1: Unicode-only lines bypass YAML validation.** `scripts/verify-agent-tiers.mjs:120` uses JavaScript `trim()` to discard blank lines, including characters YAML treats as content. Reproduce with this string, interpreting the escapes:

   ```text
   ---\nname: a\nmodel: sonnet\ntools: Read\n\n\u00a0\n---\n
   ```

   `readAgentFrontmatter` returns `error: null`, `tierPin` returns `sonnet`, and `toolProblems` returns `[]`. PyYAML 6.0.3 rejects the frontmatter with `ScannerError`. U+2003 produces the same failure. Restrict blank-line recognition to the supported YAML whitespace instead of JavaScript whitespace.

## consistency: approve

No additional findings.

## integration: needs-attention

Finding 1 allows malformed frontmatter through the agent validation gate. Actual Claude Code loading was not exercised.

## source-defect: needs-attention

Finding 1 contradicts [YAML 1.2.2 section 5.5](https://yaml.org/spec/1.2.2/#55-white-space-characters): YAML recognizes space and tab as whitespace; other printable characters are content. The deliberate flat dialect does not resolve this mismatch.

## design: advisory

No user-facing surface changed.

## record: advisory

Ran `python -B -m unittest tests.scripts.test_verify_agent_tiers`: all five tests passed despite the reproduced bypass. Guard changes received static review; their filesystem-writing tests were not rerun. No files were modified; final Git status was clean.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 5

`python -m unittest tests.scripts.test_verify_agent_tiers` -> `OK` (the `\u00a0` and `\u2003` cases fail against `0acdd49`); `node scripts/verify-agent-tiers.mjs` -> 5 definitions valid.

- Finding 1 (P1): a blank line is spaces and tabs only (YAML 5.5); a line holding a no-break space or another Unicode space is content, so it reaches the `key: value` rule and refuses.

## Round 6: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T20:49:04Z
- Effort: medium
- Lines: 33
- Digest: 9cec45b949b04148

## adversarial: needs-attention

1. **P1: Overlong mapping keys pass validation but fail YAML loading.** `scripts/verify-agent-tiers.mjs:128` accepts keys of unlimited length. Reproduce by passing this constructed string to `readAgentFrontmatter`:

   ```javascript
   "---\nname: a\nmodel: sonnet\ntools: Read\n"
     + "x".repeat(1025) + ": value\n---\n"
   ```

   In-memory execution returned `error: null`, model `sonnet`, and no tool problems. PyYAML 6.0.3 rejected the enclosed mapping with `ScannerError`. Enforce YAML’s implicit-key length limit before accepting the entry.

## consistency: approve

No additional findings.

## integration: needs-attention

Finding 1 allows a definition through the validation gate that fails YAML parsing. Actual Claude Code loading was not exercised.

## source-defect: needs-attention

Finding 1 violates the 1,024-character limit for implicit block-mapping keys in [YAML 1.2.2 section 8.2.2](https://yaml.org/spec/1.2.2/#822-block-mappings). The intentional flat dialect still uses implicit mapping keys.

## design: advisory

No user-facing surface changed.

## record: advisory

Ran `python -B -m unittest tests.scripts.test_verify_agent_tiers`: all five tests passed despite finding 1. Guard changes received static review; filesystem-writing guard tests were not rerun. No files were modified; final Git status was clean.

HIGHEST: P1
OVERALL: needs-attention

## Fixes after round 6

`python -m unittest tests.scripts.test_verify_agent_tiers` -> `OK` (the new cases fail against `be32399`); `node scripts/verify-agent-tiers.mjs` -> 5 definitions valid.

- Finding 1 (P1): frontmatter keys are an allow-list (`KNOWN_KEYS`: name, description, model, tools, color, and the tier fields this check reads), so an unknown key refuses and no key can approach YAML's 1024-character limit for an implicit key.

## Round 7: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T20:56:02Z
- Effort: medium
- Lines: 26
- Digest: a7bdcf7f29cc3127

## adversarial: needs-attention

1. **P2: Windows can still stall while opening a replacement symlink.** `scripts/campaign_guard.py:603` opens the path before checking its identity at line 610. Windows has neither `O_NOFOLLOW` nor `O_NONBLOCK`, confirmed on this interpreter. Reproduce by replacing an inspected regular file with a symlink to an unresponsive UNC target between `lstat` and `os.open`. The open follows the target and can block before the identity check; the hook deadline cannot interrupt it. The repair needs protection at the Windows open operation. This is a source-supported race, not a live network-stall reproduction.

## consistency: approve

No additional findings.

## integration: needs-attention

Finding 1 leaves the Stop hook exposed to the stall this candidate is intended to prevent. Rejecting the opened handle afterward does not bound the opening operation.

## source-defect: needs-attention

Finding 1 overlooks Windows’ distinction between opening a symlink and opening its target. Preventing target traversal requires the appropriate open semantics, documented under [CreateFile symbolic-link behavior](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew).

## design: advisory

No user-facing surface changed.

## record: advisory

Ran `python -B -m unittest tests.scripts.test_verify_agent_tiers`: five tests passed. Guard review used source inspection and in-memory mocks; filesystem-writing guard tests and a live replacement race were not run. No files were modified; Git status remained clean.

HIGHEST: P2  
OVERALL: needs-attention

## Fixes after round 7

`python -m unittest tests.scripts.test_campaign_guard` -> `Ran 34 tests`, `OK` (`test_a_real_link_is_opened_as_itself_never_followed` fails against `b57639e`).

- Finding 1 (P2): on Windows the fingerprint opens a file with `CreateFileW` and `FILE_FLAG_OPEN_REPARSE_POINT` (`_open_no_follow`), which opens a link as itself instead of following it, so a link swapped in after the `lstat` (to a stalled network share, say) is never traversed; a descriptor carrying the reparse-point attribute is then refused. POSIX keeps `O_NOFOLLOW | O_NONBLOCK`. The test uses real links: a link to a file is refused, and a dangling link opens as itself (following it would fail).

## Round 8: astra (`gpt-6-astra` via `codex`)

- Writer: claude
- Run: 2026-10-21T21:03:09Z
- Effort: medium
- Lines: 38
- Digest: 1f9f28c27475fc96

## adversarial: needs-attention

1. **P2: Replacing a parent directory with a symlink can still stall the hook.** `scripts/campaign_guard.py:611` passes the complete path to `CreateFileW`. `FILE_FLAG_OPEN_REPARSE_POINT` protects only the final component. Reproduce by letting `lstat` inspect `scratch/sub/file.txt`, then replacing `scratch/sub` with a directory symlink to an unresponsive UNC share before `_open_no_follow` executes. Opening traverses that parent and can block before the identity check. The deadline cannot interrupt this call. This is a source-supported race, not a live network-stall reproduction. [Go’s Windows implementation documents this limitation](https://go.googlesource.com/go.git/+/59ea07ba27f521de41bbfab78a6a12f71c2fb17c%5E%21/).

## consistency: needs-attention

2. **P2: Valid flat frontmatter fails when a blank or comment line is indented.** `scripts/verify-agent-tiers.mjs:153` rejects any following line beginning with whitespace, including lines explicitly accepted by line 125. Reproduce with:

   ```yaml
   ---
   name: a
   description: helper
     # comment
   model: sonnet
   tools: Read
   ---
   ```

   Replacing the comment with two spaces also fails. In-memory execution confirmed both validator refusals, while PyYAML parsed both as the same flat mapping. Exclude blank and comment lines from the continuation check.

## integration: needs-attention

Finding 1 leaves the Stop hook vulnerable to the stall being repaired. Finding 2 unnecessarily blocks valid agent definitions without introducing any nested values.

## source-defect: needs-attention

Finding 1 confuses final-component protection with protection throughout path resolution. Microsoft separately documents `OBJ_DONT_REPARSE` for refusing reparses encountered while resolving a path. [OBJECT_ATTRIBUTES documentation](https://learn.microsoft.com/en-us/windows/win32/api/ntdef/ns-ntdef-_object_attributes).

## design: advisory

No user-facing surface changed.

## record: advisory

Ran `python -B -m unittest tests.scripts.test_verify_agent_tiers`: five tests passed despite finding 2. Guard review was static; filesystem-writing guard tests and the live directory-replacement race were not run. No files were modified; Git status remained clean.

HIGHEST: P2
OVERALL: needs-attention

## Fixes after round 8

`python -m unittest tests.scripts.test_campaign_guard` -> `Ran 35 tests`, `OK`; `python -m unittest tests.scripts.test_verify_agent_tiers` -> `OK` (the new tests fail against `1471f97`).

- Finding 1 (P2): no file-system call can be interrupted once it blocks (a parent directory swapped for a link to a stalled share blocks `lstat` and `open` alike), so the fingerprint now runs each untracked path's inspection (`_untracked_line`) on a daemon worker and waits at most `FILE_SECONDS` (5 s); on a stall, that path and every later one count by name with no further I/O, and the blocked worker is abandoned (`test_a_stalled_path_is_outwaited_and_later_paths_count_by_name`).
- Finding 2 (P2): an indented blank or comment line after a value is allowed again; only an indented line with content counts as a continuation (`test_an_indented_blank_or_comment_line_is_allowed`).