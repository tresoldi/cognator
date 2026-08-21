let worker=null, wasmReady=false, lastResult=null, calibration=null;
let simulation=null, currentSort={key:null, dir:1};

const EX={};
(function initExamples(){
  const sel=document.getElementById('examples');
  if(typeof GUIDE_STEPS==='undefined')return;
  const placeholder=document.createElement('option');
  placeholder.value='';placeholder.textContent='— Choose an example —';placeholder.disabled=true;placeholder.selected=true;
  sel.appendChild(placeholder);
  for(let i=0;i<GUIDE_STEPS.length;i++){
    if(!GUIDE_STEPS[i].example)continue;
    const key='guide_'+i;
    EX[key]=GUIDE_STEPS[i].example;
    const opt=document.createElement('option');
    opt.value=key;opt.textContent=GUIDE_STEPS[i].title;
    sel.appendChild(opt);
  }
})();

function initEditor(){
  document.getElementById('taEditor').value=EX[Object.keys(EX)[0]||'']||'';
}

function getEd(){return document.getElementById('taEditor').value}
function setEd(t){document.getElementById('taEditor').value=t}
function onExampleSelect(){const k=document.getElementById('examples').value;if(!k)return;setEd(EX[k]);clearResults()}
function clearEd(){setEd('');clearResults()}
function uploadFile(ev){
  const f=ev.target.files[0];if(!f)return;
  const r=new FileReader();r.onload=e=>{setEd(e.target.result);document.getElementById('examples').value='';clearResults()};
  r.readAsText(f);ev.target.value='';
}
function saveCSV(){downloadFile('wordlist.csv',getEd())}
function downloadFile(n,c){const b=new Blob([c],{type:'text/plain'}),u=URL.createObjectURL(b),a=document.createElement('a');a.href=u;a.download=n;document.body.appendChild(a);a.click();document.body.removeChild(a);URL.revokeObjectURL(u)}

function clearResults(){
  document.getElementById('resultsPanel').style.display='none';
  document.getElementById('status').textContent='';
  lastResult=null;
  if(simulation){simulation.stop();simulation=null}
}

/* Advanced config hydration. Each field ID matches a C/WASM detect option key,
   so the mapping is 1:1. */
const KNOB_IDS=[
 'scorer_mode','iterations','lambda','dirichlet_concentration','dirichlet_sharpness','dirichlet_scale',
 'alignment_mode','class_reduce','null_mode','null_max_runs','min_pairs',
 'threshold','threshold_mode','threshold_min','threshold_max','threshold_shrink',
 'graph_mode','graph_expansion_mode','graph_top_k','graph_adaptive_margin','graph_adaptive_either_max',
 'graph_adaptive_ambiguity','graph_mutual_boost','graph_triangle_boost','graph_contrastive_penalty',
 'graph_contrastive_floor','graph_risk_rank_weight','graph_risk_gap_weight','graph_unilateral_penalty',
 'graph_rival_floor','refine_mode','refine_confidence_threshold','refine_bridge_ratio',
 'bootstrap','bootstrap_sigma','cluster_scope'
];
function readOptions(){
  const opts={};
  opts.preset=document.getElementById('preset').value;
  opts.seed=parseInt(document.getElementById('seed').value||'1',10)||1;
  for(const id of KNOB_IDS){
    const el=document.getElementById(id);
    if(!el)continue;
    if(el.type==='checkbox'){opts[id]=el.checked;continue}
    if(el.type==='number'){const v=parseFloat(el.value);if(!isNaN(v))opts[id]=v;continue}
    if(el.value!==undefined&&el.value!=='')opts[id]=el.value;
  }
  return opts;
}
/* When the preset changes, reset the knobs so the baseline matches
   what the CLI would do; the user can then override freely. */
function hydrateFromPreset(){
  const name=document.getElementById('preset').value;
  if(name!=='support-adaptive-bridge')return; /* only one defined preset */
  /* These values are the support-adaptive-bridge overrides plus the
     CLI defaults for every other knob. Kept in one place for clarity. */
  setVal('threshold','0.68');setVal('graph_mode','support');
  setVal('graph_expansion_mode','adaptive');setVal('graph_top_k','3');
  setVal('graph_contrastive_penalty','0.2');setVal('refine_mode','bridge-cut');
  setVal('threshold_mode','family');setVal('threshold_min','0');setVal('threshold_max','0');
  setVal('threshold_shrink','0.5');
  setVal('graph_adaptive_margin','0.1');setVal('graph_adaptive_either_max','0.15');
  setVal('graph_adaptive_ambiguity','0.5');setVal('graph_mutual_boost','0.2');
  setVal('graph_triangle_boost','0.3');setVal('graph_contrastive_floor','0.05');
  setVal('graph_risk_rank_weight','0.5');setVal('graph_risk_gap_weight','0.5');
  setVal('graph_unilateral_penalty','0');setVal('graph_rival_floor','0.6');
  setVal('refine_confidence_threshold','0.55');setVal('refine_bridge_ratio','0.7');
  setVal('scorer_mode','learned');setVal('iterations','3');setVal('lambda','0.5');
  setVal('dirichlet_concentration','0');setVal('dirichlet_sharpness','0');setVal('dirichlet_scale','');
  setVal('alignment_mode','combined');document.getElementById('class_reduce').checked=true;
  setVal('null_mode','marginal');setVal('null_max_runs','200');setVal('min_pairs','5');
  setVal('bootstrap','0');setVal('bootstrap_sigma','0.05');setVal('cluster_scope','concept');
}
function setVal(id,v){const el=document.getElementById(id);if(!el)return;if(el.type==='checkbox')el.checked=(v===true||v==='true');else el.value=v}

// The engine, the worker and this file are all served from the same origin and
// change together, so they are versioned together: BUILD_INFO's source hash is
// the query string on every one of them. Date.now() was there before, which
// defeats caching outright -- a 1.3 MB WebAssembly download on every reload,
// forever -- while leaving a stale worker holding a stale engine impossible to
// distinguish from a fresh one. BUILD_INFO itself is fetched no-cache; it is
// three lines.
async function buildId(){
  try{
    const r=await fetch('BUILD_INFO',{cache:'no-cache'});
    if(!r.ok)return 'dev';
    const text=await r.text();
    const m=text.match(/^sources_sha256\s+(\S+)/m);
    const c=text.match(/^git_commit\s+(\S+)/m);
    const badge=document.getElementById('buildBadge');
    if(badge&&c)badge.textContent='('+c[1]+')';
    return m?m[1].slice(0,12):'dev';
  }catch(e){return 'dev'}
}

async function initWorker(){
  const v=await buildId();
  worker=new Worker('worker.js?v='+v);
  worker.postMessage({type:'init',v});
  document.getElementById('loadInfo').style.display='block';
  document.getElementById('runBtn').disabled=true;
  worker.onmessage=function(e){
    const m=e.data;
    if(m.type==='ready'){
      wasmReady=true;
      document.getElementById('loadInfo').style.display='none';
      document.getElementById('runBtn').disabled=false;
      document.getElementById('sysBadge').textContent=m.system;
      document.getElementById('grCountBadge').textContent=m.graphemeCount;
      // Which merkmal produced the clusters. cognator's thresholds are values
      // on merkmal's segment-distance scale, so a run against a different one
      // is not comparable to the numbers in the repository, and the page is
      // the only place a visitor could find out which one it is.
      if(m.merkmal)document.getElementById('merkmalBadge').textContent=m.merkmal;
    }
    else if(m.type==='status'){document.getElementById('status').textContent=m.text;if(!wasmReady)document.getElementById('loadText').textContent=m.text}
    else if(m.type==='error'){
      document.getElementById('status').textContent='Error: '+m.message;
      document.getElementById('loadText').textContent='Error: '+m.message;
      document.getElementById('runBtn').disabled=!wasmReady;
    }
    else if(m.type==='done')onResult(m.result);
  };
  worker.onerror=function(e){document.getElementById('loadText').textContent='Worker error: '+(e.message||'unknown')};
  const t0=Date.now();setInterval(()=>{if(wasmReady)return;const s=Math.round((Date.now()-t0)/1000);const el=document.getElementById('loadText');if(s>3&&el.textContent.indexOf('Error')===-1)el.textContent='Loading cognator engine… ('+s+'s)'},2000);
}
function initCalibration(){
  // Revalidated rather than re-downloaded: no-cache sends the conditional
  // request and takes a 304 when the file has not moved.
  fetch('calibration.json',{cache:'no-cache'})
    .then(r=>r.ok?r.json():null)
    .then(j=>{calibration=j})
    .catch(()=>{calibration=null});
}

function runDetection(){
  const csv=getEd();if(!csv.trim())return;
  document.getElementById('runBtn').disabled=true;
  document.getElementById('runBtn').textContent='Running…';
  document.getElementById('status').textContent='Starting…';
  clearResults();
  worker.postMessage({type:'run',csv,options:readOptions()});
}

function onResult(result){
  document.getElementById('runBtn').disabled=false;
  document.getElementById('runBtn').textContent='Run detection';
  if(!result||!result.ok){
    document.getElementById('status').textContent='Detection failed: '+((result&&result.error)||'unknown');
    return;
  }
  lastResult=result;
  applyCalibration(result);
  renderSummary(result);
  renderTable(result);
  populateConceptDropdown(result);
  renderGraph();
  renderEval(result);
  document.getElementById('resultsPanel').style.display='block';
  document.getElementById('status').textContent='Done in '+(result.summary.elapsed_ms)+' ms ('+(result._wallTimeMs||'?')+' ms wall)';
}

/* Calibration application — mirrors scripts/calibrate_confidence.py apply */
function interpolate(mapping,x){
  if(!mapping||!mapping.length)return x;
  if(x<=mapping[0][0])return mapping[0][1];
  if(x>=mapping[mapping.length-1][0])return mapping[mapping.length-1][1];
  for(let i=0;i<mapping.length-1;i++){
    const [lx,ly]=mapping[i],[hx,hy]=mapping[i+1];
    if(lx<=x&&x<=hx){if(hx===lx)return ly;const f=(x-lx)/(hx-lx);return ly+f*(hy-ly)}
  }
  return mapping[mapping.length-1][1];
}
function stratumFor(row,quartiles){
  const size=row.cluster_size||1;
  if(size<=1)return 'singleton';
  const support=row.assigned_support||0;
  if(!quartiles||!quartiles.length)return 'multi';
  for(let i=0;i<quartiles.length;i++)if(support<=quartiles[i])return 'multi_q'+(i+1);
  return 'multi_q'+(quartiles.length+1);
}
function riskBand(cal,low,high){if(cal>=low)return 'low';if(cal<high)return 'high';return 'medium'}
function applyCalibration(result){
  if(!calibration||!calibration.strata){return}
  const low=calibration.bands.low,high=calibration.bands.high;
  const quartiles=calibration.support_quartiles||[];
  for(const p of result.predictions){
    const stratum=stratumFor(p,quartiles);
    const mapping=(calibration.strata[stratum]&&calibration.strata[stratum].mapping)||[];
    const cal=interpolate(mapping,p.confidence);
    p.confidence_calibrated=cal;
    p.risk_band=riskBand(cal,low,high);
    p.stratum=stratum;
  }
}

/* Summary card */
function renderSummary(r){
  const s=r.summary,ev=r.eval;
  const el=document.getElementById('summaryCard');
  const row=(label,v)=>`<div class="metric"><div class="metric-label">${label}</div><div class="metric-value">${v}</div></div>`;
  let html=''
    +row('Forms',s.forms)
    +row('Concepts',s.concepts)
    +row('Lects',s.lects)
    +row('Clusters',s.clusters)
    +row('Threshold',s.threshold.toFixed(3))
    +row('OOV types',s.oov_types)
    +row('Elapsed',s.elapsed_ms+' ms');
  if(ev)html+=row('F1',ev.f1.toFixed(3))+row('P / R',ev.precision.toFixed(2)+' / '+ev.recall.toFixed(2));
  el.innerHTML=html;
}

/* Table view */
const COLS=[
  ['eval_id','ID'],['language_id','Lect'],['concept','Concept'],['segments','Segments'],
  ['pred','Cluster'],['confidence','Confidence'],['confidence_calibrated','Calibrated'],
  ['risk_band','Risk'],['cluster_size','Cluster size'],['assigned_support','Assigned'],
  ['alternative_support','Alt. sup.'],['support_margin','Margin'],['gold','Gold']
];
function renderTable(r){
  const table=document.getElementById('predTable');
  const thead=table.querySelector('thead'),tbody=table.querySelector('tbody');
  thead.innerHTML='<tr>'+COLS.map(c=>`<th data-key="${c[0]}">${c[1]}</th>`).join('')+'</tr>';
  thead.querySelectorAll('th').forEach(th=>th.onclick=()=>sortTable(th.dataset.key));
  currentSort={key:null,dir:1};
  drawRows(r.predictions);
}
function drawRows(rows){
  const tbody=document.getElementById('predTable').querySelector('tbody');
  tbody.innerHTML=rows.map(p=>{
    const bandCls=p.risk_band?('band-'+(p.risk_band==='medium'?'med':p.risk_band)):'band-none';
    const dot=p.risk_band?`<span class="band-dot ${p.risk_band==='medium'?'med':p.risk_band}"></span>`:'<span class="band-dot na"></span>';
    const cal=p.confidence_calibrated!=null?p.confidence_calibrated.toFixed(3):'—';
    const band=p.risk_band||'—';
    return `<tr class="${bandCls}">
      <td>${esc(p.eval_id)}</td><td>${esc(p.language_id)}</td><td>${esc(p.concept)}</td>
      <td class="seg-mono">${esc(p.segments)}</td><td><code>${esc(p.pred)}</code></td>
      <td>${p.confidence.toFixed(3)}</td><td>${cal}</td><td>${dot}${band}</td>
      <td>${p.cluster_size}</td><td>${p.assigned_support.toFixed(2)}</td>
      <td>${p.alternative_support.toFixed(2)}</td><td>${p.support_margin.toFixed(2)}</td>
      <td>${esc(p.gold)}</td>
    </tr>`;
  }).join('');
}
function esc(s){return String(s==null?'':s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'})[c])}
function sortTable(key){
  if(!lastResult)return;
  const dir=(currentSort.key===key)?-currentSort.dir:1;
  currentSort={key,dir};
  const rows=[...lastResult.predictions].sort((a,b)=>{
    const va=a[key],vb=b[key];
    if(va==null&&vb==null)return 0;if(va==null)return 1;if(vb==null)return -1;
    if(typeof va==='number'&&typeof vb==='number')return dir*(va-vb);
    return dir*String(va).localeCompare(String(vb));
  });
  drawRows(rows);
  document.querySelectorAll('#predTable th').forEach(th=>{
    th.classList.remove('sorted-asc','sorted-desc');
    if(th.dataset.key===key)th.classList.add(dir>0?'sorted-asc':'sorted-desc');
  });
}

/* Graph view */
function populateConceptDropdown(r){
  const sel=document.getElementById('graphConcept');
  const concepts=[...new Set(r.predictions.map(p=>p.concept))].sort();
  sel.innerHTML=concepts.map(c=>`<option value="${esc(c)}">${esc(c)}</option>`).join('');
}
function renderGraph(){
  if(!lastResult)return;
  const concept=document.getElementById('graphConcept').value;
  const rows=lastResult.predictions.filter(p=>p.concept===concept);
  if(!rows.length)return;
  const svgEl=document.getElementById('graphSvg');
  const svg=d3.select(svgEl);svg.selectAll('*').remove();
  if(simulation){simulation.stop();simulation=null}

  const W=svgEl.clientWidth||800,H=520;
  const clusters=[...new Set(rows.map(r=>r.pred))];
  const color=d3.scaleOrdinal(d3.schemeTableau10).domain(clusters);
  const bandStroke=b=>({low:'#2ca02c',medium:'#ff7f0e',high:'#d62728'}[b]||'#aaa');

  const nodes=rows.map(r=>({
    id:r.eval_id,label:r.language_id,cluster:r.pred,
    confidence:r.confidence,risk:r.risk_band||'na',
    fx:null,fy:null
  }));
  /* Edges: within-cluster fully connected (visualising the cluster);
     plus a light inter-cluster edge weighted by support_margin is a
     future enhancement. Keeping it simple here. */
  const links=[];
  const byCluster={};
  for(const n of nodes){(byCluster[n.cluster]=byCluster[n.cluster]||[]).push(n)}
  for(const cl of Object.keys(byCluster)){
    const group=byCluster[cl];
    for(let i=0;i<group.length;i++)for(let j=i+1;j<group.length;j++)
      links.push({source:group[i].id,target:group[j].id,strength:0.6});
  }

  const link=svg.append('g').attr('class','links').selectAll('line').data(links).enter()
    .append('line').attr('class','link').attr('stroke-width',1.2);

  const node=svg.append('g').attr('class','nodes').selectAll('g').data(nodes).enter()
    .append('g').attr('class','node').call(d3.drag()
      .on('start',(ev,d)=>{if(!ev.active)simulation.alphaTarget(0.3).restart();d.fx=d.x;d.fy=d.y})
      .on('drag',(ev,d)=>{d.fx=ev.x;d.fy=ev.y})
      .on('end',(ev,d)=>{if(!ev.active)simulation.alphaTarget(0);d.fx=null;d.fy=null}));

  node.append('circle')
    .attr('r',d=>6+6*d.confidence)
    .attr('fill',d=>color(d.cluster))
    .attr('stroke',d=>bandStroke(d.risk))
    .attr('stroke-width',3);

  node.append('text').attr('dx',10).attr('dy',4).text(d=>d.label);
  node.append('title').text(d=>`${d.id}\ncluster: ${d.cluster}\nconfidence: ${d.confidence.toFixed(3)}\nrisk: ${d.risk}`);

  simulation=d3.forceSimulation(nodes)
    .force('link',d3.forceLink(links).id(d=>d.id).distance(70).strength(d=>d.strength))
    .force('charge',d3.forceManyBody().strength(-160))
    .force('center',d3.forceCenter(W/2,H/2))
    .force('collide',d3.forceCollide(d=>14+6*d.confidence))
    .on('tick',()=>{
      link.attr('x1',d=>d.source.x).attr('y1',d=>d.source.y)
          .attr('x2',d=>d.target.x).attr('y2',d=>d.target.y);
      node.attr('transform',d=>`translate(${d.x},${d.y})`);
    });
}

/* Eval panel */
function renderEval(r){
  const body=document.getElementById('evalBody');
  let html='';
  if(r.eval){
    html+=`<div class="eval-card"><div class="eval-grid">
      <div class="eval-metric"><span class="v">${r.eval.f1.toFixed(3)}</span><span class="l">B-cubed F1</span></div>
      <div class="eval-metric"><span class="v">${r.eval.precision.toFixed(3)}</span><span class="l">Precision</span></div>
      <div class="eval-metric"><span class="v">${r.eval.recall.toFixed(3)}</span><span class="l">Recall</span></div>
      <div class="eval-metric"><span class="v">${r.eval.scored_forms}</span><span class="l">Scored forms</span></div>
    </div></div>`;
  } else {
    html+=`<div class="eval-card" style="color:var(--muted)">No gold cognacy in the input; add a <code>Cognacy</code> column to see B-cubed scores.</div>`;
  }
  /* Risk-band distribution over this input */
  const bands={low:0,medium:0,high:0,none:0};
  for(const p of r.predictions){
    if(!p.risk_band){bands.none++;continue}
    bands[p.risk_band]=(bands[p.risk_band]||0)+1;
  }
  const total=r.predictions.length||1;
  const bandRow=(label,n,cls)=>{
    const pct=(100*n/total).toFixed(1);
    return `<div class="band-row">
      <div>${label}</div>
      <div class="bar"><div class="bar-fill ${cls}" style="width:${Math.max(0.1,100*n/total)}%">${n>0?n+' ('+pct+'%)':''}</div></div>
      <div style="font-size:.8rem;color:var(--muted);text-align:right">${n} of ${total}</div>
    </div>`;
  };
  html+=`<div class="band-summary"><h4 style="margin:.2rem 0 .5rem;color:var(--accent);font-size:1rem">Risk-band distribution</h4>`;
  html+=bandRow('Low','low' in bands?bands.low:0,'low');
  html+=bandRow('Medium',bands.medium||0,'med');
  html+=bandRow('High',bands.high||0,'high');
  if(bands.none>0)html+=`<div style="font-size:.8rem;color:var(--muted);margin-top:.3rem">${bands.none} row(s) uncalibrated (no calibration file loaded).</div>`;
  html+=`</div>`;
  body.innerHTML=html;
}

function showTab(name){
  document.querySelectorAll('.tab').forEach(t=>t.classList.toggle('active',t.dataset.tab===name));
  document.querySelectorAll('.tab-panel').forEach(p=>p.classList.toggle('active',p.id==='tab'+name.charAt(0).toUpperCase()+name.slice(1)));
  if(name==='graph')renderGraph();
}

/* Guide sidebar */
function renderGuide(){
  const box=document.getElementById('guideSteps');
  let html='';
  GUIDE_STEPS.forEach((s,i)=>{
    html+=`<div class="guide-step"><h4><span class="step-num">${i+1}</span>${esc(s.title)}</h4>
      <div class="step-body">${s.body}${s.example?`<button class="load-btn" onclick="loadGuideExample(${i})">Load in editor</button>`:''}</div></div>`;
  });
  box.innerHTML=html;
  box.querySelectorAll('.guide-step h4').forEach(h=>h.addEventListener('click',()=>{h.parentElement.classList.toggle('active');h.parentElement.querySelector('.step-body').style.display=h.parentElement.classList.contains('active')?'block':'none'}));
  /* Open the first step by default */
  const first=box.querySelector('.guide-step');if(first){first.classList.add('active');first.querySelector('.step-body').style.display='block'}
}
function loadGuideExample(i){
  const st=GUIDE_STEPS[i];if(!st||!st.example)return;
  setEd(st.example);
  const key='guide_'+i;const sel=document.getElementById('examples');sel.value=key;
  clearResults();
}
function toggleGuide(){document.getElementById('pageLayout').classList.toggle('guide-open')}

/* Boot */
window.addEventListener('DOMContentLoaded',()=>{
  initEditor();
  initCalibration();
  initWorker();
  renderGuide();
  hydrateFromPreset();
});
