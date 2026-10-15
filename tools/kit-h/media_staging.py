#!/usr/bin/env python3
"""Validate and stage static media inputs for normal kits and legacy overlays."""
import hashlib,json,re,shutil,struct,shlex
from pathlib import Path
from media_registry import registration_delta,merge_registry

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def write_json(path,value):path.write_text(json.dumps(value,indent=1,sort_keys=True)+'\n')
def copy(source,target,mode=0o644):
    target.parent.mkdir(parents=True,exist_ok=True)
    target.unlink(missing_ok=True);shutil.copyfile(source,target);target.chmod(mode)
def pe_machine(path):
    data=path.read_bytes()
    if data[:2]!=b'MZ' or len(data)<64:raise ValueError('not PE: '+str(path))
    offset=struct.unpack_from('<I',data,60)[0]
    if data[offset:offset+4]!=b'PE\0\0':raise ValueError('invalid PE header: '+str(path))
    return struct.unpack_from('<H',data,offset+4)[0]

def registry_values(delta):
    values=[]
    for key,body in delta.items():
        # Wine wraps long REG_BINARY assignments with backslash-newline continuations.
        body=re.sub(r'\\\n[ \t]*','',body)
        for line in body.splitlines():
            match=re.fullmatch(r'(@|"[^"\n]+")=(.+)',line)
            if not match:raise ValueError('unsupported media registry assignment: '+line)
            values.append({'key':key.replace('\\\\','\\'), 'name': '' if match[1]=='@' else match[1][1:-1], 'data':match[2]})
    return values

def validate_media(cross,registration,media_work):
    cross,registration,media_work=map(lambda p:Path(p).resolve(),(cross,registration,media_work))
    report=json.loads((cross/'report.json').read_text());entry=report['prx']['modules']['winegstreamer']
    audits=('unresolved','errors','title_unbound','webkit_unbound','raw_syscalls')
    if entry.get('built') is not True or any(not isinstance(entry.get(k),list) or entry[k] for k in audits):
        raise ValueError('media PRX failed the title import audit')
    prx=cross/'prx/sce_module/winegstreamer.prx'
    if sha(prx)!=entry['sha256']:raise ValueError('media PRX differs from its audit')
    dll_hashes=json.loads((registration/'dll-sha256.json').read_text())
    modules=[]
    for arch,machine,folder in [('i386-windows',0x14c,'syswow64'),('x86_64-windows',0x8664,'system32')]:
        p=cross/'pe'/arch/'winegstreamer.dll';h=sha(p)
        if pe_machine(p)!=machine:raise ValueError('wrong PE architecture: '+arch)
        if dll_hashes.get(arch)!=h or report['pe'].get(arch+'/winegstreamer.dll')!=h:
            raise ValueError('registered DLL/build report mismatch: '+arch)
        modules.append((arch,folder,p,h))
    idl=cross/'source/dlls/winegstreamer/winegstreamer_classes.idl'
    delta=registration_delta((registration/'system-before.reg').read_text(),(registration/'system-after.reg').read_text(),idl.read_text())
    values=registry_values(delta)
    licenses=cross/'prx/licenses/media'
    if not licenses.is_dir() or not any(licenses.rglob('*')):raise ValueError('media license inventory missing')
    manifests={p.name:json.loads(p.read_text()) for p in media_work.glob('*-manifest.json')}
    required={'ffmpeg','libffi','pcre2','zlib','libiconv','glib','gstreamer','gst-plugins-base','gst-plugins-good','gst-plugins-bad','gst-libav'}
    if {v['component'] for v in manifests.values()}!=required:raise ValueError('incomplete media build manifests')
    for name,manifest in manifests.items():
        for rel,digest in manifest['archives'].items():
            if sha(media_work/'root'/rel)!=digest:raise ValueError('media archive changed since manifest: '+name+'/'+rel)
        for rel,digest in manifest['licenses'].items():
            if sha(licenses/Path(rel).relative_to('licenses'))!=digest:raise ValueError('media license differs from manifest: '+rel)
    link_flags=json.loads((cross/'media-flags.json').read_text())
    linked_archives={}
    for flag in shlex.split(link_flags['libs']):
        if flag.endswith('.a'):
            path=Path(flag).resolve()
            if not path.is_relative_to(media_work/'root'):raise ValueError('non-target media archive')
            linked_archives[path.relative_to(media_work/'root').as_posix()]=sha(path)
    if not linked_archives:raise ValueError('empty static media link inventory')
    adapter_manifest=cross/'prx/media-adapters.json'
    if not adapter_manifest.is_file():raise ValueError('media adapter provenance missing')
    declared_archives={rel:digest for manifest in manifests.values() for rel,digest in manifest['archives'].items()}
    if any(declared_archives.get(rel)!=digest for rel,digest in linked_archives.items()):
        raise ValueError('linked archive missing from dependency manifests')
    for manifest in manifests.values():
        if not manifest.get('archives') or not manifest.get('licenses') or not manifest.get('source'):
            raise ValueError('media source/archive/license inventory missing')
        if not re.fullmatch(r'[0-9a-f]{64}',manifest['source'].get('sha256','')):
            raise ValueError('media source checksum missing')
        if any(flag.split('=')[0] in ('--enable-gpl','--enable-nonfree') for flag in manifest.get('configure',[])):
            raise ValueError('non-LGPL media configuration')
    tls_licenses=[]
    if (cross/'prx/sce_module/libgnutls.prx').is_file():
        for component,required in [('gnutls',('COPYING.LESSERv2','COPYING','AUTHORS','lib-inih-LICENSE.txt')),
                                   ('nettle',('COPYING.LESSERv3','COPYINGv2','COPYINGv3','AUTHORS'))]:
            directory=cross/'prx/licenses'/component
            if any(not (directory/name).is_file() for name in required):
                raise ValueError('TLS license inventory missing: '+component)
            tls_licenses.append(directory)
    return dict(delta=delta,values=values,modules=modules,prx=prx,licenses=licenses,manifests=manifests,tls_licenses=tls_licenses,
                linked_archives=linked_archives,report=report,entry=entry,idl=idl,dll_hashes=dll_hashes)

def stage_media(runtime,template,cross,registration,media_work,substitutions,evidence=None):
    runtime,template,cross,registration,media_work=map(Path,(runtime,template,cross,registration,media_work))
    evidence=evidence or validate_media(cross,registration,media_work)
    if (runtime/'lib/wine/x86_64-unix/libgnutls.prx').is_file() and not evidence['tls_licenses']:
        raise ValueError('TLS license inventory missing for retained runtime; use a complete cross build')
    delta,modules,prx,licenses,manifests,linked_archives,report,entry,idl,dll_hashes=(evidence[k] for k in ("delta","modules","prx","licenses","manifests","linked_archives","report","entry","idl","dll_hashes"))
    system_reg=template/'system.reg'
    merged=merge_registry(system_reg.read_text(),delta)
    # All checks above precede mutations to the copied kit.
    copy(prx,runtime/'lib/wine/x86_64-unix/winegstreamer.prx',0o755)
    added=['lib/wine/x86_64-unix/winegstreamer.prx']
    for arch,folder,source,digest in modules:
        rel='lib/wine/'+arch+'/winegstreamer.dll';dest='drive_c/windows/'+folder+'/winegstreamer.dll'
        copy(source,runtime/rel);copy(source,template/dest);added.append(rel)
        previous=next((v for v in substitutions if v['template_path']==dest),None)
        if previous:substitutions.remove(previous)
        substitutions.append({'template_path':dest,'runtime_source':rel,'before_sha256':previous['before_sha256'] if previous else None,'after_sha256':digest})
    system_reg.unlink();system_reg.write_text(merged);system_reg.chmod(0o644)
    destination=runtime/'share/wine/licenses/media'
    shutil.copytree(licenses,destination)
    for directory in evidence['tls_licenses']:
        shutil.copytree(directory,destination.parent/directory.name,dirs_exist_ok=True)
    provenance=runtime/'share/wine/media-build';provenance.mkdir(parents=True)
    for name,value in manifests.items():
        # Preserve exact source/archive provenance without installing builder paths.
        neutral={k:v for k,v in value.items() if k not in ('configure','source_directory')}
        neutral['full_manifest_sha256']=sha(media_work/name)
        neutral['configure_options']=[v for v in value['configure'] if v.startswith('-') and not any(x in v for x in ('/home/','/Users/','/tmp/'))]
        write_json(provenance/name,neutral)
    copy(cross/'prx/media-adapters.json',provenance/'media-adapters.json')
    write_json(provenance/'linked-archives.json',linked_archives)
    write_json(provenance/'wine-build-report.json',{'wine_commit':report['wine_commit'],'patches':report['patches'],'media_prx':entry,'pe':{arch+'/winegstreamer.dll':digest for arch,_,_,digest in modules},'full_report_sha256':sha(cross/'report.json')})
    values=evidence['values']
    write_json(provenance/'media-registry.json',{'schema':'playden-media-registry/1','values':values})
    write_json(destination/'source-manifest.json',{'schema':'playden-media-sources/1',
               'components':[{k:v for k,v in manifest.items() if k in ('component','source','archives','licenses')}
                             for manifest in manifests.values()], 'linked_archives':linked_archives})
    (runtime/'share/wine/THIRD_PARTY').write_text('Static media backend: GLib, GStreamer and FFmpeg (LGPL), plus libffi, PCRE2, zlib and libiconv.\n'
        'See licenses/media/ for component license texts and source-manifest.json for pinned sources and archive hashes.\n')
    if evidence['tls_licenses']:
        with (runtime/'share/wine/THIRD_PARTY').open('a') as notice:
            notice.write('Static TLS backend: GnuTLS and Nettle; see licenses/gnutls/ and licenses/nettle/.\n')
    return {'changed':added,'registry_keys':len(delta),'registered_dll_sha256':dll_hashes,'playback_verified':False}
