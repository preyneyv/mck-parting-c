"use client";
import dynamic from "next/dynamic";
const ManagementWorkspace = dynamic(() => import("@/components/management-workspace").then(module => module.ManagementWorkspace), { ssr: false, loading: () => <p className="text-sm text-muted-foreground">loading device tools…</p> });
export function ManageLoader() { return <ManagementWorkspace />; }
