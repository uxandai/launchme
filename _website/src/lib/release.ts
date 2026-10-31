import { useEffect, useState } from "react";

export const REPO = "uxandai/launchme";
export const GITHUB_URL = `https://github.com/${REPO}`;
export const RELEASES_URL = `${GITHUB_URL}/releases`;
export const LATEST_URL = `${GITHUB_URL}/releases/latest`;
export const CHANGELOG_URL = `${GITHUB_URL}/tree/main/changelog`;
export const ISSUES_URL = `${GITHUB_URL}/issues`;
export const SDK_URL = `${GITHUB_URL}/blob/main/docs/plugins/README.md`;
export const LINUX_DOC_URL = `${GITHUB_URL}/blob/main/docs/LINUX_WINE.md`;
export const LICENSE_URL = `${GITHUB_URL}/blob/main/LICENSE`;
export const DISCORD_URL = "https://discord.gg/y2wk3kr9wW";

export type Asset = { name: string; size: number; url: string };
export type Release = {
  tag: string;
  version: string;
  date: string | null;
  url: string;
  windows: Asset | null;
  linux: Asset | null;
  stars: number | null;
};

// Snapshot used when neither the build nor the browser can reach the GitHub API.
export const FALLBACK_RELEASE: Release = {
  tag: "v0.4.7",
  version: "0.4.7",
  date: "2026-10-31T11:27:00Z",
  url: `${GITHUB_URL}/releases/tag/v0.4.7`,
  windows: {
    name: "launchme-0.4.7-win64.zip",
    size: 214464974,
    url: `${GITHUB_URL}/releases/download/v0.4.7/launchme-0.4.7-win64.zip`,
  },
  linux: {
    name: "launchme-0.4.7-x86_64.AppImage",
    size: 283634168,
    url: `${GITHUB_URL}/releases/download/v0.4.7/launchme-0.4.7-x86_64.AppImage`,
  },
  stars: 46,
};

type GhAsset = { name: string; size: number; browser_download_url: string };
type GhRelease = { tag_name: string; published_at: string | null; html_url: string; assets: GhAsset[] };

export function releaseFromApi(release: GhRelease, stars: number | null): Release {
  const pick = (test: (name: string) => boolean): Asset | null => {
    const found = release.assets.find((asset) => test(asset.name.toLowerCase()));
    return found ? { name: found.name, size: found.size, url: found.browser_download_url } : null;
  };
  return {
    tag: release.tag_name,
    version: release.tag_name.replace(/^v/, ""),
    date: release.published_at,
    url: release.html_url,
    windows: pick((name) => name.endsWith(".zip") && (name.includes("win") || name.includes("windows"))),
    linux: pick((name) => name.endsWith(".appimage") || name.includes("linux")),
    stars,
  };
}

export async function fetchRelease(fetcher: typeof fetch = fetch): Promise<Release> {
  const headers = { Accept: "application/vnd.github+json" };
  const [latest, repo] = await Promise.all([
    fetcher(`https://api.github.com/repos/${REPO}/releases/latest`, { headers }),
    fetcher(`https://api.github.com/repos/${REPO}`, { headers }).catch(() => null),
  ]);
  if (!latest.ok) throw new Error(`GitHub API ${latest.status}`);
  const data = (await latest.json()) as GhRelease;
  let stars: number | null = null;
  if (repo && repo.ok) {
    const info = (await repo.json()) as { stargazers_count?: number };
    stars = typeof info.stargazers_count === "number" ? info.stargazers_count : null;
  }
  return releaseFromApi(data, stars);
}

const CACHE_KEY = "launchme-release-v1";

// The baked release renders first (also in the static HTML); the browser then asks GitHub for
// the newest one, so a new release shows up without rebuilding the site.
export function useLiveRelease(initial: Release): Release {
  const [release, setRelease] = useState(initial);
  useEffect(() => {
    let cancelled = false;
    try {
      const cached = sessionStorage.getItem(CACHE_KEY);
      if (cached) {
        const parsed = JSON.parse(cached) as { at: number; release: Release };
        if (Date.now() - parsed.at < 30 * 60 * 1000) {
          setRelease(parsed.release);
          return;
        }
      }
    } catch {
      /* storage unavailable */
    }
    fetchRelease()
      .then((fresh) => {
        if (cancelled) return;
        const merged = { ...fresh, stars: fresh.stars ?? initial.stars };
        setRelease(merged);
        try {
          sessionStorage.setItem(CACHE_KEY, JSON.stringify({ at: Date.now(), release: merged }));
        } catch {
          /* ignore */
        }
      })
      .catch(() => {
        /* keep the baked release */
      });
    return () => {
      cancelled = true;
    };
  }, [initial]);
  return release;
}

export function formatSize(bytes: number | undefined | null): string {
  if (!bytes) return "";
  return `${Math.round(bytes / (1024 * 1024))} MB`;
}

export function formatDate(iso: string | null, lang: "en" | "pl"): string {
  if (!iso) return "";
  const date = new Date(iso);
  return date.toLocaleDateString(lang === "pl" ? "pl-PL" : "en-GB", { day: "numeric", month: "short", year: "numeric", timeZone: "UTC" });
}
