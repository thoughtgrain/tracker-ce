# Tracker CE website

This is the landing page for the community firmware: a single static page built with [Astro](https://astro.build) and published to GitHub Pages. It lives outside `core/` on purpose, since the firmware makefile compiles everything under those folders.

## How the release list stays current

I don't keep release info in the page by hand. `src/lib/releases.ts` calls the GitHub releases API once at build time and picks two releases:

- the newest stable release;
- the newest pre-release, but only when it's newer than that stable one. It gets a stage badge read from the tag, so `v0.1.0-alpha.1` shows as "Alpha 1".

Firmware files are matched by the names the build workflow gives them (`PolyendTracker_*.ptf`, `TrackerPlus_*.ptf`, `TrackerMini_*.ptf`), along with the Synths and Effects zips. If GitHub can't be reached, the build doesn't fail. The page says so and links the releases page instead, so a flaky API never blocks a deploy.

`.github/workflows/website.yml` rebuilds the site:

- after each successful firmware workflow run. Releases the workflow publishes with `GITHUB_TOKEN` don't fire `release` events, so I hook `workflow_run` instead;
- on releases published by hand;
- on pushes to `main` that touch `website/`;
- on demand.

`workflow_run` and `release` only fire once the workflow file is on the default branch.

## One-time setup

In the repository settings, go to **Pages → Build and deployment → Source** and choose **GitHub Actions**. `actions/configure-pages` hands the build the right site URL and base path (`/tracker-ce/` for a project page), so no config changes are needed.

## Working on it

```sh
cd website
npm ci
npm run dev            # local server with live reload
npm run build          # static output in dist/
npm run check-release  # print the releases the page would show
```

Environment variables, all optional:

| Variable | Default | What it does |
| :--- | :--- | :--- |
| `RELEASE_REPO` | `GITHUB_REPOSITORY`, then `thoughtgrain/tracker-ce` | Which repository's releases to list |
| `GITHUB_TOKEN` | none | Lifts the API rate limit; CI passes it in |
| `SITE_URL`, `BASE_PATH` | `https://thoughtgrain.github.io`, `/` | Set by the Pages workflow |

Behind a proxy, Node's `fetch` ignores `HTTPS_PROXY` unless you set `NODE_USE_ENV_PROXY=1` (Node 22.21 and later).

## Design notes

I styled the page after the visual language of Polyend's own Tracker+ page, so it feels at home next to theirs. It doesn't borrow their logo, display font, photos or copy, and it says it's unofficial right at the top.

- **Layout.** A near-black ground (`#1c1c1c`) with black panels at 28 px radius. The hero and the download list sit in panels. The intro is a two-column statement in grey with white highlights.
- **Type.** Bebas Neue for the tall, condensed, uppercase headlines (Polyend uses Tanker; Bebas Neue is the closest open-licence match). Albert Sans at weight 300 for body text, which is the same family Polyend uses and is OFL. JetBrains Mono for file names and tags. All three are self-hosted through Fontsource, so there are no third-party font requests.
- **Colour.** The feature cards use the pastel colours of the Tracker's Note, Instrument, FX1 and FX2 keys, with near-black text. Every card also names its group, so colour is never the only cue. The blurred light streaks behind the hero drawing use the same four colours and are hidden from screen readers.
- **Buttons.** Pills, either outlined or solid white, at least 44 px tall.
- **Images.** The device images are SVG drawings from `src/components/TrackerDrawing.astro`, not product photos. Each one has a title and description.
- **Accessibility.** A skip link, visible focus rings, grey text at 7:1 or better, download links that name their file for screen readers, and no horizontal scroll at phone width.
- **Manuals.** Polyend's per-device downloads pages hold each manual. The links are in `src/config.ts`.
