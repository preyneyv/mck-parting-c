import type { Metadata } from "next";
import { ManageLoader } from "@/components/manage-loader";
export const metadata: Metadata = { title: "manage" };
export default function ManagePage() { return <div className="space-y-6"><div><h1 className="text-3xl font-semibold tracking-tight">manage prism</h1><p className="mt-2 text-muted-foreground">install cartridges, customize the idle LEDs, and inspect the device.</p></div><ManageLoader /></div>; }
