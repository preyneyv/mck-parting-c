export type DecodedEntry = {
  appId: number;
  deviceSerial: string;
  entryId: number;
  rawData: Uint8Array;
  game: string;
  summary: string;
  sortScore: number;
  metadata: Record<string, number | string>;
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

function u16(data: Uint8Array, offset: number) { return data[offset] | (data[offset + 1] << 8); }
function u32(data: Uint8Array, offset: number) { return (data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | (data[offset + 3] << 24)) >>> 0; }

function parseGame(appId: number, data: Uint8Array): Pick<DecodedEntry, "game" | "summary" | "sortScore" | "metadata"> {
  if (appId === 1 && data.length === 8) {
    const letters = u32(data, 0);
    const errors = u32(data, 4);
    return { game: "Morse", summary: `${letters} letters · ${errors} errors`, sortScore: letters * 100000 - errors, metadata: { letters, errors } };
  }
  if (appId === 2 && data.length === 8) {
    const elapsedMs = u32(data, 0);
    const distance = u32(data, 4);
    return { game: "Asteroids", summary: `${distance} m · ${(elapsedMs / 1000).toFixed(1)} s`, sortScore: distance * 100000000 + elapsedMs, metadata: { elapsedMs, distance } };
  }
  if (appId === 5 && data.length === 14) {
    const track = data[0] >> 1;
    const difficulty = data[0] & 1;
    const rank = data[1];
    const score = u32(data, 2);
    const maxCombo = u16(data, 6);
    const perfect = u16(data, 8);
    const good = u16(data, 10);
    const bad = data[12];
    const miss = data[13];
    return { game: "Beatline", summary: `${score.toLocaleString()} · ${maxCombo} combo`, sortScore: score, metadata: { track, difficulty, rank, score, maxCombo, perfect, good, bad, miss } };
  }
  throw new Error(`Unsupported score format (app ${appId})`);
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
  return { appId, deviceSerial, entryId, rawData, ...parseGame(appId, rawData) };
}
