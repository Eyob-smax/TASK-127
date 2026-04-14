# Test Fixtures

Deterministic seed data for tests. Applied only when the test binary is launched
with the `--fixtures` flag or when CTest runs in test mode.

Fixture files are authored alongside the domain schema migrations.

## Planned fixtures

| File | Contents |
|---|---|
| `seed_users.sql` | Bootstrap admin + one account per role for auth tests |
| `seed_members.sql` | Members with active term cards, exhausted punch cards, frozen accounts |
| `seed_questions.sql` | Questions across chapters, with tags and KP mappings |
| `seed_ingestion_jobs.sql` | Jobs in various states for scheduler tests |
| `seed_audit_entries.sql` | A short valid hash chain for audit tests |
