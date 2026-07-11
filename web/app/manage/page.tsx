import type { Metadata } from "next";
import { ManageLoader } from "@/components/manage-loader";
export const metadata: Metadata = { title: "Manage" };
export default function ManagePage() { return <div className="space-y-6"><div><h1 className="text-3xl font-semibold tracking-tight">Manage Prism</h1><p className="mt-2 text-muted-foreground">Install cartridges, customize the idle lights, and inspect the device.</p></div><ManageLoader /></div>; }
