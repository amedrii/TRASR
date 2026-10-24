CREATE TABLE IF NOT EXISTS match_navigation_schedule (
    match_id TEXT PRIMARY KEY,
    start_at_utc TEXT NOT NULL,
    created_at_utc TEXT NOT NULL
);
