import type { Metadata } from "next";
import Image from "next/image";
import Link from "next/link";
import {
  FileText,
  GameController,
  List,
  Package,
  Palette,
  PlugsConnected,
  Screwdriver,
  Trophy,
  Wrench,
} from "@phosphor-icons/react/dist/ssr";
import { Button } from "@/components/ui/button";
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card";
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table";

export const metadata: Metadata = {
  title: { absolute: "prism" },
  description: "a quick hello and a no-nonsense guide to prism.",
};

const boxContents = [
  {
    quantity: "1x",
    item: "prism",
    description: "charged and ready to play",
  },
  {
    quantity: "1x",
    item: "keycap puller",
    description: "for removing the L/R keycaps",
  },
  {
    quantity: "1x",
    item: "screwdriver",
    description: "for disassembly and reassembly",
  },
] as const;

const deviceSteps = [
  {
    icon: GameController,
    title: "move with L and R",
    description:
      "tap L or R to move through a list. hold either button to choose the thing that is selected.",
  },
  {
    icon: List,
    title: "use the menu button",
    description:
      "press menu while you are playing to pause. from there you can go home, put prism to sleep, or change the volume and brightness.",
  },
  {
    icon: Package,
    title: "pick a cartridge",
    description:
      "the home screen is your cartridge shelf. tap to browse, then hold L or R to start one.",
  },
] as const;

const websiteSteps = [
  {
    number: "01",
    title: "plug in",
    description:
      "connect prism to your computer with USB, then open the manage page in chrome or edge on desktop.",
  },
  {
    number: "02",
    title: "connect",
    description:
      "choose connect, select your prism in the browser prompt, and you are in.",
  },
  {
    number: "03",
    title: "make it yours",
    description:
      "add .prism cartridges and .prismpack files, change the RGB LEDs, and use the live screen and buttons when the device is across the desk.",
  },
] as const;

const specifications = [
  {
    label: "processor",
    value: "RP2040 · dual-core ARM Cortex-M0+ · 264 KB SRAM",
  },
  { label: "storage", value: "16 MB flash" },
  { label: "battery", value: "380 mAh LiPo battery" },
  { label: "display", value: "128 x 64 monochrome OLED · 120 FPS" },
  { label: "sockets", value: "Kailh hot-swap sockets" },
  {
    label: "switches",
    value: "Gateron Baby Kangaroo 2.0 tactile switches",
  },
  { label: "lights", value: "2x RGB LEDs" },
  { label: "enclosure", value: "3D-printed Clear PETG" },
] as const;

export default function HomePage() {
  return (
    <div className="space-y-16 pb-8">
      <section className="grid items-center gap-8 pt-4 md:grid-cols-[minmax(0,0.8fr)_minmax(0,1.2fr)]">
        <div className="space-y-6">
          <div className="space-y-4">
            <h1 className="font-display text-4xl font-semibold tracking-tight sm:text-5xl">
              say hi to prism
            </h1>
            <p className="max-w-xl text-base leading-7 text-muted-foreground sm:text-lg">
              prism is a tiny game console with two game buttons, a menu button,
              a black and white screen, colorful lights, and cartridges you can
              add whenever you want.
            </p>
          </div>
          <div className="flex flex-wrap gap-3">
            <Button
              asChild
              className="font-mono"
            >
              <Link href="/manage">
                <PlugsConnected className="size-4" />
                connect your prism
              </Link>
            </Button>
            <Button
              asChild
              variant="outline"
              className="font-mono"
            >
              <Link href="/leaderboard">
                <Trophy className="size-4" />
                see the leaderboards
              </Link>
            </Button>
          </div>
        </div>

        <Image
          src="/assembly/exploded.svg"
          alt="an exploded view of the prism assembly"
          width={1200}
          height={448}
          priority
          className="h-auto w-full"
        />
      </section>

      <section className="space-y-6">
        <div className="max-w-2xl space-y-3">
          <h2 className="font-display text-3xl font-semibold tracking-tight">
            in the box
          </h2>
        </div>
        <Card>
          <CardContent className="p-0">
            <Table>
              <TableHeader>
                <TableRow>
                  <TableHead className="w-20 px-5 font-mono sm:px-6">
                    count
                  </TableHead>
                  <TableHead className="font-mono">
                    item
                  </TableHead>
                  <TableHead className="hidden font-mono sm:table-cell">
                    use
                  </TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {boxContents.map((entry) => (
                  <TableRow key={entry.item}>
                    <TableCell className="px-5 font-mono text-muted-foreground sm:px-6">
                      {entry.quantity}
                    </TableCell>
                    <TableCell className="font-medium">{entry.item}</TableCell>
                    <TableCell className="hidden text-muted-foreground sm:table-cell">
                      {entry.description}
                    </TableCell>
                  </TableRow>
                ))}
              </TableBody>
            </Table>
          </CardContent>
        </Card>
      </section>

      <section className="space-y-6">
        <div className="max-w-2xl">
          <h2 className="font-display text-3xl font-semibold tracking-tight">
            three things to know
          </h2>
        </div>

        <div className="grid gap-4 md:grid-cols-3">
          {deviceSteps.map((step) => {
            const Icon = step.icon;
            return (
              <Card key={step.title}>
                <CardHeader>
                  <Icon className="mb-3 size-5" aria-hidden="true" />
                  <CardTitle className="font-display">
                    {step.title}
                  </CardTitle>
                </CardHeader>
                <CardContent>
                  <p className="text-sm leading-6 text-muted-foreground">
                    {step.description}
                  </p>
                </CardContent>
              </Card>
            );
          })}
        </div>

        <Card className="bg-muted/30 shadow-none">
          <CardContent className="flex flex-col gap-4 p-5 sm:flex-row sm:items-center sm:justify-between">
            <div className="flex items-start gap-3">
              <Wrench className="mt-0.5 size-5 shrink-0" aria-hidden="true" />
              <p className="text-sm leading-6 text-muted-foreground">
                hold the menu button for five seconds if prism needs a restart.
              </p>
            </div>
            <span className="w-fit shrink-0 rounded-md border bg-background px-2.5 py-1 font-mono text-xs font-medium">
              menu · 5 seconds
            </span>
          </CardContent>
        </Card>
      </section>

      <section className="space-y-6">
        <div className="max-w-2xl">
          <h2 className="font-display text-3xl font-semibold tracking-tight">
            plug in, connect, done
          </h2>
          <p className="mt-3 leading-7 text-muted-foreground">
            the website is where you look after your prism. use it to install
            cartridges, adjust the lights, check storage, and see what is
            happening on the screen.
          </p>
        </div>

        <Card>
          <CardContent className="divide-y p-0">
            {websiteSteps.map((step) => (
              <div
                key={step.number}
                className="grid gap-3 p-5 sm:grid-cols-[3rem_10rem_1fr] sm:items-start sm:gap-4 sm:p-6"
              >
                <span className="font-mono text-sm text-muted-foreground">
                  {step.number}
                </span>
                <p className="font-mono font-medium">
                  {step.title}
                </p>
                <p className="text-sm leading-6 text-muted-foreground">
                  {step.description}
                </p>
              </div>
            ))}
          </CardContent>
        </Card>

        <div className="grid gap-4 md:grid-cols-2">
          <Card>
            <CardHeader>
              <Palette className="mb-2 size-5" aria-hidden="true" />
              <CardTitle className="font-display">
                manage
              </CardTitle>
              <CardDescription>
                install and remove cartridges, tune the RGB LEDs, mirror the
                screen, and restart the device.
              </CardDescription>
            </CardHeader>
            <CardContent>
              <Button
                asChild
                variant="outline"
                className="font-mono"
              >
                <Link href="/manage">
                  <PlugsConnected className="size-4" />
                  connect your prism
                </Link>
              </Button>
            </CardContent>
          </Card>

          <Card>
            <CardHeader>
              <Trophy className="mb-2 size-5" aria-hidden="true" />
              <CardTitle className="font-display">
                leaderboards
              </CardTitle>
              <CardDescription>
                some cartridges show a QR code when a run ends. scan it, add
                your name, and your score will appear here.
              </CardDescription>
            </CardHeader>
            <CardContent>
              <Button
                asChild
                variant="outline"
                className="font-mono"
              >
                <Link href="/leaderboard">
                  <Trophy className="size-4" />
                  see the leaderboards
                </Link>
              </Button>
            </CardContent>
          </Card>
        </div>
      </section>

      <section className="space-y-6">
        <div className="max-w-2xl">
          <h2 className="font-display text-3xl font-semibold tracking-tight">
            tech specs
          </h2>
        </div>
        <Image
          src="/assembly/pcb.svg"
          alt="the PCB inside prism"
          width={1600}
          height={402}
          className="h-auto w-full"
        />
        <Card>
          <CardContent className="grid p-0 sm:grid-cols-2">
            {specifications.map((specification, index) => (
              <div
                key={specification.label}
                className={`p-5 sm:p-6 ${
                  index < specifications.length - 1 ? "border-b" : ""
                } ${index % 2 === 0 ? "sm:border-r" : ""} ${
                  index >= specifications.length - 2 ? "sm:border-b-0" : ""
                }`}
              >
                <p className="font-mono text-xs text-muted-foreground">
                  {specification.label}
                </p>
                <p className="mt-2 text-sm font-medium leading-6">
                  {specification.value}
                </p>
              </div>
            ))}
          </CardContent>
        </Card>
      </section>

      <section className="grid gap-4 md:grid-cols-2">
        <Card className="border-dashed shadow-none">
          <CardHeader>
            <CardTitle className="font-display">
              open it up
            </CardTitle>
            <CardDescription className="leading-6">
              prism was made to be opened. pull the keycaps, remove the screws,
              take a look inside, and put it back together when you are done.
            </CardDescription>
          </CardHeader>
          <CardContent>
            <div className="flex items-center gap-2 font-mono text-sm text-muted-foreground">
              <Screwdriver className="size-4" aria-hidden="true" />
              disassembly instructions coming soon
            </div>
          </CardContent>
        </Card>

        <Card className="border-dashed shadow-none">
          <CardHeader>
            <CardTitle className="font-display">
              want to see how it came together?
            </CardTitle>
            <CardDescription className="leading-6">
              i&apos;m putting together a proper making-of with the hardware,
              software, tiny mistakes, and everything i learned along the way.
            </CardDescription>
          </CardHeader>
          <CardContent>
            <div className="flex items-center gap-2 font-mono text-sm text-muted-foreground">
              <FileText className="size-4" aria-hidden="true" />
              full writeup coming soon
            </div>
          </CardContent>
        </Card>
      </section>
    </div>
  );
}
