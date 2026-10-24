import type { APIRoute } from 'astro';
import ledger from '../../../media/manifest.json';

export const GET: APIRoute = () => new Response(JSON.stringify({
  schema: ledger.schema,
  version: ledger.version,
  selections: ledger.selections,
  assets: ledger.assets.map(a => ({
    id:a.id, file:a.file, kind:a.kind, width:a.width, height:a.height,
    sha256:a.sha256, bytes:a.bytes, caption:a.caption, credit:a.credit,
    parentSha256:a.provenance.sourceSha256,
    nativeSourceRevision:a.provenance.sourceRevision,
  })),
},null,2)+'\n', {headers:{'Content-Type':'application/json; charset=utf-8'}});
