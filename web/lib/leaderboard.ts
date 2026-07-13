export const BEATLINE_TRACKS = [
  { id: "1", name: "Never Gonna", charts: { normal: "1", hard: "2" } },
  { id: "2", name: "Golden", charts: { normal: "3", hard: "4" } },
] as const;
export const BEATLINE_DIFFICULTIES = ["Normal", "Hard"] as const;
export const BEATLINE_RANKS = ["S", "A", "B", "C", "D"] as const;

type MorseResult = {
  game: "morse";
  gameName: "Morse";
  letters: number;
  errors: number;
  summary: string;
};

type AsteroidsResult = {
  game: "asteroids";
  gameName: "Asteroids";
  elapsedMs: number;
  distance: number;
  summary: string;
};

type BeatlineResult = {
  game: "beatline";
  gameName: "Beatline";
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
  summary: string;
};

export type DecodedGameResult = MorseResult | AsteroidsResult | BeatlineResult;

export type DecodedEntry = DecodedGameResult & {
  appId: number;
  deviceSerial: string;
  entryId: number;
  rawData: Uint8Array;
};

function fletcher16(bytes: Uint8Array) {
  let sum1 = 0;
  let sum2 = 0;
  for (const byte of bytes) {
    sum1 = (sum1 + byte) % 255;
    sum2 = (sum2 + sum1) % 255;
  }
  return (sum2 << 8) | sum1;
}

function base36Bytes(value: string) {
  if (!/^[0-9a-z]+$/i.test(value)) throw new Error("Invalid score code");
  let number = 0n;
  for (const char of value.toUpperCase()) {
    const digit = char >= "A" ? char.charCodeAt(0) - 55 : char.charCodeAt(0) - 48;
    number = number * 36n + BigInt(digit);
  }
  const bytes: number[] = [];
  while (number > 0n) {
    bytes.push(Number(number & 0xffn));
    number >>= 8n;
  }
  return Uint8Array.from(bytes.reverse());
}

function u16(data: Uint8Array, offset: number) {
  return data[offset] | (data[offset + 1] << 8);
}

function u32(data: Uint8Array, offset: number) {
  return (data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | (data[offset + 3] << 24)) >>> 0;
}

function u64(data: Uint8Array, offset: number) {
  let value = 0n;
  for (let index = 7; index >= 0; index--) value = (value << 8n) | BigInt(data[offset + index]);
  return value;
}

function beatlineChart(chartId: string) {
  for (const track of BEATLINE_TRACKS) {
    if (track.charts.normal === chartId) return { track, difficulty: 0 };
    if (track.charts.hard === chartId) return { track, difficulty: 1 };
  }
  return undefined;
}

export function beatlineTrackName(trackId: string) {
  return BEATLINE_TRACKS.find((track) => track.id === trackId)?.name ?? `Track ${trackId}`;
}

export function beatlineDifficultyName(difficulty: number) {
  return BEATLINE_DIFFICULTIES[difficulty] ?? `Difficulty ${difficulty}`;
}

export function beatlineRankName(rank: number) {
  return BEATLINE_RANKS[rank] ?? "?";
}

/** Decode the exact little-endian byte layouts authored by each cartridge. */
export function decodeGameData(appId: number, data: Uint8Array): DecodedGameResult {
  if (appId === 1 && data.length === 8) {
    const letters = u32(data, 0);
    const errors = u32(data, 4);
    return {
      game: "morse",
      gameName: "Morse",
      letters,
      errors,
      summary: `${letters} letters · ${errors} ${errors === 1 ? "error" : "errors"}`,
    };
  }

  if (appId === 2 && data.length === 8) {
    const elapsedMs = u32(data, 0);
    const distance = u32(data, 4);
    return {
      game: "asteroids",
      gameName: "Asteroids",
      elapsedMs,
      distance,
      summary: `${distance} m · ${(elapsedMs / 1000).toFixed(1)} s`,
    };
  }

  if (appId === 5 && data.length === 22) {
    const chartId = u64(data, 0).toString();
    const chart = beatlineChart(chartId);
    if (!chart) throw new Error("Unrecognized Beatline chart ID");

    const score = u32(data, 8);
    const maxCombo = u16(data, 12);
    const perfect = u16(data, 14);
    const good = u16(data, 16);
    const bad = u16(data, 18);
    const miss = u16(data, 20);
    const total = perfect + good + bad + miss;
    const goodOrBetter = perfect + good;
    const rank = total === 0 ? 4
      : bad === 0 && miss === 0 ? 0
      : goodOrBetter * 100 >= total * 90 ? 1
      : goodOrBetter * 100 >= total * 85 ? 2
      : goodOrBetter * 100 >= total * 70 ? 3
      : 4;
    return {
      game: "beatline",
      gameName: "Beatline",
      trackId: chart.track.id,
      chartId,
      difficulty: chart.difficulty,
      rank,
      score,
      maxCombo,
      perfect,
      good,
      bad,
      miss,
      summary: `${score.toLocaleString()} · ${maxCombo} combo · ${chart.track.name}`,
    };
  }

  throw new Error(`Unsupported score format (app ${appId}, ${data.length} bytes)`);
}

export function decodeLeaderboardPayload(payload: string): DecodedEntry {
  const bytes = base36Bytes(payload);
  if (bytes.length < 16) throw new Error("Score code is too short");
  const dataLength = bytes[13];
  if (bytes.length !== 16 + dataLength) throw new Error("Score code length does not match its header");
  const expected = (bytes[bytes.length - 2] << 8) | bytes[bytes.length - 1];
  if (fletcher16(bytes.subarray(0, bytes.length - 2)) !== expected) throw new Error("Score code checksum failed");

  const appId = bytes[0];
  const deviceSerial = Array.from(bytes.subarray(1, 9), (byte) => byte.toString(16).padStart(2, "0")).join("").toUpperCase();
  const entryId = ((bytes[9] << 24) | (bytes[10] << 16) | (bytes[11] << 8) | bytes[12]) >>> 0;
  const rawData = bytes.slice(14, 14 + dataLength);
  return { appId, deviceSerial, entryId, rawData, ...decodeGameData(appId, rawData) };
}
