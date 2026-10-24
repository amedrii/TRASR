CREATE TABLE IF NOT EXISTS match_results (
    match_id TEXT NOT NULL,
    speedrun_user_id TEXT NOT NULL,
    player_id TEXT NOT NULL,
    level TEXT NOT NULL,
    category TEXT NOT NULL,
    subcategory TEXT NOT NULL,
    igt_milliseconds INTEGER NOT NULL,
    build_hash TEXT NOT NULL,
    confirmed_at_utc TEXT NOT NULL,
    PRIMARY KEY (match_id, speedrun_user_id)
);

CREATE INDEX IF NOT EXISTS match_results_by_match
    ON match_results (match_id);
