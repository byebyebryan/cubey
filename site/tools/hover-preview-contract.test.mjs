import assert from 'node:assert/strict';
import test from 'node:test';
import {hoverPreviewErrors} from './hover-preview-contract.mjs';

function fixture(){
  const parent={id:'demo-film',kind:'video',sha256:'parent-sha',credit:{label:'Model author',url:'https://example.test',author:'Example'},provenance:{sourceRevision:'abc123'}};
  const preview={id:'preview-demo',kind:'video',width:640,height:360,bytes:12345,sha256:'preview-sha',
    credit:{...parent.credit},provenance:{parentAsset:parent.id,sourceSha256:parent.sha256,sourceRevision:'abc123',recipe:'site/media/receipts/hover-previews.json'}};
  const record={parentAsset:parent.id,parentSha256:parent.sha256,parentSourceRevision:'abc123',sha256:preview.sha256,bytes:preview.bytes,
    codec:'h264',pixelFormat:'yuv420p',width:640,height:360,frameRate:'18/1',audioStreams:0,fastStart:true,
    settings:{codec:'h264',pixelFormat:'yuv420p',width:640,height:360,frameRate:'18/1',audioStreams:0,fastStart:true,
      preset:'slow',startSeconds:0,durationSeconds:3.5,crf:26}};
  const media={selections:{demo:{film:parent.id,preview:preview.id}}};
  return {media,assets:new Map([[parent.id,parent],[preview.id,preview]]),receipt:{assets:{[preview.id]:record}}};
}

test('a complete current preview contract passes',()=>{
  const value=fixture();
  assert.deepEqual(hoverPreviewErrors(value.media,value.assets,value.receipt),[]);
});

test('a film selection with an image parent fails actionably',()=>{
  const value=fixture();
  value.assets.get('demo-film').kind='image';
  assert.deepEqual(hoverPreviewErrors(value.media,value.assets,value.receipt),[
    'Film selection parent is not a video: demo/demo-film',
  ]);
});

test('a film-backed selection without a preview fails with an actionable error',()=>{
  const value=fixture();
  delete value.media.selections.demo.preview;
  assert.deepEqual(hoverPreviewErrors(value.media,value.assets,value.receipt),[
    'Film selection has no hover preview: demo',
  ]);
});

test('oversized, wrong-sized and incorrectly attributed derivatives fail validation',()=>{
  const cases=[
    {change:preview=>preview.bytes=400_001,message:'Invalid hover preview delivery: demo'},
    {change:preview=>preview.width=320,message:'Invalid hover preview delivery: demo'},
    {change:preview=>preview.credit.author='Another author',message:'Hover preview attribution differs from its parent: demo'},
  ];
  for(const item of cases){
    const value=fixture();
    item.change(value.assets.get('preview-demo'));
    assert.ok(hoverPreviewErrors(value.media,value.assets,value.receipt).includes(item.message));
  }
});

test('a derivative whose parent digest is stale fails the shared contract',()=>{
  const value=fixture();
  value.assets.get('preview-demo').provenance.sourceSha256='old-parent-sha';
  const errors=hoverPreviewErrors(value.media,value.assets,value.receipt);
  assert.ok(errors.includes('Hover preview parent changed: demo'));
});

test('a receipt for a different parent is rejected even when the derivative matches',()=>{
  const value=fixture();
  value.receipt.assets['preview-demo'].parentSha256='old-parent-sha';
  const errors=hoverPreviewErrors(value.media,value.assets,value.receipt);
  assert.ok(errors.includes('Hover preview receipt is missing or stale: demo/preview-demo'));
});
