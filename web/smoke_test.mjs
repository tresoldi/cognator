// Node.js smoke test for cognator.wasm.
//
// Run with:
//   node web/smoke_test.mjs
//
// Loads the WASM, waits for cognatorReady, invokes cognatorDetect
// on a tiny hand-written wordlist, and prints the result. Exits
// non-zero on any error. No external deps; uses Node's built-in
// fs + globalThis shim that wasm_exec.js already knows how to drive.

import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";
import "./wasm_exec.js";

const here = dirname(fileURLToPath(import.meta.url));
const wasmPath = resolve(here, "cognator.wasm");

const go = new Go();
const bytes = await readFile(wasmPath);
const { instance } = await WebAssembly.instantiate(bytes, go.importObject);

// Run the Go main in the background. Do NOT await; it blocks forever
// on <-make(chan struct{}) by design so the exported functions stay
// callable. Any panic or unhandled rejection ends the test.
go.run(instance).catch((err) => {
    console.error("Go runtime exited with error:", err);
    process.exit(1);
});

// Poll for cognatorReady. The initial bundle load is synchronous
// from Go's perspective but the caller sees it resolve on the next
// event-loop tick.
const waitStart = Date.now();
while (!globalThis.cognatorReady) {
    if (globalThis.cognatorInitError) {
        console.error("Init failed:", globalThis.cognatorInitError);
        process.exit(1);
    }
    if (Date.now() - waitStart > 5000) {
        console.error("timed out waiting for cognatorReady");
        process.exit(1);
    }
    await new Promise((r) => setTimeout(r, 20));
}

console.log(`ready: merkmal ${globalThis.cognatorSystem}, ${globalThis.cognatorGraphemeCount} graphemes`);

// Minimal Polynesian-style wordlist. Three lects, three concepts,
// obvious cognate clusters. Cognacy is included so the eval block
// should populate.
const csv = [
    "ID,Language_ID,Concept,Segments,Cognacy",
    "f1,lectA,NIGHT,p o,1",
    "f2,lectB,NIGHT,p o,1",
    "f3,lectC,NIGHT,b o,1",
    "f4,lectA,WATER,w a i,2",
    "f5,lectB,WATER,w a i,2",
    "f6,lectC,WATER,w a i,2",
    "f7,lectA,FIRE,k a,3",
    "f8,lectB,FIRE,k a,3",
    "f9,lectC,FIRE,g a,3",
].join("\n");

const resJSON = globalThis.cognatorDetect(csv, { preset: "support-adaptive-bridge" });
const result = JSON.parse(resJSON);

if (!result.ok) {
    console.error("detect failed:", result.error);
    process.exit(1);
}

console.log(`summary: ${JSON.stringify(result.summary)}`);
if (result.eval) {
    console.log(`eval: ${JSON.stringify(result.eval)}`);
}
console.log(`predictions: ${result.predictions.length} rows`);
for (const p of result.predictions.slice(0, 3)) {
    console.log(`  ${p.eval_id}  pred=${p.pred}  conf=${p.confidence.toFixed(3)}  clustersize=${p.cluster_size}`);
}

console.log("smoke test OK");
process.exit(0);
