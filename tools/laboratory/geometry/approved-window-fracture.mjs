import sectionLayouts from './window-section-layouts.json' with {type:'json'};
import {buildStandardWindow} from './window-mesh.mjs';
import {fractureWindow} from './glass-fracture-experiment.mjs';

export function buildApprovedWindow(aperture,descriptor,state,preset){
 if(!preset?.approved||!['broken','glassRemoved'].includes(state))return buildStandardWindow(aperture,descriptor,state);
 const closed=buildStandardWindow(aperture,descriptor,'closed');
 if(!closed)return null;
 const preserveParts=fixedWindowParts(closed,aperture);
 return fractureWindow(closed,aperture,descriptor,{...preset.parameters,seed:preset.reference.seed,impact:preset.reference.impact,state,preserveParts});
}

export function fixedWindowParts(recipe,aperture){const prefixes=sectionLayouts.profiles[aperture.id]?.fixedPartPrefixes||[];return recipe.parts.filter(p=>p.breakable===false||prefixes.some(prefix=>p.name.startsWith(prefix))).map(p=>p.name);}
