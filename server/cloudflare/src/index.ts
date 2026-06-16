import { DurableObject } from "cloudflare:workers";

type Env = {
  CHAT_ROOMS: DurableObjectNamespace<ComsPlusRoom>;
};

type StoredMessage = {
  id: number;
  payload: string;
};

const maxBodyBytes = 4096;
const maxMessagesPerRoom = 250;

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
    });
  }

  poll(since: number): string {
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
    const row = this.ctx.storage.sql.exec<{ id: number }>(
      "INSERT INTO messages (payload, created_at) VALUES (?, ?) RETURNING id",
      payload,
      Date.now()
    ).one();
    this.ctx.storage.sql.exec(
      "DELETE FROM messages WHERE id NOT IN (SELECT id FROM messages ORDER BY id DESC LIMIT ?)",
      maxMessagesPerRoom
    );
    return row.id;
  }
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    if (request.method === "OPTIONS") return text("", 204);

    const url = new URL(request.url);
    const path = routePath(url.pathname);
    if (request.method === "GET" && path === "/health") return text("ok\n");

    if (path !== "/poll" && path !== "/send") return text("not found\n", 404);

    const room = env.CHAT_ROOMS.getByName(roomName(url.searchParams.get("room")));

    if (request.method === "GET" && path === "/poll") {
      const since = Number.parseInt(url.searchParams.get("since") || "0", 10) || 0;
      return text(await room.poll(since));
    }

    if (request.method === "POST" && path === "/send") {
      const body = await request.text();
      if (!validPayload(body)) return text("invalid payload\n", 400);
      return text(`${await room.send(body)}\n`);
    }

    return text("method not allowed\n", 405);
  }
};
