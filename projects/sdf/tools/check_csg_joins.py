#!/usr/bin/env python3
"""Audit saved showcase joins independently of the polish author's constants."""
import argparse
import json
import math
from pathlib import Path

from check_robot_joints import add, sub, scale, length, rotate, compose, audit as audit_robot
from refine_csg_showcase import SCENES, family, sha, save


def inverse(q):return [-q[0],-q[1],-q[2],q[3]]


def distance(node,p):
    q=node.get('rotation_xyzw',[0,0,0,1]);q=scale(q,1/length(q))
    s=node.get('scale',1)
    p=scale(rotate(inverse(q),sub(p,node.get('position',[0,0,0]))),1/s)
    kind=node['type']
    if 'children'in node:
        ds=[distance(c,p)for c in node['children']]
        if kind=='union':return min(ds)*s
        assert kind=='subtract',kind
        return max(ds[0],-min(ds[1:]))*s
    if kind=='box':
        d=[abs(a)-b for a,b in zip(p,node['half_extent'])]
        return (length([max(x,0)for x in d])+min(max(d),0)-node.get('rounding',0))*s
    if kind=='cylinder':
        d=[math.hypot(p[0],p[2])-node['radius'],abs(p[1])-node['half_length']]
        return (math.hypot(*[max(x,0)for x in d])+min(max(d),0))*s
    if kind=='capsule':
        p[1]-=min(max(p[1],-node['half_length']),node['half_length'])
        return (length(p)-node['radius'])*s
    raise ValueError('Unsupported bounded audit primitive: '+kind)


def audit(document,subject):
    # Report in authored object units. The display root's uniform scale is
    # omitted; all nested assembly transforms are composed from the saved JSON.
    nodes=[];checks=[]
    def visit(node,p=(0,0,0),q=(0,0,0,1),s=1,root=False):
        center=add(p,rotate(q,scale(node.get('position',[0,0,0]),s)))
        local=node.get('rotation_xyzw',[0,0,0,1]);local=scale(local,1/length(local))
        world_q=compose(q,local);world_s=s*(1 if root else node.get('scale',1))
        nodes.append(dict(node=node,center=center,q=world_q,size=world_s,parent=(p,q,s)))
        for child in node.get('children',[]):visit(child,center,world_q,world_s)
    visit(document['root'],root=True)
    def group(prefix):return [n for n in nodes if family(n['node'])==prefix]
    def named(name):return next(n for n in nodes if n['node']['name']==name)
    def one(prefix):
        matches=group(prefix);assert len(matches)==1,prefix
        return matches[0]
    def check(label,passed,**values):checks.append(dict(name=label,passed=bool(passed),**values))
    def sample(n,p):
        pos,q,s=n['parent'];return distance(n['node'],scale(rotate(inverse(q),sub(p,pos)),1/s))*s
    def ends(n):
        d=rotate(n['q'],[0,n['node']['half_length']*n['size'],0])
        return sub(n['center'],d),add(n['center'],d)
    def top(n):
        assert length(sub(rotate(n['q'],[0,1,0]),[0,1,0]))<1e-8
        raw=n['node'];h=raw.get('half_length',raw.get('half_extent',[0,0,0])[1])
        return n['center'][1]+(h+raw.get('rounding',0))*n['size']
    def bottom(n):return 2*n['center'][1]-top(n)
    if subject=='robot':
        result=audit_robot(document)
        check('independent articulated joint audit',result['passed'],checks=len(result['checks']))
    elif subject.startswith('observatory'):
        landing,balcony=one('top-landing'),one('balcony-platform')
        check('landing is flush with balcony',abs(top(landing)-top(balcony))<1e-8,
              height_difference=top(landing)-top(balcony))
        y=top(balcony)
        for z in (.0,.018,.038):
            for i in range(13):
                p=[.04+i*.012,y,z]
                check('continuous landing-to-balcony surface',min(sample(landing,p),sample(balcony,p))<=1e-7,point=p)
        rails=group('top-rail')+group('baluster')+group('polish-balcony-entry-post')
        for i in range(12):
            p=[.035+i*.01,y+.03,.018]
            clearance=min(sample(n,p)for n in rails)
            check('entry path clears balcony rail',clearance>.012,clearance=clearance)
        check('landing meets last stair with a modest rise',.002<top(landing)-max(top(n)for n in group('stair-tread'))<.014)
    elif subject=='canal-mill':
        planks=sorted(group('bridge-plank'),key=lambda n:n['center'][2]);caps=group('bank-cap')
        for plank in (planks[0],planks[-1]):
            row=[n for n in caps if abs(n['center'][2]-plank['center'][2])<.025]
            check('bridge entry matches coping height',all(abs(top(n)-top(plank))<1e-8 for n in row))
            for cap in row:
                c=cap['node'];clearance=abs(cap['center'][0]-plank['center'][0])-c['half_extent'][0]-c.get('rounding',0)-plank['node']['half_extent'][0]-plank['node']['rounding']
                check('bridge deck clears coping ends',clearance>.005,clearance=clearance)
        stringers=group('polish-bridge-stringer-0')+group('polish-bridge-stringer-1')
        assert len(stringers)==6
        for plank in planks:
            for x in (.173,.291):
                p=[x,bottom(plank),plank['center'][2]]
                gap=min(sample(n,p)for n in stringers)
                check('bridge plank meets a stringer',gap<.0005,signed_gap=gap)
        yard=one('yard');level=top(yard)
        far=one('far-bank');bank_back=far['center'][2]-far['node']['half_extent'][2]-far['node']['rounding']
        for sack in group('grain-sack'):
            raw=sack['node'];gap=sack['center'][1]-raw['half_length']-level
            clearance=bank_back-sack['center'][2]-max(raw['bottom_radius'],raw['top_radius'])
            check('sack rests in dry yard and clears bank',abs(gap)<.001 and clearance>.010,signed_gap=gap,bank_clearance=clearance)
        for post in group('awning-post'):
            foot=min(ends(post),key=lambda p:p[1]);foot[1]-=post['node']['radius']
            gap=min(abs(sample(cap,foot))for cap in caps)
            check('awning post seats on coping',gap<.002,surface_distance=gap)
        water=one('canal');race=one('polish-millrace-water')
        check('wheel race water matches canal',abs(top(water)-top(race))<1e-8)
        for prefix in ('island','yard','far-bank'):
            cut=named('polish-open-race-'+prefix)
            check('race cuts through '+prefix,sample(cut,[.083,top(water),.047])>.02)
        lowest=[]
        for n in group('paddle'):
            axes=[rotate(n['q'],v)for v in ([1,0,0],[0,1,0],[0,0,1])]
            extent=sum(abs(v[1])*h for v,h in zip(axes,n['node']['half_extent']))+n['node']['rounding']
            lowest.append(n['center'][1]-extent)
        immersion=top(water)-min(lowest)
        check('wheel paddles enter water without deep burial',.003<immersion<.02,immersion=immersion)
        path=group('path-slab')
        for wheel in group('cart-wheel'):
            gap=wheel['center'][1]-wheel['node']['major_radius']-wheel['node']['minor_radius']-top(path[0])
            check('cart wheel rests on paving',abs(gap)<1e-8,signed_gap=gap)
        for barrel in group('barrel'):
            edge=barrel['center'][0]+barrel['node']['radius']+.003
            margin=min(n['center'][0]-n['node']['half_extent'][0]-n['node'].get('rounding',0)-edge for n in path)
            check('barrel clears delivery path',margin>.008,clearance=margin)
    elif subject=='courtyard':
        floor=one('paving-slab');level=top(floor)
        check('no falling water primitives',not group('water-thread'))
        for n in group('bench-leg'):
            check('bench foot seats on plaza',abs(bottom(n)-level)<1e-8)
        for n in group('pot'):
            gap=n['center'][1]-n['node']['half_length']-level
            check('plant pot seats on plaza',abs(gap)<1e-8,signed_gap=gap)
        check('landing is flush with upper floor',abs(top(one('stair-landing'))-top(one('upper-floor')))<1e-8)
        levels=[level]+[top(n)for n in sorted(group('terrace-step'),key=lambda n:n['center'][2],reverse=True)]+[top(one('stair-landing'))]
        rises=[b-a for a,b in zip(levels,levels[1:])]
        check('stair rises are uniform',max(rises)-min(rises)<1e-8 and min(rises)>.01,rise=min(rises))
        rails=group('stair-rail')
        for n in group('stair-baluster'):
            head=max(ends(n),key=lambda p:p[1])
            check('baluster reaches handrail',min(sample(r,head)for r in rails)<0)
        # Check actual boolean masses, including both overlapping walls.
        wing=next(n for n in nodes if n['node']['name'].startswith('arcaded-wing-'))
        tower=next(n for n in nodes if n['node']['name'].startswith('tower-openings-'))
        for z in (-.09,-.105,-.12,-.135,-.15):
            p=[.224,.05,z]
            check('tower passage is open through both walls',sample(wing,p)>.01 and sample(tower,p)>.01,point=p)
        ball=one('fountain-finial');gap=ball['center'][1]-ball['node']['radius']-top(one('upper-bowl'))
        check('finial seats on bowl',-.002<gap<=0,signed_gap=gap)
        check('fountain base seats on plaza',-.002<bottom(one('fountain-foot'))-level<=0)
    else:raise ValueError(subject)
    return dict(subject=subject,passed=all(c['passed']for c in checks),checks=checks,
                scope='Bounded serialized geometry contacts, access and support checks in object units; not a general collision or walkability solver.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene-dir',type=Path,default=SCENES)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();rows=[]
    for s in ('robot','canal-mill','observatory','courtyard','observatory-section'):
        path=args.scene_dir/(s+'.json');row=audit(json.loads(path.read_text()),s)
        row.update(scene=str(path),scene_sha256=sha(path));rows.append(row)
    save(args.output,dict(passed=all(r['passed']for r in rows),subjects=rows))
    failures=[(r['subject'],c)for r in rows for c in r['checks']if not c['passed']]
    print('Join audit:',sum(len(r['checks'])for r in rows)-len(failures),'/',sum(len(r['checks'])for r in rows))
    for failure in failures:print('FAIL',failure)
    raise SystemExit(bool(failures))


if __name__=='__main__':main()
