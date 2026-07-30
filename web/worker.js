/*
 * cognator Web Worker
 * Loads the C/Emscripten WASM module and runs detect calls on demand.
 * Main thread posts {type:"run", csv, options}; worker posts back
 * {type:"status"|"ready"|"done"|"error"}.
 */

importScripts("cognator.js?v=" + Date.now());

var ready = false;
var modulePromise = createCognatorModule();
var engine = null;

(async function init() {
    self.postMessage({ type: "status", text: "Loading cognator C/WASM engine..." });
    try {
        engine = await modulePromise;
        ready = true;
        self.postMessage({
            type: "ready",
            system: engine.ccall("cognator_system", "string", [], []),
            graphemeCount: 0,
        });
    } catch (err) {
        self.postMessage({ type: "error", message: err.message || String(err) });
    }
})();

function detect(csv, options) {
    const ptr = engine.ccall(
        "cognator_detect_json",
        "number",
        ["string", "string"],
        [csv || "", JSON.stringify(options || {})]
    );
    if (!ptr) throw new Error("cognator_detect_json returned null");
    try {
        return JSON.parse(engine.UTF8ToString(ptr));
    } finally {
        engine.ccall("cognator_free", null, ["number"], [ptr]);
    }
}

self.onmessage = function (ev) {
    if (ev.data.type !== "run") return;
    if (!ready) {
        self.postMessage({ type: "error", message: "engine not ready" });
        return;
    }
    self.postMessage({ type: "status", text: "Running detection..." });
    const t0 = Date.now();
    try {
        const result = detect(ev.data.csv || "", ev.data.options || {});
        result._wallTimeMs = Date.now() - t0;
        self.postMessage({ type: "done", result });
    } catch (err) {
        self.postMessage({ type: "error", message: err.message || String(err) });
    }
};
