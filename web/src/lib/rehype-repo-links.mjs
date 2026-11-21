// The repository's Markdown is written to be read on GitHub, so its links are relative to the file:
// `global_file_browser.md`, `../core/source/mtStructs.h`, wiki-style `developer_guide`. On the site
// those would 404. This rewrites each relative link once, at build time:
// - a doc that has a page here goes to that page (`/docs/<name>/`, lower-cased like Starlight's slugs);
// - anything else in the repo goes to the file or folder on GitHub.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const webDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const repoRoot = path.resolve(webDir, "..");
// Pages written by scripts/sync-docs.mjs.
const pagesDir = path.join(webDir, "src/content/docs/docs");

// Repo-relative Markdown path -> page slug ("" is the docs index), or null if it has no page.
function pageFor(rel) {
  if (rel === "docs/Home.md") return "";
  const name = rel === "CHANGELOG.md" ? "changelog" : rel.startsWith("docs/") ? rel.slice(5, -3).toLowerCase() : null;
  if (name && !name.includes("/") && fs.existsSync(path.join(pagesDir, `${name}.md`))) return name;
  return null;
}

function rewrite(href, fromDir, { base, repoUrl }) {
  if (!href || /^([a-z][a-z0-9+.-]*:|#|\/)/i.test(href)) return href;
  const [target, hash = ""] = href.split(/(?=#)/);
  let abs = path.resolve(fromDir, decodeURI(target));
  if (!fs.existsSync(abs) && fs.existsSync(`${abs}.md`)) abs = `${abs}.md`; // wiki-style links
  const rel = path.relative(repoRoot, abs).split(path.sep).join("/");
  if (rel.startsWith("..")) return href;
  if (rel.endsWith(".md")) {
    const slug = pageFor(rel);
    if (slug !== null) return `${base.replace(/\/?$/, "/")}docs/${slug ? `${slug}/` : ""}${hash}`;
  }
  const kind = fs.existsSync(abs) && fs.statSync(abs).isDirectory() ? "tree" : "blob";
  return `${repoUrl}/${kind}/main/${rel}${hash}`;
}

export default function rehypeRepoLinks(options) {
  return (tree, file) => {
    // Synced docs carry their repo path in `source`; anything else resolves from where it sits.
    const synced = file.data?.astro?.frontmatter?.source;
    const source = synced ? path.join(repoRoot, synced) : file.path || file.history?.[0];
    if (!source) return;
    const fromDir = path.dirname(source);
    const visit = (node) => {
      if (node.type === "element" && node.tagName === "a" && typeof node.properties?.href === "string") {
        node.properties.href = rewrite(node.properties.href, fromDir, options);
      }
      node.children?.forEach(visit);
    };
    visit(tree);
  };
}
