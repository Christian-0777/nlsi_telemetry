import assert from "node:assert/strict";
import test from "node:test";

import {
  buildAnnouncement,
  classifyRelease,
  sendAnnouncement,
} from "../.github/scripts/discord-release.mjs";

const release = (overrides = {}) => ({
  id: 142,
  name: "NLSI Alpha",
  tag_name: "v1.4.0-alpha",
  prerelease: true,
  published_at: "2026-10-09T04:00:00Z",
  html_url: "https://github.com/Christian-0777/nlsi_telemetry/releases/tag/v1.4.0-alpha",
  body: "Local telemetry capture improvements.",
  assets: [
    {
      name: "NLSI Setup.exe",
      browser_download_url: "https://github.com/Christian-0777/nlsi_telemetry/releases/download/v1.4.0-alpha/setup.exe",
    },
  ],
  ...overrides,
});

test("classifies Alpha/Beta from tag suffix and Stable from tag plus metadata", () => {
  assert.equal(classifyRelease(release()), "alpha");
  assert.equal(classifyRelease(release({ tag_name: "v2.0.0-beta.1" })), "beta");
  assert.equal(
    classifyRelease(release({ tag_name: "v1.3.9-beta", prerelease: false })),
    "beta",
  );
  assert.equal(classifyRelease(release({ tag_name: "v2.0.0", prerelease: false })), "stable");
  assert.throws(
    () => classifyRelease(release({ tag_name: "v2.0.0", prerelease: true })),
    /do not identify/,
  );
});

test("builds a bounded embed with empty notes and missing assets handled", () => {
  const payload = buildAnnouncement(release({ body: " \n", assets: [{ name: "Not uploaded" }] }));
  assert.equal(payload.allowed_mentions.parse.length, 0);
  assert.match(payload.embeds[0].description, /No release notes/);
  assert.match(payload.embeds[0].fields[2].value, /No downloadable assets/);
  assert.equal(payload.embeds[0].fields[0].value, "v1.4.0-alpha");
});

test("truncates long notes within Discord embed limits and lists downloadable assets", () => {
  const payload = buildAnnouncement(release({
    name: "Release".repeat(100),
    body: "x".repeat(10_000),
  }));
  const embed = payload.embeds[0];
  assert.ok(embed.title.length <= 256);
  const total = embed.title.length
    + embed.description.length
    + embed.footer.text.length
    + embed.fields.reduce((sum, field) => sum + field.name.length + field.value.length, 0);
  assert.ok(embed.description.length <= 3500);
  assert.ok(total <= 6000);
  assert.match(embed.fields[2].value, /NLSI Setup\.exe/);
});

test("honors bounded Discord 429 retry-after and retries transient HTTP errors", async () => {
  const delays = [];
  let calls = 0;
  await sendAnnouncement(
    "https://discord.com/api/webhooks/123/secret-token",
    { content: "test" },
    {
      fetchImpl: async (url) => {
        assert.match(url, /wait=true/);
        calls += 1;
        if (calls === 1) {
          return {
            ok: false,
            status: 429,
            headers: { get: () => "2.5" },
            json: async () => ({ retry_after: 1 }),
          };
        }
        return { ok: true, status: 204 };
      },
      sleep: async (milliseconds) => delays.push(milliseconds),
    },
  );
  assert.equal(calls, 2);
  assert.deepEqual(delays, [2500]);

  calls = 0;
  delays.length = 0;
  await sendAnnouncement(
    "https://discord.com/api/webhooks/123/secret-token",
    {},
    {
      fetchImpl: async () => {
        calls += 1;
        return calls === 1
          ? {
              ok: false,
              status: 429,
              headers: { get: () => null },
              json: async () => ({ retry_after: 0.25 }),
            }
          : { ok: true, status: 204 };
      },
      sleep: async (milliseconds) => delays.push(milliseconds),
    },
  );
  assert.deepEqual(delays, [250]);

  calls = 0;
  await sendAnnouncement(
    "https://discord.com/api/webhooks/123/secret-token",
    {},
    {
      fetchImpl: async () => (++calls === 1
        ? { ok: false, status: 503 }
        : { ok: true, status: 204 }),
      sleep: async () => {},
    },
  );
  assert.equal(calls, 2);
});

test("fails safely for missing secrets, permanent HTTP errors, and ambiguous network failures", async () => {
  await assert.rejects(
    sendAnnouncement(undefined, {}),
    /DISCORD_RELEASE_WEBHOOK is not configured/,
  );
  await assert.rejects(
    sendAnnouncement(
      "https://discord.com/api/webhooks/123/secret-token",
      {},
      {
        fetchImpl: async () => ({ ok: false, status: 401 }),
      },
    ),
    /HTTP 401/,
  );
  await assert.rejects(
    sendAnnouncement(
      "https://discord.com/api/webhooks/123/secret-token",
      {},
      { fetchImpl: async () => { throw new Error("contains webhook secret-token"); } },
    ),
    (error) => {
      assert.doesNotMatch(error.message, /secret-token/);
      return /not retried to avoid a possible duplicate/.test(error.message);
    },
  );
});

test("bounds repeated rate-limit responses and rejects excessive retry delays", async () => {
  let calls = 0;
  const waits = [];
  const secret = "https://discord.com/api/webhooks/123/secret-token";
  const limited = {
    ok: false,
    status: 429,
    headers: { get: () => "0" },
    json: async () => ({ retry_after: 0 }),
  };
  await assert.rejects(
    sendAnnouncement(secret, {}, {
      fetchImpl: async () => {
        calls += 1;
        return limited;
      },
      sleep: async (milliseconds) => waits.push(milliseconds),
    }),
    /persisted after the bounded retries/,
  );
  assert.equal(calls, 4);
  assert.equal(waits.length, 3);

  calls = 0;
  await assert.rejects(
    sendAnnouncement(secret, {}, {
      fetchImpl: async () => {
        calls += 1;
        return {
          ...limited,
          headers: { get: () => "121" },
        };
      },
      sleep: async () => assert.fail("Must not retry before an excessive rate limit expires."),
    }),
    /exceeds the bounded retry window/,
  );
  assert.equal(calls, 1);
});
