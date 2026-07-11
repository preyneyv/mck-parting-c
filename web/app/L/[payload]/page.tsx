import { notFound } from "next/navigation";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { ScoreSubmitForm } from "@/components/score-submit-form";
import { getLatestName } from "@/lib/db";
import { decodeLeaderboardPayload } from "@/lib/leaderboard";

export const dynamic = "force-dynamic";

export default async function ScoreIngestPage({ params }: { params: Promise<{ payload: string }> }) {
  const { payload } = await params;
  let entry;
  try { entry = decodeLeaderboardPayload(payload); } catch { notFound(); }
  const initialName = await getLatestName(entry.deviceSerial);
  return <div className="mx-auto max-w-lg"><Card><CardHeader><CardTitle>Submit {entry.game} score</CardTitle><CardDescription>{entry.summary}</CardDescription></CardHeader><CardContent><ScoreSubmitForm payload={payload} deviceSerial={entry.deviceSerial} initialName={initialName} /></CardContent></Card></div>;
}
