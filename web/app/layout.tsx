import type { Metadata } from "next";
import Image from "next/image";
import Link from "next/link";
import { Figtree, Geist_Mono, Shantell_Sans } from "next/font/google";
import "./globals.css";

const figtree = Figtree({
  subsets: ["latin"],
  weight: "variable",
  variable: "--font-figtree",
});

const shantellSans = Shantell_Sans({
  subsets: ["latin"],
  weight: "variable",
  variable: "--font-shantell-sans",
});

const geistMono = Geist_Mono({
  subsets: ["latin"],
  weight: "variable",
  variable: "--font-geist-mono",
});

export const metadata: Metadata = {
  title: { default: "prism", template: "%s · prism" },
  description: "manage prism cartridges and scores.",
};

const links = [
  ["home", "/"],
  ["leaderboard", "/leaderboard"],
  ["manage", "/manage"],
] as const;

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <body
        className={`${figtree.variable} ${shantellSans.variable} ${geistMono.variable} font-sans`}
      >
        <header className="border-b">
          <div className="mx-auto flex h-14 max-w-5xl items-center justify-between px-4">
            <Link href="/" aria-label="prism home">
              <Image
                src="/assembly/wordmark.png"
                alt="prism"
                width={684}
                height={293}
                priority
                className="h-7 w-auto mix-blend-multiply"
              />
            </Link>
            <nav className="flex gap-5 font-mono text-sm text-muted-foreground">
              {links.map(([label, href]) => <Link key={href} href={href} className="hover:text-foreground">{label}</Link>)}
            </nav>
          </div>
        </header>
        <main className="mx-auto max-w-5xl px-4 py-10">{children}</main>
      </body>
    </html>
  );
}
