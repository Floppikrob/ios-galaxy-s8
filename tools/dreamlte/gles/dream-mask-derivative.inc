/* First-generation Bifrost lowers fabs-only derivatives through an XOR lane
 * permutation. Keep an opaque signed use so it selects the explicitly bounded
 * lane1/lane2 path instead. The new uniform is left at its GL default zero;
 * for finite inputs the output remains abs(dx)+abs(dy), with no radius change.
 * This is a hardware-derived workaround candidate, not proof of the exact ISA
 * fault. Only independently identified circle/supercircle bodies are rewritten.
 */
#define DREAM_MASK_DERIVATIVE_GLSL \
    "uniform highp float dream_derivative_guard;\n" \
    "highp float dfwG71(highp float v) {\n" \
    "  highp float dx = dFdx(v), dy = dFdy(v);\n" \
    "  return abs(dx) + abs(dy) + dream_derivative_guard * (dx + dy);\n" \
    "}\n"

static unsigned
dream_mask_derivative_fix(char *source)
{
    static const char *const names[]={"vec4 circleImage(","vec4 supercircleImage("};
    unsigned changed=0;
    for(unsigned i=0;i<2;i++) {
        char *begin=strstr(source,names[i]);
        if(!begin)continue;
        char *close=strchr(begin,')');if(!close)continue;
        char *body=close+1;while(*body==' '||*body=='\t'||*body=='\r'||*body=='\n')body++;
        if(*body!='{')continue;
        char *end=strchr(body,'}');if(!end)continue;
        char *use=strstr(body,"w = max(fwidth(d), 1e-4);");
        if(use && use<end) {
            /* Equal-size replacement preserves the assembler's source/head
             * buffer boundary and every other byte of this function. */
            memcpy(use+8,"dfwG71",6);changed++;
        }
    }
    return changed;
}
