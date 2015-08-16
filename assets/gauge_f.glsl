precision mediump float;

varying vec2 UV;

uniform sampler2D Tex;

uniform float Amount;

void main()
{
	float a=min(1.0, max(0.0, Amount));
	float rim=UV.x*UV.x+UV.y*UV.y;
	vec2 pos=vec2(UV.x+sin(((a+0.08)*0.85)*6.28318)*0.92, UV.y+cos(((a+0.08)*0.85)*6.28318)*0.92);
	float dot=pos.x*pos.x+pos.y*pos.y;

	float od=smoothstep(1.0, 0.99, rim);
	float id=smoothstep(0.9, 0.89, rim);
	float sd=smoothstep(0.005, 0.0049, dot);

	gl_FragColor=texture2D(Tex, UV*0.5+0.5)+vec4(sd, 0.0, 0.0, 1.0)+vec4(0.0, 0.0, od-id, 1.0);
}
