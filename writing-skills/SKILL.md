---
name: writing-skills
description: "Use when a GitHub link is supplied and you need to explore the current repository for reuse, inspiration, or guidance on building a new skill. Triggers on github-link, repo-analysis, inspiration, skill-writing, documentation-guidance."
metadata:
  category: technique
  triggers: github-link, repo-analysis, inspiration, skill-writing, documentation-guidance
---

# Writing Skills (Excellence)

## Goal
- Turn a dropped GitHub URL into a focused investigation of this repo's code, docs, and conventions so you can either re-use existing patterns or document new work in the same voice.

## Quick Decision Tree
1. **Create a NEW skill**
   - Single file and < 200 lines → follow the *Tier 1 Architecture* path.
   - Multi-concept (200–1000 lines) → extend around the *Tier 2 Architecture* layout.
   - Massive platform (10+ products, AWS, Convex, etc.) → default to *Tier 3 Architecture*.
2. **Improve or expand an EXISTING skill**
   - If the reviewer says “it is too long,” modularize by splitting into a Tier 3 pattern set.
   - If the agent keeps ignoring instructions, layer an *Anti-Rationalization* discipline section.
   - If discovery is poor, tune the metadata with a *CSO (Search Optimization)* write-up.
3. **Verify Compliance**
   - Run the *Standards* checklist (metadata, naming, directory layout).
   - Add examples or regression tests according to the *Testing* guide.

## Component Index
| Component | Purpose |
|-----------|---------|
| **CSO** | Provide SEO-friendly triggers and discovery keywords so the skill surfaces when relevant symptoms occur.
| **Standards** | Ensure names, front matter, and folder layout match the repository conventions.
| **Anti-Rationalization** | Lock down agent behavior so it cannot sidestep the instructions.
| **Testing** | Prove the skill works by exercising the happy path and at least one failure case.

## Templates (pick one before writing)
- [Technique Skill](references/templates/technique.md)
- [Reference Skill](references/templates/reference.md)
- [Discipline Skill](references/templates/discipline.md)
- [Pattern Skill](references/templates/pattern.md)
- Use the Tier 3 template (`references/templates/tier-3-platform.md`) when you need to split a bulky rule book.

## Workflow
1. **Identify the ask**: a GitHub link + a desire for usage/inspiration means this skill should inspect the repo, note relevant files, and surface any similar patterns.
2. **Select the tier**: run the decision tree, document assumptions, and choose the simplest architecture that still captures the knowledge.
3. **Scan the repo**: search for keywords mentioned in the link (e.g., feature name, API, pattern) and note where similar code already lives.
4. **Write the skill**: follow the chosen template, include `Use when...` triggers, and mention the matched files in the narrative.
5. **Apply CSO**: add 3+ metadata triggers that the repo search would naturally hit (url, repo name, file path, key concepts).
6. **Add Anti-Rationalization**: especially for discipline skills, add guardrails that force the agent to obey (e.g., “do this before that”).
7. **Test**: verify the sample GitHub link (or a close analog) leads you back to this very repository and produces actionable takeaways.

## Quick Example
```
---
name: my-technique
description: Use when [specific symptom occurs].
metadata:
  category: technique
  triggers: error-text, symptom, tool-name
---

# My Technique

## When to Use
- [Symptom A]
- [Error message]
```

## Common Mistakes
| Mistake | Fix |
|---------|-----|
| Description summarizes workflow | Start the description with “Use when…” and explain the trigger conditions. |
| No `metadata.triggers` | Add at least three searchable keywords that would match the GitHub link terms. |
| Generic name (“helper”) | Re-name to a gerund form (`creating-skills`) to reflect the skill’s behavior. |
| Skill is too long | Split content into `references/` sub-documents and keep `SKILL.md` under 500 lines. |

## Pre-Deploy Checklist
- [ ] `name` matches the directory name exactly.
- [ ] `SKILL.md` is uppercase.
- [ ] Description opens with “Use when…”.
- [ ] `metadata.triggers` lists 3+ keywords.
- [ ] Document stays < 500 lines (move extras to `references/`).
- [ ] No `@` import references in cross-links.
- [ ] Run a representative scenario from the GitHub link to prove the skill fires.

## Repo Inspiration Notes
- Whenever a GitHub URL points back to this project (or a similar OBS plugin), include a short section that points to files or sections already doing something similar and state whether they can be reused.
