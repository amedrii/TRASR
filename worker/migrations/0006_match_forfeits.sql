CREATE TABLE IF NOT EXISTS match_forfeits (
    match_id TEXT NOT NULL,
    player_id TEXT NOT NULL,
    forfeited_at_utc TEXT NOT NULL,
    PRIMARY KEY (match_id, player_id)
);
