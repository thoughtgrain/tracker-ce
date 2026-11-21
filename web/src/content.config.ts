import { defineCollection } from "astro:content";
import { docsLoader } from "@astrojs/starlight/loaders";
import { docsSchema } from "@astrojs/starlight/schema";

// Starlight's docs collection. Its pages are generated from the repo's Markdown by
// scripts/sync-docs.mjs; `source` records which repo file a page came from.
export const collections = {
  docs: defineCollection({ loader: docsLoader(), schema: docsSchema() }),
};
