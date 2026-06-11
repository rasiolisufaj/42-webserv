/* ==========================================================================
 *  Webserv 42 — Compliance Dashboard engine
 *  Plain vanilla JS. Drives every panel against the live C++98 server.
 * ========================================================================== */
(function () {
    "use strict";

    /* ---------------------------------------------------------------- utils */
    var $  = function (id) { return document.getElementById(id); };
    var el = function (sel, root) { return (root || document).querySelector(sel); };

    function codeClass(status) {
        if (!status || status === 0) return "s0";
        return "s" + String(status).charAt(0);
    }
    function pad2(n) { return (n < 10 ? "0" : "") + n; }
    function nowStamp() {
        var d = new Date();
        return pad2(d.getHours()) + ":" + pad2(d.getMinutes()) + ":" + pad2(d.getSeconds()) +
               "." + ("00" + d.getMilliseconds()).slice(-3);
    }
    function esc(s) {
        return String(s)
            .replace(/&/g, "&amp;").replace(/</g, "&lt;")
            .replace(/>/g, "&gt;");
    }
    function fmtBytes(n) {
        if (n < 1024) return n + " B";
        if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KB";
        return (n / 1048576).toFixed(2) + " MB";
    }

    /* ---------------------------------------------------- counters / footer */
    var counters = { total: 0, ok: 0, fail: 0 };
    function bumpCounters(success) {
        counters.total++;
        if (success) counters.ok++; else counters.fail++;
        $("cTotal").textContent = counters.total;
        $("cOk").textContent    = counters.ok;
        $("cFail").textContent  = counters.fail;
    }

    /* ------------------------------------------------------- request console */
    var consoleEl = $("console");
    function logLine(cls, html) {
        var div = document.createElement("div");
        div.className = "logline " + cls;
        div.innerHTML = '<span class="ts">[' + nowStamp() + "]</span> " + html;
        consoleEl.appendChild(div);
        if ($("autoscroll").checked) consoleEl.scrollTop = consoleEl.scrollHeight;
    }
    function logSys(msg) { logLine("sys", esc(msg)); }
    function logReq(method, url, status, ms, note) {
        var st = status ? status : "ERR";
        var html =
            '<span class="mth">' + esc(method) + "</span> " +
            '<span class="url">' + esc(url) + "</span> &rarr; " +
            '<span class="st">' + esc(st) + "</span>";
        if (ms != null) html += ' <span class="note">' + ms.toFixed(1) + "ms</span>";
        if (note)       html += ' <span class="note">· ' + esc(note) + "</span>";
        logLine(codeClass(status), html);
    }

    $("clearConsole").addEventListener("click", function () {
        consoleEl.innerHTML = "";
        logSys("console cleared");
    });

    /* ----------------------------------------------------------- core fetch */
    /*
     * request(method, url, opts) -> Promise<result>
     * result = { ok, status, statusText, type, headers(str), body, ms, error }
     * Never rejects: network errors are captured so the dashboard can't hang.
     */
    function request(method, url, opts) {
        opts = opts || {};
        var init = {
            method: method,
            redirect: opts.redirect || "follow",
            headers: opts.headers || undefined,
            body: opts.body != null ? opts.body : undefined,
            cache: "no-store"
        };
        var t0 = performance.now();
        return fetch(url, init).then(function (resp) {
            var ms = performance.now() - t0;
            var hdr = [];
            resp.headers.forEach(function (v, k) { hdr.push(k + ": " + v); });
            var bodyP = (method === "HEAD" || resp.type === "opaqueredirect")
                ? Promise.resolve("") : resp.text().catch(function () { return ""; });
            return bodyP.then(function (body) {
                var res = {
                    ok: resp.ok, status: resp.status, statusText: resp.statusText,
                    type: resp.type, redirected: resp.redirected, finalUrl: resp.url,
                    headers: hdr.join("\n"), body: body, ms: ms, error: null
                };
                var note = resp.type === "opaqueredirect"
                    ? "redirect intercepted by browser" :
                    (resp.redirected ? "followed → " + resp.url : null);
                logReq(method, url, res.status || (resp.type === "opaqueredirect" ? "3xx" : 0), ms, note);
                bumpCounters(resp.ok || resp.type === "opaqueredirect");
                return res;
            });
        }).catch(function (err) {
            var ms = performance.now() - t0;
            var res = { ok: false, status: 0, statusText: "", type: "error",
                        headers: "", body: "", ms: ms, error: String(err) };
            logReq(method, url, 0, ms, "network error: " + err.message);
            bumpCounters(false);
            return res;
        });
    }

    /* ---------------------------------------------------- response renderer */
    function renderResp(panelId, method, url, res, expected) {
        var panel = $(panelId);
        var status = res.status;
        var shown  = status || (res.type === "opaqueredirect" ? "3xx" : "—");
        var cc     = codeClass(status || (res.type === "opaqueredirect" ? 300 : 0));

        var badge = "";
        if (expected != null) {
            var pass;
            if (expected === "3xx")
                pass = (res.type === "opaqueredirect" || res.redirected ||
                        (status >= 300 && status < 400));
            else
                pass = (status === expected);
            badge = '<span class="badge ' + (pass ? "pass" : "fail") + '">' +
                    (pass ? "✓ expected " : "✗ expected ") + expected + "</span>";
        }

        var headerBlock = res.headers ? esc(res.headers) : "(no headers exposed by browser)";
        var bodyBlock   = res.error ? "ERROR: " + esc(res.error)
                                    : (res.body ? esc(res.body) : "(empty body)");

        panel.innerHTML =
            '<div class="resp-head">' +
                '<span class="req-line">' + esc(method) + " " + esc(url) + "</span>" +
                '<span class="code ' + cc + '">' + shown + " " + esc(res.statusText || "") + "</span>" +
                badge +
                '<span class="meta">' + res.ms.toFixed(1) + "ms · type=" + esc(res.type) + "</span>" +
            "</div>" +
            '<div class="section-label">response headers</div>' +
            "<pre>" + headerBlock + "</pre>" +
            '<div class="section-label">response body</div>' +
            "<pre>" + bodyBlock + "</pre>";
    }

    /* ====================================================================== */
    /*  1. IDENTITY / live status                                             */
    /* ====================================================================== */
    function setLive(state) { // "live" | "dead" | "pending"
        var label = state === "live" ? "ONLINE" : state === "dead" ? "OFFLINE" : "PROBING…";
        $("liveDot").className = "dot " + state;
        $("liveText").textContent = label;
        $("idDot").className = "dot " + state;
        $("idStatus").textContent = state === "live" ? "ONLINE" :
                                    state === "dead" ? "OFFLINE" : "probing…";
    }
    function probe() {
        var t0 = performance.now();
        fetch("/", { method: "HEAD", cache: "no-store" })
            .then(function () {
                setLive("live");
                $("idProbe").textContent = nowStamp() + " (" + (performance.now() - t0).toFixed(0) + "ms)";
            })
            .catch(function () {
                setLive("dead");
                $("idProbe").textContent = nowStamp() + " — unreachable";
            });
    }
    function initIdentity() {
        $("idName").textContent = location.hostname || "localhost";
        $("idPort").textContent = (location.port || "80") + " (" + (location.protocol.replace(":", "")) + ")";
        probe();
        setInterval(probe, 5000);
    }

    /* ====================================================================== */
    /*  2. CGI EXECUTION                                                      */
    /* ====================================================================== */
    var CGI = {
        hello:     { m: "GET",  url: "/cgi-bin/hello.py?name=Leny&project=Webserv" },
        hellopost: { m: "POST", url: "/cgi-bin/hello.py", body: "payload=from-dashboard&n=42" },
        slow:      { m: "GET",  url: "/cgi-bin/slow.py" },
        loop:      { m: "GET",  url: "/cgi-bin/loop.py" }
    };
    function initCgi() {
        var btns = document.querySelectorAll("#sec-cgi [data-cgi]");
        Array.prototype.forEach.call(btns, function (b) {
            b.addEventListener("click", function () {
                var job = CGI[b.getAttribute("data-cgi")];
                $("cgiResp").innerHTML = '<div class="section-label">running ' + esc(job.url) + " …</div><pre> </pre>";
                request(job.m, job.url, job.body ? { body: job.body } : {}).then(function (res) {
                    renderResp("cgiResp", job.m, job.url, res, null);
                });
            });
        });
    }

    /* ====================================================================== */
    /*  3. FILE UPLOAD (XHR for real progress)                                */
    /* ====================================================================== */
    var pendingFile = null;
    function uploadFile(file) {
        if (!file) return;
        var url = "/upload/" + encodeURIComponent(file.name);
        var bar = $("upBar"), label = $("upLabel");
        bar.style.width = "0%";
        label.textContent = "Uploading " + file.name + " (" + fmtBytes(file.size) + ") …";

        var xhr = new XMLHttpRequest();
        var t0 = performance.now();
        xhr.open("POST", url, true);
        xhr.setRequestHeader("Content-Type", file.type || "application/octet-stream");

        xhr.upload.onprogress = function (e) {
            if (e.lengthComputable) {
                var pct = (e.loaded / e.total) * 100;
                bar.style.width = pct.toFixed(1) + "%";
                label.textContent = "Uploading… " + pct.toFixed(0) + "% (" +
                                    fmtBytes(e.loaded) + " / " + fmtBytes(e.total) + ")";
            }
        };
        xhr.onload = function () {
            var ms = performance.now() - t0;
            bar.style.width = "100%";
            label.textContent = "Done: " + file.name + " — HTTP " + xhr.status;
            logReq("POST", url, xhr.status, ms, "upload " + fmtBytes(file.size));
            bumpCounters(xhr.status >= 200 && xhr.status < 400);
            renderResp("uploadResp", "POST", url, {
                status: xhr.status, statusText: xhr.statusText, type: "basic",
                headers: xhr.getAllResponseHeaders().trim(), body: xhr.responseText,
                ms: ms, error: null
            }, null);
            loadInventory();
        };
        xhr.onerror = function () {
            var ms = performance.now() - t0;
            label.textContent = "Network error.";
            logReq("POST", url, 0, ms, "network error");
            bumpCounters(false);
            renderResp("uploadResp", "POST", url,
                { status: 0, statusText: "", type: "error", headers: "", body: "",
                  ms: ms, error: "network error / connection refused" }, null);
        };
        xhr.send(file);
    }
    function initUpload() {
        var dz = $("dropzone"), input = $("fileInput");
        dz.addEventListener("click", function () { input.click(); });
        input.addEventListener("change", function () {
            if (input.files.length) uploadFile(input.files[0]);
        });
        ["dragenter", "dragover"].forEach(function (ev) {
            dz.addEventListener(ev, function (e) { e.preventDefault(); dz.classList.add("drag"); });
        });
        ["dragleave", "drop"].forEach(function (ev) {
            dz.addEventListener(ev, function (e) { e.preventDefault(); dz.classList.remove("drag"); });
        });
        dz.addEventListener("drop", function (e) {
            if (e.dataTransfer.files.length) uploadFile(e.dataTransfer.files[0]);
        });
    }

    /* ====================================================================== */
    /*  4. INVENTORY  +  5. DELETE                                            */
    /* ====================================================================== */
    function deleteFile(name, afterReload) {
        var url = "/upload/" + encodeURIComponent(name);
        $("deleteResp").innerHTML = '<div class="section-label">deleting ' + esc(name) + " …</div><pre> </pre>";
        return request("DELETE", url, {}).then(function (res) {
            renderResp("deleteResp", "DELETE", url, res, null);
            if (afterReload) loadInventory();
            return res;
        });
    }
    function loadInventory() {
        var body = $("invBody");
        body.innerHTML = '<tr><td colspan="4" class="empty">loading…</td></tr>';
        return request("GET", "/cgi-bin/list_uploads.py", {}).then(function (res) {
            var files;
            try { files = JSON.parse(res.body); } catch (e) { files = null; }
            if (!files) {
                body.innerHTML = '<tr><td colspan="4" class="empty">could not parse list_uploads.py output</td></tr>';
                return;
            }
            if (!files.length) {
                body.innerHTML = '<tr><td colspan="4" class="empty">no uploaded files</td></tr>';
                return;
            }
            body.innerHTML = "";
            files.forEach(function (f) {
                var tr = document.createElement("tr");
                tr.innerHTML =
                    "<td>" + esc(f.name) + "</td>" +
                    "<td>" + (f.size != null ? fmtBytes(f.size) : (f.size_kb + " KB")) + "</td>" +
                    "<td>" + esc(f.date || "—") + "</td>" +
                    '<td class="actions"></td>';
                var del = document.createElement("button");
                del.className = "danger";
                del.textContent = "✖ DELETE";
                del.addEventListener("click", function () { deleteFile(f.name, true); });
                el(".actions", tr).appendChild(del);
                body.appendChild(tr);
            });
        });
    }
    function initInventory() {
        $("refreshInv").addEventListener("click", loadInventory);
        $("delBtn").addEventListener("click", function () {
            var n = $("delName").value.trim();
            if (n) deleteFile(n, true);
        });
    }

    /* ====================================================================== */
    /*  6. HTTP ERROR TESTING                                                 */
    /* ====================================================================== */
    var ERR_TESTS = [
        { label: "301 / redirect", method: "GET",    url: "/errors",            expect: "3xx", redirect: "manual",
          note: "directory without trailing slash → 301 (browser hides exact code)" },
        { label: "400 Bad Request", method: "GET",   url: "/%zz",               expect: 400,
          note: "invalid percent-encoding in target" },
        { label: "403 Forbidden",   method: "POST",  url: "/upload/",           expect: 403, body: "x",
          note: "upload onto a directory path" },
        { label: "404 Not Found",   method: "GET",   url: "/no-such-file-xyz",  expect: 404 },
        { label: "405 Method N/A",  method: "DELETE", url: "/",                 expect: 405,
          note: "DELETE on a GET/HEAD-only location" },
        { label: "413 Too Large",   method: "POST",  url: "/upload/big.bin",    expect: 413, big: true,
          note: "body over client_max_body_size (2 MB)" },
        { label: "500 Internal",    method: "GET",   url: "/cgi-bin/list_uploads.py", expect: 500,
          note: "hard to force legitimately — dashboard shows the ACTUAL code" }
    ];
    function runErrTest(t) {
        var opts = { redirect: t.redirect || "follow" };
        if (t.big)  opts.body = new Blob([new Uint8Array(3 * 1024 * 1024)]); // 3 MB
        else if (t.body) opts.body = t.body;
        $("errResp").innerHTML = '<div class="section-label">testing ' + esc(t.label) + " …</div><pre> </pre>";
        return request(t.method, t.url, opts).then(function (res) {
            renderResp("errResp", t.method, t.url, res, t.expect);
        });
    }
    function initErrors() {
        var row = $("errBtns");
        ERR_TESTS.forEach(function (t) {
            var b = document.createElement("button");
            b.textContent = "Test " + t.label;
            b.title = t.note || "";
            b.addEventListener("click", function () { runErrTest(t); });
            row.appendChild(b);
        });
    }

    /* ====================================================================== */
    /*  7. HTTP METHOD TESTING                                                */
    /* ====================================================================== */
    var METHODS = {
        GET:    { url: "/" },
        POST:   { url: "/upload/method_test.txt", body: "method test body @ " + Date.now() },
        DELETE: { url: "/upload/method_test.txt" },
        HEAD:   { url: "/" }
    };
    function initMethods() {
        var btns = document.querySelectorAll("#sec-methods [data-m]");
        Array.prototype.forEach.call(btns, function (b) {
            b.addEventListener("click", function () {
                var m = b.getAttribute("data-m"), spec = METHODS[m];
                $("methodResp").innerHTML = '<div class="section-label">' + m + " " + esc(spec.url) + " …</div><pre> </pre>";
                request(m, spec.url, spec.body ? { body: spec.body } : {}).then(function (res) {
                    renderResp("methodResp", m, spec.url, res, null);
                });
            });
        });
    }

    /* ====================================================================== */
    /*  8. KEEP-ALIVE                                                         */
    /* ====================================================================== */
    function initKeepAlive() {
        $("kaRun").addEventListener("click", function () {
            var n = Math.max(1, Math.min(100, parseInt($("kaCount").value, 10) || 10));
            var times = [], i = 0;
            $("kaRun").disabled = true;
            logSys("keep-alive burst: " + n + " sequential GET / requests");
            var wall0 = performance.now();
            (function step() {
                if (i >= n) {
                    var wall = performance.now() - wall0;
                    var sum = 0, mn = Infinity, mx = 0;
                    times.forEach(function (t) { sum += t; mn = Math.min(mn, t); mx = Math.max(mx, t); });
                    $("kaTotal").textContent = n;
                    $("kaWall").textContent  = wall.toFixed(0);
                    $("kaAvg").textContent   = (sum / n).toFixed(1);
                    $("kaMin").textContent   = mn.toFixed(1);
                    $("kaMax").textContent   = mx.toFixed(1);
                    logSys("keep-alive done: wall=" + wall.toFixed(0) + "ms avg=" + (sum / n).toFixed(1) + "ms");
                    $("kaRun").disabled = false;
                    return;
                }
                i++;
                request("GET", "/", {}).then(function (res) {
                    times.push(res.ms);
                    step();
                });
            })();
        });
    }

    /* ====================================================================== */
    /*  9. CGI TIMEOUT MONITOR                                                */
    /* ====================================================================== */
    function initTimeout() {
        $("toRun").addEventListener("click", function () {
            var btn = $("toRun"), timerEl = $("toTimer");
            btn.disabled = true;
            var t0 = performance.now();
            var tick = setInterval(function () {
                timerEl.textContent = ((performance.now() - t0) / 1000).toFixed(2) + "s";
            }, 50);
            $("toResp").innerHTML = '<div class="section-label">slow CGI running — waiting for the server to reap it…</div><pre> </pre>';
            request("GET", "/cgi-bin/slow.py", {}).then(function (res) {
                clearInterval(tick);
                var secs = (performance.now() - t0) / 1000;
                timerEl.textContent = secs.toFixed(2) + "s";
                btn.disabled = false;
                var killed = (res.status === 504);
                res.body = (killed
                    ? "✓ Server returned 504 after " + secs.toFixed(2) + "s — CGI child was reaped (no hang).\n\n"
                    : "✗ Expected 504. Server answered " + res.status + " after " + secs.toFixed(2) + "s.\n\n")
                    + (res.body || "");
                renderResp("toResp", "GET", "/cgi-bin/slow.py", res, 504);
            });
        });
    }

    /* ====================================================================== */
    /* 10. SECURITY / ADVERSARIAL                                             */
    /* ====================================================================== */
    // sendable:true  → run via fetch and compare.
    // sendable:false → browser can't emit it; show the curl/nc equivalent.
    var SEC_TESTS = [
        { name: "Path traversal — encoded ../",  exp: "404 or 400 (never the file)",
          sendable: true, method: "GET", url: "/%2e%2e/%2e%2e/etc/passwd", bad: 200 },
        { name: "Path traversal — ..%2f variant", exp: "404 or 400 (never the file)",
          sendable: true, method: "GET", url: "/..%2f..%2f..%2fetc%2fpasswd", bad: 200 },
        { name: "Path traversal — literal ../ (browser normalizes)", exp: "404 (browser collapses ../ first)",
          sendable: true, method: "GET", url: "/sub/../../../etc/passwd", bad: 200 },
        { name: "Invalid HTTP method (BREW)", exp: "501 Not Implemented",
          sendable: true, method: "BREW", url: "/", bad: 200 },
        { name: "Oversized header (~16 KB)", exp: "400/431/413 (over client_max_header_size 8 KB)",
          sendable: true, method: "GET", url: "/", bigHeader: true, bad: 200 },
        { name: "Oversized body (3 MB)", exp: "413 Payload Too Large",
          sendable: true, method: "POST", url: "/upload/evil.bin", big: true, bad: 200 },
        { name: "Malformed header (raw \\r injection)", exp: "400 Bad Request",
          sendable: false, cmd: "printf 'GET / HTTP/1.1\\r\\nHost: x\\r\\nBad Header\\r\\n\\r\\n' | nc 127.0.0.1 " + (location.port || 80) },
        { name: "Invalid chunked framing", exp: "400 Bad Request",
          sendable: false, cmd: "printf 'POST /upload/x HTTP/1.1\\r\\nHost: x\\r\\nTransfer-Encoding: chunked\\r\\n\\r\\nZZ\\r\\nbad\\r\\n' | nc 127.0.0.1 " + (location.port || 80) }
    ];
    function runSecTest(t) {
        if (!t.sendable) {
            $("secResp").innerHTML =
                '<div class="resp-head"><span class="req-line">' + esc(t.name) + "</span>" +
                '<span class="badge note">browser cannot send</span></div>' +
                '<div class="section-label">expected</div><pre>' + esc(t.exp) + "</pre>" +
                '<div class="section-label">reproduce on the wire</div><pre>' + esc(t.cmd) + "</pre>";
            logSys("security (manual): " + t.name);
            return Promise.resolve();
        }
        var opts = {};
        if (t.big)       opts.body = new Blob([new Uint8Array(3 * 1024 * 1024)]);
        if (t.bigHeader) opts.headers = { "X-Big": new Array(16384).join("A") };
        $("secResp").innerHTML = '<div class="section-label">attacking: ' + esc(t.name) + " …</div><pre> </pre>";
        return request(t.method, t.url, opts).then(function (res) {
            var safe = !(res.status === t.bad);          // server did NOT serve the attack
            var head =
                '<div class="resp-head"><span class="req-line">' + esc(t.method) + " " + esc(t.url) + "</span>" +
                '<span class="code ' + codeClass(res.status) + '">' + (res.status || "ERR") + "</span>" +
                '<span class="badge ' + (safe ? "pass" : "fail") + '">' +
                (safe ? "✓ defended" : "✗ VULNERABLE") + "</span>" +
                '<span class="meta">' + res.ms.toFixed(1) + "ms</span></div>";
            $("secResp").innerHTML = head +
                '<div class="section-label">expected behaviour</div><pre>' + esc(t.exp) + "</pre>" +
                '<div class="section-label">actual body</div><pre>' + esc(res.body || "(empty)") + "</pre>";
        });
    }
    function initSecurity() {
        var list = $("secList");
        SEC_TESTS.forEach(function (t) {
            var row = document.createElement("div");
            row.className = "testrow";
            row.innerHTML =
                '<div class="label"><span class="name">' + esc(t.name) + "</span>" +
                '<span class="exp">expect: ' + esc(t.exp) + "</span></div>" +
                '<div class="result"></div>';
            var btn = document.createElement("button");
            btn.className = t.sendable ? "" : "warn";
            btn.textContent = t.sendable ? "▶ run" : "⌨ show cmd";
            btn.addEventListener("click", function () { runSecTest(t); });
            el(".result", row).appendChild(btn);
            list.appendChild(row);
        });
        $("secRunAll").addEventListener("click", function () {
            var sendable = SEC_TESTS.filter(function (t) { return t.sendable; });
            var i = 0;
            (function next() {
                if (i >= sendable.length) { logSys("security sweep complete"); return; }
                runSecTest(sendable[i++]).then(next);
            })();
        });
    }

    /* ====================================================================== */
    /*  boot                                                                  */
    /* ====================================================================== */
    function boot() {
        logSys("Webserv compliance dashboard loaded @ " + location.host);
        initIdentity();
        initCgi();
        initUpload();
        initInventory();
        initErrors();
        initMethods();
        initKeepAlive();
        initTimeout();
        initSecurity();
        loadInventory();
    }

    if (document.readyState === "loading")
        document.addEventListener("DOMContentLoaded", boot);
    else
        boot();
})();
