import { AnimatePresence, motion, useMotionValueEvent, useScroll, useSpring } from "motion/react";
import { Download, Menu, Star, X } from "lucide-react";
import { useState } from "react";
import { LogoFull } from "../components/Logo";
import { DiscordIcon, GitHubIcon } from "../components/ui";
import { cn } from "../lib/cn";
import { usePage, type Lang } from "../lib/page";
import { CHANGELOG_URL, DISCORD_URL, GITHUB_URL, LATEST_URL } from "../lib/release";

function LangSwitch({ className }: { className?: string }) {
  const { lang, layout, href } = usePage();
  return (
    <div className={cn("relative flex items-center rounded-full border border-line-strong p-0.5 text-[12.5px] font-semibold", className)} role="group" aria-label="Language">
      {(["en", "pl"] as Lang[]).map((code) => (
        <a
          key={code}
          href={href(code, layout)}
          hrefLang={code}
          aria-current={code === lang ? "true" : undefined}
          className={cn("relative rounded-full px-2.5 py-1 uppercase transition-colors", code === lang ? "text-ink" : "text-white/60 hover:text-white")}
        >
          {code === lang && <span className="absolute inset-0 rounded-full bg-white" />}
          <span className="relative">{code}</span>
        </a>
      ))}
    </div>
  );
}

export function Nav() {
  const { t, release, href, lang, layout } = usePage();
  const { scrollY, scrollYProgress } = useScroll();
  const progress = useSpring(scrollYProgress, { stiffness: 200, damping: 40, mass: 0.2 });
  const [scrolled, setScrolled] = useState(false);
  const [open, setOpen] = useState(false);
  useMotionValueEvent(scrollY, "change", (y) => setScrolled(y > 24));

  const links = [
    { href: "#layouts", label: t.nav.layouts },
    { href: "#views", label: t.nav.views },
    { href: "#features", label: t.nav.features },
    { href: "#companion", label: t.nav.companion },
    { href: "#linux", label: t.nav.linux },
    { href: CHANGELOG_URL, label: t.nav.changelog, external: true },
  ];
  const download = release.windows?.url ?? LATEST_URL;

  return (
    <header className="fixed inset-x-0 top-0 z-50">
      {/* A solid bar fades in under the links once the page moves, with a hairline and the read position. */}
      <div aria-hidden className={cn("pointer-events-none absolute inset-0 transition-opacity duration-500", scrolled ? "opacity-100" : "opacity-0")}>
        <div className="nav-bar absolute inset-0" />
        <div className="absolute inset-x-0 bottom-0 h-px bg-[linear-gradient(90deg,transparent,rgb(255_255_255/0.14)_20%,rgb(255_255_255/0.14)_80%,transparent)]" />
        <motion.div className="absolute bottom-0 left-0 h-px w-full origin-left bg-[linear-gradient(90deg,transparent,var(--ambient)_30%,#fff)]" style={{ scaleX: progress }} />
      </div>

      <motion.nav
        initial={{ y: -30, opacity: 0 }}
        animate={{ y: 0, opacity: 1 }}
        transition={{ duration: 0.6, ease: [0.22, 1, 0.36, 1] }}
        className={cn("relative mx-auto flex max-w-6xl items-center gap-2 px-4 transition-[padding] duration-500", scrolled ? "py-2.5" : "py-4")}
      >
        <a href={href(lang, layout)} className="flex items-center rounded-full py-1.5 pr-3" aria-label="launchme">
          <LogoFull className="h-[22px] w-auto text-white" />
        </a>

        <div className="ml-4 hidden items-center gap-0.5 lg:flex">
          {links.map((link) => (
            <a
              key={link.label}
              href={link.href}
              target={link.external ? "_blank" : undefined}
              rel={link.external ? "noreferrer" : undefined}
              className="rounded-full px-3 py-2 text-[14px] text-white/70 transition-colors hover:bg-white/8 hover:text-white"
            >
              {link.label}
            </a>
          ))}
        </div>

        <div className="ml-auto flex items-center gap-2">
          <LangSwitch className="hidden sm:flex" />
          <a
            href={DISCORD_URL}
            target="_blank"
            rel="noreferrer"
            aria-label={t.nav.discord}
            title={t.nav.discord}
            className="hidden size-10 items-center justify-center rounded-full border border-line-strong bg-white/5 text-white/90 transition-colors hover:bg-white/10 sm:inline-flex"
          >
            <DiscordIcon className="size-[18px]" />
          </a>
          <a
            href={GITHUB_URL}
            target="_blank"
            rel="noreferrer"
            className="hidden h-10 items-center gap-2 rounded-full border border-line-strong bg-white/5 px-4 text-[14px] text-white/90 transition-colors hover:bg-white/10 md:inline-flex"
          >
            <GitHubIcon className="size-4" />
            {t.nav.github}
            {release.stars != null && (
              <span className="ml-1 inline-flex items-center gap-1 border-l border-line-strong pl-2.5 text-white/70">
                <Star className="size-3.5 fill-current" />
                <span className="tabular-nums">{release.stars}</span>
              </span>
            )}
          </a>
          <a
            href={download}
            className="inline-flex h-10 items-center gap-2 rounded-full bg-white px-4 text-[14px] font-semibold text-ink transition-transform hover:scale-[1.03] active:scale-95"
          >
            <Download className="size-4" />
            {t.nav.download}
            <span className="hidden font-normal text-ink/55 sm:inline">v{release.version}</span>
          </a>
          <button
            type="button"
            className="inline-flex size-10 items-center justify-center rounded-full border border-line-strong text-white lg:hidden"
            onClick={() => setOpen((value) => !value)}
            aria-label={t.nav.menu}
            aria-expanded={open}
          >
            {open ? <X className="size-5" /> : <Menu className="size-5" />}
          </button>
        </div>
      </motion.nav>

      <AnimatePresence>
        {open && (
          <motion.div
            initial={{ opacity: 0, y: -12, scale: 0.98 }}
            animate={{ opacity: 1, y: 0, scale: 1 }}
            exit={{ opacity: 0, y: -12, scale: 0.98 }}
            className="nav-bar absolute inset-x-3 top-[68px] rounded-3xl border border-line p-3 lg:hidden"
          >
            {links.map((link) => (
              <a key={link.label} href={link.href} onClick={() => setOpen(false)} className="block rounded-2xl px-4 py-3 text-[16px] text-white/85 hover:bg-white/8">
                {link.label}
              </a>
            ))}
            <div className="flex items-center justify-between px-4 py-3">
              <a href={GITHUB_URL} className="flex items-center gap-2 text-[16px] text-white/85">
                <GitHubIcon className="size-4" /> GitHub
              </a>
              <a href={DISCORD_URL} className="flex items-center gap-2 text-[16px] text-white/85">
                <DiscordIcon className="size-4" /> Discord
              </a>
              <LangSwitch />
            </div>
          </motion.div>
        )}
      </AnimatePresence>
    </header>
  );
}
