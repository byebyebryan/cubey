#!/usr/bin/env python3
"""Migrate the saved organic guardian to explicit mirrored whole-stroke edits.

The source's / continue labels delimit its authored strokes. This one-time
migration checks that convention and bilateral name correspondence explicitly;
runtime grouped histories do not depend on labels or naming conventions.
"""
import copy
import hashlib
import json
from pathlib import Path

from author_sculptures import preset
from mirror_author import MirrorAuthor
from refinement_author import leaf_names
from sdf_author import union


def migrate(source):
    document=copy.deepcopy(source);leaves={}
    def collect(node):
        if 'children' not in node:
            leaves[node['name']]=node;return
        # This migration reflects already placed leaves. For nested placements
        # use reflect_x on their whole subtree instead, as the helper supports.
        assert node.get('position',[0,0,0])==[0,0,0]
        assert node.get('rotation_xyzw',[0,0,0,1])==[0,0,0,1]
        assert node.get('scale',1)==1
        for child in node['children']:collect(child)
    collect(document['root'])
    strokes=[]
    for index,item in enumerate(source['history']):
        if item['label'].endswith(' / continue'):
            assert strokes and item['label']==strokes[-1]['label']+' / continue'
            strokes[-1]['nodes'].append(item['node'])
        else:strokes.append(dict(label=item['label'],nodes=[item['node']],source_index=index))
    assert len(strokes)==source['authoring']['coherent_strokes']==113
    by_first={stroke['nodes'][0]:stroke for stroke in strokes}
    def counterpart(name):
        if name.startswith('left-'):return 'right-'+name[5:]
        if name=='alar-wing--1':return 'alar-wing-1'
        return None
    author=MirrorAuthor();consumed=set();replacements={};chapters=[]
    chapters_by_index={item['index']:item['label'] for item in source['authoring']['chapters']}
    mirrored=0
    for stroke in strokes:
        if stroke['source_index'] in chapters_by_index:
            chapters.append(dict(label=chapters_by_index[stroke['source_index']],index=len(author.history)))
        names=stroke['nodes']
        if names[0] in consumed:continue
        other=counterpart(names[0]);paired=other in by_first if other else False
        if paired:
            target=by_first[other]
            assert [counterpart(name) for name in names]==target['nodes']
            assert stroke['label']==target['label']
            consumed.update(target['nodes']);mirrored+=1
        # Whole-stroke grouping does not change the existing Boolean placement.
        # The returned reflected leaves replace their former opposite-side
        # counterparts; the original union/subtraction tree stays intact.
        nodes=[leaves[name] for name in names]
        template=nodes[0] if len(nodes)==1 else union(names[0]+'-stroke-template',*nodes)
        emitted=author.edit(template,stroke['label'],mirror=paired,
                            rename=lambda name:counterpart(name) or name+'-reflected-group')
        def emitted_leaves(node):
            if 'children' not in node:replacements[node['name']]=node
            else:
                for child in node['children']:emitted_leaves(child)
        emitted_leaves(emitted);consumed.update(names)
    assert author.names==set(leaves)==consumed
    assert len(author.history)==65 and mirrored==48
    def replace(node):
        if 'children' not in node:return copy.deepcopy(replacements[node['name']])
        node['children']=[replace(child) for child in node['children']];return node
    document['root']=replace(document['root'])
    document['name']='Guardian / mirrored strokes'
    document['history']=author.history
    document['authoring'].update(chapters=chapters,leaf_reveals=len(leaves),
        coherent_strokes=len(author.history),source_strokes=len(strokes),mirrored_operations=mirrored,
        unmirrored_operations=len(author.history)-mirrored,mirror_axis='x',
        history_unit='whole stroke, including reflected copy',
        source='projects/sdf/scenes/sculpture-guardian-organic.json',
        source_sha256=hashlib.sha256((json.dumps(source,indent=2)+'\n').encode()).hexdigest())
    assert len(leaf_names(document['root']))==1011
    return document


def main():
    root=Path(__file__).resolve().parents[3]
    source=root/'projects/sdf/scenes/sculpture-guardian-organic.json'
    document=migrate(json.loads(source.read_text()))
    suffix='sculpture-guardian-mirrored'
    scene=Path('projects/sdf/scenes')/(suffix+'.json')
    (root/scene).write_text(json.dumps(document,indent=2)+'\n')
    for build in (False,True):
        config=preset(scene,'guardian',build)
        if build:config.update({'construction-seconds':60,'duration':80})
        (root/f'projects/sdf/presets/{suffix}{"-build" if build else ""}.json').write_text(json.dumps(config,indent=2)+'\n')
    print(json.dumps(document['authoring'],indent=2))


if __name__=='__main__':main()
