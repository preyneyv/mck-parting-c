import Link from "next/link";
import { Button } from "@/components/ui/button";
import { Card, CardContent } from "@/components/ui/card";

export default function HomePage() {
  return (
    <div className="space-y-12">
      <section className="grid min-h-[28rem] items-center gap-8 md:grid-cols-2">
        <div className="space-y-5">
          <p className="text-sm text-muted-foreground">two buttons. tiny screen. your games.</p>
          <h1 className="text-5xl font-semibold tracking-tight">prism</h1>
          <p className="max-w-md text-lg text-muted-foreground">a pocket console built around cartridges you can make, share, and install.</p>
          <div className="flex gap-3">
            <Button asChild><Link href="/manage">manage a prism</Link></Button>
            <Button asChild variant="outline"><Link href="/leaderboard">leaderboard</Link></Button>
          </div>
        </div>
        <Card className="aspect-[4/3] border-dashed bg-muted/30">
          <CardContent className="flex h-full items-center justify-center p-6 text-center text-sm text-muted-foreground">
            prism CT scan hero goes here.<br />WASM emulator later.
          </CardContent>
        </Card>
      </section>
    </div>
  );
}
