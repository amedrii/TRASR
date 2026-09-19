CREATE TABLE profiles (speedrun_user_id TEXT PRIMARY KEY, display_name TEXT NOT NULL, linked_at TEXT NOT NULL);
CREATE TABLE sessions (token_hash TEXT PRIMARY KEY, speedrun_user_id TEXT NOT NULL, created_at TEXT NOT NULL, expires_at TEXT NOT NULL);
CREATE INDEX sessions_by_user ON sessions (speedrun_user_id);
CREATE TABLE queue_entries (queue_key TEXT PRIMARY KEY, player_id TEXT NOT NULL UNIQUE, speedrun_user_id TEXT NOT NULL, queue_type TEXT NOT NULL, category TEXT, subcategory TEXT, joined_at TEXT NOT NULL);
CREATE TABLE matches (player_id TEXT PRIMARY KEY, match_json TEXT NOT NULL, created_at TEXT NOT NULL);
