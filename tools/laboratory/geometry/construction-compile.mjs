import {readFileSync} from 'node:fs';
import {buildConstruction} from './construction-mesh.mjs';
const input=JSON.parse(readFileSync(0,'utf8'));
process.stdout.write(JSON.stringify(input.requests.map(request=>{
 try{return buildConstruction(request,input.descriptor);}
 catch(error){throw Error(`${request.kind} at ${request.anchor.join(',')} (${request.sources.join(', ')}): ${error.message}`,{cause:error});}
})));
