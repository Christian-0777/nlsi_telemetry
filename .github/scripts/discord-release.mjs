import { resolve } from "node:path";
import { pathToFileURL } from "node:url";

const CHANNEL_COLORS = {
  alpha: 0x7589ff,
  beta: 0xf0b232,
  stable: 0x43a047,
};

const MAX_NOTES_LENGTH = 3500;
const MAX_ASSETS_LENGTH = 1000;
const MAX_ATTEMPTS = 4;
const MAX_RATE_LIMIT_WAIT_SECONDS = 120;

function clipped(text, limit) {
  if (text.length <= limit) return text;
  return `${text.slice(0, limit - 1).trimEnd()}…`;
}

export function classifyRelease(release) {
  const tag = release?.tag_name;
  const prerelease = release?.prerelease;
  if (typeof tag !== "string" || typeof prerelease !== "boolean") {
    throw new Error("Release tag or prerelease metadata is missing.");
  }

  const versionTag = tag.replace(/^v/i, "");
  const match = versionTag.match(/^\d+\.\d+\.\d+-(alpha|beta)(?:\.\d+)?$/i);
  if (match) return match[1].toLowerCase();
  if (/^\d+\.\d+\.\d+$/.test(versionTag) && !prerelease) return "stable";
  throw new Error("Release tag and prerelease metadata do not identify Alpha, Beta, or Stable.");
}

function assetLine(asset) {
  if (typeof asset.browser_download_url !== "string") return null;
  const name = String(asset.name ?? "Download").replace(/[\\[\]()]/g, "\\$&");
  let url;
  try {
    url = new URL(asset.browser_download_url);
  } catch {
    throw new Error("Release asset metadata contains an invalid download URL.");
  }
  if (url.protocol !== "https:" || url.hostname !== "github.com") return null;
  return `• [${clipped(name, 120)}](<${url.href}>)`;
}

function assetSummary(assets) {
  const lines = assets.map(assetLine).filter(Boolean);
  if (lines.length === 0) return "No downloadable assets are attached.";

  const shown = [];
  for (const line of lines) {
    const remaining = lines.length - shown.length - 1;
    const suffix = remaining > 0 ? `\n… and ${remaining} more` : "";
    if (`${shown.join("\n")}${shown.length ? "\n" : ""}${line}${suffix}`.length > MAX_ASSETS_LENGTH) {
      break;
    }
    shown.push(line);
  }
  const omitted = lines.length - shown.length;
  if (omitted > 0) shown.push(`… and ${omitted} more downloadable asset(s)`);
  return clipped(shown.join("\n"), MAX_ASSETS_LENGTH);
}

export function buildAnnouncement(release) {
  const channel = classifyRelease(release);
  const tag = String(release.tag_name);
  const name = clipped(String(release.name || tag), 256);
  const url = new URL(release.html_url);
  if (url.protocol !== "https:" || url.hostname !== "github.com") {
    throw new Error("Release URL is not a GitHub HTTPS URL.");
  }

  const publishedAt = Date.parse(release.published_at);
  if (!Number.isFinite(publishedAt)) {
    throw new Error("Release publication date is missing or invalid.");
  }
  const releaseId = String(release.id ?? "");
  if (!releaseId) throw new Error("Release ID is missing.");

  const notes = typeof release.body === "string" ? release.body.trim() : "";
  const description = notes
    ? clipped(notes, MAX_NOTES_LENGTH)
    : "No release notes were provided.";
  const assets = Array.isArray(release.assets) ? release.assets : [];

  return {
    username: "NLSI Release Updates",
    allowed_mentions: { parse: [] },
    embeds: [
      {
        title: clipped(`${channel[0].toUpperCase()}${channel.slice(1)} · ${name}`, 256),
        url: url.href,
        color: CHANNEL_COLORS[channel],
        description,
        fields: [
          { name: "Tag / version", value: clipped(tag, 256), inline: true },
          {
            name: "Published",
            value: `<t:${Math.floor(publishedAt / 1000)}:D>`,
            inline: true,
          },
          { name: "Downloadable release assets", value: assetSummary(assets) },
        ],
        footer: { text: `NLSI Exclusive Logbook · Release ${releaseId}` },
      },
    ],
  };
}

function webhookEndpoint(secret) {
  if (typeof secret !== "string" || !secret) {
    throw new Error("Required Actions secret DISCORD_RELEASE_WEBHOOK is not configured.");
  }
  let endpoint;
  try {
    endpoint = new URL(secret);
  } catch {
    throw new Error("DISCORD_RELEASE_WEBHOOK is not a valid Discord webhook URL.");
  }
  if (
    endpoint.protocol !== "https:" ||
    endpoint.hostname !== "discord.com" ||
    !/^\/api\/webhooks\/\d+\/[^/]+\/?$/.test(endpoint.pathname)
  ) {
    throw new Error("DISCORD_RELEASE_WEBHOOK is not a supported Discord HTTPS webhook URL.");
  }
  endpoint.searchParams.set("wait", "true");
  return endpoint.href;
}

function retryDelaySeconds(response, body) {
  const header = response.headers.get("retry-after");
  const headerDelay = typeof header !== "string" || header.trim() === ""
    ? Number.NaN
    : Number(header);
  const bodyDelay = Number(body?.retry_after);
  const delay = Number.isFinite(headerDelay) && headerDelay >= 0
    ? headerDelay
    : bodyDelay;
  if (!Number.isFinite(delay) || delay < 0) return null;
  return delay;
}

const sleep = (milliseconds) =>
  new Promise((resolve) => setTimeout(resolve, milliseconds));

export async function sendAnnouncement(secret, payload, options = {}) {
  const endpoint = webhookEndpoint(secret);
  const fetchImpl = options.fetchImpl ?? fetch;
  const wait = options.sleep ?? sleep;
  const attempts = options.maxAttempts ?? MAX_ATTEMPTS;

  for (let attempt = 1; attempt <= attempts; attempt += 1) {
    let response;
    try {
      response = await fetchImpl(endpoint, {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify(payload),
        redirect: "error",
        signal: AbortSignal.timeout(15_000),
      });
    } catch {
      throw new Error("Webhook request failed without confirmation; not retried to avoid a possible duplicate.");
    }

    if (response.ok) return;

    if (response.status === 429) {
      let body = null;
      try {
        body = await response.json();
      } catch {
        body = null;
      }
      const delay = retryDelaySeconds(response, body);
      if (delay === null) {
        throw new Error("Discord rate limited the webhook without a usable retry-after value.");
      }
      if (delay > MAX_RATE_LIMIT_WAIT_SECONDS) {
        throw new Error("Discord rate limit exceeds the bounded retry window; announcement was not retried.");
      }
      if (attempt === attempts) {
        throw new Error("Discord rate limit persisted after the bounded retries.");
      }
      await wait(delay * 1000);
      continue;
    }

    if ([408, 425, 500, 502, 503, 504].includes(response.status)) {
      if (attempt === attempts) {
        throw new Error(`Discord returned HTTP ${response.status} after the bounded retries.`);
      }
      await wait(1000 * 2 ** (attempt - 1));
      continue;
    }

    throw new Error(`Discord rejected the webhook with HTTP ${response.status}.`);
  }
}

async function main() {
  const eventPath = process.env.GITHUB_EVENT_PATH;
  if (!eventPath) throw new Error("GitHub release event payload path is unavailable.");
  const { readFile } = await import("node:fs/promises");
  const event = JSON.parse(await readFile(eventPath, "utf8"));
  const payload = buildAnnouncement(event.release);
  await sendAnnouncement(process.env.DISCORD_RELEASE_WEBHOOK, payload);
  console.log("Discord release announcement delivered.");
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  main().catch((error) => {
    console.error(error.message);
    process.exitCode = 1;
  });
}
