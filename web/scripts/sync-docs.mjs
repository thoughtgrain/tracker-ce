// Copies the repository's Markdown into Starlight's content folder before each dev or build run.
// The repo's docs stay the only source: they're written for GitHub and the wiki, so they have no
// frontmatter and start with a `# Title`. Starlight wants a `title` field, so I lift the H1 into
// frontmatter and drop it from the body (Starlight prints the title itself).
//
// Output goes to src/content/docs/docs/ (gitignored), which Starlight serves under /docs/.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const webDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const repoRoot = path.resolve(webDir, "..");
const outDir = path.join(webDir, "src/content/docs/docs");
const repo = process.env.RELEASE_REPO || process.env.GITHUB_REPOSITORY || "thoughtgrain/tracker-ce";

// Repo path -> output name. docs/Home.md is the wiki's front page, so it becomes /docs/.
const sources = [
  ["docs/Home.md", "index"],
  ["CHANGELOG.md", "changelog"],
  ...fs
    .readdirSync(path.join(repoRoot, "docs"))
    .filter((f) => f.endsWith(".md") && !f.startsWith("_") && f !== "Home.md")
    .map((f) => [`docs/${f}`, f.slice(0, -3).toLowerCase()]),
];

fs.rmSync(outDir, { recursive: true, force: true });
fs.mkdirSync(outDir, { recursive: true });

for (const [rel, name] of sources) {
  const text = fs.readFileSync(path.join(repoRoot, rel), "utf8").replace(/\r\n/g, "\n");
  const match = /^#\s+(.+?)\s*#*\s*$/m.exec(text);
  if (!match) throw new Error(`${rel} has no "# Title" line for the page title`);
  const body = text.slice(0, match.index) + text.slice(match.index + match[0].length);
  // JSON strings are valid YAML, which keeps titles with ":" or "&" safe.
  const frontmatter = [
    "---",
    `title: ${JSON.stringify(match[1].replace(/`/g, ""))}`,
    `editUrl: ${JSON.stringify(`https://github.com/${repo}/edit/main/${rel}`)}`,
    // Read by src/lib/rehype-repo-links.mjs to resolve the doc's relative links from its real home.
    `source: ${JSON.stringify(rel)}`,
    "---",
    "",
  ].join("\n");
  fs.writeFileSync(path.join(outDir, `${name}.md`), frontmatter + body.replace(/^\n+/, ""));
}
console.log(`[sync-docs] ${sources.length} pages -> ${path.relative(webDir, outDir)}`);
