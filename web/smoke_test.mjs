// Node.js smoke test for the C/Emscripten cognator WASM bundle.
//
// Run after `./web/build-wasm.sh`:
//   node web/smoke_test.mjs

import { existsSync } from "node:fs";
import { fileURLToPath, pathToFileURL } from "node:url";
import { dirname, resolve } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const jsPath = resolve(here, "cognator.js");
const wasmPath = resolve(here, "cognator.wasm");

if (!existsSync(jsPath) || !existsSync(wasmPath)) {
    console.error("missing C/WASM artifacts; run ./web/build-wasm.sh first");
    process.exit(1);
}

const createCognatorModule = (await import(pathToFileURL(jsPath))).default;
const engine = await createCognatorModule();

const csv = [
    "ID,Language_ID,Concept,Segments,Cognacy",
    "f1,lectA,NIGHT,p o,1",
    "f2,lectB,NIGHT,p o,1",
    "f3,lectC,NIGHT,b o,1",
    "f4,lectA,WATER,w a i,2",
    "f5,lectB,WATER,w a i,2",
    "f6,lectC,WATER,w a i,2",
].join("\n");

const ptr = engine.ccall(
    "cognator_detect_json",
    "number",
    ["string", "string"],
    [csv, JSON.stringify({ preset: "support-adaptive-bridge" })]
);
const result = JSON.parse(engine.UTF8ToString(ptr));
engine.ccall("cognator_free", null, ["number"], [ptr]);

if (!result.ok) {
    console.error("detect failed:", result.error);
    process.exit(1);
}

console.log(`summary: ${JSON.stringify(result.summary)}`);
console.log(`eval: ${JSON.stringify(result.eval)}`);
console.log(`predictions: ${result.predictions.length} rows`);
console.log("smoke test OK");
