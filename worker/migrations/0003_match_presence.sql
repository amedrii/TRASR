CREATE TABLE IF NOT EXISTS match_presence (
    match_id TEXT NOT NULL,
    player_id TEXT NOT NULL,
    state TEXT NOT NULL,
    igt_milliseconds INTEGER,
    updated_at_utc TEXT NOT NULL,
    PRIMARY KEY (match_id, player_id)
);

CREATE INDEX IF NOT EXISTS match_presence_by_match
    ON match_presence (match_id);
