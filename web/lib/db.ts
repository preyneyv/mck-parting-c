import postgres from "postgres";

const connectionString = process.env.DATABASE_URL ?? "postgres://prism:prism@localhost:5433/prism";
export const sql = postgres(connectionString, { max: 5 });

let schemaReady: Promise<void> | undefined;

async function migrateSchema() {
  await sql`
    create table if not exists leaderboard_entries (
      id bigserial primary key,
      app_id smallint not null,
      device_serial text not null,
      entry_id bigint not null,
      player_name varchar(32) not null,
      raw_data bytea not null,
      created_at timestamptz not null default now(),
      morse_letters bigint,
      morse_errors bigint,
      asteroids_elapsed_ms bigint,
      asteroids_distance bigint,
      beatline_track smallint,
      beatline_track_id numeric(20, 0),
      beatline_chart_id numeric(20, 0),
      beatline_difficulty smallint,
      beatline_rank smallint,
      beatline_score bigint,
      beatline_max_combo integer,
      beatline_perfect integer,
      beatline_good integer,
      beatline_bad smallint,
      beatline_miss smallint,
      unique (app_id, device_serial, entry_id)
    )
  `;

  // Upgrade the original JSON-backed development schema in place. The raw
  // cartridge bytes remain the authority for every decoded value.
  await sql`
    alter table leaderboard_entries
      add column if not exists morse_letters bigint,
      add column if not exists morse_errors bigint,
      add column if not exists asteroids_elapsed_ms bigint,
      add column if not exists asteroids_distance bigint,
      add column if not exists beatline_track smallint,
      add column if not exists beatline_track_id numeric(20, 0),
      add column if not exists beatline_chart_id numeric(20, 0),
      add column if not exists beatline_difficulty smallint,
      add column if not exists beatline_rank smallint,
      add column if not exists beatline_score bigint,
      add column if not exists beatline_max_combo integer,
      add column if not exists beatline_perfect integer,
      add column if not exists beatline_good integer,
      add column if not exists beatline_bad smallint,
      add column if not exists beatline_miss smallint
  `;

  await sql`
    alter table leaderboard_entries
      alter column beatline_track_id type numeric(20, 0) using beatline_track_id::numeric,
      alter column beatline_chart_id type numeric(20, 0) using beatline_chart_id::numeric
  `;

  await sql`
    update leaderboard_entries
    set morse_letters = get_byte(raw_data, 0)::bigint
                        + (get_byte(raw_data, 1)::bigint << 8)
                        + (get_byte(raw_data, 2)::bigint << 16)
                        + (get_byte(raw_data, 3)::bigint << 24),
        morse_errors = get_byte(raw_data, 4)::bigint
                       + (get_byte(raw_data, 5)::bigint << 8)
                       + (get_byte(raw_data, 6)::bigint << 16)
                       + (get_byte(raw_data, 7)::bigint << 24)
    where app_id = 1 and octet_length(raw_data) = 8
  `;

  await sql`
    update leaderboard_entries
    set asteroids_elapsed_ms = get_byte(raw_data, 0)::bigint
                               + (get_byte(raw_data, 1)::bigint << 8)
                               + (get_byte(raw_data, 2)::bigint << 16)
                               + (get_byte(raw_data, 3)::bigint << 24),
        asteroids_distance = get_byte(raw_data, 4)::bigint
                             + (get_byte(raw_data, 5)::bigint << 8)
                             + (get_byte(raw_data, 6)::bigint << 16)
                             + (get_byte(raw_data, 7)::bigint << 24)
    where app_id = 2 and octet_length(raw_data) = 8
  `;

  await sql`
    update leaderboard_entries
    set beatline_track = (get_byte(raw_data, 0) >> 1)::smallint,
        beatline_difficulty = (get_byte(raw_data, 0) & 1)::smallint,
        beatline_rank = get_byte(raw_data, 1)::smallint,
        beatline_score = get_byte(raw_data, 2)::bigint
                         + (get_byte(raw_data, 3)::bigint << 8)
                         + (get_byte(raw_data, 4)::bigint << 16)
                         + (get_byte(raw_data, 5)::bigint << 24),
        beatline_max_combo = get_byte(raw_data, 6)
                             + (get_byte(raw_data, 7) << 8),
        beatline_perfect = get_byte(raw_data, 8)
                           + (get_byte(raw_data, 9) << 8),
        beatline_good = get_byte(raw_data, 10)
                        + (get_byte(raw_data, 11) << 8),
        beatline_bad = get_byte(raw_data, 12)::smallint,
        beatline_miss = get_byte(raw_data, 13)::smallint
    where app_id = 5 and octet_length(raw_data) = 14
  `;

  // Preserve development scores created with the retired track-index payload.
  // Track 0 was Never Gonna and track 1 was Golden.
  await sql`
    update leaderboard_entries
    set beatline_track_id = case beatline_track when 0 then 1 when 1 then 2 end,
        beatline_chart_id = case
          when beatline_track = 0 and beatline_difficulty = 0 then 1
          when beatline_track = 0 and beatline_difficulty = 1 then 2
          when beatline_track = 1 and beatline_difficulty = 0 then 3
          when beatline_track = 1 and beatline_difficulty = 1 then 4
        end
    where app_id = 5 and octet_length(raw_data) = 14
  `;

  await sql`
    alter table leaderboard_entries
      drop column if exists game,
      drop column if exists summary,
      drop column if exists sort_score,
      drop column if exists metadata,
      drop column if exists beatline_track
  `;

  await sql`create index if not exists leaderboard_morse_rank on leaderboard_entries (morse_letters desc, morse_errors asc, created_at asc) where app_id = 1`;
  await sql`create index if not exists leaderboard_asteroids_rank on leaderboard_entries (asteroids_distance desc, asteroids_elapsed_ms asc, created_at asc) where app_id = 2`;
  await sql`drop index if exists leaderboard_beatline_rank`;
  await sql`create index leaderboard_beatline_rank on leaderboard_entries (beatline_chart_id, beatline_score desc, beatline_max_combo desc, created_at asc) where app_id = 5`;
}

export function ensureSchema() {
  if (!schemaReady) {
    schemaReady = migrateSchema().catch((error) => {
      schemaReady = undefined;
      throw error;
    });
  }
  return schemaReady;
}

type BaseLeaderboardRow = {
  id: string;
  player_name: string;
  created_at: Date;
};

export type MorseLeaderboardRow = BaseLeaderboardRow & {
  letters: number;
  errors: number;
};

export type AsteroidsLeaderboardRow = BaseLeaderboardRow & {
  elapsed_ms: number;
  distance: number;
};

export type BeatlineLeaderboardRow = BaseLeaderboardRow & {
  track_id: string;
  chart_id: string;
  difficulty: number;
  rank: number;
  score: number;
  max_combo: number;
  perfect: number;
  good: number;
  bad: number;
  miss: number;
};

export type Leaderboards = {
  morse: MorseLeaderboardRow[];
  asteroids: AsteroidsLeaderboardRow[];
  beatline: BeatlineLeaderboardRow[];
};

export async function getLeaderboards(): Promise<Leaderboards> {
  await ensureSchema();

  const [morse, asteroids, beatline] = await Promise.all([
    sql<MorseLeaderboardRow[]>`
      select id::text, player_name, created_at,
             morse_letters::double precision as letters,
             morse_errors::double precision as errors
      from leaderboard_entries
      where app_id = 1 and morse_letters is not null and morse_errors is not null
      order by morse_letters desc, morse_errors asc, created_at asc
      limit 250
    `,
    sql<AsteroidsLeaderboardRow[]>`
      select id::text, player_name, created_at,
             asteroids_elapsed_ms::double precision as elapsed_ms,
             asteroids_distance::double precision as distance
      from leaderboard_entries
      where app_id = 2 and asteroids_elapsed_ms is not null and asteroids_distance is not null
      order by asteroids_distance desc, asteroids_elapsed_ms asc, created_at asc
      limit 250
    `,
    sql<BeatlineLeaderboardRow[]>`
      select id::text, player_name, created_at,
             beatline_track_id::text as track_id,
             beatline_chart_id::text as chart_id,
             beatline_difficulty as difficulty,
             beatline_rank as rank, beatline_score::double precision as score,
             beatline_max_combo as max_combo, beatline_perfect as perfect,
             beatline_good as good, beatline_bad as bad, beatline_miss as miss
      from leaderboard_entries
      where app_id = 5 and beatline_score is not null
      order by beatline_score desc, beatline_max_combo desc, created_at asc
      limit 250
    `,
  ]);

  return { morse, asteroids, beatline };
}

export async function getLatestName(deviceSerial: string) {
  try {
    await ensureSchema();
    const rows = await sql<{ player_name: string }[]>`
      select player_name from leaderboard_entries
      where device_serial = ${deviceSerial}
      order by created_at desc limit 1
    `;
    return rows[0]?.player_name ?? "";
  } catch {
    return "";
  }
}
