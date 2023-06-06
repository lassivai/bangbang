#version 120


uniform sampler2D pixelTexture;
uniform vec2 characterPos;
uniform float occlusionTextureWidth;

//uniform int numPlayers, playerIndex;

void main()
{
    float Pi = 3.1415926535;

    vec2 center = characterPos;

    float w = occlusionTextureWidth * 0.5;
    float dw = w / 256.0;

    vec2 delta = vec2(cos(gl_TexCoord[0].x * 10.0 * Pi/dw) / (w*2.0), sin(gl_TexCoord[0].x * 10.0 * Pi/dw) / (w*2.0));

    float dist = 1.0;

    for(int i=0; i<w*2*sqrt(2.0); i++) {
        vec2 pos = center + delta*float(i);

        vec4 pixel = texture2D(pixelTexture, pos);
        if(pixel.a >= 1.0) {
            //dist = 0.05 + sqrt(4.0*(pos.x-center.x)*(pos.x-center.x) + 4.0*(pos.y-center.y)*(pos.y-center.y));
            dist = 0.025 + sqrt((pos.x-center.x)*(pos.x-center.x)/2.0 + (pos.y-center.y)*(pos.y-center.y)/2.0);
            break;
        }

        if(pos.x < 0.0 || pos.x > 1.0 || pos.y < 0.0 || pos.y > 1.0) {
            dist = 1.0;
            break;
        }
    }



    //vec4 pixel = texture2D(pixelTexture, gl_TexCoord[0].xy);


    //gl_FragColor = gl_Color * pixel;

    //gl_FragColor = gl_Color;

    //gl_FragColor = pixel * vec4(gl_TexCoord[0].x, gl_TexCoord[0].y, 0.0, 1.0);

    gl_FragColor = vec4(dist, dist, dist, 1.0);
    gl_FragDepth = dist;
    //gl_FragColor = dist;
}
