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