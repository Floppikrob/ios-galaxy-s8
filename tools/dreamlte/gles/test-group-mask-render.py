"""Render the actual assembled adapter shader in a host Mesa ES3 context.
The handset is not accessed. ctypes keeps this test independent of SDK headers.
"""
from pathlib import Path
import ctypes as C,math,os,sys
os.environ['LIBGL_ALWAYS_SOFTWARE']='1'
E=C.CDLL('libEGL.so.1');P=C.c_void_p;I=C.c_int;U=C.c_uint;F=C.c_float
def egl(name,restype,*args):
 f=getattr(E,name);f.restype=restype;f.argtypes=args;return f
gp=egl('eglGetProcAddress',P,C.c_char_p)
getdisplay=C.CFUNCTYPE(P,U,P,C.POINTER(I))(gp(b'eglGetPlatformDisplayEXT'))
display=getdisplay(0x31dd,None,None);major=I();minor=I()
assert egl('eglInitialize',U,P,C.POINTER(I),C.POINTER(I))(display,C.byref(major),C.byref(minor))
assert egl('eglBindAPI',U,U)(0x30a0)
attrs=(I*17)(0x3033,1,0x3040,0x40,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3025,0,0x3026,0,0x3038)
config=P();count=I();assert egl('eglChooseConfig',U,P,C.POINTER(I),C.POINTER(P),I,C.POINTER(I))(display,attrs,C.byref(config),1,C.byref(count)) and count.value
W=H=32 # avoid a nearest-sample tie exactly at the two-texel boundary
surf=egl('eglCreatePbufferSurface',P,P,P,C.POINTER(I))(display,config,(I*5)(0x3057,W,0x3056,H,0x3038));assert surf
ctx=egl('eglCreateContext',P,P,P,P,C.POINTER(I))(display,config,None,(I*3)(0x3098,3,0x3038));assert ctx
assert egl('eglMakeCurrent',U,P,P,P,P)(display,surf,surf,ctx)
def gl(name,restype,*args):
 ptr=gp(name.encode());assert ptr,name;return C.CFUNCTYPE(restype,*args)(ptr)
getstr=gl('glGetString',C.c_char_p,U)
assert b'GL_EXT_shader_framebuffer_fetch ' in getstr(0x1f03)+b' ',getstr(0x1f03)
create=gl('glCreateShader',U,U);source=gl('glShaderSource',None,U,I,C.POINTER(C.c_char_p),C.POINTER(I));compile_=gl('glCompileShader',None,U);shaderiv=gl('glGetShaderiv',None,U,U,C.POINTER(I));shaderlog=gl('glGetShaderInfoLog',None,U,I,C.POINTER(I),P)
def shader(kind,text):
 s=create(kind);p=C.c_char_p(text.encode());source(s,1,C.byref(p),None);compile_(s);ok=I();shaderiv(s,0x8b81,C.byref(ok))
 if not ok.value:
  msg=C.create_string_buffer(8192);shaderlog(s,8192,None,msg);raise AssertionError(msg.value.decode())
 return s
root=Path(sys.argv[1]);vs=shader(0x8b31,(root/'mask.vert').read_text());fs=shader(0x8b30,(root/'mask.frag').read_text())
program=gl('glCreateProgram',U)();attach=gl('glAttachShader',None,U,U);attach(program,vs);attach(program,fs);gl('glBindAttribLocation',None,U,U,C.c_char_p)(program,0,b'vertex_position');gl('glLinkProgram',None,U)(program)
ok=I();gl('glGetProgramiv',None,U,U,C.POINTER(I))(program,0x8b82,C.byref(ok))
if not ok.value:
 msg=C.create_string_buffer(8192);gl('glGetProgramInfoLog',None,U,I,C.POINTER(I),P)(program,8192,None,msg);raise AssertionError(msg.value.decode())
gl('glUseProgram',None,U)(program)
location=gl('glGetUniformLocation',I,U,C.c_char_p);uni=gl('glUniform1i',None,I,I)
mode=location(program,b'dream_group_mode');matrix=location(program,b'dream_group_colours');assert mode>=0 and matrix>=0
values=(F*20)(*([0.]*16+[1.,0.,0.,1.]));gl('glUniform4fv',None,I,I,C.POINTER(F))(matrix,5,values)
tex=U();gl('glGenTextures',None,I,C.POINTER(U))(1,C.byref(tex));gl('glBindTexture',None,U,U)(0xde1,tex)
for name,value in [(0x2801,0x2600),(0x2800,0x2600),(0x2802,0x812f),(0x2803,0x812f)]:gl('glTexParameteri',None,U,U,I)(0xde1,name,value)
pixels=(C.c_ubyte*8)(255,255,255,128,255,255,255,255)
gl('glTexImage2D',None,U,I,I,I,I,I,U,U,P)(0xde1,0,0x1908,2,1,0,0x1908,0x1401,pixels)
uni(location(program,b'texture1'),0)
buffer=U();gl('glGenBuffers',None,I,C.POINTER(U))(1,C.byref(buffer));gl('glBindBuffer',None,U,U)(0x8892,buffer)
vertices=(F*8)(-1,-1,1,-1,-1,1,1,1);gl('glBufferData',None,U,C.c_ssize_t,P,U)(0x8892,C.sizeof(vertices),vertices,0x88e4)
gl('glVertexAttribPointer',None,U,I,U,C.c_ubyte,I,P)(0,2,0x1406,0,0,None);gl('glEnableVertexAttribArray',None,U)(0)
gl('glViewport',None,I,I,I,I)(0,0,W,H);gl('glDisable',None,U)(0xbe2)
clear=gl('glClearColor',None,F,F,F,F);clearbits=gl('glClear',None,U);draw=gl('glDrawArrays',None,U,I,I);read=gl('glReadPixels',None,I,I,I,I,U,U,P);finish=gl('glFinish',None);error=gl('glGetError',U)
old_unmasked_mismatches=0
for m in (0,1,2):
 uni(mode,m);clear(.2,.4,.6,1);clearbits(0x4000);draw(5,0,4);finish();out=(C.c_ubyte*(W*H*4))();read(0,0,W,H,0x1908,0x1401,out);assert error()==0
 for y in range(H):
  for x in range(W):
   u=2*(x+.5)/W-1;v=2*(y+.5)/H-1;bg=[.2,.4,.6,1.]
   t=max(0,min(1,(math.hypot(u,v)-.65)/.1));shape=1-t*t*(3-2*t);a=shape*(128/255 if u<0 else 1)*.5
   expected=bg if u<-.8 else ([.2*a,.6*a,.8*a,a] if m==0 else [0,0,0,a] if m==1 else [bg[0]*(1-a)+a,bg[1]*(1-a),bg[2]*(1-a),1])
   actual=out[(y*W+x)*4:(y*W+x+1)*4]
   assert all(abs(actual[c]-expected[c]*255)<=2 for c in range(4)),(m,x,y,list(actual),expected)
   if m==2 and u>=-.8:
    old_a=(128/255 if u<0 else 1)*.5
    old=[bg[0]*(1-old_a)+old_a,bg[1]*(1-old_a),bg[2]*(1-old_a),1]
    old_unmasked_mismatches+=any(abs(actual[c]-old[c]*255)>2 for c in range(4))
assert old_unmasked_mismatches>300
print('PASS: host Mesa render',getstr(0x1f01).decode(),';',W*H*3,'pixels/channel checks; circle antialias + second alpha texture + discard + opacity; original/coverage/composite modes; old bare-quad model wrong at',old_unmasked_mismatches,'pixels')
egl('eglMakeCurrent',U,P,P,P,P)(display,None,None,None);egl('eglDestroyContext',U,P,P)(display,ctx);egl('eglDestroySurface',U,P,P)(display,surf);egl('eglTerminate',U,P)(display)
