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

- **Type.** Archivo (condensed widths) for headings, Atkinson Hyperlegible for reading, JetBrains Mono for anything that's tracker data: tags, file names, step numbers. All three are self-hosted through Fontsource, so there are no third-party font requests.
- **Colour.** The Tracker's own pattern column colours (note, instrument, FX 1, FX 2) mark the four feature groups. Every group also has a text label, so colour is never the only cue. The light theme uses darker versions of those colours so they pass contrast; dark mode uses the real ones.
- **Images.** The device images are SVG drawings from `src/components/TrackerDrawing.astro`, not product photos. Each one has a title and description for screen readers.
- **Accessibility.** A skip link, visible focus rings, 44 px targets on the buttons, real table headers on the download list, and no horizontal scroll down to 320 px.
