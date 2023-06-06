#version 120

uniform float timer;

float rand(vec2 co) {
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

void main() {
    float x = int(gl_TexCoord[0].x*2560.0) / 3 * 3;
    float y = int(gl_TexCoord[0].y*1440.0) / 3 * 3;
    vec2 pos = vec2(x/2560.0, y/1440.0);
    //float c = rand(gl_TexCoord[0].xy*100.0);

    float c = rand(pos*100.0*timer);

    gl_FragColor = vec4(c, c, c, 1.0);
    //gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}