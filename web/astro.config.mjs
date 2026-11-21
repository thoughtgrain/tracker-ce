// @ts-check
import { defineConfig } from "astro/config";
import rehypeRepoLinks from "./src/lib/rehype-repo-links.mjs";

// On GitHub Pages the workflow passes the site's URL and base path (a project site lives under
// /<repo>/). Locally both default so `npm run dev` just works.
const base = process.env.BASE_PATH || "/";
const repo = process.env.RELEASE_REPO || process.env.GITHUB_REPOSITORY || "thoughtgrain/tracker-ce";

export default defineConfig({
  site: process.env.SITE_URL || "https://thoughtgrain.github.io",
  base,
  trailingSlash: "ignore",
  markdown: {
    // The docs are written for GitHub, so their relative links point at .md files and source files.
    rehypePlugins: [[rehypeRepoLinks, { base, repoUrl: `https://github.com/${repo}` }]],
  },
});
