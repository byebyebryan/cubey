#!/usr/bin/env python3
"""Build the dedicated static research report using only the standard library."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import re
import shutil


STYLE = """+:root{--ink:#192d36;--muted:#526873;--paper:#f4f2eb;--panel:#fffef9;--line:#d8ded9;--accent:#036c72;--warm:#b6532a;--night:#142d37}
*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;background:var(--paper);color:var(--ink);font:16px/1.65 system-ui,sans-serif}a{color:#006b79;text-underline-offset:3px}a:hover{color:#a94720}header{background:var(--night);color:#f8f3e7;padding:46px max(24px,calc((100vw - 1320px)/2)) 35px}.eyebrow{font-size:12px;text-transform:uppercase;letter-spacing:.16em;color:#a4d6d4;font-weight:700}h1{font-size:clamp(30px,4vw,56px);line-height:1.12;max-width:1000px;letter-spacing:-.035em;margin:15px 0}header p{max-width:850px;color:#d1dfdd;font-size:18px}header a{color:#afe0df}.status{border:1px solid #527079;padding:6px 12px;display:inline-block;border-radius:20px;font-size:13px}.links{display:flex;gap:12px 24px;flex-wrap:wrap;font-size:14px}.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:14px;margin-top:28px}.stat{border-top:1px solid #54717a;padding:13px 0}.stat b{font-size:30px;display:block;letter-spacing:-.02em}.stat small{color:#c2d4d6;display:block}.layout{max-width:1368px;padding:28px 24px 65px;margin:auto;display:grid;grid-template-columns:240px minmax(0,1fr);gap:42px}aside{position:sticky;top:16px;align-self:start;font-size:13px;max-height:94vh;overflow:auto}aside a{display:block;padding:6px 10px;border-left:2px solid #ccd6d1;text-decoration:none;color:#526873}aside a:hover{border-color:var(--accent);background:#e8ece5}main{min-width:0}section{scroll-margin-top:22px}.card{background:var(--panel);border:1px solid var(--line);border-radius:15px;padding:25px;margin-bottom:25px}.card h2{margin-top:0}.note{border-left:4px solid var(--warm);background:#fff4e7;padding:14px 18px;margin:20px 0}.tag{font-size:11px;border:1px solid #b9cbc5;border-radius:15px;padding:4px 8px;letter-spacing:.05em;text-transform:uppercase;display:inline-block;color:#28575a}h2{font-size:27px;line-height:1.25;margin:43px 0 18px;letter-spacing:-.02em}h3{font-size:20px;margin:29px 0 10px;line-height:1.35}p{margin:0 0 16px}ul,ol{padding-left:25px}li{padding-left:4px;margin:9px 0}li p{margin:0}.table-wrap{overflow:auto;margin:22px 0 28px;border:1px solid var(--line);border-radius:9px}table{border-collapse:collapse;width:100%;background:#fffef9;font-size:14px;min-width:610px}th,td{text-align:left;vertical-align:top;border-bottom:1px solid var(--line);padding:13px 15px}th{background:#e8eee8;font-weight:700;color:#284f54}tr:last-child td{border-bottom:0}td:first-child{min-width:155px}pre{overflow:auto;background:#173541;color:#dcebed;padding:17px;border-radius:9px;font-size:13px;line-height:1.6}code{font: .89em ui-monospace,monospace;background:#e6ebe5;padding:2px 4px;border-radius:3px}pre code{background:none;padding:0}.diagram{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin:20px 0}.diagram div{background:#e7eeea;padding:15px;border-radius:8px;border:1px solid #c5d6ce}.diagram b{display:block}.diagram small{font-size:13px;display:block;color:var(--muted)}.controls{display:grid;grid-template-columns:1fr 1fr;gap:18px;margin:16px 0 20px}label{font-size:14px;display:block}input[type=range]{display:block;width:100%;margin-top:10px;accent-color:var(--accent)}button{border:1px solid #8fb6b0;border-radius:6px;background:#e5f0e8;color:#164b50;padding:8px 12px;cursor:pointer;font:inherit;font-size:13px}button:hover{background:#d2e8db}.canvases{display:grid;grid-template-columns:1fr 1fr;gap:16px}.canvases h3{margin-top:0;font-size:16px}canvas{display:block;width:100%;height:auto;border:1px solid #ccd7d0;border-radius:8px;background:#fffdf5}output{display:block;font-size:14px;margin-top:16px;padding:13px 16px;background:#e8eee7;border-radius:7px}.caption{font-size:13px;color:var(--muted);margin-top:12px}.queue-canvas{max-width:740px}footer{border-top:1px solid #ced7ce;font-size:13px;color:var(--muted);padding-top:22px;margin-top:45px}details{margin:20px 0}summary{cursor:pointer;font-weight:650}.readout{font-variant-numeric:tabular-nums;color:var(--accent);font-weight:700}.verdict{font-size:19px;line-height:1.55}.document h1{display:none}.provenance{font-size:12px;overflow-wrap:anywhere;color:var(--muted)}@media(max-width:950px){.layout{display:block;max-width:950px}aside{position:static;max-height:none;display:flex;flex-wrap:wrap;gap:3px;margin-bottom:24px}aside a{padding:5px 10px;border:1px solid #ccd6d1;border-radius:5px;font-size:12px}.stats{gap:15px}}@media(max-width:600px){header{padding:30px 20px}.layout{padding:22px 16px}.stats{grid-template-columns:1fr}.stat{padding:6px 0}.stat b{font-size:26px}.controls,.canvases,.diagram{grid-template-columns:1fr}.card{padding:18px}h2{font-size:24px}.table-wrap{margin-left:-4px;margin-right:-4px}}@media print{aside,.controls,button,.links{display:none}header{background:white;color:black;padding:0}header p,header a{color:#333}.layout{display:block;padding:0}.card{break-inside:avoid}.stats{display:none}a{color:#17434d}body{background:white}}
"""

LABS = """
<section class="card" id="geometry-lab">
<span class="tag">Independent geometry demonstration</span>
<h2>A live cut can disappear when it becomes a cache</h2>
<p>This is a slice through an interior box with a spherical cutter. The left panel evaluates the cut directly. The right panel reconstructs the freshly sampled result from R16 corners. A short live expression does not guarantee an unchanged bake handoff.</p>
<div class="controls"><label for="radius">Cutter radius <span class="readout" id="radius-label"></span><input id="radius" type="range" min="0.10" max="1.20" step="0.01" value="0.35"></label><label for="phase">Cutter X grid phase <span class="readout" id="phase-label"></span><input id="phase" type="range" min="0" max="1" step="0.01" value="0.50"></label></div>
<div class="canvases"><div><h3>Live source-defined cut</h3><canvas id="live-canvas" width="400" height="400" role="img" aria-label="Analytic spherical cavity in an interior box"></canvas></div><div><h3>Freshly baked trilinear reconstruction</h3><canvas id="baked-canvas" width="400" height="400" role="img" aria-label="The same cavity reconstructed from quantized three-dimensional cell corners"></canvas></div></div>
<output id="geometry-output" aria-live="polite"></output><p class="caption">Sample spacing h = 1. Slice Z = 0.5; sampled planes lie at integer Z. Grid lines show X/Y cell boundaries, not extra samples on this slice. This is a small mathematical example, not a native Cubey capture or a measured error in Guardian.</p>
<button id="reset-geometry" type="button">Restore subgrid cavity example</button>
</section>
<section class="card" id="queue-lab"><span class="tag">Illustrative rates, not Cubey measurements</span><h2>A smooth preview can still build up a backlog</h2><p>The live renderer has a finite pending-expression capacity. In this idealized constant-rate model, the cache must commit durable segments at least as fast as accepted segments arrive. Real batches, bursts and setup delays need native measurements too.</p>
<div class="controls"><label for="arrivals">Accepted segments per second <span class="readout" id="arrivals-label"></span><input id="arrivals" type="range" min="0" max="240" step="10" value="120"></label><label for="service">Cache commits per second <span class="readout" id="service-label"></span><input id="service" type="range" min="0" max="240" step="10" value="80"></label></div>
<canvas id="queue-canvas" class="queue-canvas" width="740" height="225" role="img" aria-label="Pending segment count over five seconds with a 64-segment capacity"></canvas><output id="queue-output" aria-live="polite"></output>
</section>
"""

JS = r"""
function roundEven(x){const f=Math.floor(x),r=x-f;return r<.5?f:r>.5?f+1:f%2===0?f:f+1;}
function quantizeHalf(x){if(x===0)return x;const s=Math.sign(x),v=Math.abs(x),e=Math.floor(Math.log2(v)),step=Math.pow(2,Math.max(e-10,-24));return s*roundEven(v/step)*step;}
function sphere(p,c,r){return Math.hypot(...p.map((v,i)=>v-c[i]))-r;}
function box(p,c,e){const q=p.map((v,i)=>Math.abs(v-c[i])-e[i]);return Math.hypot(...q.map(v=>Math.max(v,0)))+Math.min(Math.max(...q),0);}
function edited(p,r,phase){return Math.max(box(p,[.5,.5,.5],[2,2,2]),-sphere(p,[phase,.5,.5],r));}
function sampled(p,r,phase){const lo=p.map(Math.floor),f=p.map((v,i)=>v-lo[i]);let value=0;for(let z=0;z<2;z++)for(let y=0;y<2;y++)for(let x=0;x<2;x++){const w=(x?f[0]:1-f[0])*(y?f[1]:1-f[1])*(z?f[2]:1-f[2]);value+=w*quantizeHalf(edited([lo[0]+x,lo[1]+y,lo[2]+z],r,phase));}return value;}
function queueState(arrivals,service,seconds=5,cap=64){const growth=Math.max(0,arrivals-service);return {growth,backlog:growth*seconds,secondsToCap:growth>0?cap/growth:Infinity,cap};}
const byId=id=>document.getElementById(id);
function sliceGrid(r,phase){const grid=[];for(let y=-1;y<=2;y++){const row=[];for(let x=-1;x<=2;x++)row.push((quantizeHalf(edited([x,y,0],r,phase))+quantizeHalf(edited([x,y,1],r,phase)))*.5);grid.push(row);}return grid;}
function sliceSample(x,y,grid){const a=Math.floor(x),b=Math.floor(y),fx=x-a,fy=y-b,gx=a+1,gy=b+1;return (grid[gy][gx]*(1-fx)+grid[gy][gx+1]*fx)*(1-fy)+(grid[gy+1][gx]*(1-fx)+grid[gy+1][gx+1]*fx)*fy;}
function paintSlice(id,baked,r,phase){const canvas=byId(id),ctx=canvas.getContext('2d'),w=canvas.width,h=canvas.height,img=ctx.createImageData(w,h),grid=baked?sliceGrid(r,phase):null;for(let py=0;py<h;py++)for(let px=0;px<w;px++){const p=[-1+(px+.5)/w*3,2-(py+.5)/h*3,.5],d=baked?sliceSample(p[0],p[1],grid):edited(p,r,phase),color=d>0?[246,242,228]:[29,62,75],i=(py*w+px)*4;img.data.set([...color,255],i);}ctx.putImageData(img,0,0);ctx.strokeStyle='rgba(123,153,157,.55)';ctx.lineWidth=1;for(let q=0;q<=3;q++){ctx.beginPath();ctx.moveTo(q/3*w,0);ctx.lineTo(q/3*w,h);ctx.stroke();ctx.beginPath();ctx.moveTo(0,q/3*h);ctx.lineTo(w,q/3*h);ctx.stroke();}ctx.strokeStyle='#db7440';ctx.setLineDash([5,4]);ctx.lineWidth=2;ctx.beginPath();ctx.ellipse((phase+1)/3*w,1.5/3*h,r/3*w,r/3*h,0,0,2*Math.PI);ctx.stroke();ctx.setLineDash([]);}
function updateGeometry(){const r=Number(byId('radius').value),p=Number(byId('phase').value);byId('radius-label').textContent=r.toFixed(2)+' h';byId('phase-label').textContent=p.toFixed(2)+' h';paintSlice('live-canvas',false,r,p);paintSlice('baked-canvas',true,r,p);const a=edited([p,.5,.5],r,p),b=sampled([p,.5,.5],r,p),changed=(a>0)!==(b>0);byId('geometry-output').textContent='At the cutter center: live '+a.toFixed(4)+' h ('+(a>0?'air':'solid')+'); baked '+b.toFixed(4)+' h ('+(b>0?'air':'solid')+'). '+(changed?'Retiring this analytic tail would fill the cavity at its center.':'The center agrees here; other positions and the boundary can still differ.');return {radius:r,phase:p,live:a,baked:b,changed};}
function updateQueue(){const a=Number(byId('arrivals').value),s=Number(byId('service').value),q=queueState(a,s);byId('arrivals-label').textContent=a;byId('service-label').textContent=s;const cv=byId('queue-canvas'),ctx=cv.getContext('2d'),w=cv.width,h=cv.height,pad=36,maxY=Math.max(80,q.backlog*1.12);ctx.clearRect(0,0,w,h);ctx.fillStyle='#fffdf5';ctx.fillRect(0,0,w,h);ctx.strokeStyle='#9bb5b1';ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(pad,12);ctx.lineTo(pad,h-pad);ctx.lineTo(w-15,h-pad);ctx.stroke();const y=v=>h-pad-v/maxY*(h-pad-15);ctx.setLineDash([5,4]);ctx.strokeStyle='#b6532a';ctx.beginPath();ctx.moveTo(pad,y(q.cap));ctx.lineTo(w-15,y(q.cap));ctx.stroke();ctx.setLineDash([]);ctx.strokeStyle='#036c72';ctx.lineWidth=3;ctx.beginPath();ctx.moveTo(pad,y(0));ctx.lineTo(w-15,y(q.backlog));ctx.stroke();ctx.font='13px system-ui';ctx.fillStyle='#506975';ctx.fillText('0',pad-16,h-pad+5);ctx.fillText('0 s',pad,h-12);ctx.fillText('5 s',w-39,h-12);ctx.fillText('capacity 64',pad+8,y(q.cap)-7);ctx.fillText('pending segments',pad+10,20);byId('queue-output').textContent=q.growth?'Backlog grows by '+q.growth+' segments/s. After 5 s: '+q.backlog+'. Capacity 64 is reached after '+q.secondsToCap.toFixed(2)+' s. A real editor needs faster service or explicit backpressure.':'No average backlog growth in this idealized model. Bursts, visibility latency and individual GPU work units still need qualification.';return q;}
byId('radius').addEventListener('input',updateGeometry);byId('phase').addEventListener('input',updateGeometry);byId('reset-geometry').addEventListener('click',()=>{byId('radius').value='.35';byId('phase').value='.5';updateGeometry();});byId('arrivals').addEventListener('input',updateQueue);byId('service').addEventListener('input',updateQueue);
window.continuousResearch={quantizeHalf,edited,sampled,sliceGrid,sliceSample,queueState,updateGeometry,updateQueue};updateGeometry();updateQueue();
"""


def slug(text):
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    base = Path(__file__).resolve().parent
    root = base.parents[2]
    plan = root / "docs/notes/sdf-continuous-editing-research-plan.md"
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    records = out / "records"
    records.mkdir(exist_ok=True)
    relative_links = {}

    def resolve_url(url):
        if url.startswith(("https://", "http://", "#")):
            return url
        if url in relative_links:
            return relative_links[url]
        path = (plan.parent / url.split("#")[0]).resolve()
        if not path.is_file():
            raise ValueError(f"Missing report reference: {url}")
        dest = records / path.name
        shutil.copy2(path, dest)
        relative_links[url] = "records/" + dest.name
        return relative_links[url]

    def inline(text):
        tokens = []

        def hold(value):
            tokens.append(value)
            return f"@@TOKEN{len(tokens)-1}@@"

        text = re.sub(r"`([^`]+)`", lambda m: hold("<code>" + html.escape(m[1]) + "</code>"), text)
        text = re.sub(r"\[([^]]+)\]\(([^)]+)\)",
                      lambda m: hold('<a href="' + html.escape(resolve_url(m[2]), quote=True) + '">' + html.escape(m[1]) + '</a>'), text)
        text = html.escape(text)
        text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
        for i, token in enumerate(tokens):
            text = text.replace(f"@@TOKEN{i}@@", token)
        return text

    lines = plan.read_text().splitlines()
    blocks, toc = [], []
    i = 0
    while i < len(lines):
        line = lines[i]
        if not line.strip():
            i += 1
            continue
        heading = re.match(r"^(#{1,3}) (.*)$", line)
        if heading:
            level, title = len(heading[1]), heading[2]
            ident = slug(title)
            blocks.append(f'<h{level} id="{ident}">{inline(title)}</h{level}>')
            if level == 2:
                toc.append((ident, title))
            i += 1
            continue
        if line.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                cells = [x.strip() for x in lines[i].strip("|").split("|")]
                if not all(re.fullmatch(r"[-: ]+", x) for x in cells):
                    rows.append(cells)
                i += 1
            blocks.append('<div class="table-wrap"><table><thead><tr>' + ''.join('<th>' + inline(c) + '</th>' for c in rows[0]) + '</tr></thead><tbody>' + ''.join('<tr>' + ''.join('<td>' + inline(c) + '</td>' for c in row) + '</tr>' for row in rows[1:]) + '</tbody></table></div>')
            continue
        marker = re.match(r"^(- |\d+\. )(.*)$", line)
        if marker:
            ordered = marker[1] != "- "
            tag = "ol" if ordered else "ul"
            items = []
            while i < len(lines):
                match = re.match(r"^(- |\d+\. )(.*)$", lines[i])
                if not match:
                    break
                text = match[2]
                i += 1
                while i < len(lines) and lines[i].startswith("  "):
                    text += " " + lines[i].strip()
                    i += 1
                items.append("<li>" + inline(text) + "</li>")
                if i < len(lines) and not lines[i].strip() and i + 1 < len(lines) and re.match(r"^(- |\d+\. )", lines[i + 1]):
                    i += 1
            blocks.append(f"<{tag}>" + ''.join(items) + f"</{tag}>")
            continue
        paragraph = [line]
        i += 1
        while i < len(lines) and lines[i].strip() and not lines[i].startswith(("#", "|", "- ")) and not re.match(r"^\d+\. ", lines[i]):
            paragraph.append(lines[i])
            i += 1
        blocks.append("<p>" + inline(' '.join(x.strip() for x in paragraph)) + "</p>")

    shutil.copy2(plan, out / "plan.md")
    shutil.copy2(base / "probe.json", out / "probe.json")
    shutil.copy2(base / "probe.py", out / "probe.py")
    shutil.copy2(base / "references.json", out / "references.json")
    shutil.copy2(base / "checks.json", out / "checks.json")
    nav = '<a href="#overview">Overview</a><a href="#geometry-lab">Bake handoff lab</a><a href="#queue-lab">Backlog lab</a>' + ''.join(f'<a href="#{k}">{html.escape(v)}</a>' for k, v in toc)
    overview = """<section class="card" id="overview"><span class="tag">Research findings · proposed native study</span><p class="verdict"><strong>Test live geometry first, then choose the updater.</strong> Retained source + a committed cache + a bounded pending CSG expression is a credible hypothesis. It needs explicit rules for cache queries, stroke capacity, bake handoff and publication before it becomes a continuous editor.</p><div class="diagram" role="img" aria-label="The committed prefix and pending suffix combine for display, while bounded construction prepares a new prefix"><div><b>Committed prefix k</b><small>Versioned cache, source and materials</small></div><div><b>Pending edits k+1…n</b><small>Ordered geometry; atomic strokes and mirrors</small></div><div><b>Edited display</b><small>Trace combined geometry while bounded work prepares the next prefix</small></div></div><p class="note">The current Hybrid tracing mode is a different axis: it chooses sampled bounds versus source queries. It does not already implement pending edits or background baking.</p><p class="caption">Read the detailed plan below, or download the <a href="plan.md">repository plan</a>, <a href="probe.py">independent probe</a> and <a href="probe.json">results/source identities</a>.</p></section>"""
    document = ''.join(blocks)
    result = '<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="description" content="Source-led research and staged validation plan for continuous SDF editing in Cubey"><title>Cubey SDF — Putting continuous editing together</title><style>' + STYLE + '</style></head><body><header><div class="eyebrow">Cubey · SDF · continuous editing</div><h1>Putting the pieces into a testable editing design</h1><p>What projectR gives us, what Cubey already retains, and the experiments needed to connect a live CSG expression to a budgeted cache updater.</p><span class="status">Research complete · native prototype not run</span><div class="stats"><div class="stat"><b>13 leaves</b><small>Room remaining in full Guardian’s source budget</small></div><div class="stat"><b>~939 ms</b><small>Prior recorded Cached geometry-action p95, not a new measurement</small></div><div class="stat"><b>11.11 ms</b><small>Proposed 90 Hz engineering profile; not headset acceptance</small></div></div><div class="links"><a href="http://starship:8787/reports/b37a56f83a63404aba37029a521824414e0e5c5cecb50423d7b2a60f706fb184/">Selectable modes and current costs</a><a href="http://starship:8787/reports/932bc8269b0a1bfc66894eaa883396cab1397e84142f1aa2199e529de8c60503/">Earlier quality/speed study</a><a href="http://starship:8787/reports/#project=cubey">Cubey report catalog</a></div></header><div class="layout"><aside aria-label="Contents">' + nav + '</aside><main>' + overview + LABS + '<article class="document">' + document + '</article><footer><p>Scope: source inspection, primary research and seven independent Python examples. No new native renderer/editor timing, Unity runtime, live device or physical VR qualification.</p><p>Static interactions and delivery are checked separately from rendered-browser acceptance. Browser discovery returned no connected browser during this pass.</p><p class="provenance">Inspected Cubey b9568b85b90f298b4f28f897e77294b081386294 · projectR e2b80796d8f2f9478965a552342bf66fab0e4114 · research 2026-10-30 UTC</p></footer></main></div><script>' + JS + '</script></body></html>'
    (out / "index.html").write_text(result)
    metadata = {"entrypoint": "index.html", "relative_link_map": relative_links,
                "files": {str(p.relative_to(out)): {"bytes": p.stat().st_size, "sha256": hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(out.rglob("*")) if p.is_file()}}
    (base / "report-manifest.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps({"output": str(out), "files": len(metadata["files"]), "entrypoint_bytes": len(result.encode()), "toc_sections": len(toc)}, indent=2))


if __name__ == "__main__":
    main()
