// Everything a maintainer might want to change in one place.

// Which GitHub repository's releases the page shows. On GitHub Actions this is the repository
// the site is built in; RELEASE_REPO overrides it (for example polyend/tracker-ce upstream).
export const releaseRepo: string =
  process.env.RELEASE_REPO || process.env.GITHUB_REPOSITORY || "thoughtgrain/tracker-ce";

export const repoUrl = `https://github.com/${releaseRepo}`;
export const releasesUrl = `${repoUrl}/releases`;
export const docsUrl = (path: string) => `${repoUrl}/blob/main/${path}`;

// Polyend's own pages. Each device's downloads page carries its user manual and stock firmware.
export const polyend = {
  home: "https://polyend.com/",
  downloads: {
    original: "https://polyend.com/downloads/tracker-downloads/",
    plus: "https://polyend.com/downloads/tracker-plus-downloads/",
    mini: "https://polyend.com/downloads/tracker-mini-downloads/",
  },
  backstage: "https://backstage.polyend.com/",
  support: "https://backstage.polyend.com/t/how-to-contact-the-polyend-customer-support/15510",
};

export const recoveryVideo = "https://youtu.be/1F6wx2ftFVA";
