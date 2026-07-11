import type { Metadata } from "next";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import { getLeaderboard } from "@/lib/db";

export const metadata: Metadata = { title: "Leaderboard" };
export const dynamic = "force-dynamic";

export default async function LeaderboardPage() {
  const rows = await getLeaderboard();
  return <div className="space-y-6">
    <div><h1 className="text-3xl font-semibold tracking-tight">Leaderboard</h1><p className="mt-2 text-muted-foreground">Scores submitted by scanning a result QR code.</p></div>
    <Card>
      <CardHeader><CardTitle>Scores</CardTitle><CardDescription>{rows.length ? `${rows.length} recent entries` : "No scores yet. PostgreSQL may not be running."}</CardDescription></CardHeader>
      <CardContent>
        <Table><TableHeader><TableRow><TableHead>Player</TableHead><TableHead>Game</TableHead><TableHead>Result</TableHead><TableHead className="hidden sm:table-cell">Prism</TableHead></TableRow></TableHeader>
          <TableBody>{rows.map(row => <TableRow key={row.id}><TableCell className="font-medium">{row.player_name}</TableCell><TableCell>{row.game}</TableCell><TableCell>{row.summary}</TableCell><TableCell className="hidden font-mono text-xs text-muted-foreground sm:table-cell">{row.device_serial}</TableCell></TableRow>)}</TableBody>
        </Table>
      </CardContent>
    </Card>
  </div>;
}
