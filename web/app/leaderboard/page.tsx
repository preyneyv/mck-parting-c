import type { Metadata } from "next";
import Image from "next/image";
import Link from "next/link";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card";
import { LeaderboardFilters } from "@/components/leaderboard-filters";
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from "@/components/ui/table";
import {
  getLeaderboards,
  type AsteroidsLeaderboardRow,
  type BeatlineLeaderboardRow,
  type Leaderboards,
  type MorseLeaderboardRow,
} from "@/lib/db";
import { beatlineDifficultyName, beatlineRankName, beatlineTrackName, BEATLINE_TRACKS } from "@/lib/leaderboard";

export const metadata: Metadata = { title: "leaderboards" };
export const dynamic = "force-dynamic";

type GameKey = keyof Leaderboards;

const EMPTY_LEADERBOARDS: Leaderboards = { morse: [], asteroids: [], beatline: [] };
const GAMES: Array<{ key: GameKey; label: string; icon: string }> = [
  { key: "morse", label: "morse", icon: "/icons/morse.png" },
  { key: "asteroids", label: "asteroids", icon: "/icons/asteroids.png" },
  { key: "beatline", label: "beatline", icon: "/icons/beatline.png" },
];

function isGame(value: string | undefined): value is GameKey {
  return value === "morse" || value === "asteroids" || value === "beatline";
}

const dateFormatter = new Intl.DateTimeFormat("en", { month: "short", day: "numeric", hour: "numeric", minute: "2-digit" });

function submittedAt(value: Date) {
  return <time dateTime={value.toISOString()}>{dateFormatter.format(value)}</time>;
}

function EmptyRow({ columns }: { columns: number }) {
  return <TableRow><TableCell colSpan={columns} className="h-24 text-center text-muted-foreground">no scores for this leaderboard yet.</TableCell></TableRow>;
}

function MorseBoard({ rows }: { rows: MorseLeaderboardRow[] }) {
  return (
    <Table>
      <TableHeader><TableRow><TableHead>#</TableHead><TableHead>player</TableHead><TableHead className="text-right">letters</TableHead><TableHead className="text-right">WPM</TableHead><TableHead className="text-right">CPM</TableHead><TableHead className="text-right">errors</TableHead><TableHead className="text-right">accuracy</TableHead><TableHead>submitted</TableHead></TableRow></TableHeader>
      <TableBody>{rows.length === 0 ? <EmptyRow columns={8} /> : rows.map((row, index) => {
        const attempts = row.letters + row.errors;
        const cpm = row.letters * 2;
        const accuracy = attempts > 0 ? Math.floor((row.letters * 100) / attempts) : 100;
        return <TableRow key={row.id}><TableCell>{index + 1}</TableCell><TableCell className="font-medium">{row.player_name}</TableCell><TableCell className="text-right tabular-nums">{row.letters}</TableCell><TableCell className="text-right tabular-nums">{Math.floor(cpm / 5)}</TableCell><TableCell className="text-right tabular-nums">{cpm}</TableCell><TableCell className="text-right tabular-nums">{row.errors}</TableCell><TableCell className="text-right tabular-nums">{accuracy}%</TableCell><TableCell className="whitespace-nowrap text-muted-foreground">{submittedAt(row.created_at)}</TableCell></TableRow>;
      })}</TableBody>
    </Table>
  );
}

function AsteroidsBoard({ rows }: { rows: AsteroidsLeaderboardRow[] }) {
  return (
    <Table>
      <TableHeader><TableRow><TableHead>#</TableHead><TableHead>player</TableHead><TableHead className="text-right">distance</TableHead><TableHead className="text-right">time</TableHead><TableHead className="text-right">average speed</TableHead><TableHead>submitted</TableHead></TableRow></TableHeader>
      <TableBody>{rows.length === 0 ? <EmptyRow columns={6} /> : rows.map((row, index) => {
        const seconds = row.elapsed_ms / 1000;
        const averageSpeed = seconds > 0 ? row.distance / seconds : 0;
        return <TableRow key={row.id}><TableCell>{index + 1}</TableCell><TableCell className="font-medium">{row.player_name}</TableCell><TableCell className="text-right tabular-nums">{row.distance.toLocaleString()} m</TableCell><TableCell className="text-right tabular-nums">{seconds.toFixed(3)} s</TableCell><TableCell className="text-right tabular-nums">{averageSpeed.toFixed(1)} m/s</TableCell><TableCell className="whitespace-nowrap text-muted-foreground">{submittedAt(row.created_at)}</TableCell></TableRow>;
      })}</TableBody>
    </Table>
  );
}

function BeatlineBoard({ rows }: { rows: BeatlineLeaderboardRow[] }) {
  return (
    <Table>
      <TableHeader><TableRow><TableHead>#</TableHead><TableHead>player</TableHead><TableHead className="text-right">score</TableHead><TableHead>song</TableHead><TableHead>difficulty</TableHead><TableHead className="text-center">rank</TableHead><TableHead className="text-right">max combo</TableHead><TableHead className="text-right">perfect</TableHead><TableHead className="text-right">good</TableHead><TableHead className="text-right">bad</TableHead><TableHead className="text-right">miss</TableHead><TableHead>submitted</TableHead></TableRow></TableHeader>
      <TableBody>{rows.length === 0 ? <EmptyRow columns={12} /> : rows.map((row, index) => <TableRow key={row.id}><TableCell>{index + 1}</TableCell><TableCell className="font-medium">{row.player_name}</TableCell><TableCell className="text-right font-medium tabular-nums">{row.score.toLocaleString()}</TableCell><TableCell className="whitespace-nowrap">{beatlineTrackName(row.track_id)}</TableCell><TableCell>{beatlineDifficultyName(row.difficulty)}</TableCell><TableCell className="text-center font-medium">{beatlineRankName(row.rank)}</TableCell><TableCell className="text-right tabular-nums">{row.max_combo}</TableCell><TableCell className="text-right tabular-nums">{row.perfect}</TableCell><TableCell className="text-right tabular-nums">{row.good}</TableCell><TableCell className="text-right tabular-nums">{row.bad}</TableCell><TableCell className="text-right tabular-nums">{row.miss}</TableCell><TableCell className="whitespace-nowrap text-muted-foreground">{submittedAt(row.created_at)}</TableCell></TableRow>)}</TableBody>
    </Table>
  );
}

export default async function LeaderboardPage({ searchParams }: { searchParams: Promise<{ game?: string; track?: string; difficulty?: string }> }) {
  const query = await searchParams;
  const selectedGame: GameKey = isGame(query.game) ? query.game : "morse";
  const selectedTrack = BEATLINE_TRACKS.some((track) => track.id === query.track) ? query.track! : "all";
  const selectedDifficulty = query.difficulty === "0" || query.difficulty === "1" ? query.difficulty : "all";

  let boards = EMPTY_LEADERBOARDS;
  let databaseAvailable = true;
  try {
    boards = await getLeaderboards();
  } catch (error) {
    databaseAvailable = false;
    console.error("could not load the leaderboard database", error);
  }

  const activeGame = GAMES.find((game) => game.key === selectedGame) ?? GAMES[0];
  const beatlineRows = boards.beatline.filter((row) => (selectedTrack === "all" || row.track_id === selectedTrack) && (selectedDifficulty === "all" || row.difficulty === Number(selectedDifficulty)));

  return (
    <div className="space-y-6">
      <div><h1 className="text-3xl font-semibold tracking-tight">leaderboards</h1><p className="mt-2 text-muted-foreground">scores submitted by scanning a cartridge result QR code.</p></div>

      <nav aria-label="choose a leaderboard" className="flex flex-wrap gap-2">
        {GAMES.map((game) => {
          const active = game.key === selectedGame;
          return <Button key={game.key} asChild className="h-auto px-3 py-2" variant={active ? "default" : "outline"}><Link href={`/leaderboard?game=${game.key}`} aria-current={active ? "page" : undefined}><Image src={game.icon} alt="" width={36} height={36} className={`size-9 rounded-[3px] border ${active ? "invert" : ""}`} />{game.label}</Link></Button>;
        })}
      </nav>

      <Card>
        <CardHeader><CardTitle>{activeGame.label}</CardTitle></CardHeader>
        <CardContent className="space-y-4">
          {selectedGame === "beatline" ? <LeaderboardFilters tracks={BEATLINE_TRACKS} selectedTrack={selectedTrack} selectedDifficulty={selectedDifficulty} /> : null}
          {!databaseAvailable ? <div className="py-12 text-center text-sm text-muted-foreground">start PostgreSQL and reload this page.</div> : selectedGame === "morse" ? <MorseBoard rows={boards.morse} /> : selectedGame === "asteroids" ? <AsteroidsBoard rows={boards.asteroids} /> : <BeatlineBoard rows={beatlineRows} />}
        </CardContent>
      </Card>
    </div>
  );
}
