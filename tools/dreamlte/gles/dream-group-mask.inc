/* Keep the original fragment shader's coverage (geometry, second texture,
 * discard and vertex colour) when compositing a deferred vibrant group.
 * Mode zero is the unmodified draw. Only non-fetch source shaders use this
 * wrapper: an already-composited result cannot be treated as source coverage.
 */
static const char dream_group_fragment[] =
    "layout(location = 0) inout highp vec4 hakios_colour;\n"
    "uniform highp int dream_group_mode;\n"
    "uniform highp vec4 dream_group_colours[5];\n"
    "#define main hakios_main\n"
    "#line 1\n";
static const char dream_group_fragment_end[] =
    "\n#undef main\n"
    "void main() {\n"
    "  hakios_main();\n"
    "  highp float coverage = clamp(hakios_data[0].a, 0., 1.);\n"
    "  if (dream_group_mode == 1) {\n"
    "    hakios_colour = vec4(0., 0., 0., coverage);\n"
    "  } else if (dream_group_mode == 2) {\n"
    "    highp vec4 behind = hakios_colour;\n"
    "    highp vec4 s = behind;\n"
    "    s.rgb /= max(s.a, .005);\n"
    "    s = s.r * dream_group_colours[0] + s.g * dream_group_colours[1]\n"
    "      + s.b * dream_group_colours[2] + s.a * dream_group_colours[3] + dream_group_colours[4];\n"
    "    s.a = clamp(s.a, 0., 1.);\n"
    "    s.rgb = clamp(s.rgb * s.a, 0., 1.);\n"
    "    hakios_colour = mix(behind, s, coverage);\n"
    "  } else { hakios_colour = hakios_data[0]; }\n"
    "}\n";

/* The original program stays current; its vertex inputs and uniforms stay as
 * CoreAnimation supplied them. Never activate only half of this protocol.
 */
static GLint
dream_group_original(GLuint program, bool cover, const GLfloat colours[20])
{
    GLint mode = glGetUniformLocation(program, "dream_group_mode");
    GLint matrix = glGetUniformLocation(program, "dream_group_colours");
    if (mode < 0 || matrix < 0 || !colours) return -1;
    glUniform4fv(matrix, 5, colours);
    glUniform1i(mode, cover ? 1 : 2);
    return mode;
}
