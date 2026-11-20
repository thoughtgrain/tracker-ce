// Prints what the homepage's release panel will show, without building the site.
// Usage: npm run check-release   (RELEASE_REPO=owner/repo to look at another repository)
const repo = process.env.RELEASE_REPO || process.env.GITHUB_REPOSITORY || "thoughtgrain/tracker-ce";
const res = await fetch(`https://api.github.com/repos/${repo}/releases?per_page=20`, {
  headers: { Accept: "application/vnd.github+json", "User-Agent": "tracker-ce-website" },
});
if (!res.ok) {
  console.error(`GitHub answered ${res.status} for ${repo}`);
  process.exit(1);
}
const releases = (await res.json()).filter((r) => !r.draft);
for (const r of releases.slice(0, 5)) {
  console.log(`${r.prerelease ? "pre-release" : "stable     "}  ${r.tag_name}  ${r.published_at}`);
  for (const a of r.assets) console.log(`               ${a.name}`);
}
