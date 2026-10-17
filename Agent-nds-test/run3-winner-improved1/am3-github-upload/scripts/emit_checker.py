"""Emit checkers and debugger metadata while preserving the seven-word vector ABI."""
import json
from cpu_groups import classify

def emit(decl,rows,namespace='cpu'):
 names=list(dict.fromkeys(v['name'] for v in rows));index={n:i for i,n in enumerate(names)}
 s='\n'.join(decl)+'''\nstruct V {void (*fn)(unsigned,unsigned,unsigned,unsigned*);unsigned a,b,initial,result,flags,mask;};
static const V vectors[]={\n'''
 for v in rows:s+='{'+v['name']+','+','.join(hex(v[k])+'u' for k in ['a','b','initial','result','flags','mask'])+'},\n'
 s+='};\nstatic const char* const families[]={'+','.join(json.dumps(n.removeprefix('cpu_')) for n in names)+'};\n'
 s+='static const unsigned short family_index[]={'+','.join(str(index[v['name']]) for v in rows)+'};\n'
 s+='static const unsigned char isa_index[]={'+','.join('2' if classify(v)[0]=='THUMB' else '1' for v in rows)+'};\n'
 first=rows[0]['id']
 s+=f'''extern "C" void cpu_trace_begin(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
extern "C" void cpu_trace_end(unsigned,unsigned,unsigned,unsigned);
extern "C" unsigned {namespace}_checks_for(unsigned* detail,unsigned isa) {{
 unsigned out[2],first=0,passed=0,failed=0;
 for(unsigned k=0;k<6;k++)detail[k]=0;
 for(unsigned i=0;i<sizeof(vectors)/sizeof(vectors[0]);i++) {{
  if(isa && isa_index[i]!=isa)continue;
  const V& v=vectors[i];unsigned id=i+{first};
  cpu_trace_begin(id,(unsigned)v.fn,v.a,v.b,v.initial,v.result,v.flags,v.mask);
  out[0]=0xbaadc0de;out[1]=0xbaadc0de;v.fn(v.a,v.b,v.initial,out);
  bool bad=out[0]!=v.result || ((out[1]^v.flags)&v.mask);
  cpu_trace_end(id,out[0],out[1],bad);
  if(bad) {{++failed;if(!first){{first=id;detail[0]=out[0];detail[1]=v.result;detail[2]=out[1];detail[3]=v.flags;}}}}
  else ++passed;
 }}
 detail[4]=passed;detail[5]=failed;return first;
}}
extern "C" unsigned {namespace}_checks(unsigned* detail) {{return {namespace}_checks_for(detail,0);}}
extern "C" unsigned {namespace}_family_count() {{return sizeof(families)/sizeof(families[0]);}}
extern "C" unsigned {namespace}_isa(unsigned id) {{return isa_index[id-{first}];}}
extern "C" const char* {namespace}_family_name(unsigned f) {{return families[f];}}
extern "C" unsigned {namespace}_family(unsigned id) {{return family_index[id-{first}];}}
extern "C" void {namespace}_info(unsigned id,unsigned* out) {{
 const V& v=vectors[id-{first}];out[0]=v.a;out[1]=v.b;out[2]=v.initial;
 out[3]=v.result;out[4]=v.flags;out[5]=v.mask;out[6]=(unsigned)v.fn;
}}
'''
 return s
