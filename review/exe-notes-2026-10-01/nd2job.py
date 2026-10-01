import sys,json,re
name,src,out=sys.argv[1],sys.argv[2],sys.argv[3]
fn,xr,rg,pt=[],[],[],[]
for l in open(src):
    p=l.split()
    if not p: continue
    a=[x.lower() for x in p[1:]]
    if p[0]=='func': fn+= [x for x in a if re.fullmatch(r'0x[0-9a-f]{8}',x)]
    elif p[0]=='xref': xr+= [x for x in a if re.fullmatch(r'0x[0-9a-f]{8}',x)]
    elif p[0]=='range': rg+= [x for x in a if re.fullmatch(r'0x[0-9a-f]{8}-0x[0-9a-f]{8}',x)]
    elif p[0]=='ptrs' and len(p)>=3: pt.append((a[0],p[2]))
S=lambda o,args,t=3600:{"kind":"ghidra","script":"team-a/ghidra/CrreishDump.java","timeout":t,"args":[o]+args}
steps=[]
for k in range(0,len(fn),30): steps.append(S("{OUT}/func%d"%(k//30+1),["func","depth:1","maxfuncs:6","maxinsn:2000"]+fn[k:k+30],5400))
if xr: steps.append(S("{OUT}/xref",["xref","xrefs:200"]+xr))
if rg: steps.append(S("{OUT}/range",["range"]+rg))
for i,(a,n) in enumerate(pt): steps.append(S("{OUT}/ptrs%d"%i,["ptrs","count:%s"%n,"nodecomp:1",a],1800))
json.dump({"team":"team-a","title":"Team A: %s — residual NEEDS-EXE next dumps (2026-10-01 re-derivation)"%name,"ref":"auto","steps":steps,"collect":["**/*.txt"]},open(out,'w'),indent=1)
print(name,len(fn),len(xr),len(rg),len(pt))
