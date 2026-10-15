#!/usr/bin/env python3
"""Extract Wine's actual media registration without host device changes."""
import re
SEPARATOR='\\\\'

def blocks(text):
    matches=list(re.finditer(r'^\[([^\n]+)\][^\n]*\n',text,re.M))
    result={}
    for i,match in enumerate(matches):
        key=match.group(1)
        value=text[match.end():matches[i+1].start() if i+1<len(matches) else len(text)]
        result[key]='\n'.join(line for line in value.splitlines() if line and not line.startswith('#time='))
    return result

def registration_delta(before,after,idl):
    classes={g.lower() for g in re.findall(r'uuid\(([0-9a-fA-F-]{36})\)',idl)}
    if '317df618-5e5a-468a-9f15-d827a9a08162' not in classes:
        raise ValueError('Wine IDL lacks the expected media handler')
    old,new=blocks(before),blocks(after)
    delta={}
    for key,value in new.items():
        if old.get(key)==value:continue
        parts=key.lower().split(SEPARATOR)
        if parts[:2]!=['software','classes']:continue
        tail=parts[2:]
        if tail and tail[0]=='wow6432node':tail=tail[1:]
        allowed=False
        if len(tail)>=2 and tail[0]=='clsid':
            allowed=(tail[1].strip('{}') in classes and tail[2:] in ([],['inprocserver32']))
            allowed |= tail==['clsid','{083863f1-70de-11d0-bd40-00a0c911ce86}','instance','{f9d8d64e-a144-47dc-8ee0-f53498372c29}']
        allowed |= tail==['media type','{e436eb83-524f-11ce-9f53-0020af0ba770}','{ffffffff-128f-4dd1-ad22-becfa66ce7aa}']
        if not allowed:continue
        if re.search(r'/home/|/users/|\\\\home\\\\|\\\\users\\\\|scratch|z:',value,re.I):
            raise ValueError('host path in media registration: '+key)
        if key in old and old[key]!=value:
            raise ValueError('media registration overwrites an existing key: '+key)
        delta[key]=value
    for view,folder in [('', 'system32'),('Wow6432Node'+SEPARATOR,'syswow64')]:
        for cls in classes:
            key=('Software'+SEPARATOR+'Classes'+SEPARATOR+view+'CLSID'+SEPARATOR+'{'+cls+'}'+SEPARATOR+'InprocServer32').lower()
            value=next((v for k,v in delta.items() if k.lower()==key),None)
            expected='C:'+SEPARATOR+'windows'+SEPARATOR+folder+SEPARATOR+'winegstreamer.dll'
            if value is None or f'@="{expected}"'.lower() not in value.lower() or '"ThreadingModel"="Both"'.lower() not in value.lower():
                raise ValueError('missing or invalid media registration: '+key)
    return delta

def merge_registry(base,delta):
    original=blocks(base)
    existing={k.lower():v for k,v in original.items()}
    for key,value in delta.items():
        if key.lower() in existing:raise ValueError('template already has media key: '+key)
        original[key]=value
    header=base[:base.index('[')]
    return header+''.join('['+key+']\n'+value+'\n\n' for key,value in sorted(original.items(),key=lambda kv:[p.lower() for p in kv[0].split(SEPARATOR)]))
