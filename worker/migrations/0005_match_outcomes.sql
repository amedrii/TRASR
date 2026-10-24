CREATE TABLE IF NOT EXISTS match_outcomes (
    match_id TEXT PRIMARY KEY,
    winner_player_id TEXT NOT NULL,
    winner_igt_milliseconds INTEGER NOT NULL,
    decided_at_utc TEXT NOT NULL
);
