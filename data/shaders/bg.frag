#version 120

uniform float timer;

/* ==================================================================== */

float rand(vec2 co) {
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

/* ==================================================================== */

vec2 random2(int i) {
  return vec2(rand(vec2(sin(float(i)*37542.73)*35.3565, sin(float(i)*46756.273)*15.54)), rand(vec2(sin(float(i+5485)*5554.273)*25.66, sin(float(i+4573)*94326.273)*465.21)));
}

/* ==================================================================== */

// Value noise by Inigo Quilez - iq/2013
// https://www.shadertoy.com/view/lsf3WH
float noise(vec2 st) {
    vec2 i = floor(st);
    vec2 f = fract(st);
    vec2 u = f*f*(3.0-2.0*f);
    return mix( mix( rand( i + vec2(0.0,0.0) ),
                     rand( i + vec2(1.0,0.0) ), u.x),
                mix( rand( i + vec2(0.0,1.0) ),
                     rand( i + vec2(1.0,1.0) ), u.x), u.y);
}

/* ==================================================================== */

float fbm(vec2 st, float t, float value = -0.50, float frequency = 1.0,
          float amplitude = 0.5, float gain = 0.8, float lacunarity = 2.5) {

  for(int i=0; i<4; i++) {
    value += amplitude * noise(st * frequency + t);
    frequency *= lacunarity;
    amplitude *= gain;
  }

  return value;
}

/* ==================================================================== */

vec2 fbmWarp2(vec2 st, int index, float freq, int levels, float[10] amounts, float[10] times, float value = -0.50, float frequency = 1.0, float amplitude = 0.5, float gain = 0.8, float lacunarity = 2.5) {
  vec2 p;

  for(int i=levels-1; i>=0; i--) {
    p.x = fbm(p*amounts[i] + random2(i*2+index) + st*freq, times[i], value, frequency, amplitude, gain, lacunarity);
    p.y = fbm(p*amounts[i] + random2(i*2+1+index) + st*freq, times[i], value, frequency, amplitude, gain, lacunarity);
  }
  return p;
}


vec4 drawStar(float time, vec2 pixelPos, vec2 starPos, int starIndex) {
    float r = distance(pixelPos, starPos);
    return vec4(1.0, 1.0, 1.0, (1.0-smoothstep(0.0005, 0.001, r))*noise(random2(starIndex) * time));
}


void main() {
    float x = int(gl_TexCoord[0].x*2560.0) / 3 * 3;
    float y = int(gl_TexCoord[0].y*1440.0) / 3 * 3;
    vec2 pos = vec2(x/2560.0, y/1440.0);
    
    //float c = rand(pos*100.0*timer);
    //float c = noise(pos*100.0+timer);
    //float c = fbm(pos*40.0, 0);
    //c = max((c-0.3) * 0.5, 0.0) + 0.1;
    //c = 0.05 + c * 0.1;

    /*float freq = 1.0;
    int levels = 6;
    float[6] amounts = float[6](1.0,1.0,1.0,1.0,1.0,1.0);
    float[6] times = float[6](1.0,1.0,1.0,1.0,1.0,1.0);
    
    for(int i=0; i<6; i++) {
        times[i] = times[i] * timer;
    }

    float value = -0.50;
    float frequency = 1.0;
    float amplitude = 0.5;
    float gain = 0.8;
    float lacunarity = 2.5;

    vec2 f = fbmWarp2(pos, 0, freq, levels, amounts, times, value, frequency, amplitude, gain, lacunarity);

    vec2 f2 = fbmWarp2(pos, 10, freq, levels, amounts, times, value, frequency, amplitude, gain, lacunarity);

    vec2 f3 = fbmWarp2(pos, 30, freq, levels, amounts, times, value, frequency, amplitude, gain, lacunarity);

    gl_FragColor = vec4(f.x, f2.x, f3.x, 1.);*/




    /*const int numStars = 100;
    vec2 starPositions[numStars];
    for(int i=0; i<numStars; i++) {
        starPositions[i] = random2(i);
    }*/


    float freq = 1.0;
    int levels = 10;
    /*float[6] amounts = float[6](0.2, .03, .05, .2, .1, .1);
    float[6] times = float[6](1.0,1.0,1.0,1.0,1.0,1.0);*/

    float[10] amounts = float[10](.2, .03, .05, .2, .1, .1, .08, .06, .009, .009);
    float[10] times = float[10](1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0);

    
    for(int i=0; i<10; i++) {
        times[i] = times[i] * timer * 0.1;
    }

    float value = -0.30;
    float frequency = 1.0;
    float amplitude = 0.51;
    float gain = 0.39;
    float lacunarity = 3;

    vec2 f = fbmWarp2(pos, 0, freq, levels, amounts, times, value, frequency, amplitude, gain, lacunarity);

    float k = max(0, pos.y - 0.5)*1.5;
    vec4 bg0 = vec4(0.02, 0.02, 0.2, 1.0);
    vec4 bg1 = vec4(0.3, 0.3, .4, 1.0);
    vec4 bg2 = vec4(0.2, 0.05, 0.1, 1.0);
    vec4 bg3 = mix(bg0, bg1, pos.y*pos.y);
    vec4 bg = mix(bg3, bg2, pos.y*pos.y*pos.y*pos.y*pos.y*pos.y);

    /*for(int i=0; i<numStars; i++) {
      vec4 starCol = drawStar(timer, pos, starPositions[i], i);
      bg = mix(bg, starCol, max(0.0, starCol.a - k));
    }*/

    vec4 a = vec4(0, 0, 0, 1);
    vec4 b = vec4(0.3, 0.02, 0.04, 1);
    vec4 fg = mix(a, b, pos.y*pos.y);
    float t = pow(f.x, 0.4);
    gl_FragColor = mix(bg, fg, clamp((t-0.2-k)*2.0, 0.0, 1.0));




    //gl_FragColor = vec4(f.x, f2.x, f3.x, 1.);
    //gl_FragColor = vec4(c, c, c, 1.0);
    //gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}