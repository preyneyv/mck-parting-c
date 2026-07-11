"use client";

import { forwardRef, useImperativeHandle, useRef } from "react";
import type { MirrorFrame } from "@/lib/prism-device";

export type PrismScreenHandle = { draw(frame: MirrorFrame): void };

export const PrismScreen = forwardRef<PrismScreenHandle>(function PrismScreen(_, ref) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  useImperativeHandle(ref, () => ({
    draw(frame) {
      const context = canvasRef.current?.getContext("2d");
      if (!context) return;
      const image = context.createImageData(128, 64);
      for (let y = 0; y < 64; y++) for (let x = 0; x < 128; x++) {
        // The physical SH1107 is 64x128 and u8g2 exposes it as 128x64 with
        // U8G2_R1. Its framebuffer therefore remains in native 64-wide page
        // order even though every app draws in landscape coordinates.
        const nativeX = 63 - y;
        const nativeY = x;
        const on = (frame.framebuffer[(nativeY >> 3) * 64 + nativeX] & (1 << (nativeY & 7))) !== 0;
        const index = (y * 128 + x) * 4, color = on ? 245 : 10;
        image.data[index] = color; image.data[index + 1] = color; image.data[index + 2] = color; image.data[index + 3] = 255;
      }
      context.putImageData(image, 0, 0);
    },
  }), []);
  return <canvas ref={canvasRef} width={128} height={64} aria-label="Prism screen mirror" className="aspect-[2/1] w-full rounded-md border bg-black [image-rendering:pixelated]" />;
});
