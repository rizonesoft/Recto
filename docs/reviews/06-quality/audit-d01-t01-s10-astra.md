# Audit - D01 T01 §10: rounds under the Astra pin and review schedule

- **Audit section:** `D06 T05 §24` (`todo/06-quality/TODO-05-stamp-audits.md`)
- **Audited section:** `D01 T01 §10` (`docs/reviews/d01-t01-s10.json`, earlier process)
- **Earlier record:** `docs/reviews/06-quality/audit-d01-t01-s10.md` holds the checkpoint re-run, the completion review and GPT-6.1 Sol rounds 1 and 2, recorded before the pin's `[schedule]` existed; their fixes (`77a2506`, `aed408b`) stand. The regular rounds restart here so the schedule replays from round 1 (`docs/phase-runs/2026-10-24-phase-1.md`, Critical events).
- **Candidate:** the stamped commit `5e4c50a` and every later change to `src/pdf/document.h`, `src/pdf/document.cpp` and `tests/recto-tests/pdf_document_tests.cpp`, up to `aed408b`.
