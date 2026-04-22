/*
 * cognator Web Worker
 * Pre-initializes the Go WASM module on load, then runs detect calls
 * on demand. Main thread posts {type:"run", csv, options}; worker
 * posts back {type:"status"|"ready"|"done"|"error"}.
 */

importScripts("wasm_exec.js?v=" + Date.now());

var ready = false;
var go = null;

(async function init() {
    self.postMessage({ type: "status", text: "Loading cognator engine (~3.2 MB gzipped)..." });
    try {
        go = new Go();
        const resp = await fetch("cognator.wasm?v=" + Date.now());
        if (!resp.ok) {
            throw new Error("fetch cognator.wasm: HTTP " + resp.status);
        }
        const bytes = await resp.arrayBuffer();
        const { instance } = await WebAssembly.instantiate(bytes, go.importObject);
        // Kick off the Go runtime; it blocks forever by design so the
        // exported functions stay callable. Do NOT await — that would
        // block this async init forever.
        go.run(instance).catch((err) => {
            self.postMessage({ type: "error", message: "Go runtime exited: " + (err.message || String(err)) });
        });

        // Poll for the `cognatorReady` flag that main() sets once the
        // embedded bundle is loaded.
        const t0 = Date.now();
        while (!self.cognatorReady) {
            if (self.cognatorInitError) {
                throw new Error("init error: " + self.cognatorInitError);
            }
            if (Date.now() - t0 > 15000) {
                throw new Error("timed out waiting for engine init");
            }
            await new Promise((r) => setTimeout(r, 25));
        }
        ready = true;
        self.postMessage({
            type: "ready",
            system: self.cognatorSystem || "(unknown)",
            graphemeCount: self.cognatorGraphemeCount || 0,
        });
    } catch (err) {
        self.postMessage({ type: "error", message: err.message || String(err) });
    }
})();

self.onmessage = function (ev) {
    if (ev.data.type !== "run") return;
    if (!ready) {
        self.postMessage({ type: "error", message: "engine not ready" });
        return;
    }
    const csv = ev.data.csv || "";
    const options = ev.data.options || {};
    self.postMessage({ type: "status", text: "Running detection..." });
    const t0 = Date.now();
    try {
        const resJSON = self.cognatorDetect(csv, options);
        const result = JSON.parse(resJSON);
        result._wallTimeMs = Date.now() - t0;
        self.postMessage({ type: "done", result });
    } catch (err) {
        self.postMessage({ type: "error", message: err.message || String(err) });
    }
};
