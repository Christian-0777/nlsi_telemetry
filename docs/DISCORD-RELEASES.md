# Discord release announcements

`.github/workflows/discord-release.yml` runs only for the GitHub Release
`published` event. It uses the tag suffix as the Alpha/Beta channel convention:
`vX.Y.Z-alpha` and `vX.Y.Z-beta` remain Alpha and Beta even if the GitHub
prerelease flag is inconsistent. An unqualified `vX.Y.Z` tag is Stable only
when GitHub marks the release as non-prerelease. Unknown tag forms or missing
prerelease metadata fail closed rather than publishing a guessed label.

## Setup

1. In Discord, open the destination channel's **Edit Channel → Integrations →
   Webhooks**, create a webhook, and copy its URL. Do not paste the URL into
   source files, workflow YAML, issue comments, or logs.
2. In the GitHub repository, open **Settings → Secrets and variables → Actions
   → New repository secret**. Set the name to `DISCORD_RELEASE_WEBHOOK` and
   the value to the copied webhook URL.
3. Publish a GitHub Release with an Alpha/Beta suffix and prerelease enabled,
   or an unqualified stable tag with prerelease disabled. Drafts and releases
   that are only created do not trigger the workflow.

The workflow uses the release's name, tag, `published_at` date, notes, URL, and
downloadable assets. Empty notes/assets get explicit fallback text. Notes and
assets are clipped to Discord embed limits, and mention parsing is disabled.
The secret is supplied only to the sending step; the script never prints it or
the webhook URL. Permissions are read-only (`contents: read`).

## Safe testing and troubleshooting

The local Node tests mock Discord responses and do not send messages. For an
end-to-end test, use a separate test channel webhook and a test/fork repository
with the same workflow, then publish an unmistakably named `TEST` prerelease;
do not temporarily point the production secret at a test channel. A published
test release may be publicly visible in a public repository.

Check the Actions run for a missing-secret, metadata, or HTTP status error.
For HTTP 429 the sender honors Discord's `Retry-After`/`retry_after` value,
with at most four attempts and a bounded wait; retryable transient HTTP errors
use bounded exponential backoff. A network failure with an unknown delivery
outcome is not retried, to avoid creating a duplicate. The workflow serializes
runs for the same GitHub release ID. Discord webhooks do not provide a durable
idempotency key, so an operator-initiated rerun or an ambiguous server failure
can still result in a duplicate message. This integration has not been tested
against a live Discord channel.
