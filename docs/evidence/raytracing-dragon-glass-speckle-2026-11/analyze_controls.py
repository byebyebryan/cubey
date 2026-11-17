# /// script
# dependencies = ["numpy==2.4.6", "pillow==12.0.0"]
# ///
"""Describe seed variation and sample tails, retaining original HDR/PNG values."""
import json
from pathlib import Path
import numpy as np
from PIL import Image

evidence = Path(__file__).resolve().parent
manifest = json.loads((evidence/"captures.json").read_text())
records = {(c["lighting"],c["samples"],c["seed"]):c for c in manifest["captures"]}
assert len(records)==12, "finish the matched controls first"
rois = {"head":[610,205,475,335], "body":[710,375,625,465], "scene":[0,0,1920,1080]}
weights = np.array([.2126,.7152,.0722])
data = {}
for key, record in records.items():
    prefix = record["prefix"]
    raw = np.fromfile(prefix+".bin",dtype="<f4").reshape(1080,1920,4)
    rgb8 = np.array(Image.open(prefix+".png"))[:,:,:3]
    assert np.isfinite(raw).all() and (raw[:,:,:3]>=0).all()
    data[key] = (raw[:,:,:3],rgb8)

broad = json.loads((evidence/"broad-sun.json").read_text())
for record in broad["captures"]:
    prefix=record["prefix"]
    raw=np.fromfile(prefix+".bin",dtype="<f4").reshape(1080,1920,4)
    assert np.isfinite(raw).all() and (raw[:,:,:3]>=0).all()
    data["broad",record["samples"],record["seed"]]=(raw[:,:,:3],np.array(Image.open(prefix+".png"))[:,:,:3])

def crop(a, rect):
    x,y,w,h=rect
    return a[y:y+h,x:x+w]

def pair_stats(lighting, samples, rect):
    a,pa = data[lighting,samples,19]
    b,pb = data[lighting,samples,43]
    a,b,pa,pb = [crop(v,rect) for v in (a,b,pa,pb)]
    linear_delta=a.astype(np.float64)-b
    display_delta=pa.astype(np.float64)-pb
    wa,wb=pa.min(axis=2)>=240,pb.min(axis=2)>=240
    return dict(seed_rgb_hdr_rmse=float(np.sqrt(np.mean(linear_delta**2))),
                seed_rgb8_rmse=float(np.sqrt(np.mean(display_delta**2))),
                seed_rgb8_mean_absolute=float(np.abs(display_delta).mean()),
                seed_rgb8_p99_absolute=float(np.quantile(np.abs(display_delta),.99)),
                white_threshold="all three RGB8 channels >=240",
                white_pixels_seed19=int(wa.sum()),white_pixels_seed43=int(wb.sum()),
                white_pixels_both=int((wa&wb).sum()),
                seed19_white_fraction_shared_with_seed43=float((wa&wb).sum()/max(1,wa.sum())))

metrics = {roi:{f"{l}-{n}":pair_stats(l,n,rect)
    for l,n in [("combined",128),("combined",512),("combined",4096),("combined",8192),("sky",4096),("sun",4096),("broad",128),("broad",512)]}
    for roi,rect in rois.items()}
points=[]
for label,x,y in [("unstable",818,372),("persistent",738,527)]:
    values={}
    for (lighting,samples,seed),(raw,png) in data.items():
        rgb=raw[y,x]
        values[f"{lighting}-{samples}-s{seed}"]=dict(hdr=rgb.tolist(),luminance=float(rgb@weights),rgb8=png[y,x].tolist())
    points.append(dict(label=label,x=x,y=y,values=values))

probes=[]
root=Path("outputs/raytracing/dragon-glass-speckle-20261116")
for name,x,y,seed in [("unstable-s19",818,372,19),("unstable-s43",818,372,43),("persistent-s19",738,527,19)]:
    path=root/"sample-probes"/(name+".journal.json")
    j=json.loads(path.read_text())
    assert j["overflow"]==0 and j["attempted_records"]==len(j["records"])
    packets=np.array(j["records"],dtype=np.uint32)
    kinds=packets[:,0,0]
    result=packets[kinds==2]
    assert len(result)==4096 and np.array_equal(result[:,7,0],np.arange(4096))
    rgb=result[:,1,:3].copy().view(np.float32).astype(np.float64)
    flags=result[:,1,3].copy().view(np.float32).astype(np.uint32)
    assert np.isfinite(rgb).all() and (rgb>=0).all() and not (flags&np.uint32(0xfffffff7)).any()
    ordinary=data["combined",4096,seed][0][y,x]
    mean=rgb.mean(axis=0)
    parity=float(np.max(np.abs(mean-ordinary)/np.maximum(1e-7,ordinary)))
    assert parity<2e-4,(name,mean,ordinary,parity)
    solar=packets[kinds==70]
    solar_rgb=solar[:,2,:3].copy().view(np.float32).astype(np.float64)
    by_sample=np.zeros((4096,3),dtype=np.float64)
    np.add.at(by_sample,solar[:,7,0],solar_rgb)
    lum=rgb@weights
    solar_lum=by_sample@weights
    hits=solar_lum>0
    order=np.argsort(lum)[::-1]
    cumulative=np.cumsum(lum)/np.arange(1,4097)
    peak=order[0]
    terminal_positive=np.any(solar_rgb>0,axis=1)
    stride=np.unique(np.r_[0,np.arange(15,4096,16),np.flatnonzero(lum>100),peak])
    probes.append(dict(name=name,pixel=[x,y],seed=seed,samples=4096,
        mean_hdr=mean.tolist(),ordinary_hdr=ordinary.tolist(),relative_parity_error=parity,
        mean_luminance=float(lum.mean()),median_sample_luminance=float(np.median(lum)),
        max_sample_luminance=float(lum.max()),peak_sample_index=int(peak),
        top_one_sample_fraction=float(lum[peak]/lum.sum()),
        top_ten_samples_fraction=float(lum[order[:10]].sum()/lum.sum()),
        samples_above_100_luminance=int((lum>100).sum()),
        brightest_samples=[dict(sample_index=int(i),luminance=float(lum[i]),solar_luminance=float(solar_lum[i]),solar_terminal_depths=solar[(solar[:,7,0]==i)&terminal_positive,0,2].tolist()) for i in order[:10]],
        direct_solar_samples=int(hits.sum()),direct_solar_terminal_paths=int(terminal_positive.sum()),
        direct_solar_fraction=float(solar_lum.sum()/lum.sum()),
        solar_terminal_depths={str(int(d)):int(np.count_nonzero(solar[terminal_positive,0,2]==d)) for d in np.unique(solar[terminal_positive,0,2])},
        sample_luminance_quantiles={str(q):float(np.quantile(lum,q)) for q in (.5,.9,.99,.999,1)},
        cumulative_luminance=[[int(i+1),float(cumulative[i])] for i in stride],
        solar_hit_indices=np.flatnonzero(hits).tolist(),
        status_or=int(np.bitwise_or.reduce(flags)),
        validation_clean=True))

result=dict(scope="two-seed still-image diagnosis; regions include background and are not segmentation masks",identity=manifest["identity"],rois=rois,metrics=metrics,points=points,probes=probes,broad_sun=broad,
    limitations=["Two seeds measure these realizations, not population variance or optical accuracy", "White-pixel persistence alone does not certify a highlight", "Selected unstable/persistent pixels are illustrative extremes, not representative averages", "Private sample-series means must match ordinary HDR before interpretation", "No clamp, denoiser, lighting or sampling default was changed"])
(evidence/"analysis.json").write_text(json.dumps(result,indent=2)+"\n")
print(json.dumps(dict(head=metrics["head"],probes=[{k:v for k,v in p.items() if k not in ("cumulative_luminance","solar_hit_indices")} for p in probes]),indent=2))
