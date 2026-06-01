uniform sampler2D Image;
uniform vec4 Tint;

in vec2 Frag_UV;
#ifdef TFE_GLES2
// GLES 2.0 / GLSL ES 1.00 has no user-declared fragment outputs; write to gl_FragColor instead.
#define Out_Color gl_FragColor
#else
out vec4 Out_Color;
#endif

void main()
{
	vec4 color = texture(Image, Frag_UV) * Tint;
	Out_Color.rgb = color.rgb * vec3(color.a);
	Out_Color.a = color.a;
}
