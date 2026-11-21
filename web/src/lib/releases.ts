// Fetches the repository's releases once, at build time, so the homepage always shows the newest
// one. If GitHub can't be reached the page still builds and points at the releases page instead.

import { releaseRepo } from "../config";

export type Device = "original" | "plus" | "mini";

export interface ReleaseFile {
  name: string;
  url: string;
  size: number;
}

export interface Release {
  tag: string;
  name: string;
  url: string;
  publishedAt: Date;
  prerelease: boolean;
  firmware: Partial<Record<Device, ReleaseFile>>;
  synths?: ReleaseFile;
  effects?: ReleaseFile;
}

export interface ReleaseInfo {
  // The newest stable release, if there is one.
  stable?: Release;
  // The newest pre-release, shown only when it's newer than the stable one.
  preview?: Release;
  // Set when the releases couldn't be read; the page then links to GitHub instead.
  error?: string;
}

interface GitHubAsset {
  name: string;
  browser_download_url: string;
  size: number;
}

interface GitHubRelease {
  tag_name: string;
  name: string | null;
  html_url: string;
  published_at: string | null;
  draft: boolean;
  prerelease: boolean;
  assets: GitHubAsset[];
}

// The firmware files the build workflow attaches, by device.
const firmwarePrefixes: Record<Device, string> = {
  original: "PolyendTracker_",
  plus: "TrackerPlus_",
  mini: "TrackerMini_",
};

function toRelease(r: GitHubRelease): Release {
  const file = (a: GitHubAsset): ReleaseFile => ({
    name: a.name,
    url: a.browser_download_url,
    size: a.size,
  });
  const firmware: Release["firmware"] = {};
  for (const [device, prefix] of Object.entries(firmwarePrefixes) as [Device, string][]) {
    const asset = r.assets.find((a) => a.name.startsWith(prefix) && a.name.endsWith(".ptf"));
    if (asset) firmware[device] = file(asset);
  }
  const synths = r.assets.find((a) => /Synths\.zip$/i.test(a.name));
  const effects = r.assets.find((a) => /Effects\.zip$/i.test(a.name));
  return {
    tag: r.tag_name,
    name: r.name || r.tag_name,
    url: r.html_url,
    publishedAt: new Date(r.published_at ?? Date.now()),
    prerelease: r.prerelease,
    firmware,
    synths: synths && file(synths),
    effects: effects && file(effects),
  };
}

let cached: Promise<ReleaseInfo> | undefined;

export function getReleaseInfo(): Promise<ReleaseInfo> {
  cached ??= load();
  return cached;
}

async function load(): Promise<ReleaseInfo> {
  const headers: Record<string, string> = {
    Accept: "application/vnd.github+json",
    "User-Agent": "tracker-ce-website",
  };
  // A token isn't needed, but it lifts the API's rate limit on shared CI runners.
  if (process.env.GITHUB_TOKEN) headers.Authorization = `Bearer ${process.env.GITHUB_TOKEN}`;

  try {
    const res = await fetch(`https://api.github.com/repos/${releaseRepo}/releases?per_page=20`, {
      headers,
      signal: AbortSignal.timeout(15000),
    });
    if (!res.ok) throw new Error(`GitHub answered ${res.status} for ${releaseRepo}`);
    const all = ((await res.json()) as GitHubRelease[])
      .filter((r) => !r.draft)
      .map(toRelease)
      .sort((a, b) => b.publishedAt.getTime() - a.publishedAt.getTime());

    const stable = all.find((r) => !r.prerelease);
    const newestPre = all.find((r) => r.prerelease);
    const preview =
      newestPre && (!stable || newestPre.publishedAt > stable.publishedAt) ? newestPre : undefined;
    return { stable, preview };
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err);
    console.warn(`[releases] couldn't read releases, linking to GitHub instead: ${message}`);
    return { error: message };
  }
}

// "Alpha 1", "Beta 2", "Release candidate 1" from a tag like v0.1.0-alpha.1.
export function stageOf(tag: string): string | undefined {
  const m = /-(alpha|beta|rc)\.?(\d+)?$/i.exec(tag);
  if (!m) return undefined;
  const word = { alpha: "Alpha", beta: "Beta", rc: "Release candidate" }[m[1].toLowerCase()];
  return m[2] ? `${word} ${m[2]}` : word;
}

export function formatSize(bytes: number): string {
  return bytes >= 1024 * 1024
    ? `${(bytes / (1024 * 1024)).toFixed(1)} MB`
    : `${Math.max(1, Math.round(bytes / 1024))} KB`;
}
