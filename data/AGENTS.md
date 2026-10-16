# data/

## What Lives Here

Static plugin assets. Currently contains localization strings (`locale/en-US.ini`) for user-facing labels and tooltips.

## Key Files

| File | Purpose |
|------|---------|
| `locale/en-US.ini` | English localization strings for the plugin UI |

## Deviations from Root

No deviations.

## Dependencies & Side Effects

- New UI strings added in `src/` should have corresponding entries in `locale/en-US.ini`.
- Additional locales can be added as `locale/<lang>.ini` files.
- The CMake install step packages runtime assets only. This `AGENTS.md` file stays in source control and must not be copied into user-facing plugin data or macOS bundles.
