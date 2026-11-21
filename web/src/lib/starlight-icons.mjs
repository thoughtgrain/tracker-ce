// Puts Phosphor icons in place of Starlight's built-in set, so the docs' search, menu, arrows,
// edit link, heading links and GitHub link match the rest of the site.
//
// Starlight draws its interface icons from one table, `Icons` in
// @astrojs/starlight/dist/components-internals/Icons.js: inner SVG markup for a 24x24 viewBox.
// `starlightIconsVitePlugin()` rewrites that table as Vite loads it, which covers every page
// component. Heading anchor icons come from Starlight's Markdown step instead, outside Vite;
// astro.config.mjs masks those with Phosphor's link icon. Entries without a Phosphor counterpart
// keep Starlight's drawing.
import { iconShapes } from "./phosphor.mjs";

// Starlight icon name -> Phosphor icon name.
const map = {
  "up-caret": "caret-up",
  "down-caret": "caret-down",
  "right-caret": "caret-right",
  "left-caret": "caret-left",
  "up-arrow": "arrow-up",
  "down-arrow": "arrow-down",
  "right-arrow": "arrow-right",
  "left-arrow": "arrow-left",
  bars: "list",
  close: "x",
  magnifier: "magnifying-glass",
  pencil: "pencil-simple",
  pen: "pen",
  link: "link",
  "link-alt": "link-simple",
  external: "arrow-up-right",
  download: "download-simple",
  "cloud-download": "cloud-arrow-down",
  document: "file-text",
  "add-document": "file-plus",
  setting: "gear",
  translate: "translate",
  moon: "moon",
  sun: "sun",
  laptop: "laptop",
  desktop: "desktop",
  clock: "clock",
  information: "info",
  warning: "warning",
  error: "warning-octagon",
  "approve-check": "check",
  "approve-check-circle": "check-circle",
  question: "question",
  "question-circle": "question",
  rocket: "rocket",
  star: "star",
  puzzle: "puzzle-piece",
  "open-book": "book-open",
  notes: "notebook",
  "code-branch": "git-branch",
  "list-format": "list-bullets",
  heart: "heart",
  email: "envelope-simple",
  rss: "rss",
  github: "github-logo",
  gitlab: "gitlab-logo",
  discord: "discord-logo",
  youtube: "youtube-logo",
  "x.com": "x-logo",
  mastodon: "mastodon-logo",
  linkedin: "linkedin-logo",
};

// Phosphor draws on a 256 grid; Starlight's <svg> wrapper is 24 wide.
const scale = (inner) => `<g transform="scale(0.09375)">${inner}</g>`;

let overrides;
export function starlightIconOverrides() {
  if (!overrides) {
    overrides = {};
    for (const [starlightName, phosphorName] of Object.entries(map)) {
      try {
        overrides[starlightName] = scale(iconShapes(phosphorName));
      } catch {
        console.warn(`[starlight-icons] Phosphor has no "${phosphorName}"; keeping Starlight's ${starlightName}`);
      }
    }
  }
  return overrides;
}

const ICONS_FILE = /@astrojs\/starlight\/dist\/components-internals\/Icons\.js$/;

// Rewrites the icon table as Vite loads it.
export function starlightIconsVitePlugin() {
  return {
    name: "tracker-ce:starlight-phosphor-icons",
    enforce: "post",
    transform(code, id) {
      if (!ICONS_FILE.test(id.split("?")[0])) return null;
      return `${code}\nObject.assign(Icons, ${JSON.stringify(starlightIconOverrides())});\n`;
    },
  };
}
