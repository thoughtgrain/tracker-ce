// @ts-check
import { defineConfig } from "astro/config";

// On GitHub Pages the workflow passes the site's URL and base path (a project site lives under
// /<repo>/). Locally both default so `npm run dev` just works.
export default defineConfig({
  site: process.env.SITE_URL || "https://thoughtgrain.github.io",
  base: process.env.BASE_PATH || "/",
  trailingSlash: "ignore",
});
