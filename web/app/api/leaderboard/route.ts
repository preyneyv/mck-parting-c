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
        (app_id, device_serial, entry_id, player_name, game, summary,
         sort_score, metadata, raw_data)
      values
        (${entry.appId}, ${entry.deviceSerial}, ${entry.entryId}, ${name},
         ${entry.game}, ${entry.summary}, ${entry.sortScore},
         ${sql.json(entry.metadata)}, ${Buffer.from(entry.rawData)})
      on conflict (app_id, device_serial, entry_id) do nothing
      returning id
    `;
    if (inserted.length === 0) return NextResponse.json({ error: "This score was already submitted." }, { status: 409 });
    return NextResponse.json({ ok: true });
  } catch (error) {
    return NextResponse.json({ error: error instanceof Error ? error.message : "Could not submit score." }, { status: 400 });
  }
}
