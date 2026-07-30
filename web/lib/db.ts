import { getCloudflareContext } from "@opennextjs/cloudflare";

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

export function getDatabase() {
  return getCloudflareContext().env.DB;
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
  const { results } = await getDatabase()
    .prepare(
      `SELECT id, player_name, result_version, result_json, created_at
       FROM leaderboard_entries
       WHERE app_id = ?1 AND scope_key = ''
       ORDER BY rank_primary DESC, rank_secondary DESC, created_at ASC
       LIMIT 250`,
    )
    .bind(1)
    .all<StoredLeaderboardRow>();

  return results.map((row) => {
    const result = parseMorseResult(row);
    return { ...baseRow(row), letters: result.letters, errors: result.errors };
  });
}

async function getAsteroidsLeaderboard(): Promise<AsteroidsLeaderboardRow[]> {
  const { results } = await getDatabase()
    .prepare(
      `SELECT id, player_name, result_version, result_json, created_at
       FROM leaderboard_entries
       WHERE app_id = ?1 AND scope_key = ''
       ORDER BY rank_primary DESC, rank_secondary DESC, created_at ASC
       LIMIT 250`,
    )
    .bind(2)
    .all<StoredLeaderboardRow>();

  return results.map((row) => {
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
  const statement = chartId
    ? getDatabase()
        .prepare(
          `SELECT id, player_name, result_version, result_json, created_at
           FROM leaderboard_entries
           WHERE app_id = ?1 AND scope_key = ?2
           ORDER BY rank_primary DESC, rank_secondary DESC, created_at ASC
           LIMIT 250`,
        )
        .bind(5, chartId)
    : getDatabase()
        .prepare(
          `SELECT id, player_name, result_version, result_json, created_at
           FROM leaderboard_entries
           WHERE app_id = ?1
           ORDER BY rank_primary DESC, rank_secondary DESC, created_at ASC
           LIMIT 250`,
        )
        .bind(5);
  const { results } = await statement.all<StoredLeaderboardRow>();

  return results.map((row) => {
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
    const row = await getDatabase()
      .prepare(
        `SELECT player_name
         FROM leaderboard_entries
         WHERE device_serial = ?1
         ORDER BY created_at DESC
         LIMIT 1`,
      )
      .bind(deviceSerial)
      .first<{ player_name: string }>();
    return row?.player_name ?? "";
  } catch {
    return "";
  }
}
