from pathlib import Path
import ctypes as C,os,sys
# Reuse only the EGL context/bootstrap from the existing owned host fixture.
root=Path(__file__).resolve().parent
bootstrap=(root/'test-group-mask-render.py').read_text().split("root=Path(sys.argv[1]);")[0]
exec(compile(bootstrap,'host-egl-bootstrap','exec'))
directory=Path(sys.argv[1]);shader_=shader
vs=shader_(0x8b31,'#version 300 es\nin highp vec2 vertex_position;out highp vec2 uv;void main(){gl_Position=vec4(vertex_position,0,1);uv=vertex_position;}')
def program(text):
 fs=shader_(0x8b30,text);p=gl('glCreateProgram',U)();gl('glAttachShader',None,U,U)(p,vs);gl('glAttachShader',None,U,U)(p,fs);gl('glBindAttribLocation',None,U,U,C.c_char_p)(p,0,b'vertex_position');gl('glLinkProgram',None,U)(p);ok=I();gl('glGetProgramiv',None,U,U,C.POINTER(I))(p,0x8b82,C.byref(ok));assert ok.value
 return p
programs=[program((directory/name).read_text()) for name in ['circle-old.frag','circle-new.frag','super-old.frag','super-new.frag']]
for p in programs[1::2]:
 place=gl('glGetUniformLocation',I,U,C.c_char_p)(p,b'dream_derivative_guard');assert place>=0
 value=F(99);gl('glGetUniformfv',None,U,I,C.POINTER(F))(p,place,C.byref(value));assert value.value==0
buffer=U();gl('glGenBuffers',None,I,C.POINTER(U))(1,C.byref(buffer));gl('glBindBuffer',None,U,U)(0x8892,buffer)
vertices=(F*8)(-1,-1,1,-1,-1,1,1,1);gl('glBufferData',None,U,C.c_ssize_t,P,U)(0x8892,C.sizeof(vertices),vertices,0x88e4);gl('glVertexAttribPointer',None,U,I,U,C.c_ubyte,I,P)(0,2,0x1406,0,0,None);gl('glEnableVertexAttribArray',None,U)(0)
tex=U();fb=U();gl('glGenTextures',None,I,C.POINTER(U))(1,C.byref(tex));gl('glBindTexture',None,U,U)(0xde1,tex);gl('glGenFramebuffers',None,I,C.POINTER(U))(1,C.byref(fb));gl('glBindFramebuffer',None,U,U)(0x8d40,fb)
checked=0;worst=0
for size in (32,216):
 gl('glTexImage2D',None,U,I,I,I,I,I,U,U,P)(0xde1,0,0x8058,size,size,0,0x1908,0x1401,None);gl('glFramebufferTexture2D',None,U,U,U,U,I)(0x8d40,0x8ce0,0xde1,tex,0);assert gl('glCheckFramebufferStatus',U,U)(0x8d40)==0x8cd5
 gl('glViewport',None,I,I,I,I)(0,0,size,size);gl('glEnable',None,U)(0xbe2);gl('glBlendFunc',None,U,U)(1,0x303)
 for family in (0,2):
  results=[]
  for p in programs[family:family+2]:
   gl('glUseProgram',None,U)(p);gl('glClearColor',None,F,F,F,F)(20/255,20/255,20/255,138/255);gl('glClear',None,U)(0x4000);gl('glDrawArrays',None,U,I,I)(5,0,4);gl('glFinish',None)();out=(C.c_ubyte*(size*size*4))();gl('glReadPixels',None,I,I,I,I,U,U,P)(0,0,size,size,0x1908,0x1401,out);assert gl('glGetError',U)()==0;results.append(bytes(out))
  difference=max(abs(a-b) for a,b in zip(*results));assert difference<=1,(size,family,difference);worst=max(worst,difference);checked+=size*size
  for x,y in [(int(size*.05),int(size*.05)),(int(size*.95),int(size*.05)),(int(size*.05),int(size*.95)),(int(size*.95),int(size*.95))]:assert results[1][(y*size+x)*4:(y*size+x)*4+4]==bytes([20,20,20,138])
print('PASS: host Mesa circle/supercircle original-vs-rewritten',checked,'pixels/all RGBA channels; max delta',worst,'; guard is active uniform initialized to zero; outside corners transparent at32/216px')
egl('eglMakeCurrent',U,P,P,P,P)(display,None,None,None);egl('eglTerminate',U,P)(display)
