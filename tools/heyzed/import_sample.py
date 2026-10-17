"""Run inside the original Epic sample editor; migrate only locomotion dependencies."""
import json
import os
from pathlib import Path
import unreal

ROOT='/Game/Characters/UEFN_Mannequin'
DATA=ROOT+'/Animations/MotionMatchingData/Databases'
groups={
    'Idle':['Dense/PSD_Dense_Stand_Idles'],
    'Turn':['Dense/PSD_Dense_Stand_TurnInPlace'],
    'CrouchIdle':['Dense/PSD_Dense_Crouch_Idles'],
    'CrouchTurn':['Dense/PSD_Dense_Crouch_TurnInPlace'],
}
for name,source in [('Walk','Stand_Walk'),('Run','Stand_Run'),('Sprint','Stand_Sprint'),('Crouch','Crouch_Walk')]:
    groups[name]=['Sparse/PSD_Sparse_'+source+'_'+kind for kind in ['Loops','Starts','Stops','Pivots']]
manifest={'source':'Epic Games Game Animation Sample','engine':'5.8',
          'source_url':'https://www.fab.com/listings/880e319a-a59e-4ed2-b268-b32dac7fa016',
          'mesh':ROOT+'/Meshes/SKM_UEFN_Mannequin','groups':{}}
packages={manifest['mesh'], '/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin'}
for name,databases in groups.items():
    sequences=set()
    for path in databases:
        database=unreal.load_asset(DATA+'/'+path)
        if not database: raise RuntimeError('Missing sample database '+path)
        for index in range(database.get_num_animation_assets()):
            asset=database.get_animation_asset(index)
            if not isinstance(asset,unreal.AnimSequence): raise RuntimeError('Unexpected animation '+str(asset))
            sequences.add(asset.get_path_name().split('.')[0])
    if not sequences: raise RuntimeError('Empty animation group '+name)
    manifest['groups'][name]=sorted(sequences)
    packages.update(sequences)
destination=Path(os.environ['HEYZED_SAMPLE_DESTINATION']).resolve()
if destination.name!='Content': raise RuntimeError('Expected the client Content directory')
unreal.AssetToolsHelpers.get_asset_tools().migrate_packages(sorted(packages),str(destination),unreal.MigrationOptions())
for package in packages:
    local=destination/(package.removeprefix('/Game/')+'.uasset')
    if not local.is_file(): raise RuntimeError('Migration did not produce '+str(local))
Path(os.environ['HEYZED_SAMPLE_MANIFEST']).write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
unreal.log('heyzed migrated '+str(len(packages))+' locomotion roots')
