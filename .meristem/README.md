<!-- meristem-template:v1 -->
# Meristem Project State

This directory holds durable, repository-scoped project context that should survive chats, agents, devices, and time **and is intended to be safe to commit with the repository**.

## Authority map

- `PROJECT.md` — stable project purpose, architecture boundaries, invariants, and verification expectations.
- `DECISIONS.md` — consequential decisions whose rationale matters for future work.
- `LEARNINGS.md` — reusable project-local lessons supported by evidence.
- `ERRORS.md` — costly or recurring failures worth preserving so they are not rediscovered.
- `FEATURE_REQUESTS.md` — requested capabilities that are not yet implemented.

Current execution state does **not** belong here by default. Keep ordinary session detail in the active interaction. Create or update `../ACTIVE_WORK.md` only when ongoing work is multi-step, cross-session, cross-agent, or expensive to reconstruct.

## Privacy and local state

Canonical files in this directory are commit-safe project knowledge. Do not place credentials, tokens, personal/private continuity, raw conversations, or machine-specific paths/configuration in them.

Use `local/` for repo-local private or machine-specific state. The initializer keeps `.meristem/local/` ignored without ignoring the canonical `.meristem/` project files. Universal user/persona continuity belongs outside this repository.

## Evidence and authority

Current repository/runtime truth outranks stale notes. User decisions outrank inferred preferences. Recurrence nominates a lesson for review; it does not automatically grant authority to rewrite project instructions.

Do not store credentials, tokens, secrets, personal/private continuity, raw conversations, machine-specific configuration, or unnecessary sensitive data in commit-safe Meristem files.
