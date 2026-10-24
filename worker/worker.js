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

    if (request.method === "POST" && url.pathname === "/queue/cancel") {
      const data = await body(request);
      const playerId = typeof data?.playerId === "string" ? data.playerId.trim() : "";
      const session = await sessionFor(env, data?.sessionToken);
      if (!playerId) return json({ error: "A player ID is required." }, 400);
      if (!session) return json({ error: "Link your Speedrun.com account before matchmaking." }, 401);
      await env.DB.prepare(`DELETE FROM queue_entries WHERE player_id = ? AND speedrun_user_id = ?`)
        .bind(playerId, session.speedrun_user_id).run();
      return json({ status: "cancelled" });
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

    if (request.method === "POST"
        && url.pathname.startsWith("/matches/")
        && url.pathname.endsWith("/presence")) {
      const playerId = decodeURIComponent(
        url.pathname.slice("/matches/".length, -"/presence".length)
      );
      const data = await body(request);
      const session = await sessionFor(env, data?.sessionToken);
      const state = typeof data?.state === "string" ? data.state : "";
      const igtMilliseconds = data?.igtMilliseconds == null
        ? null : Number(data.igtMilliseconds);
      const allowedStates = new Set(["waiting", "ready", "arming", "armed", "running", "restarted", "finished"]);

      if (!playerId) return json({ error: "A player is required." }, 400);
      if (!session) return json({ error: "Link your Speedrun.com account before updating match status." }, 401);
      if (!allowedStates.has(state)) return json({ error: "Invalid match status." }, 400);
      if (igtMilliseconds != null
          && (!Number.isInteger(igtMilliseconds) || igtMilliseconds < 0 || igtMilliseconds > 86400000)) {
        return json({ error: "Invalid in-game time." }, 400);
      }

      const stored = await env.DB.prepare(`SELECT match_json FROM matches WHERE player_id = ?`)
        .bind(playerId).first();
      if (!stored) return json({ status: "none" });

      const match = JSON.parse(stored.match_json);
      const isParticipant = match.players?.some((player) =>
        player.playerId === playerId && player.speedrunUserId === session.speedrun_user_id
      );
      if (!isParticipant) return json({ error: "This status does not belong to your active match." }, 403);

      const now = new Date().toISOString();
      await env.DB.prepare(
        `INSERT INTO match_presence (match_id, player_id, state, igt_milliseconds, updated_at_utc)
         VALUES (?, ?, ?, ?, ?)
         ON CONFLICT(match_id, player_id) DO UPDATE SET
           state = excluded.state,
           igt_milliseconds = excluded.igt_milliseconds,
           updated_at_utc = excluded.updated_at_utc`
      ).bind(match.id, playerId, state, igtMilliseconds, now).run();

      const rows = await env.DB.prepare(
        `SELECT player_id, state, igt_milliseconds, updated_at_utc
         FROM match_presence WHERE match_id = ?`
      ).bind(match.id).all();
      const presenceByPlayer = new Map(
        (rows.results ?? []).map((row) => [row.player_id, row])
      );
      let navigationStartAtUtc = null;
      const bothPlayersReady = match.players.length === 2 && match.players.every(
        (player) => presenceByPlayer.get(player.playerId)?.state === "ready"
      );
      if (bothPlayersReady) {
        await env.DB.prepare(
          `INSERT OR IGNORE INTO match_navigation_schedule (match_id, start_at_utc, created_at_utc)
           VALUES (?, ?, ?)`
        ).bind(
          match.id,
          new Date(Date.now() + 5000).toISOString(),
          now
        ).run();
        const schedule = await env.DB.prepare(
          `SELECT start_at_utc FROM match_navigation_schedule WHERE match_id = ?`
        ).bind(match.id).first();
        navigationStartAtUtc = schedule?.start_at_utc ?? null;
      } else {
        await env.DB.prepare(
          `DELETE FROM match_navigation_schedule WHERE match_id = ?`
        ).bind(match.id).run();
      }
      const navigationDelayMilliseconds = navigationStartAtUtc
        ? Math.max(0, Date.parse(navigationStartAtUtc) - Date.now())
        : null;

      let startAtUtc = null;
      const bothPlayersArmed = match.players.length === 2 && match.players.every(
        (player) => presenceByPlayer.get(player.playerId)?.state === "armed"
      );
      if (bothPlayersArmed) {
        await env.DB.prepare(
          `INSERT OR IGNORE INTO match_start_schedule (match_id, start_at_utc, created_at_utc)
           VALUES (?, ?, ?)`
        ).bind(
          match.id,
          new Date(Date.now() + 8000).toISOString(),
          now
        ).run();
        const schedule = await env.DB.prepare(
          `SELECT start_at_utc FROM match_start_schedule WHERE match_id = ?`
        ).bind(match.id).first();
        startAtUtc = schedule?.start_at_utc ?? null;
      }
      const startDelayMilliseconds = startAtUtc
        ? Math.max(0, Date.parse(startAtUtc) - Date.now())
        : null;
      const outcome = await env.DB.prepare(
        `SELECT winner_player_id, winner_igt_milliseconds, decided_at_utc
         FROM match_outcomes WHERE match_id = ?`
      ).bind(match.id).first();
      return json({
        status: "matched",
        navigationStartAtUtc,
        navigationDelayMilliseconds,
        startAtUtc,
        startDelayMilliseconds,
        winnerPlayerId: outcome?.winner_player_id ?? null,
        winnerIgtMilliseconds: outcome?.winner_igt_milliseconds ?? null,
        decidedAtUtc: outcome?.decided_at_utc ?? null,
        players: match.players.map((player) => {
          const presence = presenceByPlayer.get(player.playerId);
          return {
            playerId: player.playerId,
            state: presence?.state ?? "waiting",
            igtMilliseconds: presence?.igt_milliseconds ?? null,
            updatedAtUtc: presence?.updated_at_utc ?? null
          };
        })
      });
    }
    if (request.method === "POST"
        && url.pathname.startsWith("/matches/")
        && url.pathname.endsWith("/forfeit")) {
      const playerId = decodeURIComponent(
        url.pathname.slice("/matches/".length, -"/forfeit".length)
      );
      const data = await body(request);
      const session = await sessionFor(env, data?.sessionToken);
      if (!playerId) return json({ error: "A player is required." }, 400);
      if (!session) return json({ error: "Link your Speedrun.com account before forfeiting." }, 401);

      const stored = await env.DB.prepare(`SELECT match_json FROM matches WHERE player_id = ?`)
        .bind(playerId).first();
      if (!stored) return json({ error: "No active match was found." }, 404);
      const match = JSON.parse(stored.match_json);
      const isParticipant = match.players?.some((player) =>
        player.playerId === playerId && player.speedrunUserId === session.speedrun_user_id
      );
      if (!isParticipant) return json({ error: "This forfeit does not belong to your active match." }, 403);
      const opponent = match.players?.find((player) => player.playerId !== playerId);
      if (!opponent) return json({ error: "A two-player match is required to forfeit." }, 409);

      const existingOutcome = await env.DB.prepare(
        `SELECT winner_player_id, decided_at_utc FROM match_outcomes WHERE match_id = ?`
      ).bind(match.id).first();
      if (existingOutcome) {
        return json({
          status: "already_decided",
          winnerPlayerId: existingOutcome.winner_player_id,
          decidedAtUtc: existingOutcome.decided_at_utc
        });
      }

      const now = new Date().toISOString();
      await env.DB.batch([
        env.DB.prepare(
          `INSERT OR IGNORE INTO match_forfeits (match_id, player_id, forfeited_at_utc)
           VALUES (?, ?, ?)`
        ).bind(match.id, playerId, now),
        env.DB.prepare(
          `INSERT OR IGNORE INTO match_outcomes
            (match_id, winner_player_id, winner_igt_milliseconds, decided_at_utc)
           VALUES (?, ?, ?, ?)`
        ).bind(match.id, opponent.playerId, 0, now)
      ]);
      const outcome = await env.DB.prepare(
        `SELECT winner_player_id, winner_igt_milliseconds, decided_at_utc
         FROM match_outcomes WHERE match_id = ?`
      ).bind(match.id).first();
      return json({
        status: outcome.winner_player_id === opponent.playerId ? "forfeited" : "already_decided",
        winnerPlayerId: outcome.winner_player_id,
        decidedAtUtc: outcome.decided_at_utc
      });
    }
    if (request.method === "POST"
        && url.pathname.startsWith("/matches/")
        && url.pathname.endsWith("/result")) {
      const playerId = decodeURIComponent(
        url.pathname.slice("/matches/".length, -"/result".length)
      );
      const data = await body(request);
      const session = await sessionFor(env, data?.sessionToken);
      const igtMilliseconds = Number(data?.igtMilliseconds);
      const buildHash = typeof data?.buildHash === "string" ? data.buildHash : "";

      if (!playerId) return json({ error: "A player is required." }, 400);
      if (!session) return json({ error: "Link your Speedrun.com account before submitting a result." }, 401);
      if (!Number.isInteger(igtMilliseconds) || igtMilliseconds <= 0 || igtMilliseconds > 86400000) {
        return json({ error: "The submitted in-game time is invalid." }, 400);
      }

      const stored = await env.DB.prepare(`SELECT match_json FROM matches WHERE player_id = ?`)
        .bind(playerId).first();
      if (!stored) return json({ error: "No active match was found." }, 404);

      const match = JSON.parse(stored.match_json);
      const isParticipant = match.players?.some((player) =>
        player.playerId === playerId && player.speedrunUserId === session.speedrun_user_id
      );
      if (!isParticipant) {
        return json({ error: "This result does not belong to your active match." }, 403);
      }
      if (data?.level !== match.level
          || data?.category !== match.category
          || data?.subcategory !== match.subcategory) {
        return json({ error: "This result does not match the active ruleset." }, 409);
      }

      const existingOutcome = await env.DB.prepare(
        `SELECT winner_player_id, winner_igt_milliseconds, decided_at_utc
         FROM match_outcomes WHERE match_id = ?`
      ).bind(match.id).first();
      if (existingOutcome) {
        return json({
          status: "already_decided",
          winnerPlayerId: existingOutcome.winner_player_id,
          winnerIgtMilliseconds: existingOutcome.winner_igt_milliseconds,
          decidedAtUtc: existingOutcome.decided_at_utc,
          youWon: existingOutcome.winner_player_id === playerId
        });
      }

      const now = new Date().toISOString();
      const writes = await env.DB.batch([
        env.DB.prepare(
          `INSERT OR IGNORE INTO match_results
            (match_id, speedrun_user_id, player_id, level, category, subcategory,
             igt_milliseconds, build_hash, confirmed_at_utc)
           VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`
        ).bind(
          match.id, session.speedrun_user_id, playerId, match.level, match.category,
          match.subcategory, igtMilliseconds, buildHash, now
        ),
        env.DB.prepare(
          `INSERT OR IGNORE INTO match_outcomes
            (match_id, winner_player_id, winner_igt_milliseconds, decided_at_utc)
           VALUES (?, ?, ?, ?)`
        ).bind(match.id, playerId, igtMilliseconds, now)
      ]);
      const outcome = await env.DB.prepare(
        `SELECT winner_player_id, winner_igt_milliseconds, decided_at_utc
         FROM match_outcomes WHERE match_id = ?`
      ).bind(match.id).first();

      return json({
        status: writes[0].meta.changes > 0 ? "submitted" : "already_submitted",
        winnerPlayerId: outcome.winner_player_id,
        winnerIgtMilliseconds: outcome.winner_igt_milliseconds,
        decidedAtUtc: outcome.decided_at_utc,
        youWon: outcome.winner_player_id === playerId
      });
    }
    if (request.method === "GET" && url.pathname.startsWith("/matches/")) {
      const playerId = decodeURIComponent(url.pathname.slice("/matches/".length));
      const result = await env.DB.prepare(`SELECT match_json FROM matches WHERE player_id = ?`).bind(playerId).first();
      return result ? json({ status: "matched", match: JSON.parse(result.match_json) }) : json({ status: "none" });
    }
    return json({ error: "Not found." }, 404);
  }
};
