import { DurableObject } from "cloudflare:workers";

type Env = {
  CHAT_ROOMS: DurableObjectNamespace<ComsPlusRoom>;
};

type StoredMessage = {
  id: number;
  payload: string;
};

type PresenceRow = {
  key: string;
  name: string;
  account_id: number;
  icon: string;
  joined_at: number;
  last_seen: number;
  message_count: number;
};

type ActivityRow = {
  created_at: number;
  text: string;
};

const maxBodyBytes = 4096;
const maxMessagesPerRoom = 250;
const maxActivityRows = 80;
const presenceTtlMs = 45_000;

function text(body: string, status = 200): Response {
  return new Response(body, {
    status,
    headers: {
      "Access-Control-Allow-Origin": "*",
      "Access-Control-Allow-Methods": "GET,POST,OPTIONS",
      "Access-Control-Allow-Headers": "Content-Type",
      "Cache-Control": "no-store",
      "Content-Type": "text/plain; charset=utf-8"
    }
  });
}

function roomName(value: string | null): string {
  const clean = (value || "main").replace(/[^a-zA-Z0-9_-]/g, "").slice(0, 32);
  return clean || "main";
}

function routePath(pathname: string): string {
  return pathname.startsWith("/comsplus/") ? pathname.slice("/comsplus".length) : pathname;
}

function validPayload(payload: string): boolean {
  if (!payload || payload.length > maxBodyBytes) return false;
  try {
    const parsed = JSON.parse(payload) as Record<string, unknown>;
    return parsed.v === 1 && typeof parsed.id === "string" && typeof parsed.name === "string";
  } catch {
    return false;
  }
}

function sanitizeField(value: string | null, fallback: string, maxLength: number): string {
  const clean = (value || fallback).replace(/[\t\r\n]/g, " ").trim().slice(0, maxLength);
  return clean || fallback;
}

function parsePayload(payload: string): Record<string, unknown> | null {
  if (!validPayload(payload)) return null;
  try {
    return JSON.parse(payload) as Record<string, unknown>;
  } catch {
    return null;
  }
}

function presenceKey(accountId: number, name: string): string {
  return accountId > 0 ? `a:${accountId}` : `n:${name.toLowerCase()}`;
}

export class ComsPlusRoom extends DurableObject<Env> {
  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
    ctx.blockConcurrencyWhile(async () => {
      this.ctx.storage.sql.exec(`
        CREATE TABLE IF NOT EXISTS messages (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          payload TEXT NOT NULL,
          created_at INTEGER NOT NULL
        )
      `);
      this.ctx.storage.sql.exec(`
        CREATE TABLE IF NOT EXISTS presence (
          key TEXT PRIMARY KEY,
          name TEXT NOT NULL,
          account_id INTEGER NOT NULL,
          icon TEXT NOT NULL,
          joined_at INTEGER NOT NULL,
          last_seen INTEGER NOT NULL,
          message_count INTEGER NOT NULL DEFAULT 0
        )
      `);
      this.ctx.storage.sql.exec(`
        CREATE TABLE IF NOT EXISTS activity (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          created_at INTEGER NOT NULL,
          text TEXT NOT NULL
        )
      `);
    });
  }

  private prune(now: number): void {
    this.ctx.storage.sql.exec("DELETE FROM presence WHERE last_seen < ?", now - presenceTtlMs);
  }

  private addActivity(now: number, textValue: string): void {
    const textLine = sanitizeField(textValue, "activity", 96);
    this.ctx.storage.sql.exec(
      "INSERT INTO activity (created_at, text) VALUES (?, ?)",
      now,
      textLine
    );
    this.ctx.storage.sql.exec(
      "DELETE FROM activity WHERE id NOT IN (SELECT id FROM activity ORDER BY id DESC LIMIT ?)",
      maxActivityRows
    );
  }

  heartbeat(nameValue: string, accountId: number, iconValue: string, now = Date.now()): void {
    const name = sanitizeField(nameValue, "Player", 24);
    const icon = sanitizeField(iconValue, "", 64);
    const key = presenceKey(accountId, name);
    this.prune(now);

    const existing = this.ctx.storage.sql.exec<PresenceRow>(
      "SELECT key, last_seen FROM presence WHERE key = ?",
      key
    ).toArray()[0];
    const joinedAt = existing ? existing.last_seen : now;
    const rejoined = !existing || existing.last_seen < now - presenceTtlMs;

    this.ctx.storage.sql.exec(
      `INSERT INTO presence (key, name, account_id, icon, joined_at, last_seen, message_count)
       VALUES (?, ?, ?, ?, ?, ?, 0)
       ON CONFLICT(key) DO UPDATE SET
         name = excluded.name,
         account_id = excluded.account_id,
         icon = excluded.icon,
         last_seen = excluded.last_seen`,
      key,
      name,
      accountId,
      icon,
      rejoined ? now : joinedAt,
      now
    );

    if (rejoined) {
      this.addActivity(now, `${name} joined main chat`);
    }
  }

  poll(since: number, name: string, accountId: number, icon: string): string {
    this.heartbeat(name, accountId, icon);
    const last = this.ctx.storage.sql.exec<{ id: number }>(
      "SELECT COALESCE(MAX(id), 0) AS id FROM messages"
    ).one().id;
    const rows = this.ctx.storage.sql.exec<StoredMessage>(
      "SELECT id, payload FROM messages WHERE id > ? ORDER BY id ASC LIMIT ?",
      since,
      maxMessagesPerRoom
    ).toArray();
    return `${[String(last), ...rows.map(row => row.payload)].join("\n")}\n`;
  }

  send(payload: string): number {
    const parsed = parsePayload(payload);
    if (!parsed) return 0;
    const now = Date.now();
    const name = sanitizeField(typeof parsed.name === "string" ? parsed.name : null, "Player", 24);
    const accountId = typeof parsed.aid === "number" ? Math.trunc(parsed.aid) : 0;
    const icon = sanitizeField(typeof parsed.icon === "string" ? parsed.icon : null, "", 64);
    this.heartbeat(name, accountId, icon, now);

    const row = this.ctx.storage.sql.exec<{ id: number }>(
      "INSERT INTO messages (payload, created_at) VALUES (?, ?) RETURNING id",
      payload,
      now
    ).one();
    this.ctx.storage.sql.exec(
      "UPDATE presence SET message_count = message_count + 1, last_seen = ? WHERE key = ?",
      now,
      presenceKey(accountId, name)
    );
    const kind = typeof parsed.kind === "string" ? parsed.kind : "user";
    const activityText = kind === "report" ?
      `${name} sent a report` :
      kind === "mod" ? `${name} used moderation` : `${name} sent a message`;
    this.addActivity(now, activityText);
    this.ctx.storage.sql.exec(
      "DELETE FROM messages WHERE id NOT IN (SELECT id FROM messages ORDER BY id DESC LIMIT ?)",
      maxMessagesPerRoom
    );
    return row.id;
  }

  presence(): string {
    const now = Date.now();
    this.prune(now);
    const rows = this.ctx.storage.sql.exec<PresenceRow>(
      "SELECT name, account_id, icon, joined_at, last_seen, message_count FROM presence ORDER BY last_seen DESC LIMIT 80"
    ).toArray();
    const lines = [String(rows.length)];
    for (const row of rows) {
      lines.push([
        row.account_id,
        sanitizeField(row.name, "Player", 24),
        sanitizeField(row.icon, "", 64),
        row.joined_at,
        row.last_seen,
        row.message_count
      ].join("\t"));
    }
    return `${lines.join("\n")}\n`;
  }

  activity(): string {
    const rows = this.ctx.storage.sql.exec<ActivityRow>(
      "SELECT created_at, text FROM activity ORDER BY id DESC LIMIT 60"
    ).toArray();
    const lines = [String(rows.length)];
    for (const row of rows) {
      lines.push(`${row.created_at}\t${sanitizeField(row.text, "activity", 96)}`);
    }
    return `${lines.join("\n")}\n`;
  }
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    if (request.method === "OPTIONS") return text("", 204);

    const url = new URL(request.url);
    const path = routePath(url.pathname);
    if (request.method === "GET" && path === "/health") return text("ok\n");

    if (path !== "/poll" && path !== "/send" && path !== "/presence" && path !== "/activity") {
      return text("not found\n", 404);
    }

    const room = env.CHAT_ROOMS.getByName(roomName(url.searchParams.get("room")));

    if (request.method === "GET" && path === "/poll") {
      const since = Number.parseInt(url.searchParams.get("since") || "0", 10) || 0;
      const accountId = Number.parseInt(url.searchParams.get("aid") || "0", 10) || 0;
      return text(await room.poll(
        since,
        url.searchParams.get("name") || "Player",
        accountId,
        url.searchParams.get("icon") || ""
      ));
    }

    if (request.method === "GET" && path === "/presence") {
      return text(await room.presence());
    }

    if (request.method === "GET" && path === "/activity") {
      return text(await room.activity());
    }

    if (request.method === "POST" && path === "/send") {
      const body = await request.text();
      if (!validPayload(body)) return text("invalid payload\n", 400);
      const id = await room.send(body);
      if (id <= 0) return text("invalid payload\n", 400);
      return text(`${id}\n`);
    }

    return text("method not allowed\n", 405);
  }
};
