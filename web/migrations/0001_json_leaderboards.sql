CREATE TABLE leaderboard_entries (
  id              INTEGER PRIMARY KEY AUTOINCREMENT,
  app_id          INTEGER NOT NULL,
  device_serial   TEXT NOT NULL,
  entry_id        INTEGER NOT NULL,
  player_name     TEXT NOT NULL,
  raw_data        BLOB NOT NULL,

  result_version  INTEGER NOT NULL,
  result_json     TEXT NOT NULL CHECK (json_valid(result_json)),

  scope_key       TEXT NOT NULL DEFAULT '',
  rank_primary    INTEGER NOT NULL,
  rank_secondary  INTEGER NOT NULL DEFAULT 0,

  created_at      TEXT NOT NULL DEFAULT (
    strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
  ),

  UNIQUE (app_id, device_serial, entry_id)
) STRICT;

CREATE INDEX leaderboard_rank
ON leaderboard_entries (
  app_id,
  scope_key,
  rank_primary DESC,
  rank_secondary DESC,
  created_at ASC
);
