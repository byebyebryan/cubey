import design from '../../../docs/notes/cubey-site-content-map.json';
import ledger from '../../media/manifest.json';

export interface Asset {
  id: string;
  file: string;
  kind: 'image' | 'video';
  width: number;
  height: number;
  thumbnail?: string;
  sha256: string;
  bytes: number;
  caption: string;
  credit: { label: string; url: string; author?: string; source?: string };
  provenance: Record<string, unknown>;
}
export interface Selection { poster: string; film?: string; preview?: string; gallery?: string[] }
export const media = ledger as unknown as { assets: Asset[]; selections: Record<string, Selection> };
export const assets = Object.fromEntries(media.assets.map(a => [a.id, a]));
export const subjects = design.families;
export const bands = design.browse_bands;
export const connections = design.connections;
const publicScope: Record<string,string> = {
  'water-3d': 'A tank-scale liquid experiment with a reconstructed surface and whitewater. The long tank is the rendering domain.',
  'fire-3d': 'Fire and smoke evolve on a 3D grid. This visual study has not been validated as a physical combustion model.',
  'explosion-3d': 'Uses the same fire and smoke solver as Fire 3D, with a short impulse that creates the expanding plume.',
  'water-2d': 'A two-dimensional liquid sandbox. Viscosity, surface tension and adaptive storage are outside this version.',
  'smoke-2d': 'Colour is carried through a two-dimensional velocity field. The fluid fills the domain; it does not form a moving liquid surface.',
  'shallow-water': 'The water here is simulated by Cubey’s shallow-water solver. Terrain Water plays a separate dataset prepared with SynxFlow.',
  'terrain-rendering': 'Prepared heightfields and surface detail form the terrain backdrop. This scene uses a fixed focus; it does not demonstrate continuous traversal or live terrain generation.',
  'terrain-water': 'Water motion is replayed from a saved SynxFlow dataset. The sun and lighting follow a separate timeline, so the film is not a real-time solver demonstration.',
  'ocean-surface': 'This capture uses the Windy sea state. The renderer focuses on an open-water surface; shorelines, wakes and underwater rendering are outside this demo.',
  'atmosphere-light': 'Direct single scattering from air and aerosols, with ozone absorption and a sun disk. These views do not demonstrate global illumination or multiple-scattering transport.',
  'clouds': 'The camera views the cloud layer from the ground. Flying through clouds and orbital views are outside this scene.',
  'day-night': 'A complete accelerated solar day, from afternoon through sunset, night and daylight again. This is native recorded playback of the shared solar clock.',
  'night-sky': 'Procedural stars and a generated Milky Way are composed in the native sky renderer; the solar cycle controls their transition into daylight.',
  'guardian': 'The settled sculpture, shown in restrained marble and limestone. These views use the same authored form.',
  'robot': 'Playback of a saved authored construction, including additive and subtractive operations. This is a recorded native result.',
  'sdf-materials': 'Each solid uses its own continuous procedural material field. Glass samples the environment through a bounded interface path; it does not refract the neighboring solids.',
  'gltf-layers': 'This lamp is a representative example of anisotropy and clearcoat. The supported extension list below describes the renderer’s broader scope.',
  'gltf-motion': 'The Fox example shows core animation and skinning. A separate morph-target example is identified where shown. This is a single-asset renderer, with no animation editor.',
  'gltf-textures': 'This example shows authored UV placement. Cubey also supports PNG/JPEG and BasisU KTX2 textures. Draco/meshopt compression, arbitrary UV sets and material variants are outside the current renderer.',
};
const summaries: Record<string,string> = {
  'shallow-water':'Water flows through a valley from source to outlet, using Cubey’s shallow-water solver.',
  'terrain-rendering':'Ridges, valleys and fine surface detail take shape through light and self-shadowing.',
  'robot':'A robot takes shape as parts are added, joined and cut.',
  'guardian':'A horned sculpture, built from mirrored forms and shown in marble and limestone.',
  'gltf-layers':'Directional highlights and a clear coat reveal the authored surface of an imported barn lamp.',
  'gltf-optics':'Thickness and dispersion shape light through a grid of imported transparent prisms.',
  'gltf-motion':'A skinned Fox walks through its authored clip; a separate cube shows morph-target deformation.',
  'gltf-textures':'Authored UV transforms retain their placement across different material channels.',
};
const cueOverrides: Record<string,string[]> = {
  'shallow-water':['In-engine solver','Finite volume','Source and outlet'],
  'gltf-layers':['Anisotropy','Clearcoat','Imported materials'],
  'gltf-optics':['Transmission','Volume thickness','Dispersion'],
  'gltf-textures':['UV0 and UV1','Texture transforms','Material channels'],
};
export const entries = design.entries.map(e => ({...e, summary:summaries[e.id]||e.summary, cues:cueOverrides[e.id]||e.cues, scope: publicScope[e.id] || e.boundary}));
export type Entry = typeof entries[number];
export const byId = Object.fromEntries(entries.map(e => [e.id,e]));
export const familyEntries = (id: string) => entries.filter(e => e.family === id);
export const sourceURL = (e: Entry) => `https://github.com/byebyebryan/cubey/blob/main/${e.source}`;
export function rendererLabel(e: Entry): string | undefined {
  if(e.family==='sdf'||e.id==='sdf-materials')return 'SDF ray marching';
  if(e.family==='gltf'||e.id==='mesh-materials')return 'Forward PBR';
}
export function entryPath(e: Entry): string {
  if (e.family === 'simulation' || e.family === 'sdf') return `/demos/${e.family}/${e.id}/`;
  return `/demos/${e.family}/${['terrain','atmosphere','materials','gltf'].includes(e.family) ? '#'+e.id : ''}`;
}
export function selected(id: string): Selection {
  const value=media.selections[id];
  if (!value) throw new Error(`No selected native media for ${id}`);
  return value;
}
export function asset(id: string): Asset {
  if (!assets[id]) throw new Error(`Unknown media asset ${id}`);
  return assets[id];
}
