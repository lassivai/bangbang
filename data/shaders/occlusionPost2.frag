
//uniform sampler2DShadow occlusionTexture;
uniform sampler2D occlusionTexture0, occlusionTexture1;
uniform vec2 characterPos0, characterPos1;
uniform float occlusionTextureWidth;

void main()
{
    float Pi = 3.1415926535;
    float q = 1.0 /(occlusionTextureWidth*0.5);

    vec2 pos0 = (gl_TexCoord[0].xy - characterPos0) * 2.0;
    float angle0 = mod(atan(pos0.y, pos0.x), 2.0*Pi);

    float occlusionCoord0 = angle0 / (2.0 * Pi);

    float dist0 = length(pos0) / (2.0*sqrt(2.0));

    float t0 = 0.0;

    t0 += step(texture(occlusionTexture0, vec2(occlusionCoord0 - q*4.0, 0.5)).r, dist0) * 0.05;
    t0 += step(texture(occlusionTexture0, vec2(occlusionCoord0 - q*3.0, 0.5)).r, dist0) * 0.09;
    t0 += step(texture(occlusionTexture0, vec2(occlusionCoord0 - q*2.0, 0.5)).r, dist0) * 0.12;
    t0 += step(texture(occlusionTexture0, vec2(occlusionCoord0 - q*1.0, 0.5)).r, dist0) * 0.15;

    t0 += step(texture(occlusionTexture0, vec2(occlusionCoord0, 0.5)).r, dist0) * 0.18;

    t0 += step(texture(occlusionTexture0, vec2(occlusionCoord0 + q*1.0, 0.5)).r, dist0) * 0.15;
    t0 += step(texture(occlusionTexture0, vec2(occlusionCoord0 + q*2.0, 0.5)).r, dist0) * 0.12;
    t0 += step(texture(occlusionTexture0, vec2(occlusionCoord0 + q*3.0, 0.5)).r, dist0) * 0.09;
    t0 += step(texture(occlusionTexture0, vec2(occlusionCoord0 + q*4.0, 0.5)).r, dist0) * 0.05;



    vec2 pos1 = (gl_TexCoord[0].xy - characterPos1) * 2.0;
    float angle1 = mod(atan(pos1.y, pos1.x), 2.0*Pi);

    float occlusionCoord1 = angle1 / (2.0 * Pi);

    float dist1 = length(pos1) / (2.0*sqrt(2.0));

    float t1 = 0.0;

    t1 += step(texture(occlusionTexture1, vec2(occlusionCoord1 - q*4.0, 0.5)).r, dist1) * 0.05;
    t1 += step(texture(occlusionTexture1, vec2(occlusionCoord1 - q*3.0, 0.5)).r, dist1) * 0.09;
    t1 += step(texture(occlusionTexture1, vec2(occlusionCoord1 - q*2.0, 0.5)).r, dist1) * 0.12;
    t1 += step(texture(occlusionTexture1, vec2(occlusionCoord1 - q*1.0, 0.5)).r, dist1) * 0.15;

    t1 += step(texture(occlusionTexture1, vec2(occlusionCoord1, 0.5)).r, dist1) * 0.18;

    t1 += step(texture(occlusionTexture1, vec2(occlusionCoord1 + q*1.0, 0.5)).r, dist1) * 0.15;
    t1 += step(texture(occlusionTexture1, vec2(occlusionCoord1 + q*2.0, 0.5)).r, dist1) * 0.12;
    t1 += step(texture(occlusionTexture1, vec2(occlusionCoord1 + q*3.0, 0.5)).r, dist1) * 0.09;
    t1 += step(texture(occlusionTexture1, vec2(occlusionCoord1 + q*4.0, 0.5)).r, dist1) * 0.05;



    gl_FragColor = vec4(0, 0, 0, min(t0, t1));

}
