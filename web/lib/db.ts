import {
  createClient,
  type Client,
  type Row,
} from "@libsql/client/web";

const RESULT_VERSION = 1;

type StoredLeaderboardRow = {
  id: number;
  player_name: string;
  result_version: number;
  result_json: string;
  created_at: string;
};

type MorseResult = {
  letters: number;
  errors: number;
};

type AsteroidsResult = {
  elapsedMs: number;
  distance: number;
};

type BeatlineResult = {
  trackId: string;
  chartId: string;
  difficulty: number;
  rank: number;
  score: number;
  maxCombo: number;
  perfect: number;
  good: number;
  bad: number;
  miss: number;
};

export type MorseLeaderboardRow = {
  id: string;
  player_name: string;
  created_at: Date;
  letters: number;
  errors: number;
};

export type AsteroidsLeaderboardRow = {
  id: string;
  player_name: string;
  created_at: Date;
  elapsed_ms: number;
  distance: number;
};

export type BeatlineLeaderboardRow = {
  id: string;
  player_name: string;
  created_at: Date;
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

export type LeaderboardKey = keyof Leaderboards;

let database: Client | undefined;

export function getDatabase() {
  if (database) return database;

  const url = process.env.TURSO_DATABASE_URL;
  if (!url) {
    throw new Error("TURSO_DATABASE_URL is not configured.");
  }

  database = createClient({
    url,
    authToken: process.env.TURSO_AUTH_TOKEN,
  });
  return database;
}

function integerValue(value: unknown, field: string) {
  const number = typeof value === "bigint" ? Number(value) : value;
  if (!isFiniteInteger(number)) {
    throw new Error(`Stored leaderboard ${field} is invalid.`);
  }
  return number;
}

function stringValue(value: unknown, field: string) {
  if (typeof value !== "string") {
    throw new Error(`Stored leaderboard ${field} is invalid.`);
  }
  return value;
}

function storedLeaderboardRow(row: Row): StoredLeaderboardRow {
  return {
    id: integerValue(row.id, "id"),
    player_name: stringValue(row.player_name, "player name"),
    result_version: integerValue(row.result_version, "result version"),
    result_json: stringValue(row.result_json, "result JSON"),
    created_at: stringValue(row.created_at, "creation time"),
  };
}

function isFiniteInteger(value: unknown): value is number {
  return typeof value === "number" && Number.isSafeInteger(value);
}

function parseJsonObject(value: string): Record<string, unknown> {
  const parsed: unknown = JSON.parse(value);
  if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
    throw new Error("Stored leaderboard result is not an object.");
  }
  return parsed as Record<string, unknown>;
}

function parseMorseResult(row: StoredLeaderboardRow): MorseResult {
  if (row.result_version !== RESULT_VERSION) {
    throw new Error(`Unsupported Morse result version ${row.result_version}.`);
  }
  const result = parseJsonObject(row.result_json);
  if (!isFiniteInteger(result.letters) || !isFiniteInteger(result.errors)) {
    throw new Error("Stored Morse result is invalid.");
  }
  return { letters: result.letters, errors: result.errors };
}

function parseAsteroidsResult(row: StoredLeaderboardRow): AsteroidsResult {
  if (row.result_version !== RESULT_VERSION) {
    throw new Error(`Unsupported Asteroids result version ${row.result_version}.`);
  }
  const result = parseJsonObject(row.result_json);
  if (!isFiniteInteger(result.elapsedMs) || !isFiniteInteger(result.distance)) {
    throw new Error("Stored Asteroids result is invalid.");
  }
  return { elapsedMs: result.elapsedMs, distance: result.distance };
}

function parseBeatlineResult(row: StoredLeaderboardRow): BeatlineResult {
  if (row.result_version !== RESULT_VERSION) {
    throw new Error(`Unsupported Beatline result version ${row.result_version}.`);
  }
  const result = parseJsonObject(row.result_json);
  const numericKeys = [
    "difficulty",
    "rank",
    "score",
    "maxCombo",
    "perfect",
    "good",
    "bad",
    "miss",
  ] as const;
  if (
    typeof result.trackId !== "string" ||
    typeof result.chartId !== "string" ||
    numericKeys.some((key) => !isFiniteInteger(result[key]))
  ) {
    throw new Error("Stored Beatline result is invalid.");
  }
  return {
    trackId: result.trackId,
    chartId: result.chartId,
    difficulty: result.difficulty as number,
    rank: result.rank as number,
    score: result.score as number,
    maxCombo: result.maxCombo as number,
    perfect: result.perfect as number,
    good: result.good as number,
    bad: result.bad as number,
    miss: result.miss as number,
  };
}

function baseRow(row: StoredLeaderboardRow) {
  return {
    id: String(row.id),
    player_name: row.player_name,
    created_at: new Date(row.created_at),
  };
}

async function getMorseLeaderboard(): Promise<MorseLeaderboardRow[]> {
  const { rows } = await getDatabase().execute({
    sql: `SELECT id, player_name, result_version, result_json, created_at
          FROM leaderboard_entries
          WHERE app_id = ? AND scope_key = ''
          ORDER BY rank_primary DESC, rank_secondary DESC, created_at ASC
          LIMIT 250`,
    args: [1],
  });

  return rows.map(storedLeaderboardRow).map((row) => {
    const result = parseMorseResult(row);
    return { ...baseRow(row), letters: result.letters, errors: result.errors };
  });
}

async function getAsteroidsLeaderboard(): Promise<AsteroidsLeaderboardRow[]> {
  const { rows } = await getDatabase().execute({
    sql: `SELECT id, player_name, result_version, result_json, created_at
          FROM leaderboard_entries
          WHERE app_id = ? AND scope_key = ''
          ORDER BY rank_primary DESC, rank_secondary DESC, created_at ASC
          LIMIT 250`,
    args: [2],
  });

  return rows.map(storedLeaderboardRow).map((row) => {
    const result = parseAsteroidsResult(row);
    return {
      ...baseRow(row),
      elapsed_ms: result.elapsedMs,
      distance: result.distance,
    };
  });
}

async function getBeatlineLeaderboard(
  chartId?: string,
): Promise<BeatlineLeaderboardRow[]> {
  const { rows } = await getDatabase().execute({
    sql: chartId
      ? `SELECT id, player_name, result_version, result_json, created_at
         FROM leaderboard_entries
         WHERE app_id = ? AND scope_key = ?
         ORDER BY rank_primary DESC, rank_secondary DESC, created_at ASC
         LIMIT 250`
      : `SELECT id, player_name, result_version, result_json, created_at
         FROM leaderboard_entries
         WHERE app_id = ?
         ORDER BY rank_primary DESC, rank_secondary DESC, created_at ASC
         LIMIT 250`,
    args: chartId ? [5, chartId] : [5],
  });

  return rows.map(storedLeaderboardRow).map((row) => {
    const result = parseBeatlineResult(row);
    return {
      ...baseRow(row),
      track_id: result.trackId,
      chart_id: result.chartId,
      difficulty: result.difficulty,
      rank: result.rank,
      score: result.score,
      max_combo: result.maxCombo,
      perfect: result.perfect,
      good: result.good,
      bad: result.bad,
      miss: result.miss,
    };
  });
}

export async function getLeaderboard(
  key: "morse",
): Promise<MorseLeaderboardRow[]>;
export async function getLeaderboard(
  key: "asteroids",
): Promise<AsteroidsLeaderboardRow[]>;
export async function getLeaderboard(
  key: "beatline",
  chartId?: string,
): Promise<BeatlineLeaderboardRow[]>;
export async function getLeaderboard(
  key: LeaderboardKey,
  chartId?: string,
): Promise<
  MorseLeaderboardRow[] | AsteroidsLeaderboardRow[] | BeatlineLeaderboardRow[]
> {
  if (key === "morse") return getMorseLeaderboard();
  if (key === "asteroids") return getAsteroidsLeaderboard();
  return getBeatlineLeaderboard(chartId);
}

export async function getLatestName(deviceSerial: string) {
  try {
    const { rows } = await getDatabase().execute({
      sql: `SELECT player_name
            FROM leaderboard_entries
            WHERE device_serial = ?
            ORDER BY created_at DESC
            LIMIT 1`,
      args: [deviceSerial],
    });
    return rows.length > 0
      ? stringValue(rows[0].player_name, "player name")
      : "";
  } catch {
    return "";
  }
}
