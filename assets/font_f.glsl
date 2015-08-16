precision mediump float;

varying vec2 UV;
uniform sampler2D Tex;

void main()
{
	float width=1.0/256.0;
	gl_FragColor=vec4(1.0, 1.0, 1.0, smoothstep(0.5-width, 0.5+width, texture2D(Tex, UV).x)+0.5);
}
