import type { Metadata } from "next";
import Link from "next/link";
import "./globals.css";

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
      <body>
        <header className="border-b">
          <div className="mx-auto flex h-14 max-w-5xl items-center justify-between px-4">
            <Link href="/" className="font-semibold">prism</Link>
            <nav className="flex gap-5 text-sm text-muted-foreground">
              {links.map(([label, href]) => <Link key={href} href={href} className="hover:text-foreground">{label}</Link>)}
            </nav>
          </div>
        </header>
        <main className="mx-auto max-w-5xl px-4 py-10">{children}</main>
      </body>
    </html>
  );
}
