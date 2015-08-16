attribute vec4 vVert;
varying vec2 UV;

const float left=0.0;
const float bottom=0.0;
uniform float right;
uniform float top;
const float zNear=-1.0;
const float zFar=1.0;

void main()
{
	mat4 m;

	m[0]=vec4(2.0/(right-left), 0.0, 0.0, 0.0);
	m[1]=vec4(0.0, 2.0/(top-bottom), 0.0, 0.0);
	m[2]=vec4(0.0, 0.0, -2.0/(zFar-zNear), 0.0);
	m[3]=vec4(-(right+left)/(right-left), -(top+bottom)/(top-bottom), -(zFar+zNear)/(zFar-zNear), 1.0);


	gl_Position=m*vec4(vVert.xy, 0.0, 1.0);
	UV=vVert.zw;
}
