#!/usr/bin/env python3
"""Create an offline HTML comparison from portable measurement JSON."""
import argparse
from html import escape
import json
from pathlib import Path


def measurement_points(data):
    return [{"id": r["id"], "group": r.get("chart_group", r["condition"] + "/" + r["state"]),
               "startup": r["seconds_to_gameplay"], "fps": r["frames"]["fps"],
               "preload": (r.get("diagnostics", {}).get("preload") or {}).get("ms", 0) / 1000
                          if (r.get("diagnostics", {}).get("preload") is not None) else None,
               "hitches": r["frames"]["hitches"]["33.333"]["count"],
               "early_hitches": (r.get("early_gameplay") or {}).get("hitches", {}).get("33.333", {}).get("count"),
               "hitch_time": r["frames"]["hitches"]["33.333"]["total_ms"],
               "footprint": r["memory"]["settled_median_bytes"]["footprint_bytes"]/2**20,
               "pressure": 100*sum(v for k, v in r["memory"]["system_pressure_samples"].items() if int(k) >= 2)
                           /sum(r["memory"]["system_pressure_samples"].values())}
              for r in data["runs"]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("measurements", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data = json.loads(args.measurements.read_text())
    points = measurement_points(data)
    groups = data.get("chart_groups", [["main/first", "Main · fresh", 1], ["main/restart", "Main · restart", 1],
                       ["empty/first", "Feature · no preparation", 2], ["empty/restart", "Feature · runtime reuse", 2],
                       ["prepared/first", "Feature · full preload", 3], ["prepared/restart", "Feature · observed preload", 3]])
    note = data.get("chart_note", "Fresh = a new verified app Metal cache namespace and isolated renderer inputs. Restart reuses that launch’s caches. System and file caches remain shared. Hitches are CPU presentation intervals, separate from driver fallback counts. Footprint is charged process memory; it is distinct from RSS and system pressure.")
    html = '''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>__TITLE__</title>
<style>:root{font-family:var(--font-sans,system-ui);color:var(--foreground,#ddd);font-size:14px}body{margin:0}p{color:var(--muted-foreground,#999);line-height:1.5;margin:0 0 16px}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.card{background:var(--card,#202124);border-radius:var(--radius,10px);padding:16px;min-width:0}h3{font-size:15px;margin:0 0 5px}small{display:block;color:var(--muted-foreground,#999);font-size:12px;margin-bottom:14px}.row{margin-top:6px}.label{display:flex;justify-content:space-between;gap:8px;font-size:12px;margin-bottom:5px}.value{font-family:var(--font-mono,monospace);white-space:nowrap}.track{height:13px;position:relative;background:var(--secondary,#303236);border-radius:3px}.point{position:absolute;width:9px;height:9px;top:2px;border-radius:50%;background:var(--chart-1,#75aaf0);transform:translateX(-50%);outline:1px solid var(--card,#202124)}.feature{background:var(--chart-2,#6ac3a3)}.prepared{background:var(--chart-3,#e1b05e)}.median{position:absolute;top:-2px;height:17px;width:2px;background:var(--foreground,#ddd);transform:translateX(-50%)}.axis{display:flex;justify-content:space-between;font-size:10px;color:var(--muted-foreground,#999);margin-top:6px}.note{margin-top:16px;font-size:12px}@media(max-width:580px){.grid{grid-template-columns:1fr}}</style></head><body>
<p>1280×720 · TAA · 60-second hidden gameplay · fixed camera · 60 FPS cap. Each dot is one launch; the line marks the median. Hover a dot for its value.</p><div class="grid" id="charts"></div>
<p class="note">__NOTE__</p>
<script>const points=__DATA__;
const groups=__GROUPS__;
const metrics=[['startup','Time to verified gameplay','seconds; preparation time excluded',1],['preload','Synchronous preload','seconds; all selected states ready before gameplay',2],['hitches','Frame intervals >33.333 ms','median count in the 60-second window',1],['fps','Presentation cadence','FPS under the 60 FPS cap',2],['footprint','Settled process footprint','MiB; median of final 10 gameplay seconds',1],['hitch_time','Time in long frames','ms; summed duration of intervals >33.333 ms',1],['pressure','Elevated system pressure','% of startup + gameplay samples; warning or critical',1]];
if(points.some(x=>Number.isFinite(x.early_hitches)))metrics.splice(3,0,['early_hitches','First 10 seconds: long frames','intervals >33.333 ms; complete gameplay intervals only',1]);
for(const [key,title,note,decimals] of metrics){const card=document.createElement('section');card.className='card';const h=document.createElement('h3');h.textContent=title;card.append(h);const sub=document.createElement('small');sub.textContent=note;card.append(sub);let max=key==='fps'?60.1:key==='pressure'?100:Math.max(...points.map(x=>x[key]).filter(Number.isFinite),1)*1.08;
if(key.includes('hitches'))max=Math.ceil(max);for(const [group,label,color] of groups){const values=points.filter(x=>x.group===group&&Number.isFinite(x[key])).sort((a,b)=>a[key]-b[key]);const row=document.createElement('div');row.className='row';const l=document.createElement('div');l.className='label';const name=document.createElement('span');name.textContent=label;const value=document.createElement('span');value.className='value';const n=values.length;const median=n?(values[Math.floor((n-1)/2)][key]+values[Math.ceil((n-1)/2)][key])/2:null;value.textContent=n?median.toFixed(decimals)+' · n='+n:'unavailable';l.append(name,value);row.append(l);const track=document.createElement('div');track.className='track';for(const item of values){const dot=document.createElement('span');dot.className='point'+(color===2?' feature':color===3?' prepared':'');if(color>3)dot.style.background='var(--chart-'+color+',var(--chart-1,#75aaf0))';dot.style.left=(Math.max(0,Math.min(100,item[key]/max*100)))+'%';dot.title=item.id+': '+item[key].toFixed(3);track.append(dot)}if(n){const m=document.createElement('i');m.className='median';m.style.left=(median/max*100)+'%';track.append(m)}row.append(track);card.append(row)}const axis=document.createElement('div');axis.className='axis';axis.innerHTML='<span>0</span><span>'+max.toFixed(key.includes('hitches')?0:1)+'</span>';card.append(axis);document.querySelector('#charts').append(card)}</script></body></html>'''
    args.output.write_text(html.replace("__DATA__", json.dumps(points).replace("<", "\\u003c"))
                          .replace("__GROUPS__", json.dumps(groups).replace("<", "\\u003c"))
                          .replace("__TITLE__", escape(data.get("chart_title", "Main versus shader performance")))
                          .replace("__NOTE__", escape(note)))


if __name__ == "__main__":
    main()
