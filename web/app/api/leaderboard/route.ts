import { NextResponse } from "next/server";
import { getDatabase } from "@/lib/db";
import {
  decodeLeaderboardPayload,
  type DecodedEntry,
} from "@/lib/leaderboard";

const RESULT_VERSION = 1;

function storedResult(entry: DecodedEntry) {
  if (entry.game === "morse") {
    return {
      json: JSON.stringify({
        letters: entry.letters,
        errors: entry.errors,
      }),
      scopeKey: "",
      rankPrimary: entry.letters,
      rankSecondary: -entry.errors,
    };
  }

  if (entry.game === "asteroids") {
    return {
      json: JSON.stringify({
        elapsedMs: entry.elapsedMs,
        distance: entry.distance,
      }),
      scopeKey: "",
      rankPrimary: entry.distance,
      rankSecondary: -entry.elapsedMs,
    };
  }

  return {
    json: JSON.stringify({
      trackId: entry.trackId,
      chartId: entry.chartId,
      difficulty: entry.difficulty,
      rank: entry.rank,
      score: entry.score,
      maxCombo: entry.maxCombo,
      perfect: entry.perfect,
      good: entry.good,
      bad: entry.bad,
      miss: entry.miss,
    }),
    scopeKey: entry.chartId,
    rankPrimary: entry.score,
    rankSecondary: entry.maxCombo,
  };
}

export async function POST(request: Request) {
  try {
    const body = (await request.json()) as {
      payload?: string;
      name?: string;
    };
    const name = body.name?.trim();
    if (!name || name.length > 32) {
      return NextResponse.json(
        { error: "Enter a name up to 32 characters." },
        { status: 400 },
      );
    }
    if (!body.payload) {
      return NextResponse.json(
        { error: "Missing score code." },
        { status: 400 },
      );
    }

    const entry = decodeLeaderboardPayload(body.payload);
    const result = storedResult(entry);
    const inserted = await getDatabase()
      .prepare(
        `INSERT INTO leaderboard_entries
          (app_id, device_serial, entry_id, player_name, raw_data,
           result_version, result_json, scope_key,
           rank_primary, rank_secondary)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)
         ON CONFLICT (app_id, device_serial, entry_id) DO NOTHING`,
      )
      .bind(
        entry.appId,
        entry.deviceSerial,
        entry.entryId,
        name,
        entry.rawData,
        RESULT_VERSION,
        result.json,
        result.scopeKey,
        result.rankPrimary,
        result.rankSecondary,
      )
      .run();

    if (inserted.meta.changes === 0) {
      return NextResponse.json(
        { error: "This score was already submitted." },
        { status: 409 },
      );
    }
    return NextResponse.json({ ok: true });
  } catch (error) {
    return NextResponse.json(
      {
        error:
          error instanceof Error ? error.message : "Could not submit score.",
      },
      { status: 400 },
    );
  }
}
