// @ts-check
import fs from "node:fs";
import { defineConfig } from "astro/config";
import rehypeRepoLinks from "./src/lib/rehype-repo-links.mjs";
import rehypeEmojiIcons from "./src/lib/rehype-emoji-icons.mjs";
import { iconDataUrl } from "./src/lib/phosphor.mjs";
import { starlightIconsVitePlugin } from "./src/lib/starlight-icons.mjs";
import starlight from "@astrojs/starlight";

// On GitHub Pages the workflow passes the site's URL and base path (a project site lives under
// /<repo>/). Locally both default so `npm run dev` just works.
const base = process.env.BASE_PATH || "/";
const repo = process.env.RELEASE_REPO || process.env.GITHUB_REPOSITORY || "thoughtgrain/tracker-ce";
const repoUrl = `https://github.com/${repo}`;

// The docs sidebar comes from docs/_Sidebar.md, the same file the GitHub wiki uses, so the two
// never drift. `#### Heading` starts a group; `[Label](target)` is a page or an outside link.
function sidebarFromWiki() {
  const md = fs.readFileSync(new URL("../docs/_Sidebar.md", import.meta.url), "utf8");
  const groups = [{ label: "Tracker CE", items: [] }];
  for (const line of md.split(/\r?\n/)) {
    const heading = /^####\s+(.+)/.exec(line);
    if (heading) {
      groups.push({ label: heading[1].trim(), items: [] });
      continue;
    }
    const link = /\[([^\]]+)\]\(([^)]+)\)/.exec(line);
    if (!link || !/^\s*[*-]/.test(line)) continue;
    const [, label, target] = link;
    const item = /^https?:/.test(target)
      ? { label, link: target, attrs: { rel: "noopener" } }
      : { label, slug: target === "Home" ? "docs" : `docs/${target.toLowerCase()}` };
    groups.at(-1).items.push(item);
  }
  return groups.filter((g) => g.items.length);
}

export default defineConfig({
  site: process.env.SITE_URL || "https://thoughtgrain.github.io",
  base,
  trailingSlash: "ignore",
  markdown: {
    // The docs are written for GitHub: their relative links point at .md files and source files,
    // and they use emoji, which the site shows as Phosphor icons instead.
    rehypePlugins: [[rehypeRepoLinks, { base, repoUrl }], rehypeEmojiIcons],
  },
  vite: { plugins: [starlightIconsVitePlugin()] },
  integrations: [
    starlight({
      title: "Tracker CE Docs",
      description: "Guides and developer docs for the Polyend Tracker Community Edition, the official community firmware.",
      logo: { src: "./src/assets/mark.svg" },
      favicon: "/favicon.svg",
      social: [{ icon: "github", label: "Source on GitHub", href: repoUrl }],
      sidebar: sidebarFromWiki(),
      // The code-block copy button is drawn by Expressive Code, not Starlight's icon table.
      expressiveCode: { styleOverrides: { frames: { copyIcon: iconDataUrl("copy") } } },
      // Heading anchors get their icon from Starlight's Markdown step, which the Vite plugin
      // can't reach, so draw Phosphor's link icon over them with a mask instead.
      head: [
        {
          tag: "style",
          content: `.sl-anchor-icon svg{background-color:currentColor;-webkit-mask:${iconDataUrl("link-simple")} center/contain no-repeat;mask:${iconDataUrl("link-simple")} center/contain no-repeat}.sl-anchor-icon svg>*{display:none}`,
        },
      ],
      customCss: [
        "@fontsource/bebas-neue/400.css",
        "@fontsource-variable/albert-sans/wght.css",
        "@fontsource/jetbrains-mono/500.css",
        "./src/styles/starlight.css",
      ],
      // The landing page is dark only, so the docs are too.
      components: {
        ThemeProvider: "./src/components/starlight/ThemeProvider.astro",
        ThemeSelect: "./src/components/starlight/ThemeSelect.astro",
      },
    }),
  ],
});
