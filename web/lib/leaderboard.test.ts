import assert from "node:assert/strict";
import test from "node:test";
import { decodeGameData } from "./leaderboard.ts";

function bytes(hex: string) {
  return Uint8Array.from(Buffer.from(hex, "hex"));
}

test("decodes the Morse cartridge byte layout", () => {
  const result = decodeGameData(1, bytes("2400000001000000"));
  assert.equal(result.game, "morse");
  assert.equal(result.letters, 36);
  assert.equal(result.errors, 1);
});

test("decodes the Asteroids cartridge byte layout", () => {
  const result = decodeGameData(2, bytes("b1440000fe020000"));
  assert.equal(result.game, "asteroids");
  assert.equal(result.elapsedMs, 17_585);
  assert.equal(result.distance, 766);
});

test("decodes every packed Beatline field", () => {
  const result = decodeGameData(5, bytes("0201de2001002a0081000c000102"));
  assert.equal(result.game, "beatline");
  assert.deepEqual(
    {
      track: result.track,
      difficulty: result.difficulty,
      rank: result.rank,
      score: result.score,
      maxCombo: result.maxCombo,
      perfect: result.perfect,
      good: result.good,
      bad: result.bad,
      miss: result.miss,
    },
    { track: 1, difficulty: 0, rank: 1, score: 73_950, maxCombo: 42, perfect: 129, good: 12, bad: 1, miss: 2 },
  );
});

test("rejects a score whose byte count does not match its cartridge", () => {
  assert.throws(() => decodeGameData(5, bytes("0001")), /Unsupported score format/);
});
