#!/usr/bin/env node
// Enforce the delegation tier invariant on this project's agent definitions.
//
// The rule is in AGENTS.md: mechanical subtasks delegate to subagents on the
// Sonnet tier; an elevated tier is reserved for work whose verdict quality
// demands it; Haiku is allowed only for near-mechanical work (at least 90%
// mechanical) whose output a script or the main loop verifies; and no subagent
// writes: subagents read and run, and the main loop makes every change.
//
// A rule stated in prose is protected by discipline. This makes it protected by
// a check: an agent with no `model:` line, an agent that could edit files, or a
// Haiku agent that does not meet every Haiku condition below, fails here instead
// of passing every gate and being noticed later, or never.
//
// An absent `model:` is an error rather than a default, because an unpinned
// agent silently inherits the caller's model: mechanical work then runs on the
// expensive tier with no signal at all, which is the exact regression this
// exists to catch.
//
// No dependencies and no repository-specific paths, so a bootstrapped project
// can run it from day one. With no agents defined yet it passes with nothing
// checked, and starts enforcing the moment the first definition appears.
//
//   node scripts/verify-agent-tiers.mjs

import { existsSync, readdirSync, readFileSync, statSync } from "node:fs";
import { isAbsolute, join, normalize, sep } from "node:path";

/**
 * Tier vocabulary.
 *
 * Kept as substrings rather than exact model IDs so a version bump (sonnet-5
 * to a later sonnet) does not silently fail a project that is still obeying
 * the rule. The tier is the invariant; the exact pin is a routing decision that
 * belongs elsewhere. Haiku is the exception: it must be an exact, versioned pin.
 */
export const BASE_AGENT_MODEL_TOKENS = ["sonnet"];
export const ELEVATED_AGENT_MODEL_TOKENS = ["opus", "fable"];
export const RESTRICTED_AGENT_MODEL_TOKENS = ["haiku"];

/**
 * The Haiku allowance, every condition required.
 *
 * - An exact pin of Haiku 5.5 or later (`claude-haiku-5-5`, optionally with a
 *   date suffix). The bare `haiku` alias is refused because it is not a version.
 * - `mechanical_share:` of 90 or more: the share of the agent's work that is
 *   mechanical (lookups, inventories, structural checks), declared so a reviewer
 *   can challenge it.
 * - `haiku_replay:` naming a file in this repository that records a replay on
 *   known-answer inputs showing no loss against the base tier.
 * - No judgment work in its name or description: never review, audit,
 *   research, security, debugging, diagnosis, planning or design.
 */
export const HAIKU_MINIMUM_VERSION = [5, 5];
export const HAIKU_PIN = /^claude-haiku-(\d+)-(\d+)(?:-\d{8})?$/;
export const HAIKU_MECHANICAL_FIELD = "mechanical_share";
export const HAIKU_MINIMUM_MECHANICAL_SHARE = 90;
export const HAIKU_REPLAY_FIELD = "haiku_replay";
export const HAIKU_JUDGMENT_TERMS = /\b(review|audit|research|secur|debug|diagnos|plan|design|architect|judg|verdict)/i;

/** Where an elevated pin must record why it is elevated. */
export const ELEVATION_RATIONALE_FIELD = "model_rationale";

/** The one key whose value may be a nested block: Claude Code's per-agent hooks. */
export const HOOKS_FIELD = "hooks";

/** The frontmatter keys Recto's agent definitions may carry. */
const KNOWN_KEYS = new Set(["name", "description", "model", "tools", "color", HAIKU_MECHANICAL_FIELD, HAIKU_REPLAY_FIELD,
  ELEVATION_RATIONALE_FIELD, HOOKS_FIELD]);

/**
 * Tools no subagent on any tier is given. A `*` grant includes them, and so
 * does a missing `tools:` list, because the agent then inherits every tool.
 */
export const EDIT_TOOLS = ["edit", "write", "multiedit", "notebookedit"];

/**
 * Read the opening frontmatter as a strict, closed subset of YAML.
 *
 * Three review rounds on 2026-09-15 each found another way a regex over this
 * block reads a value that is not the value: a body line outside the
 * frontmatter, a trailing `# comment`, a comment inside quotes, an alias
 * (`model: *sonnet` resolving to a Haiku anchor), a `model:` line inside a
 * multi-line quoted scalar, a duplicate key read from the wrong end, and a
 * block scalar whose indentation indicator makes its only line empty. Chasing
 * them one at a time is how this file got to three rounds.
 *
 * So the reader models a deliberately small language and REFUSES everything
 * else. An agent definition's frontmatter is a handful of flat scalar keys; a
 * file using anchors, flow collections, tags or multi-line quoting is refused
 * as unreadable rather than guessed at. Failing closed on an exotic definition
 * costs whoever wrote it one plain rewrite. Failing open costs a run on the
 * wrong tier, silently, for as long as nobody looks.
 *
 * Recto reads flat `key: value` lines only (2026-10-21): no `hooks:` block and no
 * block scalar. Each review round on the nested readers found another YAML rule
 * they missed, and a reader that accepts what Claude Code's YAML loader refuses
 * passes a definition that never loads. Recto's agents use neither form; a
 * value is plain or quoted on its own line, in YAML's printable characters.
 *
 * Returns `{ fields, error }`: `error` is set when the block cannot be read
 * under these rules, and `fields` maps key to `{ value, isQuoted }`.
 */
export function readAgentFrontmatter(text) {
  const match = /^---[ \t]*\r?\n([\s\S]*?)\r?\n---[ \t]*(?:\r?\n|$)/.exec(text);
  if (!match) return { fields: null, error: "has no opening frontmatter block; the tier pin must be a machine-read field, not prose" };

  // YAML admits only printable characters (tab, line breaks, and the printable ranges); a control character or a lone
  // surrogate makes the loader refuse the whole file.
  // A bare CR, NEL, LS or PS ends a line for some YAML loaders and not for this reader (which splits on LF and CRLF),
  // so it is refused rather than read two ways.
  const unprintable = /[\x00-\x08\x0B\x0C\x0E-\x1F\x7F-\x9F\u2028\u2029\uFFFE\uFFFF]|\r(?!\n)|\p{Cs}/u.exec(match[1]);
  if (unprintable) {
    return { fields: null, error: `frontmatter holds U+${unprintable[0].codePointAt(0).toString(16).toUpperCase().padStart(4, "0")}, a character YAML refuses` };
  }

  const lines = match[1].split(/\r?\n/);
  const fields = new Map();
  let index = 0;

  while (index < lines.length) {
    const line = lines[index];
    index += 1;
    // Blank means spaces and tabs only (YAML 5.5): a no-break or other Unicode space is content to YAML.
    if (/^[ \t]*$/.test(line) || /^[ \t]*#/.test(line)) continue;

    // YAML needs whitespace after the colon in a block mapping: `model:sonnet`
    // is the plain scalar "model:sonnet", not a pin, and reading it as one let
    // a definition with no model field through.
    // Spaces only between the colon and the value: some YAML loaders refuse a tab there, and this check must not
    // accept a line the agent loader cannot read.
    const entry = /^([A-Za-z_][A-Za-z0-9_-]*):(?:[ ]+([^\r\n]*?))?[ \t]*$/.exec(line);
    if (!entry) {
      return { fields: null, error: `frontmatter line ${JSON.stringify(line)} is not a plain "key: value" pair; agent frontmatter is read as a closed subset of YAML and anything else is refused rather than guessed` };
    }

    const [, key, rest = ""] = entry;
    // Only the keys Recto's agents and this check use: an unknown key is refused, which also keeps every key far below
    // YAML's 1024-character limit for an implicit key.
    if (!KNOWN_KEYS.has(key)) {
      return { fields: null, error: `declares "${key.slice(0, 40)}:", a key Recto's agent definitions do not use (${[...KNOWN_KEYS].join(", ")})` };
    }
    if (fields.has(key)) {
      // This reader takes the first, a YAML parser takes the last. Refuse the
      // ambiguity rather than pick a winner: `model: sonnet` above
      // `model: claude-haiku-4-5` read as Sonnet and ran as Haiku.
      return { fields: null, error: `declares "${key}:" more than once; a duplicate key is read from opposite ends by this check and by YAML` };
    }

    if (key === HOOKS_FIELD) {
      return { fields: null, error: `"${key}:" is a nested block, and this check reads flat "key: value" lines only; Recto's agents declare no hooks, so one is refused rather than half-read` };
    }
    if (/^[ \t]/.test(lines[index] ?? "")) {
      return { fields: null, error: `"${key}:" is followed by an indented line ${JSON.stringify(lines[index])}; this check reads flat "key: value" lines only` };
    }

    // A comment after the value is ordinary YAML, and rejecting it would fail
    // correct definitions. It is stripped here, outside the quoted region, so
    // `model: "sonnet # haiku"` still keeps its hash as content.
    // A quoted value is one run of characters between its quotes with no quote or backslash inside (YAML would end
    // the scalar at an inner quote, or decode an escape this check does not), then optionally a comment.
    const quotedWithComment = /^("[^"\\]*"|'[^'\\]*')[ \t]+#[^\r\n]*$/.exec(rest);
    const value = quotedWithComment?.[1] ?? rest;

    if (/^[|>]/.test(value)) {
      return { fields: null, error: `"${key}:" uses a block scalar (${JSON.stringify(value)}); this check reads flat "key: value" lines only, so write the value on one line` };
    }

    const quoted = /^(["'])((?:[^\\]|\\.)*?)\1[ \t]*$/.exec(value);
    if (quoted && quoted[2].includes(quoted[1])) {
      return { fields: null, error: `"${key}:" has a quote inside its quoted value (${JSON.stringify(value)}); YAML ends the scalar there` };
    }
    if (quoted) {
      if (/\\/.test(quoted[2])) {
        return { fields: null, error: `"${key}:" contains a backslash escape; this check does not decode escapes, so write the value plainly` };
      }
      fields.set(key, { value: quoted[2].trim(), isQuoted: true });
      continue;
    }

    if (/^["']/.test(value)) {
      return { fields: null, error: `"${key}:" opens a quote that does not close on the same line; a multi-line quoted scalar can hide another key inside it` };
    }

    const plain = value.replace(/(?:^|[ \t])#.*$/, "").trim();
    if (/^[&*!]/.test(plain) || /^[[{]/.test(plain)) {
      return { fields: null, error: `"${key}:" uses YAML ${plain.startsWith("[") || plain.startsWith("{") ? "flow collection" : "anchor, alias or tag"} syntax (${JSON.stringify(plain)}); write the value plainly, because an alias resolves to something this check cannot see` };
    }
    // A plain scalar YAML can read: it does not start with an indicator (a leading -, ? or : only when a non-space
    // character follows), and holds no ": " or trailing ":", which YAML reads as a nested mapping and refuses. A line
    // this check accepted but the agent loader cannot parse would pass here and break at load time.
    if (/^[,\[\]{}#&*!|>'"%@`]/.test(plain) || /^[-?:](?:[ \t]|$)/.test(plain) || /:(?:[ \t]|$)/.test(plain)) {
      return { fields: null, error: `"${key}:" has a plain value YAML cannot read (${JSON.stringify(plain)}); quote it` };
    }
    fields.set(key, { value: plain, isQuoted: false });
  }

  return { fields, error: null };
}

/** A tier pin: one bare token, optionally quoted, and nothing else. */
export function tierPin(fields, field) {
  const entry = fields.get(field);
  if (!entry || entry.value === "") return null;
  return /^[A-Za-z0-9][A-Za-z0-9._-]*$/.test(entry.value) ? entry.value : null;
}

/**
 * A recorded justification: prose that survives unquoting and trimming.
 *
 * An unquoted `null` or `~` is YAML's way of writing nothing, so it records
 * nothing. Quoted, it is a word someone chose to write, and stays content.
 */
export function recordedProse(fields, field) {
  const entry = fields.get(field);
  if (!entry) return null;
  const value = entry.value.trim();
  if (value === "") return null;
  if (!entry.isQuoted && /^(?:null|Null|NULL|~)$/.test(value)) return null;
  return value;
}

export function verifyAgentTiers(repoRoot = process.cwd(), relativeRoot = join(".claude", "agents")) {
  const errors = [];
  const absoluteRoot = join(repoRoot, relativeRoot);
  if (!existsSync(absoluteRoot)) return { checked: 0, elevated: 0, errors };

  const files = readdirSync(absoluteRoot, { withFileTypes: true })
    .filter((entry) => entry.isFile() && entry.name.endsWith(".md"))
    .map((entry) => entry.name)
    .sort();

  let elevated = 0;
  let haiku = 0;
  for (const name of files) {
    const relative = join(relativeRoot, name);
    const text = readFileSync(join(absoluteRoot, name), "utf8");
    const { fields, error } = readAgentFrontmatter(text);

    if (error) {
      errors.push(`${relative} ${error}`);
      continue;
    }

    errors.push(...toolProblems(fields).map((problem) => `${relative} ${problem}`));

    const model = tierPin(fields, "model")?.toLowerCase() ?? null;

    if (!model) {
      errors.push(
        `${relative} must declare an explicit "model:" field in its frontmatter carrying one bare tier token; an unpinned or unparseable pin inherits the caller's model`,
      );
      continue;
    }

    if (RESTRICTED_AGENT_MODEL_TOKENS.some((token) => model.includes(token))) {
      const problems = haikuProblems(repoRoot, fields, model);
      if (problems.length > 0) {
        errors.push(...problems.map((problem) => `${relative} pins "${model}": ${problem}`));
      } else {
        haiku += 1;
      }
      continue;
    }

    const isBase = BASE_AGENT_MODEL_TOKENS.some((token) => model.includes(token));
    const isElevated = ELEVATED_AGENT_MODEL_TOKENS.some((token) => model.includes(token));
    if (!isBase && !isElevated) {
      errors.push(`${relative} pins unrecognized tier "${model}"; use the base tier, or an elevated tier with a recorded rationale`);
      continue;
    }

    if (isElevated) {
      elevated += 1;
      // An elevated pin has to be argued for. Without this the expensive tier
      // becomes the path of least resistance and drifts upward silently.
      if (!recordedProse(fields, ELEVATION_RATIONALE_FIELD)) {
        errors.push(
          `${relative} pins the elevated tier "${model}" without a non-empty "${ELEVATION_RATIONALE_FIELD}:" field; `
          + "an elevated agent must be a deliberate decision, not a default that drifted",
        );
      }
    }
  }

  return { checked: files.length, elevated, haiku, errors };
}

/**
 * A subagent reads and runs; the main loop writes. A grant such as
 * `Edit(src/**)` is still Edit, so the rule spec after the name is ignored.
 */
export function toolProblems(fields) {
  const tools = recordedProse(fields, "tools");
  if (!tools) {
    return ['needs an explicit "tools:" list; without one the agent inherits every tool, edit tools included'];
  }
  const names = tools.split(",").map((tool) => tool.replace(/\(.*$/, "").trim().toLowerCase());
  if (names.some((tool) => tool === "*" || EDIT_TOOLS.includes(tool))) {
    return ["grants edit tools (Edit, Write, MultiEdit, NotebookEdit or *); subagents read and run, and the main loop writes"];
  }
  return [];
}

/** Every unmet Haiku condition, so one run names them all. */
export function haikuProblems(repoRoot, fields, model) {
  const problems = [];
  const pin = HAIKU_PIN.exec(model);
  const [minMajor, minMinor] = HAIKU_MINIMUM_VERSION;
  if (!pin || Number(pin[1]) < minMajor || (Number(pin[1]) === minMajor && Number(pin[2]) < minMinor)) {
    problems.push("Haiku must be an exact pin of claude-haiku-5-5 or later, never the bare alias or an older version");
  }

  const share = recordedProse(fields, HAIKU_MECHANICAL_FIELD);
  if (!share || !/^\d{1,3}$/.test(share) || Number(share) < HAIKU_MINIMUM_MECHANICAL_SHARE || Number(share) > 100) {
    problems.push(
      `Haiku needs "${HAIKU_MECHANICAL_FIELD}:" as a whole percentage from ${HAIKU_MINIMUM_MECHANICAL_SHARE} to 100; `
      + "Haiku is only for near-mechanical work",
    );
  }

  const replay = recordedProse(fields, HAIKU_REPLAY_FIELD);
  const replayPath = replay ? normalize(replay) : null;
  const inside = replayPath && !isAbsolute(replayPath) && replayPath !== ".." && !replayPath.startsWith(`..${sep}`);
  if (!inside || !existsSync(join(repoRoot, replayPath)) || !statSync(join(repoRoot, replayPath)).isFile()) {
    problems.push(
      `Haiku needs "${HAIKU_REPLAY_FIELD}:" naming a file in this repository that records a known-answer replay `
      + "showing no loss against the base tier",
    );
  }

  const purpose = `${recordedProse(fields, "name") ?? ""} ${recordedProse(fields, "description") ?? ""}`;
  const judgment = HAIKU_JUDGMENT_TERMS.exec(purpose);
  if (judgment) {
    problems.push(
      `Haiku never does judgment work, and this agent's name or description says "${judgment[0]}"; `
      + "review, audit, research, security, debugging, diagnosis, planning and design stay on Sonnet or above",
    );
  }
  return problems;
}

if (process.argv[1]?.endsWith("verify-agent-tiers.mjs")) {
  const result = verifyAgentTiers();
  if (result.errors.length > 0) {
    process.stderr.write(`Agent tier invariant failed:\n${result.errors.map((line) => `  - ${line}`).join("\n")}\n`);
    process.exit(1);
  }
  process.stdout.write(
    `Agent tiers valid: ${result.checked} definition(s) checked, ${result.elevated} elevated with a recorded rationale, `
    + `${result.haiku} on Haiku under the mechanical-work allowance.\n`,
  );
}
