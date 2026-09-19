const LEVELS = ["Mountain Caves", "City of Vilcabamba", "The Lost Valley", "Tomb of Qualopec", "St. Francis Folly", "The Coliseum", "Midas Palace", "Tomb of Tihocan", "Temple of Khamoon", "Obelisk of Khamoon", "Sanctuary of the Scion", "Natla's Mines", "The Great Pyramid", "Final Conflict"];
const CATEGORIES = ["Any%", "100%"];
const SUBCATEGORIES = ["No Bug Jump", "Bug Jump", "Better Than Glitchless", "Glitchless"];
const headers = { "Access-Control-Allow-Origin": "https://trsr.app", "Access-Control-Allow-Methods": "GET, POST, OPTIONS", "Access-Control-Allow-Headers": "Content-Type", "Content-Type": "application/json" };
const json = (body, status = 200) => new Response(JSON.stringify(body), { status, headers });
const randomItem = (items) => items[Math.floor(Math.random() * items.length)];
const queueKey = (type, category, subcategory) => type === "random" ? "random" : `ruleset:${category}:${subcategory}`;

async function body(request) {
  try { return await request.json(); } catch { return null; }
}

async function hash(value) {
  const digest = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(value));
  return [...new Uint8Array(digest)].map((byte) => byte.toString(16).padStart(2, "0")).join("");
}

async function sessionFor(env, token) {
  if (typeof token !== "string" || !token.trim()) return null;
  return env.DB.prepare(`SELECT sessions.speedrun_user_id, profiles.display_name FROM sessions JOIN profiles ON profiles.speedrun_user_id = sessions.speedrun_user_id WHERE sessions.token_hash = ? AND sessions.expires_at > ?`)
    .bind(await hash(token), new Date().toISOString()).first();
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (request.method === "OPTIONS") return new Response(null, { status: 204, headers });
    if (request.method === "GET" && url.pathname === "/health") return json({ status: "ok", service: "TRSR matchmaking server" });

    if (request.method === "POST" && url.pathname === "/auth/speedrun") {
      const data = await body(request);
      const apiKey = typeof data?.apiKey === "string" ? data.apiKey.trim() : "";
      if (!apiKey) return json({ error: "A Speedrun.com API key is required." }, 400);
      let response;
      try { response = await fetch("https://www.speedrun.com/api/v1/profile", { headers: { "X-API-Key": apiKey } }); }
      catch { return json({ error: "Could not reach Speedrun.com." }, 502); }
      if (!response.ok) return json({ error: "Speedrun.com rejected that API key." }, 401);
      const user = (await response.json()).data;
      if (!user?.id) return json({ error: "Speedrun.com returned an unexpected profile." }, 502);
      const displayName = user.names?.international ?? user.id;
      const token = crypto.randomUUID();
      const now = new Date().toISOString();
      const expiresAt = new Date(Date.now() + 90 * 86400000).toISOString();
      await env.DB.batch([
        env.DB.prepare(`INSERT INTO profiles (speedrun_user_id, display_name, linked_at) VALUES (?, ?, ?) ON CONFLICT(speedrun_user_id) DO UPDATE SET display_name = excluded.display_name, linked_at = excluded.linked_at`).bind(user.id, displayName, now),
        env.DB.prepare(`DELETE FROM sessions WHERE expires_at <= ?`).bind(now),
        env.DB.prepare(`INSERT INTO sessions (token_hash, speedrun_user_id, created_at, expires_at) VALUES (?, ?, ?, ?)`).bind(await hash(token), user.id, now, expiresAt)
      ]);
      return json({ status: "linked", sessionToken: token, profile: { id: user.id, displayName } });
    }

    if (request.method === "POST" && url.pathname === "/auth/session") {
      const session = await sessionFor(env, (await body(request))?.sessionToken);
      return session ? json({ status: "valid", profile: { id: session.speedrun_user_id, displayName: session.display_name } }) : json({ error: "Your TRSR login has expired." }, 401);
    }

    if (request.method === "POST" && url.pathname === "/queue") {
      const data = await body(request);
      const playerId = typeof data?.playerId === "string" ? data.playerId.trim() : "";
      const { queueType, category, subcategory } = data ?? {};
      if (!playerId) return json({ error: "A player ID is required." }, 400);
      const session = await sessionFor(env, data?.sessionToken);
      if (!session) return json({ error: "Link your Speedrun.com account before matchmaking." }, 401);
      if (queueType !== "random" && queueType !== "ruleset") return json({ error: "Invalid queue type." }, 400);
      if (queueType === "ruleset" && (!CATEGORIES.includes(category) || !SUBCATEGORIES.includes(subcategory))) return json({ error: "Choose a valid category and subcategory." }, 400);
      const key = queueKey(queueType, category, subcategory);
      await env.DB.prepare(`DELETE FROM queue_entries WHERE player_id = ?`).bind(playerId).run();
      const waiting = await env.DB.prepare(`SELECT * FROM queue_entries WHERE queue_key = ?`).bind(key).first();
      if (!waiting) {
        await env.DB.prepare(`INSERT INTO queue_entries (queue_key, player_id, speedrun_user_id, queue_type, category, subcategory, joined_at) VALUES (?, ?, ?, ?, ?, ?, ?)`)
          .bind(key, playerId, session.speedrun_user_id, queueType, category ?? null, subcategory ?? null, new Date().toISOString()).run();
        return json({ status: "waiting", message: "Waiting for an opponent." });
      }
      if (waiting.player_id === playerId) return json({ status: "waiting", message: "You are already waiting in this queue." });
      const createdAtUtc = new Date().toISOString();
      const match = { id: crypto.randomUUID(), players: [{ playerId: waiting.player_id, speedrunUserId: waiting.speedrun_user_id }, { playerId, speedrunUserId: session.speedrun_user_id }], level: randomItem(LEVELS), category: queueType === "random" ? randomItem(CATEGORIES) : category, subcategory: queueType === "random" ? randomItem(SUBCATEGORIES) : subcategory, difficulty: "Easy", createdAtUtc };
      const matchJson = JSON.stringify(match);
      await env.DB.batch([
        env.DB.prepare(`DELETE FROM queue_entries WHERE queue_key = ?`).bind(key),
        env.DB.prepare(`INSERT OR REPLACE INTO matches (player_id, match_json, created_at) VALUES (?, ?, ?)`).bind(waiting.player_id, matchJson, createdAtUtc),
        env.DB.prepare(`INSERT OR REPLACE INTO matches (player_id, match_json, created_at) VALUES (?, ?, ?)`).bind(playerId, matchJson, createdAtUtc)
      ]);
      return json({ status: "matched", match });
    }

    if (request.method === "GET" && url.pathname.startsWith("/matches/")) {
      const playerId = decodeURIComponent(url.pathname.slice("/matches/".length));
      const result = await env.DB.prepare(`SELECT match_json FROM matches WHERE player_id = ?`).bind(playerId).first();
      return result ? json({ status: "matched", match: JSON.parse(result.match_json) }) : json({ status: "none" });
    }
    return json({ error: "Not found." }, 404);
  }
};
