"use client";

import { useRouter } from "next/navigation";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";

export function LeaderboardFilters({
  tracks,
  selectedTrack,
  selectedDifficulty,
}: {
  tracks: string[];
  selectedTrack: string;
  selectedDifficulty: string;
}) {
  const router = useRouter();

  function navigate(track: string, difficulty: string) {
    const params = new URLSearchParams({ game: "beatline" });
    if (track !== "all") params.set("track", track);
    if (difficulty !== "all") params.set("difficulty", difficulty);
    router.push(`/leaderboard?${params.toString()}`);
  }

  return (
    <div className="flex flex-wrap items-center gap-4">
      <div className="flex items-center gap-2">
        <span className="text-sm text-muted-foreground">song</span>
        <Select value={selectedTrack} onValueChange={(value) => navigate(value, selectedDifficulty)}>
          <SelectTrigger className="w-40" aria-label="song">
            <SelectValue />
          </SelectTrigger>
          <SelectContent>
            <SelectItem value="all">All songs</SelectItem>
            {tracks.map((track, index) => <SelectItem key={track} value={String(index)}>{track}</SelectItem>)}
          </SelectContent>
        </Select>
      </div>

      <div className="flex items-center gap-2">
        <span className="text-sm text-muted-foreground">difficulty</span>
        <Select value={selectedDifficulty} onValueChange={(value) => navigate(selectedTrack, value)}>
          <SelectTrigger className="w-32" aria-label="difficulty">
            <SelectValue />
          </SelectTrigger>
          <SelectContent>
            <SelectItem value="all">All</SelectItem>
            <SelectItem value="0">Normal</SelectItem>
            <SelectItem value="1">Hard</SelectItem>
          </SelectContent>
        </Select>
      </div>
    </div>
  );
}
