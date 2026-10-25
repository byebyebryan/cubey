const equal = (left,right) => {
  const canonical=value=>Array.isArray(value)?value.map(canonical):value&&typeof value==='object'
    ?Object.fromEntries(Object.keys(value).sort().map(key=>[key,canonical(value[key])])):value;
  return JSON.stringify(canonical(left))===JSON.stringify(canonical(right));
};

export function hoverPreviewErrors(media,assets,receipt){
  const errors=[];
  for(const [entry,selection] of Object.entries(media.selections||{})){
    if(!selection.film)continue;
    const parent=assets.get(selection.film);
    if(!parent){errors.push(`Film selection points to a missing parent: ${entry}/${selection.film}`);continue;}
    if(parent.kind!=='video'){errors.push(`Film selection parent is not a video: ${entry}/${selection.film}`);continue;}
    if(!selection.preview){errors.push(`Film selection has no hover preview: ${entry}`);continue;}
    const preview=assets.get(selection.preview);
    if(!preview){errors.push(`Film selection points to a missing hover preview: ${entry}/${selection.preview}`);continue;}
    if(preview.kind!=='video'||preview.width!==640||preview.height!==360||preview.bytes>400_000)
      errors.push(`Invalid hover preview delivery: ${entry}`);
    const provenance=preview.provenance||{};
    if(provenance.parentAsset!==parent.id||provenance.sourceSha256!==parent.sha256||
       provenance.sourceRevision!==parent.provenance?.sourceRevision)
      errors.push(`Hover preview parent changed: ${entry}`);
    if(!equal(preview.credit,parent.credit))errors.push(`Hover preview attribution differs from its parent: ${entry}`);
    if(provenance.recipe!=='site/media/receipts/hover-previews.json')
      errors.push(`Hover preview receipt provenance is missing: ${entry}`);
    const record=receipt?.assets?.[preview.id];
    const settings=record?.settings;
    const invalidSettings=!settings||settings.codec!=='h264'||settings.pixelFormat!=='yuv420p'||
      settings.width!==640||settings.height!==360||settings.frameRate!=='18/1'||
      settings.audioStreams!==0||settings.fastStart!==true||settings.preset!=='slow'||
      !Number.isFinite(settings.startSeconds)||settings.startSeconds<0||
      !Number.isFinite(settings.durationSeconds)||settings.durationSeconds<=0||settings.durationSeconds>3.56||
      !Number.isInteger(settings.crf)||settings.crf<18||settings.crf>36;
    if(!record||record.parentAsset!==parent.id||record.parentSha256!==parent.sha256||
       record.parentSourceRevision!==parent.provenance?.sourceRevision||record.sha256!==preview.sha256||
       record.bytes!==preview.bytes||record.codec!=='h264'||record.pixelFormat!=='yuv420p'||
       record.width!==640||record.height!==360||record.frameRate!=='18/1'||record.audioStreams!==0||
       record.fastStart!==true||invalidSettings){
      errors.push(`Hover preview receipt is missing or stale: ${entry}/${preview.id}`);
    }
  }
  return errors;
}
