const crypto = require("crypto");
const express = require("express");

const app = express();
const port = 3000;

app.use(express.json());

const levels = [
    "Mountain Caves",
    "City of Vilcabamba",
    "The Lost Valley",
    "Tomb of Qualopec",
    "St. Francis Folly",
    "The Coliseum",
    "Midas Palace",
    "Tomb of Tihocan",
    "Temple of Khamoon",
    "Obelisk of Khamoon",
    "Sanctuary of the Scion",
    "Natla's Mines",
    "The Great Pyramid",
    "Final Conflict"
];

const categories = ["Any%", "100%"];
const subcategories = [
    "No Bug Jump",
    "Bug Jump",
    "Better Than Glitchless",
    "Glitchless"
];

const waitingQueues = new Map();
const matchesByPlayer = new Map();

function randomItem(items) {
    return items[Math.floor(Math.random() * items.length)];
}

function queueKey(queueType, category, subcategory) {
    if (queueType === "random") {
        return "random";
    }

    return `ruleset:${category}:${subcategory}`;
}

app.get("/health", (request, response) => {
    response.json({
        status: "ok",
        service: "TRASR matchmaking server"
    });
});

const sessions = new Map();

app.post("/auth/speedrun", async (request, response) => {
    const apiKey =
        typeof request.body.apiKey === "string"
            ? request.body.apiKey.trim()
            : "";

    if (!apiKey) {
        return response.status(400).json({
            error: "A Speedrun.com API key is required."
        });
    }

    let speedrunResponse;

    try {
        speedrunResponse = await fetch(
            "https://www.speedrun.com/api/v1/profile",
            {
                headers: {
                    "X-API-Key": apiKey
                }
            }
        );
    } catch {
        return response.status(502).json({
            error: "Could not reach Speedrun.com."
        });
    }

    if (!speedrunResponse.ok) {
        return response.status(401).json({
            error: "Speedrun.com rejected that API key."
        });
    }

    const speedrunProfile = await speedrunResponse.json();
    const user = speedrunProfile.data;

    if (!user?.id) {
        return response.status(502).json({
            error: "Speedrun.com returned an unexpected profile."
        });
    }

    const sessionToken = crypto.randomUUID();

    // Keep our temporary TRASR session; never save the SRC API key.
    sessions.set(sessionToken, {
        speedrunUserId: user.id,
        displayName: user.names?.international ?? user.id,
        createdAtUtc: new Date().toISOString()
    });

    response.json({
        status: "linked",
        sessionToken,
        profile: {
            id: user.id,
            displayName: user.names?.international ?? user.id
        }
    });
});

app.post("/queue", (request, response) => {
    const { playerId, displayName, queueType, category, subcategory } =
        request.body;

    if (!playerId || !displayName) {
        return response.status(400).json({
            error: "playerId and displayName are required."
        });
    }

    if (queueType !== "random" && queueType !== "ruleset") {
        return response.status(400).json({
            error: 'queueType must be "random" or "ruleset".'
        });
    }

    if (
        queueType === "ruleset" &&
        (!categories.includes(category) || !subcategories.includes(subcategory))
    ) {
        return response.status(400).json({
            error: "Choose a valid category and subcategory for a ruleset queue."
        });
    }

    const key = queueKey(queueType, category, subcategory);
    const waitingPlayer = waitingQueues.get(key);

    if (!waitingPlayer) {
        waitingQueues.set(key, {
            playerId,
            displayName,
            queueType,
            category,
            subcategory
        });

        return response.json({
            status: "waiting",
            message: "Waiting for an opponent."
        });
    }

    if (waitingPlayer.playerId === playerId) {
        return response.json({
            status: "waiting",
            message: "You are already waiting in this queue."
        });
    }

    waitingQueues.delete(key);

    const matchCategory =
        queueType === "random" ? randomItem(categories) : category;

    const matchSubcategory =
        queueType === "random" ? randomItem(subcategories) : subcategory;

    const match = {
        id: crypto.randomUUID(),
        players: [
            {
                playerId: waitingPlayer.playerId,
                displayName: waitingPlayer.displayName
            },
            { playerId, displayName }
        ],
        level: randomItem(levels),
        category: matchCategory,
        subcategory: matchSubcategory,
        difficulty: "Easy"
    };

    for (const player of match.players) {
        matchesByPlayer.set(player.playerId, match);
    }

    response.json({
        status: "matched",
        match
    });
});

app.get("/matches/:playerId", (request, response) => {
    const match = matchesByPlayer.get(request.params.playerId);

    if (!match) {
        return response.json({
            status: "none"
        });
    }

    response.json({
        status: "matched",
        match
    });
});

app.listen(port, () => {
    console.log(`TRASR server is running at http://localhost:${port}`);
});