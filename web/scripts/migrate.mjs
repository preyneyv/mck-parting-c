import { readFile, readdir } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createClient } from "@libsql/client";

const databaseUrl = process.env.TURSO_DATABASE_URL;
if (!databaseUrl) {
  throw new Error("TURSO_DATABASE_URL is required.");
}

const database = createClient({
  url: databaseUrl,
  authToken: process.env.TURSO_AUTH_TOKEN,
});

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const migrationsDirectory = path.resolve(scriptDirectory, "../migrations");

await database.execute(`
  CREATE TABLE IF NOT EXISTS schema_migrations (
    name TEXT PRIMARY KEY,
    applied_at TEXT NOT NULL DEFAULT (
      strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
    )
  ) STRICT
`);

const migrations = (await readdir(migrationsDirectory))
  .filter((name) => name.endsWith(".sql"))
  .sort();

for (const name of migrations) {
  const existing = await database.execute({
    sql: "SELECT 1 FROM schema_migrations WHERE name = ? LIMIT 1",
    args: [name],
  });
  if (existing.rows.length > 0) {
    console.log(`already applied: ${name}`);
    continue;
  }

  const sql = await readFile(path.join(migrationsDirectory, name), "utf8");
  const transaction = await database.transaction("write");
  try {
    await transaction.executeMultiple(sql);
    await transaction.execute({
      sql: "INSERT INTO schema_migrations (name) VALUES (?)",
      args: [name],
    });
    await transaction.commit();
    console.log(`applied: ${name}`);
  } catch (error) {
    await transaction.rollback();
    throw error;
  } finally {
    transaction.close();
  }
}

database.close();
