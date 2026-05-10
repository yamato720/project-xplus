#!/usr/bin/env python3

from __future__ import annotations

import json
import math
import sys
from pathlib import Path


def fmt_sci(value: float) -> str:
    return f"{value:.3e}"


def fmt_ms(value: float) -> str:
    return f"{value:.3f}"


def badge(pass_value: bool) -> tuple[str, str]:
    if pass_value:
        return "PASS", "badge-yes"
    return "FAIL", "badge-no"


def render_iteration_rows(traces: list[dict]) -> str:
    rows = []
    for item in traces:
        rows.append(
            "<tr>"
            f"<td>{item['iteration']}</td>"
            f"<td>{fmt_sci(item['alpha'])}</td>"
            f"<td>{fmt_sci(item['beta'])}</td>"
            f"<td>{fmt_sci(item['rz'])}</td>"
            f"<td>{fmt_sci(item['rr'])}</td>"
            f"<td>{fmt_sci(item['residual'])}</td>"
            "</tr>"
        )
    return "\n".join(rows)


def render_iteration_json(traces: list[dict]) -> str:
    return json.dumps(traces, ensure_ascii=False)


def render_timing_bars(timing: dict[str, float]) -> str:
    total = max(timing.get("total", 0.0), 1e-12)
    bars = []
    for key in ["host_setup", "buffer_h2d", "kernel_total", "buffer_d2h", "verify"]:
        value = timing.get(key, 0.0)
        width = max(1.0, value / total * 100.0) if value > 0.0 else 0.0
        label = key
        bars.append(
            "<div class='timeline-row'>"
            f"<div class='timeline-label'>{label}</div>"
            "<div class='timeline-axis'>"
            f"<div class='timeline-bar' style='left:0%; width:{width:.2f}%;'></div>"
            "</div>"
            f"<div class='timeline-range'>{fmt_ms(value)} ms</div>"
            "</div>"
        )
    return "\n".join(bars)


def render_kernel_timing_bars(kernel_timing: dict[str, float]) -> str:
    total = max(
        kernel_timing.get("spmv_total", 0.0)
        + kernel_timing.get("init_total", 0.0)
        + kernel_timing.get("dot_total", 0.0)
        + kernel_timing.get("update_xrz_total", 0.0)
        + kernel_timing.get("update_p_total", 0.0),
        1e-12,
    )
    bars = []
    for key in ["spmv_total", "init_total", "dot_total", "update_xrz_total", "update_p_total"]:
        value = kernel_timing.get(key, 0.0)
        width = max(1.0, value / total * 100.0) if value > 0.0 else 0.0
        bars.append(
            "<div class='timeline-row'>"
            f"<div class='timeline-label'>{key}</div>"
            "<div class='timeline-axis'>"
            f"<div class='timeline-bar' style='left:0%; width:{width:.2f}%;'></div>"
            "</div>"
            f"<div class='timeline-range'>{fmt_ms(value)} ms</div>"
            "</div>"
        )
    return "\n".join(bars)


def render_html_interactive(data: dict) -> str:
    result = data["result"]
    dataset = data["dataset"]
    host_timing = data["host_timing_ms"]
    kernel_timing = data["kernel_timing_ms"]
    traces = data.get("iterations_trace", [])
    pass_label, pass_class = badge(result["pass"])
    pager_script = """
  <script>
    const iterationData = __ITERATION_JSON__;
    let pageSize = 30;
    let currentPage = 1;

    const bodyEl = document.getElementById("iter-table-body");
    const emptyEl = document.getElementById("iter-empty");
    const metaEl = document.getElementById("iter-page-meta");
    const firstBtn = document.getElementById("iter-first");
    const prevBtn = document.getElementById("iter-prev");
    const nextBtn = document.getElementById("iter-next");
    const lastBtn = document.getElementById("iter-last");
    const pageInput = document.getElementById("iter-page-input");
    const pageGoBtn = document.getElementById("iter-page-go");
    const pageSizeRadios = document.querySelectorAll("input[name='iter-page-size']");

    function totalPages() {
      return Math.max(1, Math.ceil(iterationData.length / pageSize));
    }

    function clampPage(page) {
      return Math.min(totalPages(), Math.max(1, page));
    }

    function rowHtml(item) {
      return "<tr>"
        + "<td>" + item.iteration + "</td>"
        + "<td>" + Number(item.alpha).toExponential(3) + "</td>"
        + "<td>" + Number(item.beta).toExponential(3) + "</td>"
        + "<td>" + Number(item.rz).toExponential(3) + "</td>"
        + "<td>" + Number(item.rr).toExponential(3) + "</td>"
        + "<td>" + Number(item.residual).toExponential(3) + "</td>"
        + "</tr>";
    }

    function renderPage(page) {
      currentPage = clampPage(page);
      if (iterationData.length === 0) {
        bodyEl.innerHTML = "";
        emptyEl.classList.remove("d-none");
      } else {
        emptyEl.classList.add("d-none");
        const start = (currentPage - 1) * pageSize;
        const end = Math.min(start + pageSize, iterationData.length);
        bodyEl.innerHTML = iterationData.slice(start, end).map(rowHtml).join("");
      }

      const pages = totalPages();
      metaEl.textContent = `第 ${currentPage} / ${pages} 页`;
      firstBtn.disabled = currentPage <= 1;
      prevBtn.disabled = currentPage <= 1;
      nextBtn.disabled = currentPage >= pages;
      lastBtn.disabled = currentPage >= pages;
      pageInput.value = String(currentPage);
    }

    firstBtn.addEventListener("click", () => renderPage(1));
    prevBtn.addEventListener("click", () => renderPage(currentPage - 1));
    nextBtn.addEventListener("click", () => renderPage(currentPage + 1));
    lastBtn.addEventListener("click", () => renderPage(totalPages()));
    pageGoBtn.addEventListener("click", () => renderPage(Number(pageInput.value || "1")));
    pageInput.addEventListener("keydown", (event) => {
      if (event.key === "Enter") {
        renderPage(Number(pageInput.value || "1"));
      }
    });
    pageSizeRadios.forEach((radio) => {
      radio.addEventListener("change", () => {
        if (radio.checked) {
          pageSize = Number(radio.value || "30");
          currentPage = 1;
          renderPage(1);
        }
      });
    });

    renderPage(1);
  </script>
""".replace("__ITERATION_JSON__", render_iteration_json(traces))

    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Project-XPlus Report</title>
  <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css" rel="stylesheet">
  <style>
    :root {{
      --bg: #f6f4ee;
      --panel: #fffdf8;
      --line: #ddd7cc;
      --ink: #1f2937;
      --muted: #6b7280;
    }}
    body {{
      background: linear-gradient(180deg, #f6f4ee 0%, #efebe2 100%);
      color: var(--ink);
      font-family: "Helvetica Neue", Arial, sans-serif;
    }}
    .page {{
      max-width: 1680px;
      margin: 0 auto;
      padding: 24px;
    }}
    .report-card {{
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 18px;
      box-shadow: 0 8px 24px rgba(15, 23, 42, 0.05);
      height: 100%;
    }}
    .report-card .card-body {{
      padding: 20px 22px;
    }}
    .report-title {{
      font-size: 2rem;
      font-weight: 700;
      letter-spacing: -0.02em;
    }}
    .report-subtitle {{
      color: var(--muted);
      word-break: break-all;
    }}
    .section-title {{
      font-size: 1.05rem;
      font-weight: 700;
      margin-bottom: 12px;
    }}
    .section-note {{
      color: var(--muted);
      font-size: 0.85rem;
      margin-bottom: 12px;
    }}
    .kv-grid {{
      display: grid;
      gap: 10px;
    }}
    .kv-row {{
      display: grid;
      grid-template-columns: 150px 1fr;
      gap: 14px;
      align-items: baseline;
      font-size: 0.95rem;
    }}
    .kv-key {{
      color: var(--muted);
    }}
    .kv-value {{
      font-weight: 700;
      word-break: break-word;
    }}
    .badge-yes {{
      background: #dcfce7;
      color: #166534;
    }}
    .badge-no {{
      background: #fee2e2;
      color: #991b1b;
    }}
    .timeline {{
      display: flex;
      flex-direction: column;
      gap: 10px;
    }}
    .timeline-row {{
      display: grid;
      grid-template-columns: 180px minmax(240px, 1fr) 120px;
      gap: 14px;
      align-items: center;
    }}
    .timeline-label {{
      font-weight: 600;
      overflow-wrap: anywhere;
    }}
    .timeline-axis {{
      position: relative;
      height: 16px;
      background: #ede7dd;
      border-radius: 999px;
      overflow: hidden;
    }}
    .timeline-bar {{
      position: absolute;
      top: 0;
      height: 100%;
      border-radius: 999px;
      background: linear-gradient(90deg, #2563eb 0%, #0f766e 100%);
      min-width: 2px;
    }}
    .timeline-range {{
      font-size: 0.85rem;
      color: var(--muted);
      text-align: right;
    }}
    .table-wrap {{
      overflow-x: auto;
    }}
    .table-wrap table {{
      min-width: 100%;
      white-space: nowrap;
    }}
    .pager-bar {{
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 14px;
    }}
    .pager-controls {{
      display: flex;
      flex-wrap: wrap;
      gap: 8px;
      align-items: center;
    }}
    .pager-controls button {{
      border: 1px solid var(--line);
      background: #fff;
      color: var(--ink);
      border-radius: 10px;
      padding: 6px 12px;
      font-size: 0.9rem;
    }}
    .pager-controls button:disabled {{
      opacity: 0.45;
      cursor: not-allowed;
    }}
    .pager-meta {{
      color: var(--muted);
      font-size: 0.9rem;
    }}
    .pager-jump {{
      display: flex;
      gap: 8px;
      align-items: center;
    }}
    .pager-jump input {{
      width: 96px;
      border: 1px solid var(--line);
      border-radius: 10px;
      padding: 6px 10px;
      background: #fff;
    }}
    .pager-size {{
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
      align-items: center;
    }}
    .pager-size label {{
      display: inline-flex;
      align-items: center;
      gap: 6px;
      font-size: 0.9rem;
      color: var(--ink);
      padding: 6px 10px;
      border: 1px solid var(--line);
      border-radius: 10px;
      background: #fff;
    }}
    .pager-size input {{
      margin: 0;
    }}
    .pager-btn {{
      border: 1px solid var(--line);
      background: #fff;
      color: var(--ink);
      border-radius: 10px;
      padding: 6px 12px;
      font-size: 0.9rem;
      line-height: 1.2;
    }}
    .pager-btn:disabled {{
      opacity: 0.45;
      cursor: not-allowed;
    }}
    .pager-empty {{
      color: var(--muted);
      padding: 12px 4px;
    }}
    @media (max-width: 1100px) {{
      .kv-row {{
        grid-template-columns: 1fr;
      }}
      .timeline-row {{
        grid-template-columns: 1fr;
      }}
      .timeline-range {{
        text-align: left;
      }}
      .pager-bar {{
        align-items: flex-start;
      }}
    }}
  </style>
</head>
<body>
  <div class="page">
    <div class="mb-4">
      <div class="report-title">Project-XPlus Dashboard</div>
      <div class="report-subtitle">{dataset['path']}</div>
    </div>

    <div class="row g-4 mb-4">
      <div class="col-12 col-xl-4">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">Problem</div>
          <div class="kv-grid">
            <div class="kv-row"><div class="kv-key">n / nnz</div><div class="kv-value">{dataset['n']} / {dataset['nnz']}</div></div>
            <div class="kv-row"><div class="kv-key">max_iters</div><div class="kv-value">{dataset['max_iters']}</div></div>
            <div class="kv-row"><div class="kv-key">tau</div><div class="kv-value">{fmt_sci(dataset['tau'])}</div></div>
            <div class="kv-row"><div class="kv-key">device_index</div><div class="kv-value">{dataset['device_index']}</div></div>
          </div>
        </div></div>
      </div>
      <div class="col-12 col-xl-4">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">Accuracy</div>
          <div class="mb-3">
            <span class="badge rounded-pill {pass_class} px-3 py-2">{pass_label}</span>
          </div>
          <div class="kv-grid">
            <div class="kv-row"><div class="kv-key">status</div><div class="kv-value">{result['status']}</div></div>
            <div class="kv-row"><div class="kv-key">converged</div><div class="kv-value">{'yes' if result['converged'] else 'no'}</div></div>
            <div class="kv-row"><div class="kv-key">iterations</div><div class="kv-value">{result['iterations']}</div></div>
            <div class="kv-row"><div class="kv-key">final_rr</div><div class="kv-value">{fmt_sci(result['final_rr'])}</div></div>
            <div class="kv-row"><div class="kv-key">final_residual</div><div class="kv-value">{fmt_sci(result['final_residual_norm'])}</div></div>
            <div class="kv-row"><div class="kv-key">max_abs_diff</div><div class="kv-value">{fmt_sci(result['solution_max_abs_diff'])}</div></div>
          </div>
        </div></div>
      </div>
      <div class="col-12 col-xl-4">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">Host Timing</div>
          <div class="section-note">End-to-end and host-side timing in milliseconds</div>
          <div class="kv-grid">
            <div class="kv-row"><div class="kv-key">total</div><div class="kv-value">{fmt_ms(host_timing['total'])}</div></div>
            <div class="kv-row"><div class="kv-key">host_setup</div><div class="kv-value">{fmt_ms(host_timing['host_setup'])}</div></div>
            <div class="kv-row"><div class="kv-key">buffer_h2d</div><div class="kv-value">{fmt_ms(host_timing['buffer_h2d'])}</div></div>
            <div class="kv-row"><div class="kv-key">kernel_total</div><div class="kv-value">{fmt_ms(host_timing['kernel_total'])}</div></div>
            <div class="kv-row"><div class="kv-key">buffer_d2h</div><div class="kv-value">{fmt_ms(host_timing['buffer_d2h'])}</div></div>
            <div class="kv-row"><div class="kv-key">verify</div><div class="kv-value">{fmt_ms(host_timing['verify'])}</div></div>
          </div>
        </div></div>
      </div>
    </div>

    <div class="row g-4 mb-4">
      <div class="col-12 col-xxl-5">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">Host Breakdown</div>
          <div class="section-note">Relative share against end-to-end total</div>
          <div class="timeline">
            {render_timing_bars(host_timing)}
          </div>
        </div></div>
      </div>
      <div class="col-12 col-xxl-7">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">Kernel Breakdown</div>
          <div class="section-note">Only kernel-internal categories, separated from host macro timing</div>
          <div class="kv-grid mb-3">
            <div class="kv-row"><div class="kv-key">spmv avg / calls</div><div class="kv-value">{fmt_ms(kernel_timing['spmv_avg'])} / {kernel_timing['spmv_calls']}</div></div>
            <div class="kv-row"><div class="kv-key">init avg / calls</div><div class="kv-value">{fmt_ms(kernel_timing['init_avg'])} / {kernel_timing['init_calls']}</div></div>
            <div class="kv-row"><div class="kv-key">dot avg / calls</div><div class="kv-value">{fmt_ms(kernel_timing['dot_avg'])} / {kernel_timing['dot_calls']}</div></div>
            <div class="kv-row"><div class="kv-key">update_xrz avg / calls</div><div class="kv-value">{fmt_ms(kernel_timing['update_xrz_avg'])} / {kernel_timing['update_xrz_calls']}</div></div>
            <div class="kv-row"><div class="kv-key">update_p avg / calls</div><div class="kv-value">{fmt_ms(kernel_timing['update_p_avg'])} / {kernel_timing['update_p_calls']}</div></div>
          </div>
          <div class="timeline">
            {render_kernel_timing_bars(kernel_timing)}
          </div>
        </div></div>
      </div>
    </div>

    <div class="row g-4 mb-4">
      <div class="col-12">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">Iteration Trace</div>
          <div class="section-note">alpha / beta / residual progression, 30 rows per page</div>
          <div class="pager-bar">
            <div class="pager-controls">
              <button type="button" class="pager-btn" id="iter-first">最前</button>
              <button type="button" class="pager-btn" id="iter-prev">上一页</button>
              <button type="button" class="pager-btn" id="iter-next">下一页</button>
              <button type="button" class="pager-btn" id="iter-last">最后</button>
            </div>
            <div class="pager-meta" id="iter-page-meta">第 1 / 1 页</div>
            <div class="pager-jump">
              <input id="iter-page-input" type="number" min="1" step="1" placeholder="页码" />
              <button type="button" class="pager-btn" id="iter-page-go">跳转</button>
            </div>
          </div>
          <div class="pager-bar">
            <div class="pager-size">
              <span class="pager-meta">每页显示</span>
              <label><input type="radio" name="iter-page-size" value="10" />10</label>
              <label><input type="radio" name="iter-page-size" value="30" checked />30</label>
              <label><input type="radio" name="iter-page-size" value="50" />50</label>
            </div>
          </div>
          <div class="table-wrap">
            <table class="table table-sm align-middle">
              <thead>
                <tr>
                  <th>iter</th>
                  <th>alpha</th>
                  <th>beta</th>
                  <th>rz</th>
                  <th>rr</th>
                  <th>residual</th>
                </tr>
              </thead>
              <tbody id="iter-table-body"></tbody>
            </table>
          </div>
          <div class="pager-empty d-none" id="iter-empty">没有迭代数据。</div>
        </div></div>
      </div>
    </div>
  </div>
  {pager_script}
</body>
</html>
"""


def render_html_static(data: dict) -> str:
    result = data["result"]
    dataset = data["dataset"]
    host_timing = data["host_timing_ms"]
    kernel_timing = data["kernel_timing_ms"]
    traces = data.get("iterations_trace", [])
    pass_label, pass_class = badge(result["pass"])

    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Project-XPlus Report Static</title>
  <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css" rel="stylesheet">
  <style>
    :root {{
      --bg: #f6f4ee;
      --panel: #fffdf8;
      --line: #ddd7cc;
      --ink: #1f2937;
      --muted: #6b7280;
    }}
    body {{
      background: linear-gradient(180deg, #f6f4ee 0%, #efebe2 100%);
      color: var(--ink);
      font-family: "Helvetica Neue", Arial, sans-serif;
    }}
    .page {{
      max-width: 1680px;
      margin: 0 auto;
      padding: 24px;
    }}
    .report-card {{
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 18px;
      box-shadow: 0 8px 24px rgba(15, 23, 42, 0.05);
      height: 100%;
    }}
    .report-card .card-body {{
      padding: 20px 22px;
    }}
    .report-title {{
      font-size: 2rem;
      font-weight: 700;
      letter-spacing: -0.02em;
    }}
    .report-subtitle {{
      color: var(--muted);
      word-break: break-all;
    }}
    .section-title {{
      font-size: 1.05rem;
      font-weight: 700;
      margin-bottom: 12px;
    }}
    .section-note {{
      color: var(--muted);
      font-size: 0.85rem;
      margin-bottom: 12px;
    }}
    .kv-grid {{
      display: grid;
      gap: 10px;
    }}
    .kv-row {{
      display: grid;
      grid-template-columns: 150px 1fr;
      gap: 14px;
      align-items: baseline;
      font-size: 0.95rem;
    }}
    .kv-key {{
      color: var(--muted);
    }}
    .kv-value {{
      font-weight: 700;
      word-break: break-word;
    }}
    .badge-yes {{
      background: #dcfce7;
      color: #166534;
    }}
    .badge-no {{
      background: #fee2e2;
      color: #991b1b;
    }}
    .timeline {{
      display: flex;
      flex-direction: column;
      gap: 10px;
    }}
    .timeline-row {{
      display: grid;
      grid-template-columns: 180px minmax(240px, 1fr) 120px;
      gap: 14px;
      align-items: center;
    }}
    .timeline-label {{
      font-weight: 600;
      overflow-wrap: anywhere;
    }}
    .timeline-axis {{
      position: relative;
      height: 16px;
      background: #ede7dd;
      border-radius: 999px;
      overflow: hidden;
    }}
    .timeline-bar {{
      position: absolute;
      top: 0;
      height: 100%;
      border-radius: 999px;
      background: linear-gradient(90deg, #2563eb 0%, #0f766e 100%);
      min-width: 2px;
    }}
    .timeline-range {{
      font-size: 0.85rem;
      color: var(--muted);
      text-align: right;
    }}
    .table-wrap {{
      overflow-x: auto;
    }}
    .table-wrap table {{
      min-width: 100%;
      white-space: nowrap;
    }}
    @media (max-width: 1100px) {{
      .kv-row {{
        grid-template-columns: 1fr;
      }}
      .timeline-row {{
        grid-template-columns: 1fr;
      }}
      .timeline-range {{
        text-align: left;
      }}
    }}
  </style>
</head>
<body>
  <div class="page">
    <div class="mb-4">
      <div class="report-title">Project-XPlus Dashboard Static</div>
      <div class="report-subtitle">{dataset['path']}</div>
    </div>

    <div class="row g-4 mb-4">
      <div class="col-12 col-xl-4">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">Problem</div>
          <div class="kv-grid">
            <div class="kv-row"><div class="kv-key">n / nnz</div><div class="kv-value">{dataset['n']} / {dataset['nnz']}</div></div>
            <div class="kv-row"><div class="kv-key">max_iters</div><div class="kv-value">{dataset['max_iters']}</div></div>
            <div class="kv-row"><div class="kv-key">tau</div><div class="kv-value">{fmt_sci(dataset['tau'])}</div></div>
            <div class="kv-row"><div class="kv-key">device_index</div><div class="kv-value">{dataset['device_index']}</div></div>
          </div>
        </div></div>
      </div>
      <div class="col-12 col-xl-4">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">Accuracy</div>
          <div class="mb-3">
            <span class="badge rounded-pill {pass_class} px-3 py-2">{pass_label}</span>
          </div>
          <div class="kv-grid">
            <div class="kv-row"><div class="kv-key">status</div><div class="kv-value">{result['status']}</div></div>
            <div class="kv-row"><div class="kv-key">converged</div><div class="kv-value">{'yes' if result['converged'] else 'no'}</div></div>
            <div class="kv-row"><div class="kv-key">iterations</div><div class="kv-value">{result['iterations']}</div></div>
            <div class="kv-row"><div class="kv-key">final_rr</div><div class="kv-value">{fmt_sci(result['final_rr'])}</div></div>
            <div class="kv-row"><div class="kv-key">final_residual</div><div class="kv-value">{fmt_sci(result['final_residual_norm'])}</div></div>
            <div class="kv-row"><div class="kv-key">max_abs_diff</div><div class="kv-value">{fmt_sci(result['solution_max_abs_diff'])}</div></div>
          </div>
        </div></div>
      </div>
      <div class="col-12 col-xl-4">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">Host Timing</div>
          <div class="section-note">End-to-end and host-side timing in milliseconds</div>
          <div class="kv-grid">
            <div class="kv-row"><div class="kv-key">total</div><div class="kv-value">{fmt_ms(host_timing['total'])}</div></div>
            <div class="kv-row"><div class="kv-key">host_setup</div><div class="kv-value">{fmt_ms(host_timing['host_setup'])}</div></div>
            <div class="kv-row"><div class="kv-key">buffer_h2d</div><div class="kv-value">{fmt_ms(host_timing['buffer_h2d'])}</div></div>
            <div class="kv-row"><div class="kv-key">kernel_total</div><div class="kv-value">{fmt_ms(host_timing['kernel_total'])}</div></div>
            <div class="kv-row"><div class="kv-key">buffer_d2h</div><div class="kv-value">{fmt_ms(host_timing['buffer_d2h'])}</div></div>
            <div class="kv-row"><div class="kv-key">verify</div><div class="kv-value">{fmt_ms(host_timing['verify'])}</div></div>
          </div>
        </div></div>
      </div>
    </div>

    <div class="row g-4 mb-4">
      <div class="col-12 col-xxl-5">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">Host Breakdown</div>
          <div class="section-note">Relative share against end-to-end total</div>
          <div class="timeline">
            {render_timing_bars(host_timing)}
          </div>
        </div></div>
      </div>
      <div class="col-12 col-xxl-7">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">Kernel Breakdown</div>
          <div class="section-note">Only kernel-internal categories, separated from host macro timing</div>
          <div class="kv-grid mb-3">
            <div class="kv-row"><div class="kv-key">spmv avg / calls</div><div class="kv-value">{fmt_ms(kernel_timing['spmv_avg'])} / {kernel_timing['spmv_calls']}</div></div>
            <div class="kv-row"><div class="kv-key">init avg / calls</div><div class="kv-value">{fmt_ms(kernel_timing['init_avg'])} / {kernel_timing['init_calls']}</div></div>
            <div class="kv-row"><div class="kv-key">dot avg / calls</div><div class="kv-value">{fmt_ms(kernel_timing['dot_avg'])} / {kernel_timing['dot_calls']}</div></div>
            <div class="kv-row"><div class="kv-key">update_xrz avg / calls</div><div class="kv-value">{fmt_ms(kernel_timing['update_xrz_avg'])} / {kernel_timing['update_xrz_calls']}</div></div>
            <div class="kv-row"><div class="kv-key">update_p avg / calls</div><div class="kv-value">{fmt_ms(kernel_timing['update_p_avg'])} / {kernel_timing['update_p_calls']}</div></div>
          </div>
          <div class="timeline">
            {render_kernel_timing_bars(kernel_timing)}
          </div>
        </div></div>
      </div>
    </div>

    <div class="row g-4 mb-4">
      <div class="col-12">
        <div class="report-card card"><div class="card-body">
          <div class="section-title">Iteration Trace</div>
          <div class="section-note">Static fully expanded table for VSCode preview compatibility</div>
          <div class="table-wrap">
            <table class="table table-sm align-middle">
              <thead>
                <tr>
                  <th>iter</th>
                  <th>alpha</th>
                  <th>beta</th>
                  <th>rz</th>
                  <th>rr</th>
                  <th>residual</th>
                </tr>
              </thead>
              <tbody>
                {render_iteration_rows(traces)}
              </tbody>
            </table>
          </div>
        </div></div>
      </div>
    </div>
  </div>
</body>
</html>
"""


def main() -> int:
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <interactive|static> <input.json> <output.html>", file=sys.stderr)
        return 1

    mode = sys.argv[1]
    json_path = Path(sys.argv[2])
    html_path = Path(sys.argv[3])
    data = json.loads(json_path.read_text(encoding="utf-8"))
    html_path.parent.mkdir(parents=True, exist_ok=True)
    if mode == "interactive":
        html = render_html_interactive(data)
    elif mode == "static":
        html = render_html_static(data)
    else:
        print(f"unknown mode: {mode}", file=sys.stderr)
        return 1
    html_path.write_text(html, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
