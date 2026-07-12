import { NextResponse } from "next/server";
import { ensureSchema, sql } from "@/lib/db";
import { decodeLeaderboardPayload } from "@/lib/leaderboard";

export async function POST(request: Request) {
  try {
    const body = await request.json() as { payload?: string; name?: string };
    const name = body.name?.trim();
    if (!name || name.length > 32) return NextResponse.json({ error: "Enter a name up to 32 characters." }, { status: 400 });
    if (!body.payload) return NextResponse.json({ error: "Missing score code." }, { status: 400 });
    const entry = decodeLeaderboardPayload(body.payload);
    await ensureSchema();
    const inserted = await sql`
      insert into leaderboard_entries
        (app_id, device_serial, entry_id, player_name, raw_data,
         morse_letters, morse_errors,
         asteroids_elapsed_ms, asteroids_distance,
         beatline_track, beatline_difficulty, beatline_rank, beatline_score,
         beatline_max_combo, beatline_perfect, beatline_good,
         beatline_bad, beatline_miss)
      values
        (${entry.appId}, ${entry.deviceSerial}, ${entry.entryId}, ${name},
         ${Buffer.from(entry.rawData)},
         ${entry.game === "morse" ? entry.letters : null},
         ${entry.game === "morse" ? entry.errors : null},
         ${entry.game === "asteroids" ? entry.elapsedMs : null},
         ${entry.game === "asteroids" ? entry.distance : null},
         ${entry.game === "beatline" ? entry.track : null},
         ${entry.game === "beatline" ? entry.difficulty : null},
         ${entry.game === "beatline" ? entry.rank : null},
         ${entry.game === "beatline" ? entry.score : null},
         ${entry.game === "beatline" ? entry.maxCombo : null},
         ${entry.game === "beatline" ? entry.perfect : null},
         ${entry.game === "beatline" ? entry.good : null},
         ${entry.game === "beatline" ? entry.bad : null},
         ${entry.game === "beatline" ? entry.miss : null})
      on conflict (app_id, device_serial, entry_id) do nothing
      returning id
    `;
    if (inserted.length === 0) return NextResponse.json({ error: "This score was already submitted." }, { status: 409 });
    return NextResponse.json({ ok: true });
  } catch (error) {
    return NextResponse.json({ error: error instanceof Error ? error.message : "Could not submit score." }, { status: 400 });
  }
}
