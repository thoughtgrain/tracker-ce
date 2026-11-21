# Tracker CE website

This is the community firmware's website, built with [Astro](https://astro.build) and published to GitHub Pages. It lives in `web/`, outside `core/` on purpose, because the firmware makefile compiles everything under those folders.

It has two parts:

- **`/`, the landing page** (`src/pages/index.astro`): the latest release, a feature tour, the devices, install steps, and links to Polyend.
- **`/docs/`, the docs**, built with [Starlight](https://starlight.astro.build): the repository's own Markdown (`docs/Home.md` as the docs home, `CHANGELOG.md` and every guide in `docs/`) with search (Pagefind), an "on this page" outline, edit-on-GitHub links and previous/next navigation. Joseph Venuto built the first docs site, which rendered these same files. This keeps his idea of using the repo's Markdown directly, so there's no second copy to keep in sync.

The landing page uses `src/components/Head.astro`, `src/components/SiteHeader.astro` and `src/styles/global.css`. Starlight uses `src/styles/starlight.css`, which maps the same colours and fonts onto Starlight's variables. Both are dark only: `src/components/starlight/` replaces Starlight's theme provider and theme picker.

## How the docs get into Starlight

The docs live where they always have, in `docs/` and the repo root, written for GitHub and the wiki. Starlight wants pages in `src/content/docs/` with a `title` in frontmatter. So `scripts/sync-docs.mjs` runs before every `dev` and `build`:

1. It copies `docs/Home.md` to `/docs/`, `CHANGELOG.md` to `/docs/changelog/`, and each `docs/<name>.md` to `/docs/<name>/` (lower case, like Starlight's slugs). The output goes to `src/content/docs/docs/`, which is gitignored.
2. It lifts each file's `# Title` into frontmatter, because Starlight prints the title itself.
3. It records the file's repo path in `source` and sets `editUrl`, so "Edit page" opens the real file on GitHub.

A new guide in `docs/` gets a page automatically. To list it in the sidebar, add it to `docs/_Sidebar.md`. `astro.config.mjs` builds Starlight's sidebar from that file (`#### Heading` is a group, `[Label](target)` is a page or an outside link), so the wiki and the site share one sidebar.

## How docs links work

The Markdown's links are relative to where each file lives in the repo: `global_file_browser.md`, `../core/source/mtStructs.h`, or wiki-style `developer_guide`. `src/lib/rehype-repo-links.mjs` rewrites each one at build time. It resolves the link from the file's real location (the `source` field), then:

- a doc that has a page goes to that page;
- anything else in the repo goes to the file or folder on GitHub.

It runs on Astro's remark/rehype Markdown pipeline (`@astrojs/markdown-remark`), which Starlight also needs. Astro 7's default Markdown processor doesn't run rehype plugins.

## How the release list stays current

I don't keep release info in the page by hand. `src/lib/releases.ts` calls the GitHub releases API once at build time and picks two releases:

- the newest stable release;
- the newest pre-release, but only when it's newer than that stable one. It gets a stage badge read from the tag, so `v0.1.0-alpha.1` shows as "Alpha 1".

Firmware files are matched by the names the build workflow gives them (`PolyendTracker_*.ptf`, `TrackerPlus_*.ptf`, `TrackerMini_*.ptf`), along with the Synths and Effects zips. If GitHub can't be reached, the build doesn't fail. The page says so and links the releases page instead, so a flaky API never blocks a deploy.

The site lives on the `work/website` branch, so `main` stays firmware only. `.github/workflows/web.yml` on this branch deploys it:

- on every push to `work/website`;
- when started by hand (Actions → Website → Run workflow, on `work/website`).

Each build pulls `docs/`, `CHANGELOG.md` and `README.md` from `main` first, so the docs pages always match the firmware's docs, even though this branch's own copies go stale. GitHub only runs workflows for events on `main` (releases, firmware builds) from files on `main`, so a new release reaches the homepage on the next deploy from this branch.

Pages has to be allowed to deploy from this branch: Settings → Environments → `github-pages` → Deployment branches and tags → add `work/website`.

## Release notes on the page

Each release in the download panel shows its notes under "What's new". They come from the same API call as the files, as `body_html`: GitHub renders and sanitises the Markdown, so PR titles in generated notes can't inject markup here. Where the notes come from depends on how the release was made (see `.github/workflows/build.yml`):

- **A pushed `v*` tag** gets GitHub's generated notes, grouped by PR label using `.github/release.yml` (New features, Synth engines and effects, Fixes, Docs and website, Build and tooling, Other changes). Give a PR the `skip-changelog` label to keep it out.
- **A manual run with `prerelease_tag`** publishes a pre-release whose notes are exactly the `release_notes` text given to the run.

## One-time setup

In the repository settings, go to **Pages → Build and deployment → Source** and choose **GitHub Actions**. `actions/configure-pages` hands the build the right site URL and base path (`/tracker-ce/` for a project page), so no config changes are needed.

## Working on it

```sh
cd web
npm ci
npm run dev            # local server with live reload
npm run build          # syncs the docs, then static output in dist/
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

I styled the page after the visual language of Polyend's own Tracker+ page, so it feels at home next to theirs. It doesn't borrow their logo, display font, photos or copy,.

- **Layout.** A near-black ground (`#1c1c1c`) with black panels at 28 px radius. The hero and the download list sit in panels. The intro is a two-column statement in grey with white highlights.
- **Type.** Bebas Neue for the tall, condensed, uppercase headlines (Polyend uses Tanker; Bebas Neue is the closest open-licence match). Albert Sans at weight 300 for body text, which is the same family Polyend uses and is OFL. JetBrains Mono for file names and tags. All three are self-hosted through Fontsource, so there are no third-party font requests.
- **Colour.** The feature cards use the pastel colours of the Tracker's Note, Instrument, FX1 and FX2 keys, with near-black text. Every card also names its group, so colour is never the only cue. The blurred light streaks behind the hero drawing use the same four colours and are hidden from screen readers.
- **Buttons.** Pills, either outlined or solid white, at least 44 px tall.
- **Icons.** [Phosphor](https://phosphoricons.com) only, inlined as SVG at build time (`src/components/Icon.astro`, `src/lib/phosphor.mjs`). The site has no emoji: `src/lib/rehype-emoji-icons.mjs` swaps any emoji in the rendered docs for a labelled Phosphor icon. The Markdown files keep theirs, because `README.md` and `CHANGELOG.md` come from upstream and editing them would conflict on every merge. An emoji with no icon mapped is dropped with a build warning; add it to the map in that file. Starlight's own interface icons (search, menu, arrows, edit, GitHub, code copy, heading links) are swapped for Phosphor too; `src/lib/starlight-icons.mjs` has the mapping and explains how.
- **Images.** The device images are SVG drawings from `src/components/TrackerDrawing.astro`, not product photos. Each one has a title and description.
- **Accessibility.** A skip link, visible focus rings, grey text at 7:1 or better, download links that name their file for screen readers, and no horizontal scroll at phone width.
- **Manuals.** Polyend's per-device downloads pages hold each manual. The links are in `src/config.ts`.
