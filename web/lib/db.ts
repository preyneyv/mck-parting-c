import postgres from "postgres";

const connectionString = process.env.DATABASE_URL ?? "postgres://prism:prism@localhost:5433/prism";
export const sql = postgres(connectionString, { max: 5 });

export async function ensureSchema() {
  await sql`
    create table if not exists leaderboard_entries (
      id bigserial primary key,
      app_id smallint not null,
      device_serial text not null,
      entry_id bigint not null,
      player_name varchar(32) not null,
      game text not null,
      summary text not null,
      sort_score double precision not null,
      metadata jsonb not null,
      raw_data bytea not null,
      created_at timestamptz not null default now(),
      unique (app_id, device_serial, entry_id)
    )
  `;
}

export type LeaderboardRow = {
  id: string;
  app_id: number;
  device_serial: string;
  player_name: string;
  game: string;
  summary: string;
  sort_score: number;
  metadata: Record<string, number | string>;
  created_at: Date;
};

export async function getLeaderboard() {
  try {
    await ensureSchema();
    return await sql<LeaderboardRow[]>`
      select id, app_id, device_serial, player_name, game, summary,
             sort_score, metadata, created_at
      from leaderboard_entries
      order by game asc, sort_score desc, created_at asc
      limit 250
    `;
  } catch {
    return [];
  }
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
