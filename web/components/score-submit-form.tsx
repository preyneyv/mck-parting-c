"use client";

import { useEffect, useState } from "react";
import { useRouter } from "next/navigation";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";

export function ScoreSubmitForm({ payload, deviceSerial, initialName }: { payload: string; deviceSerial: string; initialName: string }) {
  const router = useRouter();
  const [name, setName] = useState(initialName);
  const [error, setError] = useState("");
  const [pending, setPending] = useState(false);

  useEffect(() => {
    const local = localStorage.getItem(`prism:name:${deviceSerial}`);
    if (local) requestAnimationFrame(() => setName(local));
  }, [deviceSerial]);

  async function submit(event: React.FormEvent) {
    event.preventDefault(); setPending(true); setError("");
    const response = await fetch("/api/leaderboard", { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ payload, name }) });
    const result = await response.json() as { error?: string };
    setPending(false);
    if (!response.ok) { setError(result.error ?? "Could not submit score."); return; }
    localStorage.setItem(`prism:name:${deviceSerial}`, name.trim());
    router.push("/leaderboard"); router.refresh();
  }

  return <form onSubmit={submit} className="space-y-4"><div className="space-y-2"><Label htmlFor="name">Your name</Label><Input id="name" value={name} onChange={event => setName(event.target.value)} maxLength={32} autoFocus required /></div>{error ? <p className="text-sm text-destructive">{error}</p> : null}<Button disabled={pending}>{pending ? "Submitting…" : "Submit score"}</Button></form>;
}
